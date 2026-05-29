using UnrealBuildTool;
using System.IO;
using System;

public class V8 : ModuleRules
{
    protected string ThirdPartyPath
    {
        get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty")); }
    }

    public int[] GetV8Version()
    {
        string[] VersionHeader = Utils.ReadAllText(Path.Combine(ThirdPartyPath, "v8", "include", "v8-version.h")).Replace("\r\n", "\n").Replace("\t", " ").Split('\n');
        string VersionMajor = "0";
        string VersionMinor = "0";
        string VersionPatch = "0";
        foreach (string Line in VersionHeader)
        {
            if (Line.StartsWith("#define V8_MAJOR_VERSION"))
            {
                VersionMajor = Line.Split(' ')[2];
            }
            else if (Line.StartsWith("#define V8_MINOR_VERSION "))
            {
                VersionMinor = Line.Split(' ')[2];
            }
            else if (Line.StartsWith("#define V8_PATCH_VERSION "))
            {
                VersionPatch = Line.Split(' ')[2];
            }
        }
        return new int[] { Int32.Parse(VersionMajor), Int32.Parse(VersionMinor), Int32.Parse(VersionPatch) };
    }

    public V8(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bLegacyPublicIncludePaths = false;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Error;
        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(ThirdPartyPath, "v8", "include")
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine", "Sockets", "ApplicationCore", "NavigationSystem", "OpenSSL"
        });

        if (Target.bBuildEditor)
        {
            PublicDependencyModuleNames.AddRange(new string[]
            {
                "DirectoryWatcher"
            });
        }

        HackWebSocketIncludeDir(Path.Combine(Directory.GetCurrentDirectory(), "ThirdParty", "libWebSockets", "libwebsockets"), Target);

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd"
            });
        }

        bEnableExceptions = true;

        LoadV8(Target);
    }

    private void HackWebSocketIncludeDir(String WebsocketPath, ReadOnlyTargetRules Target)
    {
        string PlatformSubdir = Target.Platform.ToString();

        bool bHasZlib = false;

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PlatformSubdir = Path.Combine(PlatformSubdir, "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
            bHasZlib = true;

        }
        /*else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PlatformSubdir = Path.Combine(PlatformSubdir, Target.Architecture);
        }*/

        PrivateDependencyModuleNames.Add("libWebSockets");

        if (bHasZlib)
        {
            PrivateDependencyModuleNames.Add("zlib");
        }
        PrivateIncludePaths.Add(Path.Combine(WebsocketPath, "include"));
        PrivateIncludePaths.Add(Path.Combine(WebsocketPath, "include", PlatformSubdir));
    }

    private bool LoadV8(ReadOnlyTargetRules Target)
    {
        int[] v8_version = GetV8Version();
        bool ShouldLink_libsampler = !(v8_version[0] == 5 && v8_version[1] < 3);
        bool ShouldLink_lib_v8_compiler = (v8_version[0] > 6 && v8_version[1] > 6);
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string LibrariesPath = Path.Combine(ThirdPartyPath, "v8", "lib");

            LibrariesPath = Path.Combine(LibrariesPath, "Win64");

            if (Target.Configuration == UnrealTargetConfiguration.Debug)
            {
                LibrariesPath = Path.Combine(LibrariesPath, "Debug");
            }
            else
            {
                LibrariesPath = Path.Combine(LibrariesPath, "Release");
            }

            // V8 14.6+ is linked as a single monolithic static library
            // (gn: v8_monolithic=true, is_component_build=false).
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "v8_monolith.lib"));

            // System libraries the V8 monolith depends on (RNG, timers, symbols).
            PublicSystemLibraries.AddRange(new string[]
            {
                "winmm.lib", "dbghelp.lib", "shlwapi.lib", "bcrypt.lib"
            });

            PublicDefinitions.Add(string.Format("WITH_V8=1"));

            PublicDefinitions.Add(string.Format("USING_V8_PLATFORM_SHARED=0"));

            // ABI defines MUST match the gn args the monolith was built with.
            // Built with v8_enable_pointer_compression=true, v8_enable_sandbox=false.
            // These control Tagged-pointer / Smi sizes in the public headers; a
            // mismatch silently corrupts every handle. Do NOT define V8_ENABLE_SANDBOX.
            PublicDefinitions.Add("V8_COMPRESS_POINTERS");
            PublicDefinitions.Add("V8_31BIT_SMIS_ON_64BIT_ARCH");
            PublicDefinitions.Add("V8_COMPRESS_POINTERS_IN_SHARED_CAGE");

            return true;
        }
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
            string LibrariesPath = Path.Combine(ThirdPartyPath, "v8", "lib", "Android", "ARM64");
            //string LibrariesPath = Path.Combine(ThirdPartyPath, "v8", "lib", "Android", "ARMv7");

            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_init.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_initializers.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_base.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libbase.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libplatform.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_nosnapshot.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libsampler.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libtorque_generated_initializers.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libinspector.a"));

            PublicDefinitions.Add(string.Format("WITH_V8=1"));

            return true;
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            string LibrariesPath = Path.Combine(ThirdPartyPath, "v8", "lib", "Linux");
            if (Target.Configuration == UnrealTargetConfiguration.Debug)
            {
                LibrariesPath = Path.Combine(LibrariesPath, "Debug");
            }
            else
            {
                LibrariesPath = Path.Combine(LibrariesPath, "Release");
            }

            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_init.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_initializers.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libbase.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libplatform.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_nosnapshot.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libsampler.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libtorque_base.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libtorque_generated_initializers.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libinspector.a"));

            if (ShouldLink_lib_v8_compiler)
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_compiler.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_base_without_compiler.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libinspector_string_conversions.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libencoding.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libbindings.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libtorque_generated_definitions.a"));
            }
            else
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_base.a"));
            }

            PublicDefinitions.Add(string.Format("WITH_V8=1"));

            return true;
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string LibrariesPath = Path.Combine(ThirdPartyPath, "v8", "lib", "Mac");

            if (Target.Configuration == UnrealTargetConfiguration.Debug)
            {
                LibrariesPath = Path.Combine(LibrariesPath, "Debug");
            }
            else
            {
                LibrariesPath = Path.Combine(LibrariesPath, "Release");
            }

            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_init.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_initializers.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libbase.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libplatform.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_nosnapshot.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libsampler.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libtorque_base.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libtorque_generated_initializers.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libinspector.a"));


            if (ShouldLink_lib_v8_compiler)
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_compiler.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_base_without_compiler.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libinspector_string_conversions.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libencoding.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libbindings.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libtorque_generated_definitions.a"));
            }
            else
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_base.a"));
            }

            PublicDefinitions.Add(string.Format("WITH_V8=1"));

            return true;
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            string LibrariesPath = Path.Combine(ThirdPartyPath, "v8", "lib", "IOS");

            if (Target.Configuration == UnrealTargetConfiguration.Debug)
            {
                LibrariesPath = Path.Combine(LibrariesPath, "Debug");
            }
            else
            {
                LibrariesPath = Path.Combine(LibrariesPath, "Release");
            }

            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_init.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_initializers.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libbase.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libplatform.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_nosnapshot.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_libsampler.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libtorque_generated_initializers.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libinspector.a"));

            if (ShouldLink_lib_v8_compiler)
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_compiler.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_base_without_compiler.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libinspector_string_conversions.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libencoding.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libbindings.a"));
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libtorque_generated_definitions.a"));
            }
            else
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libv8_base.a"));
            }

            PublicDefinitions.Add(string.Format("WITH_V8=1"));

            return true;
        }

        PublicDefinitions.Add(string.Format("WITH_V8=0"));
        return false;
    }
}
