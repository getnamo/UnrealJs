# V8 build utility (UnrealJs)

Automates building the V8 monolith the way UnrealJs needs it and staging the
result into `ThirdParty/v8/`. This replaces the manual patch/configure/build
steps described in the plugin README — you should not have to hand-edit V8.

The prebuilt lib + headers are already committed under `ThirdParty/v8/`, so for
normal plugin use you do **not** need any of this. Use it only to bump the V8
version, rebuild, or add a platform.

## Contents

| File | Purpose |
|------|---------|
| `Build-V8-Win64.ps1` | One-command Win64 build: patch → configure → build → stage. Re-runnable (patching is idempotent). |
| `Build-V8-Android.sh` | Android build (arm64 / x64) from a Linux or WSL2 host: setup → patch → ndk → configure → build → stage. Re-runnable per step. |
| `args/win64.release.gn` | The UE-matched gn args (must stay in sync with the ABI defines in `Source/V8/V8.Build.cs`). |
| `args/android.release.gn` | Android gn args template (same ABI args as Win64 + NDK/libc++ settings; the script fills in the arch/NDK/API level). |
| `patches/*.patch` | The required V8-source patches as reference diffs (the script applies these automatically; the `.patch` files are the canonical record and are reusable for the Linux/Android bash flow). |
| `vendored/interface-types.h` | The `v8::debug` console header V8 removed from its public include set; re-staged into `include/` after each build. |
| `vendored/atomic_ref_compat.h` | C++20 `std::atomic_ref` for NDK libc++ (Android build only; see below). |

## Prerequisites

- Visual Studio 2022 with an MSVC toolset **>= 14.40** (the script pins one via
  `-VcVarsVer`; default `14.44.35207`) and a recent Windows 10/11 SDK.
- ~40 GB free disk for a V8 checkout + build.
- Git on PATH. depot_tools is cloned automatically next to the V8 checkout when
  you pass `-Fetch` (the UE-bundled depot_tools is too old for modern V8 DEPS).

## Usage

```powershell
# You already have a V8 checkout (with patches re-applied automatically):
./Build-V8-Win64.ps1 -V8Root C:\v8work\v8

# Or set everything up from scratch (downloads several GB, builds ~30-60 min):
./Build-V8-Win64.ps1 -V8Root C:\v8work\v8 -Fetch
```

The script pins V8 to `branch-heads/14.6` by default (`-Branch`). After it
finishes, `ThirdParty/v8/lib/Win64/Release/v8_monolith.lib` and
`ThirdParty/v8/include/` are updated; rebuild the plugin normally.

## What the patches do (and why)

All four exist because we embed V8 into UE's toolchain/ABI rather than
Chromium's:

1. **`bind-internal-std-function`** — `use_custom_libcxx=false` makes V8 compile
   against the MSVC STL, whose `std::function` doesn't match V8's callable
   trait; add a specialization.
2. **`setup_toolchain-pin-vcvars`** — V8 14.6 needs MSVC >= 14.40; pin it.
3. **`compiler-has-exceptions`** — UE builds with `_HAS_EXCEPTIONS=1`/`/EHsc`;
   match it or `std::_Raise_handler` is unresolved at link.
4. **`win-dynamic-crt`** — UE uses the dynamic CRT (`/MD`); V8 defaults to
   static (`/MT`) on desktop.

## Android

V8/Chromium can only target Android from a Linux host; WSL2 Ubuntu works. Keep the work dir on the
Linux filesystem (default `~/v8build`), not `/mnt/c`.

```bash
./Build-V8-Android.sh all arm64     # phones / Meta Quest (arm64-v8a)
./Build-V8-Android.sh build x64     # x86_64 (emulator) reusing the same checkout
./Build-V8-Android.sh stage x64
```

`all` shallow-clones the V8 tag (a full-history fetch tends to stall under WSL2), syncs the Android
deps (~11 GB), downloads NDK r27c, and builds (~15 min per arch on 36 cores). `stage` refuses to copy
a lib that isn't a regular (non-thin) archive built against the NDK libc++.

Why it's set up this way:

- **C++ standard library.** UE links the NDK's static libc++ (`std::__ndk1`) and the plugin passes std
  types across the V8 API (e.g. `std::shared_ptr<BackingStore>`), so V8 is built with
  `use_custom_libcxx=false` against the same NDK UE uses (r27c for UE 5.8). The old 7.4 libs were built
  against libstdc++, which is why they never linked (getnamo/UnrealJs#14).
- **`use_clang_modules=false`**: Chromium otherwise precompiles its bundled libc++ as clang modules,
  which fails once the target uses the NDK libc++.
- **`atomic_ref_compat.h`**: V8 14.6 uses C++20 `std::atomic_ref`, which the NDK libc++ doesn't ship
  (r27c is libc++ 18; even V8's bundled r28 snapshot lacks it). The `patch` step force-includes this
  header for the Android target toolchain. It's header-only and never crosses V8's public API, so it has
  no ABI impact, and it compiles to nothing on a stdlib that has `atomic_ref`.
- **API level 26**: UE 5.8's minimum (V8 defaults to 29).
- **Monolith, not thin archives**: the old Linux libs were thin archives that pointed at `.o` files that
  weren't shipped (getnamo/UnrealJs#10).

## Notes / TODO

- Linux build script is not written yet. Same approach as Android: `use_custom_libcxx=false` against
  UE's bundled libc++ (the `v26_clang` cross-toolchain sysroot).
- If a future V8 version shifts the patched code, the script throws
  "Patch anchor not found" — update the anchor (or the `.patch`) to match.
