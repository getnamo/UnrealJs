// Automation tests for defining UClasses in JavaScript: an ES6 class that
// extends an engine UClass is registered through Content/Scripts/uclass.js,
// which builds a real UJavascriptGeneratedClass_Native behind it. Covers
// declared UPROPERTYs, plain JS methods, and members inherited from the parent.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "JavascriptIsolate.h"
#include "JavascriptContext.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealJsSubclassActorTest,
	"UnrealJS.V8.SubclassActorFromJavascript",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsSubclassActorTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("CreateWorld"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	UJavascriptIsolate* Isolate = NewObject<UJavascriptIsolate>(GetTransientPackage());
	Isolate->AddToRoot();
	TMap<FString, FString> Features = UJavascriptIsolate::DefaultIsolateFeatures();
	Isolate->Init(GIsEditor, Features);

	UJavascriptContext* Context = Isolate->CreateContext();
	auto Teardown = [&]()
	{
		Isolate->RemoveFromRoot();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	if (!TestNotNull(TEXT("CreateContext"), Context))
	{
		Teardown();
		return false;
	}
	Context->Expose(TEXT("GWorld"), World);

	auto Run = [&](const TCHAR* Script) -> FString
	{
		return Context->RunScript(Script, false);
	};

	// The `/*int*/` comment in properties() is how uclass.js declares a typed
	// UPROPERTY; Bump() carries no decoration, so it stays a plain JS method.
	const FString Registered = Run(TEXT(R"JS(
(function () {
	globalThis.MyJsActor = class MyJsActor extends Actor {
		properties() { this.Counter /*int*/; }
		Bump() { this.Counter += 41; return this.Counter; }
	};
	globalThis.MyJsActorClass = require('uclass')()(globalThis, globalThis.MyJsActor);
	return typeof globalThis.MyJsActorClass;
})()
)JS"));
	if (!TestEqual(TEXT("uclass registers the JS class"), Registered, TEXT("function")))
	{
		Teardown();
		return false;
	}

	// The spawned object is a real actor of the generated class.
	TestEqual(TEXT("instance is an Actor and an instance of the JS class"),
		Run(TEXT("(function(){ globalThis.A = new MyJsActorClass(GWorld); return (A instanceof Actor) + ',' + (A instanceof MyJsActorClass); })()")),
		TEXT("true,true"));

	// A declared UPROPERTY round-trips, and an undecorated JS method can use it.
	TestEqual(TEXT("declared UPROPERTY drives a plain JS method"),
		Run(TEXT("(function(){ A.Counter = 1; return A.Bump() + ',' + A.Counter; })()")),
		TEXT("42,42"));

	// Members inherited from AActor still resolve on the subclass.
	TestEqual(TEXT("inherited UPROPERTY"),
		Run(TEXT("(function(){ A.CustomTimeDilation = 2.5; return '' + A.CustomTimeDilation; })()")),
		TEXT("2.5"));

	TestEqual(TEXT("inherited UFUNCTION returning a USTRUCT"),
		Run(TEXT("(function(){ let l = A.K2_GetActorLocation(); return (typeof l.X) + ',' + (typeof l.Y) + ',' + (typeof l.Z); })()")),
		TEXT("number,number,number"));

	// uclass.js must only collect the JS class's own methods, not re-register
	// the native parent's UFUNCTIONs as plain JS methods on the subclass.
	TestEqual(TEXT("native parent methods are not copied onto the subclass"),
		Run(TEXT("(function(){ let p = MyJsActorClass.prototype; return '' + p.hasOwnProperty('Bump') + ',' + p.hasOwnProperty('K2_GetActorLocation') + ',' + p.hasOwnProperty('K2_DestroyActor'); })()")),
		TEXT("true,false,false"));

	Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
