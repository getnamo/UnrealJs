#!/usr/bin/env bash
#
# Build-V8-Linux.sh
#
# Builds the V8 monolith for Linux the way UnrealJs needs it and stages it into
# ThirdParty/v8/lib/Linux/<arch>/libv8_monolith.a. Linux host (WSL2 Ubuntu works); can share the
# checkout with Build-V8-Android.sh.
#
# UE builds Linux targets with its own cross-toolchain sysroot (Rocky Linux glibc) and links its bundled
# libc++ (std::__1) statically. V8 must be compiled against that same sysroot + libc++ or it won't
# link into UE (and must not be a thin archive: getnamo/UnrealJs#10). See README.md.
#
# Usage:  ./Build-V8-Linux.sh [step] [x64]
#   setup      depot_tools + shallow clone of the V8 tag + gclient sync (skipped if already checked out)
#   sysroot    copy UE's Linux toolchain sysroot onto the Linux filesystem
#   patch      make the Linux build use the sysroot's libc++ (idempotent)
#   configure  write out/linux-<arch>.release/args.gn from args/linux.release.gn + gn gen
#   build      ninja v8_monolith
#   stage      verify the archive (libc++ flavour, not thin) and copy it into ThirdParty/v8/lib
#   all        (default) every step above, in order
#
# Env overrides: V8_TAG, WORK_DIR, UE_LINUX_TOOLCHAIN (the v*_clang-* toolchain root UE uses,
# i.e. LINUX_MULTIARCH_ROOT; default is the UE 5.8 one under C:\UnrealToolchains).
#
set -euo pipefail

V8_TAG="${V8_TAG:-14.6.202.34}"                 # keep in step with the Win64/Android libs
UE_LINUX_TOOLCHAIN="${UE_LINUX_TOOLCHAIN:-/mnt/c/UnrealToolchains/v26_clang-20.1.8-rockylinux8}"

WORK_DIR="${WORK_DIR:-$HOME/v8build}"
DEPOT_TOOLS="$WORK_DIR/depot_tools"
V8_DIR="$WORK_DIR/v8"

BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # ThirdParty/v8/build
V8_THIRDPARTY="$(dirname "$BUILD_DIR")"                      # ThirdParty/v8

STEP="${1:-all}"
ARCH="${2:-x64}"
case "$ARCH" in
    x64) TRIPLE="x86_64-unknown-linux-gnu"; UE_ARCH_DIR="x86_64" ;;
    *) echo "Unknown arch '$ARCH' (x64)"; exit 1 ;;
esac
OUT_DIR="out/linux-$ARCH.release"
SYSROOT="$WORK_DIR/ue-sysroot/$(basename "$UE_LINUX_TOOLCHAIN")/$TRIPLE"

export PATH="$DEPOT_TOOLS:$PATH"
export GIT_TERMINAL_PROMPT=0
export GIT_HTTP_LOW_SPEED_LIMIT=1000
export GIT_HTTP_LOW_SPEED_TIME=300

step_setup() {
    if [ -d "$V8_DIR/.git" ] && [ -f "$WORK_DIR/.gclient" ]; then
        echo "V8 checkout already at $V8_DIR"; return
    fi
    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"
    [ -d "$DEPOT_TOOLS" ] || git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS"
    "$DEPOT_TOOLS/ensure_bootstrap"
    # Shallow clone of just the release tag (full-history fetches stall under WSL2)
    [ -d "$V8_DIR/.git" ] || git clone --depth 1 --branch "$V8_TAG" https://chromium.googlesource.com/v8/v8.git "$V8_DIR"
    cat > "$WORK_DIR/.gclient" <<'GCLIENT'
solutions = [
  {
    "name": "v8",
    "url": "https://chromium.googlesource.com/v8/v8.git",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
  },
]
GCLIENT
    cd "$V8_DIR"
    gclient sync -D --no-history
}

step_sysroot() {
    local SRC="$UE_LINUX_TOOLCHAIN/$TRIPLE"
    [ -d "$SRC/include/c++/v1" ] || { echo "ERROR: no libc++ headers under $SRC (set UE_LINUX_TOOLCHAIN)"; exit 1; }
    mkdir -p "$(dirname "$SYSROOT")"
    # Copy (not symlink) onto the Linux filesystem: /mnt/c is far too slow for every compile
    rsync -a --delete "$SRC/" "$SYSROOT/"
    echo "sysroot: $SYSROOT ($(grep -h 'define _LIBCPP_VERSION' "$SYSROOT/include/c++/v1/__config" | awk '{print "libc++ " $3}'))"
}

step_patch() {
    cd "$V8_DIR"
    # With use_custom_libcxx=false Chromium's Linux build falls back to the sysroot's libstdc++.
    # Mirror UE's LinuxToolChain instead: -nostdinc++ + <sysroot>/include/c++/v1, link libc++.a/libc++abi.a.
    if ! grep -q "UnrealJs: UE bundled libc++" build/config/linux/BUILD.gn; then
        python3 - <<'PY'
p = "build/config/linux/BUILD.gn"
s = open(p).read()
imp = 'import("//build/config/c++/c++.gni")\n'
assert imp in s, "Patch anchor not found (c++.gni import) in build/config/linux/BUILD.gn; update Build-V8-Linux.sh"
s = s.replace(imp, imp + 'import("//build/config/sysroot.gni")\n', 1)
anchor = 'config("runtime_library") {\n'
assert anchor in s, "Patch anchor not found (runtime_library) in build/config/linux/BUILD.gn; update Build-V8-Linux.sh"
hook = ('  # UnrealJs: UE bundled libc++ from the toolchain sysroot (matches UE LinuxToolChain)\n'
        '  if (!use_custom_libcxx && sysroot != "") {\n'
        '    cflags_cc = [\n'
        '      "-nostdinc++",\n'
        '      "-isystem",\n'
        '      rebase_path("$sysroot/include/c++/v1", root_build_dir),\n'
        '    ]\n'
        '    ldflags = [ "-nostdlib++" ]\n'
        '    libs = [\n'
        '      "$sysroot/lib64/libc++.a",\n'
        '      "$sysroot/lib64/libc++abi.a",\n'
        '    ]\n'
        '  }\n\n')
s = s.replace(anchor, anchor + hook, 1)
# the existing `libs = [ "atomic" ]` in this config must now append rather than assign
s = s.replace('    libs = [ "atomic" ]\n', '    if (!defined(libs)) {\n      libs = []\n    }\n    libs += [ "atomic" ]\n', 1)
open(p, "w").write(s)
PY
    fi
    echo "patches applied"
}

step_configure() {
    cd "$V8_DIR"
    [ -d "$SYSROOT" ] || { echo "ERROR: run the sysroot step first"; exit 1; }
    mkdir -p "$OUT_DIR"
    sed -e "s|@TARGET_CPU@|$ARCH|" -e "s|@SYSROOT@|$SYSROOT|" "$BUILD_DIR/args/linux.release.gn" > "$OUT_DIR/args.gn"
    gn gen "$OUT_DIR"
}

step_build() {
    cd "$V8_DIR"
    autoninja -C "$OUT_DIR" v8_monolith
}

step_stage() {
    cd "$V8_DIR"
    local LIB="$OUT_DIR/obj/libv8_monolith.a"
    [ -f "$LIB" ] || { echo "Missing $LIB"; exit 1; }

    # A thin archive only references .o files in the build dir and breaks elsewhere (UnrealJs#10)
    if head -c 8 "$LIB" | grep -q '!<thin>'; then echo "ERROR: $LIB is a thin archive"; exit 1; fi

    local NM="third_party/llvm-build/Release+Asserts/bin/llvm-nm"
    local SYMS CR ONE GLIBCXX
    SYMS=$("$NM" -C --undefined-only "$LIB" 2>/dev/null || true)
    CR=$(grep -c 'std::__Cr::' <<<"$SYMS" || true)
    ONE=$(grep -c 'std::__1::' <<<"$SYMS" || true)
    GLIBCXX=$(grep -Ec 'std::__cxx11::|std::string::_Rep|__throw_out_of_range_fmt' <<<"$SYMS" || true)
    echo "libc++ symbol check: std::__1 refs=$ONE, std::__Cr refs=$CR, libstdc++ refs=$GLIBCXX"
    if [ "$CR" != "0" ] || [ "$GLIBCXX" != "0" ] || [ "$ONE" = "0" ]; then
        echo "ERROR: lib is not built against UE's bundled libc++"; exit 1
    fi

    local DEST="$V8_THIRDPARTY/lib/Linux/$UE_ARCH_DIR"
    mkdir -p "$DEST"
    cp "$LIB" "$DEST/"
    echo "Staged $(du -h "$DEST/libv8_monolith.a" | cut -f1) -> $DEST/libv8_monolith.a"
}

case "$STEP" in
    setup) step_setup ;;
    sysroot) step_sysroot ;;
    patch) step_patch ;;
    configure) step_configure ;;
    build) step_build ;;
    stage) step_stage ;;
    all) step_setup; step_sysroot; step_patch; step_configure; step_build; step_stage ;;
    *) echo "Unknown step '$STEP'"; exit 1 ;;
esac
