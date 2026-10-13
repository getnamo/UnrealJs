#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "JavascriptAsync.generated.h"

class FJavascriptWorkerHost;
struct FJavascriptWorkerMessage;

/** Kind is one of: complete, reply, error, bridge, stopped (protocol documented in async.js) */
DECLARE_DYNAMIC_DELEGATE_FiveParams(FJsWorkerMessageSignature, FString, Kind, int32, WorkerId, int32, RequestId, FString, Name, FString, Payload);

/**
 * Game-thread facade over a set of background JS workers (Async.Lambda in async.js).
 *
 * Each worker runs on its own thread with its own isolate. Workers are owned by this object:
 * Shutdown() (called by the owning component on reload/EndPlay) or destruction stops and joins
 * them, so no worker outlives the script context that started it.
 */
UCLASS(BlueprintType, ClassGroup = Script, Blueprintable)
class V8_API UJavascriptAsync : public UObject
{
	GENERATED_UCLASS_BODY()
public:

	/**
	 * Legacy accessor: returns a new instance owned by Owner (the transient package if null).
	 * It is not rooted; it lives while referenced and stops its workers when collected.
	 * Prefer the 'Async' object exposed by UJavascriptInstanceComponent, which is shut down on reload.
	 */
	UFUNCTION(BlueprintCallable, Category = "Javascript|Async")
	static UJavascriptAsync* StaticInstance(UObject* Owner = nullptr);

	/** Worker -> game thread messages, delivered on the game thread once per tick */
	UPROPERTY()
	FJsWorkerMessageSignature OnWorkerMessage;

	/** Queue a UObject to be exposed as a global in the next spawned worker */
	UFUNCTION(BlueprintCallable, Category = "Javascript|Async")
	void PreExposeObject(UObject* Object, const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "Javascript|Async")
	void ClearPreExposures();

	/** Starts a worker running Source (after async-worker.js). Consumes pre-exposures. Returns the worker id, 0 on failure */
	UFUNCTION(BlueprintCallable, Category = "Javascript|Async")
	int32 SpawnWorker(const FString& Source, bool bEnableRequire = false);

	/** Sends a message to a worker (kinds: call, bridge-reply, bridge-error, eval). False if the worker is gone */
	UFUNCTION(BlueprintCallable, Category = "Javascript|Async")
	bool PostToWorker(int32 WorkerId, const FString& Kind, int32 RequestId, const FString& Name, const FString& Payload);

	/** Exposes a UObject as a global in a running worker. The worker keeps it alive until it exits */
	UFUNCTION(BlueprintCallable, Category = "Javascript|Async")
	bool ExposeObjectInWorker(int32 WorkerId, UObject* Object, const FString& Name);

	/** Non-blocking stop: terminates running script; OnWorkerMessage gets "stopped" when the thread exits */
	UFUNCTION(BlueprintCallable, Category = "Javascript|Async")
	void StopWorker(int32 WorkerId);

	/** Blocking (bounded) stop of every worker owned by this object */
	UFUNCTION(BlueprintCallable, Category = "Javascript|Async")
	void StopAllWorkers();

	UFUNCTION(BlueprintPure, Category = "Javascript|Async")
	int32 NumWorkers() const;

	/** Delivers pending worker messages now instead of on the next tick */
	UFUNCTION(BlueprintCallable, Category = "Javascript|Async")
	void ProcessWorkerMessages();

	/** Live worker threads in the whole process (diagnostics/tests) */
	UFUNCTION(BlueprintPure, Category = "Javascript|Async")
	static int32 NumLiveWorkerThreads();

	/**
	 * Owner teardown (script reload, EndPlay): unbinds OnWorkerMessage, then stops and joins all
	 * workers. Afterwards spawning is refused, so stale script can't start new workers.
	 */
	void Shutdown();

	virtual void BeginDestroy() override;

protected:
	void HandleWorkerMessage(const FJavascriptWorkerMessage& Message);

	TSharedPtr<FJavascriptWorkerHost> Host;

	TArray<TPair<FString, TWeakObjectPtr<UObject>>> PreExposures;
};
