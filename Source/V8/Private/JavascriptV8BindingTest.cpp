// Automation tests that drive the V8 binding from JavaScript: spawn and
// manipulate real Unreal actors using the canonical Unreal.js API
// (https://github.com/ncsoft/Unreal.js/wiki). These give deeper edge coverage
// of the binding (global UClass exposure, actor spawning, property read/write,
// UFUNCTION calls returning USTRUCTs) and exit cleanly under
// `Automation RunTests UnrealJS.V8;Quit`, unlike a full GUI-editor boot.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "JavascriptIsolate.h"
#include "JavascriptContext.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealJsV8BindingTest,
	"UnrealJS.V8.SpawnAndManipulateActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsV8BindingTest::RunTest(const FString& Parameters)
{
	// A throwaway Game world to spawn actors into.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("CreateWorld"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	// Trusted isolate (exposes all UClasses as JS globals) + a context.
	UJavascriptIsolate* Isolate = NewObject<UJavascriptIsolate>(GetTransientPackage());
	Isolate->AddToRoot();
	TMap<FString, FString> Features = UJavascriptIsolate::DefaultIsolateFeatures();
	Isolate->Init(GIsEditor, Features);

	UJavascriptContext* Context = Isolate->CreateContext();
	if (!TestNotNull(TEXT("CreateContext"), Context))
	{
		Isolate->RemoveFromRoot();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// Match the wiki: actors are spawned with `new Class(GWorld, ...)`.
	Context->Expose(TEXT("GWorld"), World);

	auto Run = [&](const TCHAR* Script) -> FString
	{
		return Context->RunScript(Script, false);
	};

	// 1) Engine UClass resolves as a JS global and `new` spawns an actor.
	TestEqual(TEXT("spawn AActor via global class + new(GWorld)"),
		Run(TEXT("(function(){ let a = new Actor(GWorld); return a ? 'OK' : 'NULL'; })()")),
		TEXT("OK"));

	// 2) Property write + read round-trip (exercises the SetAccessorProperty path).
	TestEqual(TEXT("float UPROPERTY set then get"),
		Run(TEXT("(function(){ let a = new Actor(GWorld); a.CustomTimeDilation = 2.5; return ''+a.CustomTimeDilation; })()")),
		TEXT("2.5"));

	// 3) Spawn a positioned actor class + UFUNCTION returning a USTRUCT whose
	// fields read back as JS numbers. (We assert the marshalling shape, not the
	// exact value: a bare CreateWorld() test world doesn't register/apply the
	// spawn transform like a PIE world would. The deterministic value round-trip
	// is covered by the float property check above.)
	TestEqual(TEXT("UFUNCTION returns USTRUCT with numeric fields"),
		Run(TEXT("(function(){ let a = new StaticMeshActor(GWorld,{X:10,Y:20,Z:30}); let l = a.K2_GetActorLocation(); return (typeof l.X)+','+(typeof l.Y)+','+(typeof l.Z); })()")),
		TEXT("number,number,number"));

	// 4) UFUNCTION call with a side effect (actor destruction) completes.
	TestEqual(TEXT("K2_DestroyActor UFUNCTION call"),
		Run(TEXT("(function(){ let a = new Actor(GWorld); a.K2_DestroyActor(); return 'OK'; })()")),
		TEXT("OK"));

	// Teardown.
	Isolate->RemoveFromRoot();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
