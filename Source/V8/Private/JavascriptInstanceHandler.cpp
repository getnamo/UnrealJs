#include "JavascriptInstanceHandler.h"
#include "Async/Async.h"

namespace
{
	TSharedPtr<FJavascriptInstanceHandler> MainHandler = nullptr;
}


FJavascriptInstanceHandler::FJavascriptInstanceHandler()
{
	HandlerThreadId = FPlatformTLS::GetCurrentThreadId();
	bAllocateEmptyIsolatesOnBgThread = true;
}

FJavascriptInstanceHandler::~FJavascriptInstanceHandler()
{
	//Cleanup all instances for this handler
	for (auto& Pair : InstanceThreadMap)
	{
		//Per thread
		auto& Instances = Pair.Value;

		for (auto& Instance : Instances)
		{
			ReleaseInstance(Instance.Get());
		}
		
	}

	InstanceThreadMap.Empty();
}

FJavascriptInstanceHandler* FJavascriptInstanceHandler::GetMainHandler()
{
	if (!MainHandler)
	{
		MainHandler = MakeShareable(new FJavascriptInstanceHandler());
	}
	return MainHandler.Get();
}

EJSInstanceResult FJavascriptInstanceHandler::RequestInstance(const FJSInstanceOptions& InOptions, FJavascriptInstance*& OutInstance, TFunction<void(FJavascriptInstance*)> OnDelayedResult /*= nullptr*/)
{
	//Check requesting thread
	//int32 RequestingThread = FPlatformTLS::GetCurrentThreadId();

	//for now, sanity check: we only allow
	if (!IsInGameThread())
	{
		UE_LOG(LogTemp, Warning, TEXT("FJavascriptInstanceHandler::RequestInstance tried to request from non-gamethread. Nullptr returned"));
		if (OnDelayedResult)
		{
			OnDelayedResult(nullptr);
		}
		return EJSInstanceResult::RESULT_ERROR;
	}

	//Requesting an instance in GameThread
	int32 TargetThreadId = 0;

	if (InOptions.ThreadOption == EUJSThreadOption::USE_DEFAULT || InOptions.ThreadOption == EUJSThreadOption::USE_GAME_THREAD)
	{
		TargetThreadId = GGameThreadId;
	}
	else
	{
		//Set a bg thread id e.g. 0
	}

	//Check for array
	if (!InstanceThreadMap.Contains(TargetThreadId))
	{
		TArray<TSharedPtr<FJavascriptInstance>> ThreadArray;

		InstanceThreadMap.Add(TargetThreadId, ThreadArray);
	}

	//Load array
	TArray<TSharedPtr<FJavascriptInstance>>& ThreadArray = InstanceThreadMap[TargetThreadId];
	
	//Find instance with same isolate domain
	for (auto Instance : ThreadArray)
	{
		Instance->IsolateDomain == InOptions.IsolateDomain;

		if (OnDelayedResult)
		{
			OnDelayedResult(Instance.Get());
		}
		OutInstance = Instance.Get();
		return EJSInstanceResult::RESULT_INSTANT;
	}

	//Instance not found, allocate instance on BG thread if empty
	if (InOptions.Features.IsEmpty() && bAllocateEmptyIsolatesOnBgThread)
	{
		const FJSInstanceOptions OptionsCopy = InOptions;
		Async(EAsyncExecution::ThreadPool, [OptionsCopy, TargetThreadId, OnDelayedResult, this]()
		{
			//Allocate and callback
			TSharedPtr<FJavascriptInstance> NewInstance = MakeShareable(new FJavascriptInstance(OptionsCopy));

			//add the result to array on game thread
			Async(EAsyncExecution::TaskGraphMainThread, [NewInstance, TargetThreadId, OnDelayedResult, this]()
			{
				TArray<TSharedPtr<FJavascriptInstance>>& ThreadArray = InstanceThreadMap[TargetThreadId];
				ThreadArray.AddUnique(NewInstance);
				if (OnDelayedResult)
				{
					OnDelayedResult(NewInstance.Get());
				}
			});
		});
		return EJSInstanceResult::RESULT_DELAYED;
	}
	//If not empty or disabled, it will be allocated in thread
	else
	{
		//Didn't find a matching instance, make one and return it
		TSharedPtr<FJavascriptInstance> NewInstance = MakeShareable(new FJavascriptInstance(InOptions));
		ThreadArray.AddUnique(NewInstance);

		if (OnDelayedResult)
		{
			OnDelayedResult(NewInstance.Get());
		}
		OutInstance = NewInstance.Get();
		return EJSInstanceResult::RESULT_INSTANT;
	}
}

void FJavascriptInstanceHandler::ReleaseInstance(FJavascriptInstance*)
{

}

bool FJavascriptInstanceHandler::IsInGameThread()
{
	if (GIsGameThreadIdInitialized)
	{
		return FPlatformTLS::GetCurrentThreadId() == GGameThreadId;
	}
	else
	{
		return true;
	}
}
