#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "HAL/Runnable.h"
#include "UObject/StrongObjectPtr.h"
#include "V8PCH.h"
#include <atomic>

class FEvent;
class FRunnableThread;
struct FJavascriptContext;

/**
 * Background JS workers backing Async.Lambda (see Content/Scripts/async.js).
 *
 * Each worker owns one dedicated thread, and that thread creates, runs, pumps and disposes
 * the worker's V8 isolate. An isolate therefore never changes threads, so no v8::Locker is
 * needed, and the isolate's foreground tasks are pumped on the thread that uses it.
 *
 * Workers belong to an FJavascriptWorkerHost (owned by the UJavascriptAsync object exposed to
 * a context). Owner teardown stops them deterministically: flag + TerminateExecution + join.
 * Game thread <-> worker traffic is message passing only; message kinds are strings so the
 * protocol lives in async.js / async-worker.js.
 */

/** One message between the game thread and a worker */
struct FJavascriptWorkerMessage
{
	FString Kind;
	int32 WorkerId = 0;
	int32 RequestId = 0;
	FString Name;
	FString Payload;

	/** Raw bytes riding with the payload (typed arrays in the value, see the codec in async.js) */
	TArray<uint8> Bytes;

	/** Kind "expose" only. Kept alive by the worker's strong references, which live on the game thread */
	UObject* Object = nullptr;
};

/** Worker -> game thread queue. Shared by a host and its workers so a worker never needs the host itself */
class FJavascriptWorkerOutbox
{
public:
	void Post(FJavascriptWorkerMessage&& Message);
	bool Dequeue(FJavascriptWorkerMessage& OutMessage);
	void Empty();

	/** After closing, posts are dropped (e.g. from workers that outlived their host's stop timeout) */
	void Close() { bClosed = true; }

private:
	TQueue<FJavascriptWorkerMessage, EQueueMode::Mpsc> Queue;
	std::atomic<bool> bClosed{ false };
};

struct FJavascriptWorkerSpec
{
	/** Bootstrap source, run after async-worker.js */
	FString Source;

	/** Exposes require() inside the worker */
	bool bEnableRequire = false;

	/** UObjects exposed as globals before the bootstrap runs */
	TArray<TPair<FString, UObject*>> Exposures;

	uint32 StackSize = 4 * 1024 * 1024;
};

class FJavascriptWorker : public FRunnable
{
public:
	FJavascriptWorker(int32 InId, FJavascriptWorkerSpec&& InSpec, TSharedRef<FJavascriptWorkerOutbox> InOutbox);
	virtual ~FJavascriptWorker();

	// Game thread
	bool Start();
	void Post(FJavascriptWorkerMessage&& Message);
	void AddStrongReference(UObject* Object);

	// Any thread
	void RequestStop();
	bool HasExited() const { return bHasExited; }
	bool WaitForExit(uint32 TimeoutMs);
	int32 GetId() const { return Id; }

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override { RequestStop(); }

private:
	void RunIsolate(uintptr_t StackTop);
	void InstallBridge(FJavascriptContext& Context);
	bool RunScript(FJavascriptContext& Context, const FString& FileName, const FString& Source);
	void Dispatch(FJavascriptContext& Context, const FJavascriptWorkerMessage& Message);

	/** Runs due worker timers (__worker.tick). Returns ms until the next timer, or -1 if none */
	int32 RunTimers(FJavascriptContext& Context);
	void ReportScriptError(v8::Isolate* V8Isolate, v8::TryCatch& TryCatch, int32 RequestId, const FString& Name);

	const int32 Id;
	FJavascriptWorkerSpec Spec;
	TSharedRef<FJavascriptWorkerOutbox> Outbox;

	/** Captured on the game thread: IV8::Get() must not be the first module lookup off it */
	void* Platform;
	TArray<FString> ScriptPaths;

	/** Game thread -> worker */
	TQueue<FJavascriptWorkerMessage, EQueueMode::Spsc> Inbox;
	FEvent* WakeEvent;
	FEvent* ExitedEvent;
	FRunnableThread* Thread = nullptr;

	/** Guards LiveIsolate so RequestStop never calls TerminateExecution on a disposed isolate */
	FCriticalSection IsolateLock;
	v8::Isolate* LiveIsolate = nullptr;

	std::atomic<bool> bStopRequested{ false };
	std::atomic<bool> bHasExited{ false };

	/** Worker thread only: set by __worker.close() when a one-shot lambda is done */
	bool bCloseRequested = false;

	/** Worker thread only: the __worker bridge object (holds dispatch) */
	v8::Global<v8::Object> BridgeObject;

	/** Game thread only: keeps exposed UObjects alive while the worker can reach them */
	TArray<TStrongObjectPtr<UObject>> StrongReferences;
};

class FJavascriptWorkerHost : public TSharedFromThis<FJavascriptWorkerHost>
{
public:
	FJavascriptWorkerHost();
	~FJavascriptWorkerHost();

	// Game thread only

	/** Returns the new worker id, or 0 if the worker could not start */
	int32 Spawn(FJavascriptWorkerSpec&& Spec);
	bool Post(int32 WorkerId, FJavascriptWorkerMessage&& Message);
	bool Expose(int32 WorkerId, UObject* Object, const FString& Name);

	/** Non-blocking: the worker reports "stopped" once its thread has exited */
	void Stop(int32 WorkerId);

	/** Blocking, bounded: stops and joins every worker, then reports "stopped" for each */
	void StopAll(uint32 TimeoutMs = DefaultStopTimeoutMs);

	/** Owner teardown: unbinds OnMessage, stops all workers, drops anything still in flight */
	void Shutdown(uint32 TimeoutMs = DefaultStopTimeoutMs);

	/** Drains worker -> game thread messages. Runs on the core ticker; callable directly (tests) */
	void ProcessMessages(int32 MaxMessages = 10000);

	int32 NumWorkers() const { return Workers.Num(); }
	bool IsShutdown() const { return bIsShutdown; }

	/** Game thread. The handler may take Message.Bytes */
	TFunction<void(FJavascriptWorkerMessage&)> OnMessage;

	/** Module shutdown: stops every live worker in the process. False if some never exited */
	static bool ShutdownAllWorkers(uint32 TimeoutMs);

	/** Live worker threads across all hosts, including ones past their stop timeout */
	static int32 NumLiveWorkerThreads();

	static constexpr uint32 DefaultStopTimeoutMs = 2000;

private:
	void EnsureTicker();

	TSharedRef<FJavascriptWorkerOutbox> Outbox;
	TMap<int32, TSharedPtr<FJavascriptWorker>> Workers;
	int32 NextWorkerId = 1;
	FTSTicker::FDelegateHandle TickHandle;
	bool bIsShutdown = false;
};
