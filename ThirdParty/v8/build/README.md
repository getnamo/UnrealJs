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
| `args/win64.release.gn` | The UE-matched gn args (must stay in sync with the ABI defines in `Source/V8/V8.Build.cs`). |
| `patches/*.patch` | The required V8-source patches as reference diffs (the script applies these automatically; the `.patch` files are the canonical record and are reusable for the Linux/Android bash flow). |
| `vendored/interface-types.h` | The `v8::debug` console header V8 removed from its public include set; re-staged into `include/` after each build. |

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

## Notes / TODO

- Linux and Android (ARM64) build scripts are not written yet (Phase 2). The
  same patches apply; the gn args differ by `target_os`/`target_cpu` and NDK.
- If a future V8 version shifts the patched code, the script throws
  "Patch anchor not found" — update the anchor (or the `.patch`) to match.
