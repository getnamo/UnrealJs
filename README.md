# Unreal.js Plugin - getnamo fork

Some opinionated changes to add certain features for live gameplay coding. Not all upstream functionality is guaranteed, but best effort is used when upgrading engine versions.

See upstream for wiki/instructions, API is largely the same: https://github.com/ncsoft/Unreal.js

## V8 version

As of **2.0.0**, this fork targets **UE 5.7** and embeds **V8 14.6.202** (built from
the `branch-heads/14.6` stable line), upgraded from the long-standing V8 7.4.288. V8 is
linked as a single monolithic static library (`ThirdParty/v8/lib/<Platform>/Release/v8_monolith.lib`).
Win64 is built and validated; Linux and Android (ARM64) are in progress.

## Build & setup

The prebuilt V8 monolith + headers ship under `ThirdParty/v8/`, so for normal use you only
need to enable the plugin and build your project as usual (UE 5.7).

### Rebuilding V8 from source (only needed to bump the V8 version / add a platform)

V8 has no prebuilt UE-compatible distribution, so it is built from source:

1. Use an up-to-date **depot_tools** (the copy bundled with the UE toolchain is too old to
   parse modern V8 `DEPS`). `fetch v8`, then `git checkout branch-heads/14.6` and `gclient sync`.
2. The V8 build must be configured to match the engine ABI. Apply these **gn args**
   (`out/<cfg>/args.gn`): `v8_monolithic=true`, `is_component_build=false`,
   `v8_use_external_startup_data=false`, `use_custom_libcxx=false`,
   `v8_enable_pointer_compression=true`, `v8_enable_sandbox=false`,
   `v8_enable_i18n_support=false`, `use_allocator_shim=false`,
   `v8_enable_temporal_support=false`.
3. Apply these **V8 source patches** (required for a UE-linkable Windows build; re-apply after
   any `gclient sync`):
   - `build/toolchain/win/setup_toolchain.py` — pin the MSVC toolset via `-vcvars_ver` (V8 14.6
     needs MSVC 14.40+; the default may be older and fails to compile).
   - `src/base/functional/bind-internal.h` — add `#include <functional>` and a `std::function`
     specialization of `ExtractCallableRunTypeImpl` (MSVC STL compatibility).
   - `build/config/compiler/BUILD.gn` — `config("no_exceptions")` on Windows must use
     `_HAS_EXCEPTIONS=1` + `/EHsc` to match UE (otherwise `std::_Raise_handler` link errors).
   - `build/config/win/BUILD.gn` — `default_crt` desktop branch must select `dynamic_crt` (`/MD`,
     matching UE) instead of `static_crt`.
4. `ninja -C out/<cfg> v8_monolith`, then copy `obj/v8_monolith.lib` to
   `ThirdParty/v8/lib/<Platform>/Release/` and refresh `ThirdParty/v8/include/` from the checkout.

`Source/V8/V8.Build.cs` links the monolith plus `winmm/dbghelp/shlwapi/bcrypt`, and defines
`V8_COMPRESS_POINTERS`, `V8_31BIT_SMIS_ON_64BIT_ARCH`, and `V8_COMPRESS_POINTERS_IN_SHARED_CAGE`.
These **must** match the gn args the lib was built with or every V8 handle silently corrupts.

## Tests

C++ automation tests (`UnrealJS.V8.*`) spawn and manipulate real actors from JavaScript. Run headless:

```
UnrealEditor-Cmd <Project>.uproject -ExecCmds="Automation RunTests UnrealJS.V8;Quit" -unattended -nullrhi -nopause -log
```

## Fork Changes

### Async

Call a function on a background thread and optionally expose communication bridges auto-magically.

[![example.png](https://i.imgur.com/ozenYI8.png)](https://twitter.com/getnamo/status/1276977110762979328)

Format is 
```Async.Lambda( capture params, function, result callback );```

See https://github.com/getnamo/UnrealJs/blob/master/Content/Scripts/async.js for detailed API.

### JavascriptInstance

A javascript component with more fine-grained control over features exposed to js. Can also re-use contexts and isolates.

[![exposure.png](https://i.imgur.com/cZsjeRn.png)](https://twitter.com/getnamo/status/1271772156611817472)

See https://github.com/getnamo/UnrealJs/blob/master/Source/V8/Public/JavascriptInstanceComponent.h for detailed API.


### UModule Package Manager

Should enable 'require' like include with code and asset support and simple external package management for modular development.

WIP: https://github.com/getnamo/UnrealJs/tree/feature-umodule
