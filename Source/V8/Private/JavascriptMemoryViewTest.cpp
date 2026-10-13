// Automation tests for binary data between scripts and C++: typed arrays into TArray parameters, and
// memory.access views that are detached when the memory they show goes away.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "JavascriptIsolate.h"
#include "JavascriptContext.h"
#include "JavascriptMemoryObject.h"
#include "JavascriptAsync.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace JavascriptMemoryViewTest
{
	/** One isolate and context for a test (rooted while it runs). */
	struct FScriptEnv
	{
		UJavascriptIsolate* Isolate = nullptr;
		UJavascriptContext* Context = nullptr;

		FScriptEnv()
		{
			Isolate = NewObject<UJavascriptIsolate>(GetTransientPackage());
			Isolate->AddToRoot();
			TMap<FString, FString> Features = UJavascriptIsolate::DefaultIsolateFeatures();
			Isolate->Init(GIsEditor, Features);
			Context = Isolate->CreateContext();
		}

		~FScriptEnv()
		{
			// Dispose the isolate now: full isolates left for a later GC add up (V8 shares one pointer cage)
			Isolate->RemoveFromRoot();
			Context = nullptr;
			Isolate = nullptr;
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}

		FString Run(const FString& Script)
		{
			return Context ? Context->RunScript(Script, false) : FString();
		}
	};
}

using namespace JavascriptMemoryViewTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsTypedArrayTest, "UnrealJS.V8.TypedArrays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsTypedArrayTest::RunTest(const FString& Parameters)
{
	FScriptEnv Env;
	if (!TestNotNull(TEXT("Context"), Env.Context))
	{
		return false;
	}
	auto Max = [&Env](const TCHAR* Function, const TCHAR* Argument)
	{
		return Env.Run(FString::Printf(TEXT("(function(){ const r = KismetMathLibrary.%s(%s); return r.IndexOfMaxValue + ':' + r.MaxValue; })()"), Function, Argument));
	};

	// Same element type: copied as is (TArray<float>, TArray<int32>, TArray<uint8>)
	TestEqual(TEXT("Float32Array -> TArray<float>"), Max(TEXT("MaxOfFloatArray"), TEXT("new Float32Array([1.5, 7.25, 3])")), FString(TEXT("1:7.25")));
	TestEqual(TEXT("Int32Array -> TArray<int32>"), Max(TEXT("MaxOfIntArray"), TEXT("new Int32Array([4, -2, 11, 3])")), FString(TEXT("2:11")));
	TestEqual(TEXT("Uint8Array -> TArray<uint8>"), Max(TEXT("MaxOfByteArray"), TEXT("new Uint8Array([3, 200, 7])")), FString(TEXT("1:200")));

	// Views copy their own range, not the whole buffer
	TestEqual(TEXT("Float32Array.subarray"), Max(TEXT("MaxOfFloatArray"), TEXT("new Float32Array([100, 2, 30, 4, 500]).subarray(1, 4)")), FString(TEXT("1:30")));
	TestEqual(TEXT("Uint8Array at an offset"), Max(TEXT("MaxOfByteArray"), TEXT("new Uint8Array(Uint8Array.from([250, 0, 0, 0, 5, 9, 1, 0]).buffer, 4, 3)")), FString(TEXT("1:9")));

	// Other element types are converted per element
	TestEqual(TEXT("Float64Array -> TArray<float>"), Max(TEXT("MaxOfFloatArray"), TEXT("new Float64Array([1, 9.5, 2])")), FString(TEXT("1:9.5")));
	TestEqual(TEXT("Uint8Array -> TArray<int32>"), Max(TEXT("MaxOfIntArray"), TEXT("new Uint8Array([4, 200, 3])")), FString(TEXT("1:200")));
	TestEqual(TEXT("Int16Array -> TArray<float>"), Max(TEXT("MaxOfFloatArray"), TEXT("new Int16Array([-5, -1, -9])")), FString(TEXT("1:-1")));

	// Raw bytes: an ArrayBuffer or a DataView into TArray<uint8>
	TestEqual(TEXT("ArrayBuffer -> TArray<uint8>"), Max(TEXT("MaxOfByteArray"), TEXT("Uint8Array.from([1, 2, 99, 4]).buffer")), FString(TEXT("2:99")));
	TestEqual(TEXT("DataView -> TArray<uint8>"), Max(TEXT("MaxOfByteArray"), TEXT("new DataView(Uint8Array.from([250, 6, 8, 251]).buffer, 1, 2)")), FString(TEXT("1:8")));

	// Plain arrays still work
	TestEqual(TEXT("Array -> TArray<float>"), Max(TEXT("MaxOfFloatArray"), TEXT("[0.5, 0.25]")), FString(TEXT("0:0.5")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsMemoryViewTest, "UnrealJS.V8.MemoryViews",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsMemoryViewTest::RunTest(const FString& Parameters)
{
	FScriptEnv Env;
	if (!TestNotNull(TEXT("Context"), Env.Context))
	{
		return false;
	}

	// External memory: a script reads and writes it in place
	TArray<float> Values = { 1.f, 2.f, 3.f, 4.f };
	int32 Releases = 0;
	UJavascriptExternalMemory* Memory = NewObject<UJavascriptExternalMemory>();
	Memory->AddToRoot();
	Memory->SetMemory(Values.GetData(), Values.Num() * sizeof(float), [&Releases]() { Releases++; });
	Env.Context->Expose(TEXT("Lent"), Memory);

	TestEqual(TEXT("View over the memory"), Env.Run(TEXT("globalThis.view = new Float32Array(memory.access(Lent)); view[1] = 20; view.length + ':' + view[3]")), FString(TEXT("4:4")));
	TestEqual(TEXT("Writes land in C++ memory"), Values[1], 20.f);
	TestEqual(TEXT("A second view"), Env.Run(TEXT("globalThis.kept = memory.access(Lent); kept.byteLength")), FString(TEXT("16")));

	// Release from C++: every view is detached, the memory is given back once
	Memory->Release();
	TestEqual(TEXT("Released once"), Releases, 1);
	TestEqual(TEXT("Views read as empty"), Env.Run(TEXT("view.length + ':' + kept.byteLength + ':' + view[1]")), FString(TEXT("0:0:undefined")));
	TestEqual(TEXT("No memory left"), Env.Run(TEXT("Lent.HasMemory()")), FString(TEXT("false")));
	TestEqual(TEXT("Values untouched after release"), Values[1], 20.f);

	// Release from the script
	Memory->SetMemory(Values.GetData(), Values.Num() * sizeof(float), [&Releases]() { Releases++; });
	TestEqual(TEXT("Script releases"), Env.Run(TEXT("(function(){ const v = new Uint8Array(memory.access(Lent)); const before = v.length; Lent.Release(); return before + ':' + v.length; })()")), FString(TEXT("16:0")));
	TestEqual(TEXT("Released twice in all"), Releases, 2);

	// Setting new memory releases the old (and its views)
	Memory->SetMemory(Values.GetData(), Values.Num() * sizeof(float), [&Releases]() { Releases++; });
	Env.Run(TEXT("globalThis.late = new Float32Array(memory.access(Lent));"));
	Memory->SetMemory(Values.GetData() + 2, 2 * sizeof(float));
	TestEqual(TEXT("Replacing the memory releases the old"), Releases, 3);
	TestEqual(TEXT("Views of the old memory read as empty, new views see the new"), Env.Run(TEXT("late.length + ':' + new Float32Array(memory.access(Lent))[0]")), FString(TEXT("0:3")));
	Memory->Release();
	Memory->RemoveFromRoot();

	// Other memory objects: worker bytes are only valid during a message
	UJavascriptWorkerBytes* Bytes = NewObject<UJavascriptWorkerBytes>();
	Bytes->AddToRoot();
	Bytes->Bytes = { 7, 8, 9 };
	Env.Context->Expose(TEXT("WorkerBytes"), Bytes);
	TestEqual(TEXT("Worker bytes view"), Env.Run(TEXT("globalThis.wb = new Uint8Array(memory.access(WorkerBytes)); wb[2]")), FString(TEXT("9")));
	Bytes->ReleaseMemoryViews();
	Bytes->Bytes.Empty();
	TestEqual(TEXT("Worker bytes view detached"), Env.Run(TEXT("wb.length")), FString(TEXT("0")));
	Bytes->RemoveFromRoot();

	// Views the script dropped don't pile up (weak), and a fresh view after a release works
	Memory = NewObject<UJavascriptExternalMemory>();
	Memory->AddToRoot();
	Memory->SetMemory(Values.GetData(), Values.Num() * sizeof(float));
	Env.Context->Expose(TEXT("Lent2"), Memory);
	TestEqual(TEXT("Many short-lived views"), Env.Run(TEXT("(function(){ let s = 0; for (let i = 0; i < 2000; i++) { s += new Float32Array(memory.access(Lent2))[1]; } return s; })()")), FString(TEXT("40000")));
	Memory->Release();
	Memory->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
