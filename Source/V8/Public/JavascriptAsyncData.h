#pragma once

#include "Async/Async.h"
#include "JavascriptAsyncData.generated.h"

/** NB: ordering is different from async, default is game thread */
UENUM(BlueprintType)
enum class EJavascriptAsyncOption : uint8
{
	TaskGraphMainThread = 0,//main thread, short task (~<2sec)
	TaskGraph,				//background on taskgraph, short task (~<2sec)
	Thread,					//background thread
	ThreadPool				//background in threadpool
};

struct FJavascriptAsyncUtil
{
	static bool IsBgThread(EJavascriptAsyncOption Option);

	static EAsyncExecution ToAsyncExecution(EJavascriptAsyncOption Option);
};

USTRUCT()
struct FJavascriptRemoteFunctionData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FString Event;

	UPROPERTY()
	FString Args;

	UPROPERTY()
	int32 CallbackId;

	//If false assume Event == script
	UPROPERTY()
	bool bIsFunctionCall;

	UPROPERTY()
	bool bHasExposedObject;

	UPROPERTY()
	UObject* ExposedObject;

	FJavascriptRemoteFunctionData() 
	{
		bIsFunctionCall = true;
		bHasExposedObject = false;
		ExposedObject = nullptr;
	}
};

struct FJavascriptAsyncLambdaPinData
{
	TSharedPtr<TQueue<FJavascriptRemoteFunctionData>> MessageQueue;

	FThreadSafeBool bShouldPin;
	FThreadSafeBool bIsPinned;
};

class FJavascriptAsyncLambdaMapData
{
public:
	FJavascriptAsyncLambdaPinData& DataForId(int32 LambdaId);
	TQueue<FJavascriptRemoteFunctionData>* MessageQueue(int32 LambdaId);
	TArray<int32> LambdaIds();

protected:
	TMap<int32, FJavascriptAsyncLambdaPinData> PinData;
};

