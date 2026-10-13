// Automation tests for Async.Lambda workers (async.js / async-worker.js / FJavascriptWorkerHost).
// Run headless: Automation RunTests UnrealJS.Async;Quit

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "JavascriptIsolate.h"
#include "JavascriptContext.h"
#include "JavascriptAsync.h"
#include "JavascriptInstanceComponent.h"
#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** A game-thread context with async.js loaded and an Async object, as UJavascriptInstanceComponent sets up */
	struct FAsyncTestEnv
	{
		FAutomationTestBase& Test;
		UJavascriptIsolate* Isolate = nullptr;
		UJavascriptContext* Context = nullptr;
		UJavascriptAsync* Async = nullptr;
		int32 Loads = 0;

		explicit FAsyncTestEnv(FAutomationTestBase& InTest) : Test(InTest) {}
		~FAsyncTestEnv() { Teardown(); }

		bool Init()
		{
			Isolate = NewObject<UJavascriptIsolate>(GetTransientPackage());
			Isolate->AddToRoot();
			TMap<FString, FString> Features = UJavascriptIsolate::DefaultIsolateFeatures();
			Isolate->Init(GIsEditor, Features);
			Context = Isolate->CreateContext();
			return Reload();
		}

		/**
		 * What a script reload does to async: the old Async object is shut down (stopping its workers)
		 * and async.js runs against a new one. The context is reused because a full game-thread
		 * context per iteration (every UClass exported) would exhaust the test isolate's heap.
		 */
		bool Reload()
		{
			ShutdownAsync();

			Async = NewObject<UJavascriptAsync>(GetTransientPackage());
			Async->AddToRoot();
			const FString ExposedName = FString::Printf(TEXT("Async_%d"), Loads++);
			Context->Expose(ExposedName, Async);
			Run(FString::Printf(TEXT("globalThis.Async = %s;"), *ExposedName));

			const FString AsyncJs = Context->ReadScriptFile(TEXT("async.js"));
			if (!Test.TestFalse(TEXT("async.js found"), AsyncJs.IsEmpty()))
			{
				return false;
			}
			Context->RunScript(AsyncJs, false);
			return Test.TestEqual(TEXT("async.js loaded"), Run(TEXT("typeof Async.Lambda")), FString(TEXT("function")));
		}

		void ShutdownAsync()
		{
			if (Async)
			{
				Async->Shutdown();
				Async->RemoveFromRoot();
				Async = nullptr;
			}
		}

		void Teardown()
		{
			ShutdownAsync();
			if (Isolate)
			{
				Isolate->RemoveFromRoot();
				Isolate = nullptr;
			}
			Context = nullptr;
		}

		FString Run(const FString& Script)
		{
			return Context->RunScript(Script, false);
		}

		/** Delivers worker messages until the JS expression Condition is truthy */
		bool PumpUntil(const FString& Condition, double TimeoutSeconds = 10.0)
		{
			const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
			while (FPlatformTime::Seconds() < Deadline)
			{
				if (Async)
				{
					Async->ProcessWorkerMessages();
				}
				if (Run(FString::Printf(TEXT("!!(%s)"), *Condition)) == TEXT("true"))
				{
					return true;
				}
				FPlatformProcess::Sleep(0.002f);
			}
			return false;
		}
	};

	bool WaitForLiveWorkerThreads(int32 AtMost, double TimeoutSeconds = 5.0)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		while (UJavascriptAsync::NumLiveWorkerThreads() > AtMost)
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				return false;
			}
			FPlatformProcess::Sleep(0.005f);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncLambdaTest, "UnrealJS.Async.Lambda",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncLambdaTest::RunTest(const FString& Parameters)
{
	FAsyncTestEnv Env(*this);
	if (!Env.Init())
	{
		return false;
	}

	// The README/screenshot example: captures, GT bridges, exports, return callback
	Env.Run(TEXT(R"JS(
		globalThis.T = { notes: [], workResults: [], subResult: undefined, total: undefined, valid: undefined };
		const subtractor = (args) => args.a - args.b;
		const notify = (message) => { T.notes.push(message); };
		globalThis.lambda = Async.Lambda({value: 1, zeros: 5, sub: subtractor, notify: notify}, () => {
			const max = value * (10 ** zeros);
			let total = 0;
			let workCount = 0;
			const spin = () => { let i = 0; for (; i < max; i++) {} workCount++; total = total + i; return total; };
			exports.work = () => { const spinResult = spin(); notify(`work ${workCount} complete with ${spinResult}`); return spinResult; };
			exports.addToTotal = (args) => { total = total + args.a + args.b; return total; };
			exports.subResult = () => globalThis.subResult;
			sub({a: 10, b: 3}, c => { globalThis.subResult = c; });
			return {valid: spin() == max};
		}, result => { T.valid = result.valid; });
		lambda.onerror = (e) => { T.error = e.message; };
		// callable before the lambda reports complete: calls queue behind the bootstrap
		for (let i = 0; i < 5; i++) { lambda.exports.work(r => T.workResults.push(r)); }
		lambda.exports.addToTotal({a: 1, b: 5}, result => { T.total = result; });
	)JS"));

	TestTrue(TEXT("lambda finished its calls"), Env.PumpUntil(TEXT("T.total !== undefined && T.notes.length === 5")));
	TestEqual(TEXT("return callback"), Env.Run(TEXT("String(T.valid)")), FString(TEXT("true")));
	TestEqual(TEXT("export results in call order"), Env.Run(TEXT("JSON.stringify(T.workResults)")), FString(TEXT("[200000,300000,400000,500000,600000]")));
	TestEqual(TEXT("addToTotal after the queued work"), Env.Run(TEXT("String(T.total)")), FString(TEXT("600006")));
	TestEqual(TEXT("notify bridge ran on game thread"), Env.Run(TEXT("T.notes[4]")), FString(TEXT("work 6 complete with 600000")));
	TestEqual(TEXT("lambda is pinned"), Env.Run(TEXT("String(lambda.pinned)")), FString(TEXT("true")));
	TestEqual(TEXT("export names"), Env.Run(TEXT("JSON.stringify(Object.keys(lambda.exports))")), FString(TEXT("[\"work\",\"addToTotal\",\"subResult\"]")));

	// sub's callback runs in the worker once the GT reply arrives
	Env.Run(TEXT("T.sub = undefined; (async () => { for (;;) { const v = await lambda.exports.subResult(); if (v !== undefined) { T.sub = v; break; } } })();"));
	TestTrue(TEXT("bridge callback ran in worker"), Env.PumpUntil(TEXT("T.sub !== undefined")));
	TestEqual(TEXT("bridge result"), Env.Run(TEXT("String(T.sub)")), FString(TEXT("7")));
	TestEqual(TEXT("no errors"), Env.Run(TEXT("String(T.error)")), FString(TEXT("undefined")));
	TestEqual(TEXT("one live worker"), Env.Async->NumWorkers(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncMessagingTest, "UnrealJS.Async.Messaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncMessagingTest::RunTest(const FString& Parameters)
{
	FAsyncTestEnv Env(*this);
	if (!Env.Init())
	{
		return false;
	}

	Env.Run(TEXT(R"JS(
		globalThis.T = {};
		globalThis.Payload = {s: "it's \"quoted\"\nnew line \\ back   tab\t uni ✓", n: [1, [2, {z: null}]], f: 0.5};
		globalThis.l = Async.Lambda({bad: () => { throw new Error('gt side failed'); }, asyncGt: async (x) => x + 1}, () => {
			exports.echo = (a) => a;
			exports.later = async (a) => { await null; return a.x * 2; };
			exports.boom = () => { throw new Error('kaboom'); };
			exports.bridges = async () => {
				const r = {};
				try { await bad(); } catch (e) { r.bad = e.message; }
				r.asyncGt = await asyncGt(41);
				return r;
			};
		});
		l.onerror = (e) => { T.errors = (T.errors || 0) + 1; };
		(async () => {
			T.echo = JSON.stringify(await l.exports.echo(Payload)) === JSON.stringify(Payload);
			T.later = await l.exports.later({x: 21});
			try { await l.exports.boom(); } catch (e) { T.boom = e.message; T.boomStack = e.stack; }
			try { await l.exports.missing(); } catch (e) { T.missing = e.message; }
			T.bridges = await l.exports.bridges();
			T.done = true;
		})();
	)JS"));

	TestTrue(TEXT("messaging finished"), Env.PumpUntil(TEXT("T.done")));
	TestEqual(TEXT("payload round-trips unchanged"), Env.Run(TEXT("String(T.echo)")), FString(TEXT("true")));
	TestEqual(TEXT("async export"), Env.Run(TEXT("String(T.later)")), FString(TEXT("42")));
	TestEqual(TEXT("throwing export rejects"), Env.Run(TEXT("T.boom")), FString(TEXT("kaboom")));
	TestTrue(TEXT("rejection carries the worker stack"), Env.Run(TEXT("T.boomStack")).Contains(TEXT("async-lambda-")));
	TestEqual(TEXT("missing export rejects"), Env.Run(TEXT("T.missing")), FString(TEXT("no exported function 'missing'")));
	TestEqual(TEXT("GT bridge error rejects in worker"), Env.Run(TEXT("T.bridges.bad")), FString(TEXT("gt side failed")));
	TestEqual(TEXT("promise-returning GT bridge"), Env.Run(TEXT("String(T.bridges.asyncGt)")), FString(TEXT("42")));
	TestEqual(TEXT("onerror saw boom, missing and the GT bridge failure"), Env.Run(TEXT("String(T.errors)")), FString(TEXT("3")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncBinaryTest, "UnrealJS.Async.Binary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncBinaryTest::RunTest(const FString& Parameters)
{
	FAsyncTestEnv Env(*this);
	if (!Env.Init())
	{
		return false;
	}

	// Typed arrays anywhere in a payload cross threads as raw bytes, both directions
	Env.Run(TEXT(R"JS(
		globalThis.T = {};
		globalThis.l = Async.Lambda({ echo: (x) => x }, () => {
			exports.scale = (a) => ({ f: a.f.map((x) => x * 2), raw: a.raw, n: a.n, nested: { d: new Float64Array([a.n, 0.5]) } });
			exports.image = (a) => { const px = new Uint8Array(a.w * a.h * 4); for (let i = 0; i < px.length; i++) { px[i] = i & 255; } return px; };
			exports.viaBridge = async () => { const back = await echo({ i: new Int32Array([-7, 8]) }); return back.i instanceof Int32Array ? Array.from(back.i).join() : 'not typed'; };
		});
		l.onerror = (e) => { T.error = e.message; };
		const big = new Uint8Array(64);
		for (let i = 0; i < 64; i++) { big[i] = i; }
		(async () => {
			const r = await l.exports.scale({ f: new Float32Array([1, 2.5, -3]), raw: big.subarray(3, 7), n: 9 });
			T.f = r.f instanceof Float32Array ? Array.from(r.f).join() : 'not typed';
			T.raw = Array.from(r.raw).join();
			T.d = r.nested.d instanceof Float64Array ? Array.from(r.nested.d).join() : 'not typed';
			const px = await l.exports.image({ w: 192, h: 192 });
			T.px = (px instanceof Uint8Array) + ',' + px.length + ',' + px[300] + ',' + px[147455];
			T.bridge = await l.exports.viaBridge();
			T.done = true;
		})();
	)JS"));

	TestTrue(TEXT("binary calls finished"), Env.PumpUntil(TEXT("T.done || T.error")));
	TestEqual(TEXT("Float32Array round-trip"), Env.Run(TEXT("T.f")), FString(TEXT("2,5,-6")));
	TestEqual(TEXT("subarray sent by its own range"), Env.Run(TEXT("T.raw")), FString(TEXT("3,4,5,6")));
	TestEqual(TEXT("nested Float64Array"), Env.Run(TEXT("T.d")), FString(TEXT("9,0.5")));
	TestEqual(TEXT("147 KB Uint8Array frame"), Env.Run(TEXT("T.px")), FString(TEXT("true,147456,44,255")));
	TestEqual(TEXT("typed array through a GT bridge and back"), Env.Run(TEXT("T.bridge")), FString(TEXT("-7,8")));
	TestEqual(TEXT("no errors"), Env.Run(TEXT("String(T.error)")), FString(TEXT("undefined")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncTimersTest, "UnrealJS.Async.Timers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncTimersTest::RunTest(const FString& Parameters)
{
	const int32 BaselineThreads = UJavascriptAsync::NumLiveWorkerThreads();

	FAsyncTestEnv Env(*this);
	if (!Env.Init())
	{
		return false;
	}

	Env.Run(TEXT(R"JS(
		globalThis.T = {};
		// A pending timer keeps a one-shot alive; it exits after the last one fires
		globalThis.oneShot = Async.Lambda({ done: (v) => { T.done = v; } }, () => {
			let n = 0;
			const id = setInterval(() => { n++; if (n === 3) { clearInterval(id); setTimeout(() => done(n), 20); } }, 5);
		});
		// A free-running loop still answers calls between batches
		globalThis.loop = Async.Lambda(() => {
			let generation = 0;
			const run = () => { generation++; setTimeout(run, 0); };
			run();
			exports.generation = () => generation;
		});
		loop.onerror = oneShot.onerror = (e) => { T.error = e.message; };
		(async () => {
			const a = await loop.exports.generation();
			const b = await loop.exports.generation();
			T.loop = [a, b];
		})();
	)JS"));

	TestTrue(TEXT("interval + timeout ran and the one-shot exited"), Env.PumpUntil(TEXT("T.done !== undefined && oneShot.stopped")));
	TestEqual(TEXT("interval fired 3 times"), Env.Run(TEXT("String(T.done)")), FString(TEXT("3")));
	TestTrue(TEXT("free-running worker answered"), Env.PumpUntil(TEXT("T.loop !== undefined")));
	TestEqual(TEXT("and kept advancing"), Env.Run(TEXT("String(T.loop[1] > T.loop[0] && T.loop[0] > 0)")), FString(TEXT("true")));
	TestEqual(TEXT("no errors"), Env.Run(TEXT("String(T.error)")), FString(TEXT("undefined")));

	Env.ShutdownAsync();
	TestTrue(TEXT("free-running worker stopped on shutdown"), WaitForLiveWorkerThreads(BaselineThreads));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsTextureUploadTest, "UnrealJS.Async.TextureUpload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsTextureUploadTest::RunTest(const FString& Parameters)
{
	FAsyncTestEnv Env(*this);
	if (!Env.Init())
	{
		return false;
	}

	// The subarray is offset into a larger buffer: only its own 32 bytes may be copied
	TestEqual(TEXT("create + upload from a Uint8Array subarray"), Env.Run(TEXT(R"JS(
		(() => {
			const texture = JavascriptLibrary.CreateTransientTexture2D(4, 2, true, false);
			const buffer = new Uint8Array(64).fill(200);
			return [texture.Blueprint_GetSizeX(), texture.Blueprint_GetSizeY(), JavascriptLibrary.UpdateTexture2D(texture, buffer.subarray(16, 48))].join();
		})()
	)JS")), FString(TEXT("4,2,true")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncOneShotTest, "UnrealJS.Async.OneShotNoLeak",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncOneShotTest::RunTest(const FString& Parameters)
{
	const int32 BaselineThreads = UJavascriptAsync::NumLiveWorkerThreads();

	FAsyncTestEnv Env(*this);
	if (!Env.Init())
	{
		return false;
	}

	// One-shots (no exports) exit on their own once their body and bridge calls settle
	Env.Run(TEXT(R"JS(
		globalThis.T = { results: 0, stopped: 0, sum: 0, failed: 0 };
		globalThis.lambdas = [];
		for (let i = 0; i < 64; i++) {
			const l = Async.Lambda({i, ask: (q) => q * 2}, async () => (await ask(i)) + 1, (r) => { T.results++; T.sum += r; });
			l.onerror = () => { T.failed++; };
			lambdas.push(l);
		}
	)JS"));

	TestTrue(TEXT("all one-shots completed and exited"), Env.PumpUntil(TEXT("lambdas.every(l => l.stopped)"), 20.0));
	TestEqual(TEXT("results"), Env.Run(TEXT("String(T.results)")), FString(TEXT("64")));
	TestEqual(TEXT("sum of (2i+1)"), Env.Run(TEXT("String(T.sum)")), FString(TEXT("4096")));
	TestEqual(TEXT("no failures"), Env.Run(TEXT("String(T.failed)")), FString(TEXT("0")));
	TestEqual(TEXT("host tracks no workers"), Env.Async->NumWorkers(), 0);
	TestTrue(TEXT("worker threads exited"), WaitForLiveWorkerThreads(BaselineThreads));

	// A synchronous one-shot closes itself while its bootstrap is still running
	Env.Run(TEXT("T.sync = undefined; globalThis.syncOne = Async.Lambda(() => 6 * 7, (r) => { T.sync = r; });"));
	TestTrue(TEXT("sync one-shot completes and exits"), Env.PumpUntil(TEXT("syncOne.stopped && T.sync === 42")));

	// A body that throws rejects ready and still exits
	Env.Run(TEXT("T.bad = undefined; globalThis.bad = Async.Lambda(() => { throw new TypeError('bad body'); }); bad.onerror = () => {}; bad.ready.catch(e => { T.bad = e.message; });"));
	TestTrue(TEXT("throwing body exits"), Env.PumpUntil(TEXT("bad.stopped && T.bad !== undefined")));
	TestEqual(TEXT("ready rejected with the body's error"), Env.Run(TEXT("T.bad")), FString(TEXT("bad body")));
	TestTrue(TEXT("worker threads exited"), WaitForLiveWorkerThreads(BaselineThreads));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncReloadSoakTest, "UnrealJS.Async.ReloadSoak",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncReloadSoakTest::RunTest(const FString& Parameters)
{
	const int32 BaselineThreads = UJavascriptAsync::NumLiveWorkerThreads();

	FAsyncTestEnv Env(*this);
	if (!Env.Init())
	{
		return false;
	}

	// Each iteration is a "ctrl+s": the old Async object is shut down with its pinned workers, async.js reloads
	constexpr int32 Reloads = 40;
	constexpr int32 PinnedPerReload = 3;
	for (int32 Reload = 0; Reload < Reloads; ++Reload)
	{
		if (Reload > 0 && !Env.Reload())
		{
			return false;
		}

		Env.Run(FString::Printf(TEXT(R"JS(
			globalThis.pinned = [];
			for (let i = 0; i < %d; i++) {
				pinned.push(Async.Lambda({i, tick: () => 0}, () => {
					exports.ping = (x) => x + i;
					tick();
				}));
			}
			globalThis.pongs = 0;
			pinned.forEach((l, i) => l.exports.ping(1, () => pongs++));
		)JS"), PinnedPerReload));

		if (!TestTrue(FString::Printf(TEXT("reload %d: pinned workers answer"), Reload), Env.PumpUntil(FString::Printf(TEXT("pongs === %d"), PinnedPerReload))))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("reload %d: live workers"), Reload), Env.Async->NumWorkers(), PinnedPerReload);

		// Old pinned workers must be gone: threads stay bounded instead of growing with every reload
		TestTrue(FString::Printf(TEXT("reload %d: no leaked threads"), Reload), UJavascriptAsync::NumLiveWorkerThreads() <= BaselineThreads + PinnedPerReload);
	}

	Env.ShutdownAsync();
	TestTrue(TEXT("all worker threads exited after final shutdown"), WaitForLiveWorkerThreads(BaselineThreads));

	// The old implementation parked pinned lambdas on GThreadPool forever; make sure the pool still runs work
	TSharedRef<std::atomic<bool>> bProbeRan = MakeShared<std::atomic<bool>>(false);
	AsyncPool(*GThreadPool, [bProbeRan]() { *bProbeRan = true; });
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (!*bProbeRan && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.005f);
	}
	TestTrue(TEXT("GThreadPool still runs work"), bProbeRan->load());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncTerminateTest, "UnrealJS.Async.TerminateRunaway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncTerminateTest::RunTest(const FString& Parameters)
{
	const int32 BaselineThreads = UJavascriptAsync::NumLiveWorkerThreads();

	FAsyncTestEnv Env(*this);
	if (!Env.Init())
	{
		return false;
	}

	// stop() interrupts a script that never yields
	Env.Run(TEXT(R"JS(
		globalThis.T = {};
		globalThis.runaway = Async.Lambda(() => { exports.spin = () => { for (;;) {} }; });
		runaway.onerror = () => {};
		runaway.exports.spin().catch(e => { T.rejected = e.message; });
	)JS"));
	Env.PumpUntil(TEXT("runaway.pinned"), 5.0);
	FPlatformProcess::Sleep(0.1f);	// let it get stuck in the loop
	Env.Run(TEXT("runaway.stop();"));
	TestTrue(TEXT("stopped runaway reports stopped"), Env.PumpUntil(TEXT("runaway.stopped && T.rejected !== undefined"), 5.0));
	TestTrue(TEXT("pending call rejected"), Env.Run(TEXT("T.rejected")).Contains(TEXT("stopped")));
	TestTrue(TEXT("runaway thread exited"), WaitForLiveWorkerThreads(BaselineThreads));

	// Owner shutdown (reload) does the same for every worker, bounded
	Env.Run(TEXT(R"JS(
		globalThis.spinners = [1, 2, 3].map(() => Async.Lambda(() => { exports.spin = () => { for (;;) {} }; }));
		spinners.forEach(l => { l.onerror = () => {}; l.exports.spin().catch(() => {}); });
	)JS"));
	Env.PumpUntil(TEXT("spinners.every(l => l.pinned)"), 5.0);
	FPlatformProcess::Sleep(0.1f);

	const double StartSeconds = FPlatformTime::Seconds();
	Env.ShutdownAsync();
	const double ShutdownSeconds = FPlatformTime::Seconds() - StartSeconds;
	TestTrue(FString::Printf(TEXT("shutdown with 3 runaway workers took %.3fs"), ShutdownSeconds), ShutdownSeconds < 1.0);
	TestTrue(TEXT("runaway threads exited"), WaitForLiveWorkerThreads(BaselineThreads));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncExposeTest, "UnrealJS.Async.ExposeObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncExposeTest::RunTest(const FString& Parameters)
{
	FAsyncTestEnv Env(*this);
	if (!Env.Init())
	{
		return false;
	}

	// UObject captures and exposeAs() become globals in the worker
	UObject* Exposed = NewObject<UJavascriptAsync>(GetTransientPackage());
	Env.Context->Expose(TEXT("Exposed"), Exposed);
	Env.Run(TEXT(R"JS(
		globalThis.T = {};
		globalThis.l = Async.Lambda({captured: Exposed}, () => {
			exports.kinds = () => [typeof captured, typeof globalThis.later];
		});
		l.onerror = (e) => { T.error = e.message; };
		l.exposeAs(Exposed, 'later');
		l.exports.kinds((k) => { T.kinds = k; });
	)JS"));

	TestTrue(TEXT("worker answered"), Env.PumpUntil(TEXT("T.kinds !== undefined")));
	TestEqual(TEXT("captured and exposed UObjects are objects in the worker"), Env.Run(TEXT("JSON.stringify(T.kinds)")), FString(TEXT("[\"object\",\"object\"]")));
	TestEqual(TEXT("no errors"), Env.Run(TEXT("String(T.error)")), FString(TEXT("undefined")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncComponentReloadTest, "UnrealJS.Async.ComponentReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncComponentReloadTest::RunTest(const FString& Parameters)
{
	// The real ctrl+s path: UJavascriptInstanceComponent::Reload() on a script that starts pinned lambdas
	const int32 BaselineThreads = UJavascriptAsync::NumLiveWorkerThreads();
	constexpr int32 PinnedPerScript = 3;

	const FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UnrealJsTests/async_component_reload.js"));
	const FString Script = FString::Printf(TEXT("for (let i = 0; i < %d; i++) { Async.Lambda({i}, () => { exports.ping = (x) => x + i; }); }\n"), PinnedPerScript);
	if (!TestTrue(TEXT("write test script"), FFileHelper::SaveStringToFile(Script, *ScriptPath)))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("CreateWorld"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	AActor* Actor = World->SpawnActor<AActor>();
	UJavascriptInstanceComponent* Component = NewObject<UJavascriptInstanceComponent>(Actor);
	Component->InstanceOptions.Features.FeatureMap.Add(TEXT("Async"), TEXT("default"));
	Component->bCreateInspectorOnInstanceStartup = false;
	Component->DefaultScript = ScriptPath;
	Component->RegisterComponent();
	if (!Component->HasBeenInitialized())
	{
		Component->InitializeComponent();
	}

	auto WaitForThreads = [](int32 Expected)
	{
		const double Deadline = FPlatformTime::Seconds() + 5.0;
		while (UJavascriptAsync::NumLiveWorkerThreads() != Expected && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.005f);
		}
		return UJavascriptAsync::NumLiveWorkerThreads() == Expected;
	};

	TestTrue(TEXT("initial script started its pinned workers"), WaitForThreads(BaselineThreads + PinnedPerScript));

	constexpr int32 Reloads = 25;
	for (int32 Reload = 0; Reload < Reloads; ++Reload)
	{
		Component->Reload();
		if (!TestTrue(FString::Printf(TEXT("reload %d: worker threads stay at %d"), Reload, PinnedPerScript), WaitForThreads(BaselineThreads + PinnedPerScript)))
		{
			break;
		}
	}

	Component->UninitializeComponent();
	TestTrue(TEXT("uninitialize stops the workers"), WaitForThreads(BaselineThreads));

	Component->DestroyComponent();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	IFileManager::Get().Delete(*ScriptPath);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealJsAsyncExitTest, "UnrealJS.Async.ExitWithRunningWorker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealJsAsyncExitTest::RunTest(const FString& Parameters)
{
	// Leaves a runaway worker running on purpose: module shutdown at Quit must terminate and join it
	// before tearing V8 down. The check is that the process exits cleanly afterwards.
	UJavascriptAsync* Leaked = NewObject<UJavascriptAsync>(GetTransientPackage());
	Leaked->AddToRoot();

	const int32 WorkerId = Leaked->SpawnWorker(TEXT("__worker.runMain(() => { exports.spin = () => { for (;;) {} }; }, true);"));
	TestTrue(TEXT("spawned"), WorkerId > 0);
	TestTrue(TEXT("call posted"), Leaked->PostToWorker(WorkerId, TEXT("call"), 1, TEXT("spin"), FString()));
	TestTrue(TEXT("worker running"), UJavascriptAsync::NumLiveWorkerThreads() > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
