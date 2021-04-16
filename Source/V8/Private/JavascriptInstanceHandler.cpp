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

EJSInstanceResult FJavascriptInstanceHandler::RequestInstance(const FJSInstanceOptions& InOptions, TFunction<void(TSharedPtr<FJavascriptInstance>, EJSInstanceResultType)> OnDelayedResult /*= nullptr*/)
{
	//Check requesting thread
	//int32 RequestingThread = FPlatformTLS::GetCurrentThreadId();

	//for now, sanity check: we only allow game thread calls for this
	if (!IsInGameThread())
	{
		UE_LOG(LogTemp, Warning, TEXT("FJavascriptInstanceHandler::RequestInstance tried to request from non-gamethread. Nullptr returned"));
		if (OnDelayedResult)
		{
			OnDelayedResult(nullptr, EJSInstanceResultType::RESULT_NONE_ERROR);
		}
		return EJSInstanceResult::RESULT_ERROR;
	}

	//Requesting an instance in GameThread
	FJSInstanceContextSettings ContextSettings;

	if (InOptions.UsesGameThread())
	{
		ContextSettings.ThreadId = GGameThreadId;
	}
	else
	{
		//BG thread Id is 0 for now
	}

	//Check for array
	if (!InstanceThreadMap.Contains(ContextSettings.ThreadId))
	{
		TArray<TSharedPtr<FJavascriptInstance>> ThreadArray;

		InstanceThreadMap.Add(ContextSettings.ThreadId, ThreadArray);
	}

	//Load array
	TArray<TSharedPtr<FJavascriptInstance>>& ThreadArray = InstanceThreadMap[ContextSettings.ThreadId];
	
	//Temp limit to gt only
	if (InOptions.UsesGameThread())
	{
		//Find instance with same isolate domain
		for (auto Instance : ThreadArray)
		{
			Instance->IsolateDomain = InOptions.IsolateDomain;

			if (OnDelayedResult)
			{
				OnDelayedResult(Instance, EJSInstanceResultType::RESULT_REUSE);
			}
			return EJSInstanceResult::RESULT_INSTANT;
		}
	}

	//Instance not found, allocate instance on BG thread if empty
	if (!InOptions.UsesGameThread())//(InOptions.Features.IsEmpty() && bAllocateEmptyIsolatesOnBgThread)
	{
		const FJSInstanceOptions OptionsCopy = InOptions;
		int32 TargetThreadId = ContextSettings.ThreadId;
		
		Async(EAsyncExecution::ThreadPool, [OptionsCopy, ContextSettings, OnDelayedResult, this]()
		{
			//Allocate and callback
			TSharedPtr<FJavascriptInstance> NewInstance = MakeShareable(new FJavascriptInstance(OptionsCopy, ContextSettings));

			/*if (OptionsCopy.bAttachInspector)
			{
				NewInstance->ContextSettings.Context->CreateInspector(9229);
			}*/

			//add the result to array on game thread
			Async(EAsyncExecution::TaskGraphMainThread, [NewInstance, ContextSettings, OnDelayedResult, this]()
			{
				TArray<TSharedPtr<FJavascriptInstance>>& ThreadArrayTemp = InstanceThreadMap[ContextSettings.ThreadId];
				ThreadArrayTemp.AddUnique(NewInstance);

				if (OnDelayedResult)
				{
					OnDelayedResult(NewInstance, EJSInstanceResultType::RESULT_NEW);
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
			OnDelayedResult(NewInstance, EJSInstanceResultType::RESULT_NEW);
		}
		return EJSInstanceResult::RESULT_INSTANT;
	}
}

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
