<#
.SYNOPSIS
    Builds the V8 monolith for Win64 the way UnrealJs (UE 5.7) needs it, and
    stages the result into ThirdParty/v8. Automates the source patches that the
    README otherwise describes by hand. Safe to re-run (patching is idempotent).

.DESCRIPTION
    Steps:
      1. (optional) -Fetch: set up depot_tools + fetch the V8 source at -Branch.
      2. Apply the required V8-source patches (see ./patches for the diffs).
      3. Write the UE-matched gn args to <V8Root>/out/<OutDir>/args.gn.
      4. gn gen + ninja v8_monolith.
      5. Stage v8_monolith.lib -> ThirdParty/v8/lib/Win64/<Release|Debug>/ and
         refresh ThirdParty/v8/include from the checkout.

.PARAMETER V8Root
    Path to the V8 checkout (the folder containing 'include', 'src', 'build',
    'out'). If it doesn't exist and -Fetch is given, it will be fetched here.

.PARAMETER DepotTools
    Path to an up-to-date depot_tools. The copy bundled with the UE toolchain is
    too old to parse modern V8 DEPS, so by default we use/clone a fresh one next
    to V8Root.

.PARAMETER Config
    Release (default) or Debug. Release is the validated configuration.

.PARAMETER VcVarsVer
    MSVC toolset to pin (V8 14.6 needs >= 14.40; the install default may be older
    and fails to compile). Defaults to 14.44.35207.

.PARAMETER Fetch
    Clone depot_tools (if needed) and fetch/sync V8 at -Branch before building.

.PARAMETER Branch
    V8 release branch to checkout when -Fetch is used. Default: branch-heads/14.6.

.EXAMPLE
    ./Build-V8-Win64.ps1 -V8Root C:\v8work\v8

.EXAMPLE
    # One-shot from scratch (downloads several GB, takes a while):
    ./Build-V8-Win64.ps1 -V8Root C:\v8work\v8 -Fetch
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $V8Root,
    [string] $DepotTools,
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [string] $VcVarsVer = '14.44.35207',
    [switch] $Fetch,
    [string] $Branch = 'branch-heads/14.6'
)

$ErrorActionPreference = 'Stop'
$ThirdPartyV8 = Split-Path -Parent $PSScriptRoot   # ...\ThirdParty\v8
$OutDir = if ($Config -eq 'Debug') { 'win64.debug' } else { 'win64.release' }
$StageCfg = $Config

function Write-Step([string] $msg) { Write-Host "==== $msg ====" -ForegroundColor Cyan }

# --- Idempotent in-place text patch helper ------------------------------------
function Apply-Patch {
    param([string] $File, [string] $Find, [string] $Replace, [string] $AlreadyMarker)
    if (-not (Test-Path $File)) { throw "Patch target missing: $File" }
    $text = [System.IO.File]::ReadAllText($File)
    if ($text.Contains($AlreadyMarker)) {
        Write-Host "  already patched: $File"
        return
    }
    if (-not $text.Contains($Find)) {
        throw "Patch anchor not found in $File (V8 layout may have changed; see ./patches)."
    }
    $text = $text.Replace($Find, $Replace)
    [System.IO.File]::WriteAllText($File, $text, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "  patched: $File"
}

# --- Optional fetch -----------------------------------------------------------
if (-not $DepotTools) {
    $DepotTools = Join-Path (Split-Path -Parent $V8Root) 'depot_tools'
}
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$env:GCLIENT_SUPPRESS_GIT_VERSION_WARNING = '1'

if ($Fetch) {
    Write-Step "Fetching depot_tools + V8 ($Branch)"
    if (-not (Test-Path (Join-Path $DepotTools 'gclient.bat'))) {
        & git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git $DepotTools
    }
    $env:PATH = "$DepotTools;$env:PATH"
    $parent = Split-Path -Parent $V8Root
    if (-not (Test-Path (Join-Path $parent '.gclient'))) {
        Push-Location $parent; & cmd /c 'fetch v8'; Pop-Location
    }
    Push-Location $V8Root
    & git fetch origin "refs/$Branch`:refs/heads/v8-build"
    & git checkout v8-build
    & cmd /c 'gclient sync -D --force'
    Pop-Location
}
else {
    $env:PATH = "$DepotTools;$env:PATH"
}

if (-not (Test-Path $V8Root)) { throw "V8Root not found: $V8Root  (run with -Fetch to set it up)" }

# --- 1) Patches ---------------------------------------------------------------
Write-Step 'Applying V8 source patches (idempotent)'

# (a) MSVC STL: std::function -> base::FunctionRef trait specialization.
$bind = Join-Path $V8Root 'src/base/functional/bind-internal.h'
Apply-Patch -File $bind -AlreadyMarker "#include <functional>" `
    -Find "#define V8_BASE_FUNCTIONAL_BIND_INTERNAL_H_`n`n#include <type_traits>" `
    -Replace "#define V8_BASE_FUNCTIONAL_BIND_INTERNAL_H_`n`n#include <functional>`n#include <type_traits>"
Apply-Patch -File $bind -AlreadyMarker 'std::function<R(Args...)>, Signature' `
    -Find "#undef BIND_INTERNAL_EXTRACT_CALLABLE_RUN_TYPE_WITH_QUALS`n`n// Evaluated to the RunType" `
    -Replace @"
#undef BIND_INTERNAL_EXTRACT_CALLABLE_RUN_TYPE_WITH_QUALS

// MSVC STL compatibility (UnrealJs embed build, use_custom_libcxx=false): the
// member-pointer specializations above don't match MSVC's std::function::
// operator(), so without this std::function falls through to the undefined
// primary template. Key directly on std::function instead.
template <typename R, typename... Args, typename Signature>
struct ExtractCallableRunTypeImpl<std::function<R(Args...)>, Signature> {
  using Type = R(Args...);
};

// Evaluated to the RunType
"@

# (b) Pin the MSVC toolset used by the build.
$setup = Join-Path $V8Root 'build/toolchain/win/setup_toolchain.py'
Apply-Patch -File $setup -AlreadyMarker '-vcvars_ver=' `
    -Find "    args.append(SDK_VERSION)`n    variables = _LoadEnvFromBat(args)" `
    -Replace "    args.append(SDK_VERSION)`n    args.append('-vcvars_ver=$VcVarsVer')`n    variables = _LoadEnvFromBat(args)"

# (c) Match UE's STL exception config (_HAS_EXCEPTIONS=1 + /EHsc on Windows).
$compiler = Join-Path $V8Root 'build/config/compiler/BUILD.gn'
# The patched no_exceptions block is structurally identical to the `exceptions`
# config, so we tag the replacement with a one-line marker comment and key on it.
Apply-Patch -File $compiler -AlreadyMarker '# UnrealJs: match UE STL (_HAS_EXCEPTIONS=1 + /EHsc)' `
    -Find "    if (!use_custom_libcxx) {`n      defines = [ `"_HAS_EXCEPTIONS=0`" ]`n    }`n  } else {" `
    -Replace "    # UnrealJs: match UE STL (_HAS_EXCEPTIONS=1 + /EHsc)`n    if (!use_custom_libcxx) {`n      defines = [ `"_HAS_EXCEPTIONS=1`" ]`n    }`n    cflags_cc = [ `"/EHsc`" ]`n  } else {"

# (d) Match UE's dynamic CRT (/MD) on desktop Windows.
$winbuild = Join-Path $V8Root 'build/config/win/BUILD.gn'
Apply-Patch -File $winbuild -AlreadyMarker 'Desktop Windows: dynamic CRT' `
    -Find "      # Desktop Windows: static CRT.`n      configs = [ `":static_crt`" ]" `
    -Replace "      # Desktop Windows: dynamic CRT (/MD) to match Unreal Engine's runtime.`n      configs = [ `":dynamic_crt`" ]"

# --- 2) gn args ---------------------------------------------------------------
Write-Step "Writing gn args ($OutDir)"
$outPath = Join-Path $V8Root "out/$OutDir"
New-Item -ItemType Directory -Force $outPath | Out-Null
$argsSrc = Join-Path $PSScriptRoot 'args/win64.release.gn'
$argsText = [System.IO.File]::ReadAllText($argsSrc)
if ($Config -eq 'Debug') { $argsText = $argsText.Replace('is_debug = false', 'is_debug = true') }
[System.IO.File]::WriteAllText((Join-Path $outPath 'args.gn'), $argsText, (New-Object System.Text.UTF8Encoding($false)))

# --- 3) Build -----------------------------------------------------------------
Write-Step 'gn gen + ninja v8_monolith (this takes a while)'
Push-Location $V8Root
& cmd /c "gn gen out/$OutDir"
if ($LASTEXITCODE -ne 0) { Pop-Location; throw 'gn gen failed' }
& cmd /c "ninja -C out/$OutDir v8_monolith"
if ($LASTEXITCODE -ne 0) { Pop-Location; throw 'ninja build failed' }
Pop-Location

# --- 4) Stage -----------------------------------------------------------------
Write-Step 'Staging lib + headers into ThirdParty/v8'
$libDst = Join-Path $ThirdPartyV8 "lib/Win64/$StageCfg"
New-Item -ItemType Directory -Force $libDst | Out-Null
Copy-Item (Join-Path $V8Root "out/$OutDir/obj/v8_monolith.lib") (Join-Path $libDst 'v8_monolith.lib') -Force

$incDst = Join-Path $ThirdPartyV8 'include'
if (Test-Path $incDst) { Remove-Item -Recurse -Force $incDst }
New-Item -ItemType Directory -Force $incDst | Out-Null
Copy-Item (Join-Path $V8Root 'include/*') $incDst -Recurse -Force
# Keep the vendored debug-console header (removed from the public V8 include set).
$vendored = Join-Path $PSScriptRoot 'vendored/interface-types.h'
if (Test-Path $vendored) { Copy-Item $vendored (Join-Path $incDst 'interface-types.h') -Force }

$ver = Get-Content (Join-Path $incDst 'v8-version.h') | Select-String 'V8_(MAJOR|MINOR|BUILD)_VERSION'
Write-Step 'Done'
Write-Host "Staged v8_monolith.lib -> $libDst"
$ver | ForEach-Object { Write-Host "  $_" }
