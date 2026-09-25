#!/usr/bin/env bash
#
# Build-V8-Android.sh
#
# Builds the V8 monolith for Android the way UnrealJs needs it and stages it into
# ThirdParty/v8/lib/Android/<abi>/libv8_monolith.a. Linux host only (V8/Chromium can't target
# Android from Windows); WSL2 Ubuntu works.
#
# The lib must match the Win64 build (same V8 tag + ABI gn args) and be compiled against the SAME
# C++ standard library UE links on Android: the NDK's static libc++ (std::__ndk1). See README.md.
#
# Usage:  ./Build-V8-Android.sh [step] [arm64|x64]
#   setup      depot_tools + shallow clone of the V8 tag + gclient sync (with target_os android)
#   patch      apply the NDK-libc++ compatibility patch to the checkout (idempotent)
#   ndk        download the NDK version UE builds with
#   configure  write out/android-<arch>.release/args.gn from args/android.release.gn + gn gen
#   build      ninja v8_monolith
#   stage      verify the archive (libc++ flavour, not thin) and copy it into ThirdParty/v8/lib
#   all        (default) every step above, in order
#
# Env overrides: V8_TAG, WORK_DIR (keep it on the Linux filesystem, not /mnt/c), NDK_VERSION,
# ANDROID_API_LEVEL.
#
set -euo pipefail

V8_TAG="${V8_TAG:-14.6.202.34}"                 # keep in step with the Win64 lib
NDK_VERSION="${NDK_VERSION:-r27c}"              # UE 5.8: Engine/Config/Android/Android_SDK.json
ANDROID_API_LEVEL="${ANDROID_API_LEVEL:-26}"    # UE 5.8 MinSDKVersion (BaseEngine.ini)

WORK_DIR="${WORK_DIR:-$HOME/v8build}"
DEPOT_TOOLS="$WORK_DIR/depot_tools"
V8_DIR="$WORK_DIR/v8"
NDK_DIR="$WORK_DIR/android-ndk-$NDK_VERSION"

BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # ThirdParty/v8/build
V8_THIRDPARTY="$(dirname "$BUILD_DIR")"                      # ThirdParty/v8

STEP="${1:-all}"
ARCH="${2:-arm64}"
case "$ARCH" in
    arm64) UE_ARCH_DIR="arm64-v8a" ;;
    x64)   UE_ARCH_DIR="x86_64" ;;
    *) echo "Unknown arch '$ARCH' (arm64|x64)"; exit 1 ;;
esac
OUT_DIR="out/android-$ARCH.release"

export PATH="$DEPOT_TOOLS:$PATH"
# Fail (instead of hanging forever) on stalled HTTPS transfers or credential prompts
export GIT_TERMINAL_PROMPT=0
export GIT_HTTP_LOW_SPEED_LIMIT=1000
export GIT_HTTP_LOW_SPEED_TIME=300

step_setup() {
    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"
    if [ ! -d "$DEPOT_TOOLS" ]; then
        git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS"
    fi
    # Bootstraps depot_tools' bundled python/cipd (fetch/gclient silently no-op without it).
    # update_depot_tools can hang on its own git fetch; a fresh clone is already current.
    "$DEPOT_TOOLS/ensure_bootstrap"

    # Shallow clone of just the release tag: a full-history `fetch v8` (several GB over HTTPS)
    # tends to stall under WSL2, and history isn't needed to build.
    if [ ! -d "$V8_DIR/.git" ]; then
        git clone --depth 1 --branch "$V8_TAG" https://chromium.googlesource.com/v8/v8.git "$V8_DIR"
    fi
    # Unmanaged solution (gclient leaves v8 at the tag). target_os adds the Android deps.
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
target_os = ["android"]
GCLIENT
    cd "$V8_DIR"
    gclient sync -D --no-history
}

step_patch() {
    cd "$V8_DIR"
    # std::atomic_ref shim: NDK libc++ lacks it but V8 14.6 uses it (see vendored/atomic_ref_compat.h).
    # Force-included for the Android target toolchain only, and only when using the NDK libc++.
    cp "$BUILD_DIR/vendored/atomic_ref_compat.h" build/config/android/atomic_ref_compat.h
    if ! grep -q "atomic_ref_compat.h" build/config/android/BUILD.gn; then
        python3 - <<'PY'
p = "build/config/android/BUILD.gn"
s = open(p).read()
anchor = 'config("compiler") {\n  cflags = [\n    "-ffunction-sections",\n    "-fno-short-enums",\n  ]\n'
assert anchor in s, "Patch anchor not found in build/config/android/BUILD.gn; update Build-V8-Android.sh"
hook = ('  # UnrealJs: NDK libc++ has no std::atomic_ref (used by V8 14.6)\n'
        '  if (!use_custom_libcxx) {\n'
        '    cflags_cc = [ "-include", rebase_path("//build/config/android/atomic_ref_compat.h", root_build_dir) ]\n'
        '  }\n')
open(p, "w").write(s.replace(anchor, anchor + hook, 1))
PY
    fi
    echo "patches applied"
}

step_ndk() {
    if [ -d "$NDK_DIR" ]; then echo "NDK already at $NDK_DIR"; return; fi
    cd "$WORK_DIR"
    curl -fL -o "android-ndk-$NDK_VERSION-linux.zip" "https://dl.google.com/android/repository/android-ndk-$NDK_VERSION-linux.zip"
    unzip -q "android-ndk-$NDK_VERSION-linux.zip"
    rm "android-ndk-$NDK_VERSION-linux.zip"
}

step_configure() {
    cd "$V8_DIR"
    mkdir -p "$OUT_DIR"
    sed -e "s|@TARGET_CPU@|$ARCH|" -e "s|@NDK_ROOT@|$NDK_DIR|" -e "s|@NDK_VERSION@|$NDK_VERSION|" \
        -e "s|@API_LEVEL@|$ANDROID_API_LEVEL|" "$BUILD_DIR/args/android.release.gn" > "$OUT_DIR/args.gn"
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

    # Must reference the NDK libc++ only (UnrealJs#14 was libstdc++ symbols in the old libs)
    local NM="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm"
    local SYMS CR NDK1 GLIBCXX
    SYMS=$("$NM" -C --undefined-only "$LIB" 2>/dev/null || true)
    CR=$(grep -c 'std::__Cr::' <<<"$SYMS" || true)
    NDK1=$(grep -c 'std::__ndk1::' <<<"$SYMS" || true)
    GLIBCXX=$(grep -Ec 'std::string::_Rep|__throw_out_of_range_fmt' <<<"$SYMS" || true)
    echo "libc++ symbol check: std::__ndk1 refs=$NDK1, std::__Cr refs=$CR, libstdc++ refs=$GLIBCXX"
    if [ "$CR" != "0" ] || [ "$GLIBCXX" != "0" ] || [ "$NDK1" = "0" ]; then
        echo "ERROR: lib is not built against the NDK libc++"; exit 1
    fi

    local DEST="$V8_THIRDPARTY/lib/Android/$UE_ARCH_DIR"
    mkdir -p "$DEST"
    cp "$LIB" "$DEST/"
    echo "Staged $(du -h "$DEST/libv8_monolith.a" | cut -f1) -> $DEST/libv8_monolith.a"
}

case "$STEP" in
    setup) step_setup ;;
    patch) step_patch ;;
    ndk) step_ndk ;;
    configure) step_configure ;;
    build) step_build ;;
    stage) step_stage ;;
    all) step_setup; step_patch; step_ndk; step_configure; step_build; step_stage ;;
    *) echo "Unknown step '$STEP'"; exit 1 ;;
esac
