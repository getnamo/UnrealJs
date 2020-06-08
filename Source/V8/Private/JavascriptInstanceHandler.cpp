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
			ReleaseInstance(Instance);
		}
		
	}

	InstanceThreadMap.Empty();
}

TWeakPtr<FJavascriptInstanceHandler> FJavascriptInstanceHandler::GetMainHandler()
{
	if (!MainHandler)
	{
		MainHandler = MakeShareable(new FJavascriptInstanceHandler());
	}
	return MainHandler;
}

EJSInstanceResult FJavascriptInstanceHandler::RequestInstance(const FJSInstanceOptions& InOptions, TFunction<void(TSharedPtr<FJavascriptInstance>)> OnDelayedResult /*= nullptr*/)
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
	FJSInstanceContextSettings ContextSettings;

	if (InOptions.ThreadOption == EUJSThreadOption::USE_DEFAULT || InOptions.ThreadOption == EUJSThreadOption::USE_GAME_THREAD)
	{
		ContextSettings.ThreadId = GGameThreadId;
	}
	else
	{
		//Set a bg thread id e.g. 0
	}

	//Check for array
	if (!InstanceThreadMap.Contains(ContextSettings.ThreadId))
	{
		TArray<TSharedPtr<FJavascriptInstance>> ThreadArray;

		InstanceThreadMap.Add(ContextSettings.ThreadId, ThreadArray);
	}

	//Load array
	TArray<TSharedPtr<FJavascriptInstance>>& ThreadArray = InstanceThreadMap[ContextSettings.ThreadId];
	
	//Find instance with same isolate domain
	for (auto Instance : ThreadArray)
	{
		Instance->IsolateDomain == InOptions.IsolateDomain;

		if (OnDelayedResult)
		{
			OnDelayedResult(Instance);
		}
		return EJSInstanceResult::RESULT_INSTANT;
	}

	//Instance not found, allocate instance on BG thread if empty
	if (InOptions.Features.IsEmpty() && bAllocateEmptyIsolatesOnBgThread)
	{
		const FJSInstanceOptions OptionsCopy = InOptions;
		int32 TargetThreadId = ContextSettings.ThreadId;
		
		Async(EAsyncExecution::ThreadPool, [OptionsCopy, ContextSettings, OnDelayedResult, this]()
		{
			//Allocate and callback
			TSharedPtr<FJavascriptInstance> NewInstance = MakeShareable(new FJavascriptInstance(OptionsCopy, ContextSettings));

			//add the result to array on game thread
			Async(EAsyncExecution::TaskGraphMainThread, [NewInstance, ContextSettings, OnDelayedResult, this]()
			{
				TArray<TSharedPtr<FJavascriptInstance>>& ThreadArray = InstanceThreadMap[ContextSettings.ThreadId];
				ThreadArray.AddUnique(NewInstance);
				if (OnDelayedResult)
				{
					OnDelayedResult(NewInstance);
				}
			});
		});
		return EJSInstanceResult::RESULT_DELAYED;
	}
	//If not empty or disabled, it will be allocated in thread
	else
	{
		//Didn't find a matching instance, make one and return it
		TSharedPtr<FJavascriptInstance> NewInstance = MakeShareable(new FJavascriptInstance(InOptions, ContextSettings));
		ThreadArray.AddUnique(NewInstance);

		if (OnDelayedResult)
		{
			OnDelayedResult(NewInstance);
		}
		return EJSInstanceResult::RESULT_INSTANT;
	}
}

/*void FJavascriptInstanceHandler::ReleaseInstance(FJavascriptInstance* Instance)
{
	if (InstanceThreadMap.Contains(Instance->ContextSettings.ThreadId))
	{
		TArray<TSharedPtr<FJavascriptInstance>>& ThreadArray = InstanceThreadMap[Instance->ContextSettings.ThreadId];

		//Because we use shared ptrs we need to use a loop for removal
		int32 RemoveIndex = 0;
		for (int i=0; i<ThreadArray.Num();i++)
		{
			TSharedPtr<FJavascriptInstance>& Comparator = ThreadArray[i];

			if (Comparator.Get() == Instance)
			{
				RemoveIndex = i;
				break;
			}
		}

		ThreadArray.RemoveAt(RemoveIndex);
	}
}*/

void FJavascriptInstanceHandler::ReleaseInstance(TSharedPtr<FJavascriptInstance> Instance)
{
	if (InstanceThreadMap.Contains(Instance->ContextSettings.ThreadId))
	{
		TArray<TSharedPtr<FJavascriptInstance>>& ThreadArray = InstanceThreadMap[Instance->ContextSettings.ThreadId];
		ThreadArray.Remove(Instance);
	}
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
