#include "JavascriptAsync.h"
#include "Async/Async.h"
#include "JavascriptInstance.h"
#include "JavascriptInstanceHandler.h"

int32 UJavascriptAsync::IdCounter = 0;
TSharedPtr<FJavascriptInstanceHandler> UJavascriptAsync::MainHandler = nullptr;

UJavascriptAsync::UJavascriptAsync(class FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MainHandler = FJavascriptInstanceHandler::GetMainHandler().Pin();
	IdCounter = 0;
	CallableWrapper = ObjectInitializer.CreateDefaultSubobject<UJavascriptCallableWrapper>(this, TEXT("CallableWrapper"));
	CallableWrapper->SetLambdaLink(this);
}

UJavascriptAsync* UJavascriptAsync::StaticInstance(UObject* Owner)
{
	if (Owner == nullptr)
	{
		auto Instance = NewObject<UJavascriptAsync>();
		Instance->AddToRoot();
		return Instance;
	}
	else
	{
		return NewObject<UJavascriptAsync>(Owner);
	}
}

int32 UJavascriptAsync::NextLambdaId()
{
	return IdCounter + 1;
}


void UJavascriptAsync::PreExposeObject(UObject* Object, const FString& Name)
{
	PreExposures.Add(Name, Object);
}


void UJavascriptAsync::ClearPreExposures()
{
	PreExposures.Empty();
}

int32 UJavascriptAsync::RunScript(const FString& Script, EJavascriptAsyncOption ExecutionContext, bool bPinAfterRun,
	bool bEnableRequire)
{
	FJSInstanceOptions InstanceOptions;
	InstanceOptions.ThreadOption = ExecutionContext;
	const int32 LambdaId = ++IdCounter;

	const FString SafeScript = Script;
	FJavascriptInstanceHandler::GetMainHandler().Pin()->RequestInstance(InstanceOptions, [SafeScript, LambdaId, bPinAfterRun, bEnableRequire, this](TSharedPtr<FJavascriptInstance> NewInstance, EJSInstanceResultType ResultType)
	{
		//Convert context
		const EAsyncExecution AsyncExecutionContext = FJavascriptAsyncUtil::ToAsyncExecution(NewInstance->Options.ThreadOption);

		//Create a message queue if we're going to be pinned
		if (bPinAfterRun) 
		{
			LambdaMapData.DataForId(LambdaId).bShouldPin = true;
		}

		//Expose function callback feature
		FJavascriptAsyncLambdaPinData& PinData = LambdaMapData.DataForId(LambdaId);

		Async(AsyncExecutionContext, [NewInstance, bEnableRequire, SafeScript, LambdaId, bPinAfterRun, &PinData, this]()
		{
			//Enable require is exposed
			if (bEnableRequire)
			{
				NewInstance->ContextSettings.Context->ExposeUModule();
			}

			//Expose async callbacks
			NewInstance->ContextSettings.Context->Expose(TEXT("_asyncUtil"), CallableWrapper);
			const FString ExposeAdditional = TEXT("_asyncUtil.Callbacks = {}; _asyncUtil.CallbackIndex = 0;");

			//Pre-expose any uobjects
			if (PreExposures.Num() > 0)
			{
				for (auto& Pair : PreExposures)
				{
					NewInstance->ContextSettings.Context->Expose(Pair.Key, Pair.Value);
				}
				PreExposures.Empty();
			}
			
			//run the main lambda script
			FString ReturnValue = NewInstance->ContextSettings.Context->Public_RunScript(ExposeAdditional + SafeScript, false);
			
			Async(EAsyncExecution::TaskGraphMainThread, [this, ReturnValue, LambdaId]
			{
				OnLambdaComplete.ExecuteIfBound(ReturnValue, LambdaId, -1);
			});

			if (bPinAfterRun)
			{
				PinData.bIsPinned = true;

				//while bShouldRun => spin(), check if we got a message call, call it
				while (PinData.bShouldPin)
				{
					while (!PinData.MessageQueue->IsEmpty())
					{
						FJavascriptRemoteFunctionData MessageFunctionData;
						PinData.MessageQueue->Dequeue(MessageFunctionData);

						if (MessageFunctionData.bIsFunctionCall)
						{
							FString RemoteFunctionScript = FString::Printf(TEXT("%s?%s(_asyncUtil.parseArgs('%s')):undefined;"),
								*MessageFunctionData.Event,
								*MessageFunctionData.Event,
								*MessageFunctionData.Args);
							FString MessageReturn = NewInstance->ContextSettings.Context->Public_RunScript(RemoteFunctionScript, false);

							int32 CallbackId = MessageFunctionData.CallbackId;

							if (CallbackId != -1) {
								//We call back on gamethread always
								Async(EAsyncExecution::TaskGraphMainThread, [this, MessageReturn, LambdaId, CallbackId] 
								{
									OnMessage.ExecuteIfBound(MessageReturn, LambdaId, CallbackId);
								});
							}
						}
						//expose object case, NB: hmm should these be an enum instead of a collection of bools?
						else if (MessageFunctionData.bHasExposedObject)
						{
							if (MessageFunctionData.ExposedObject != nullptr)
							{
								NewInstance->ContextSettings.Context->Expose(MessageFunctionData.Args, MessageFunctionData.ExposedObject);
							}
						}
						else
						{
							//The message wanted to run some raw script without return
							NewInstance->ContextSettings.Context->Public_RunScript(MessageFunctionData.Event, false);
						}
					}
					//1ms sleep
					FPlatformProcess::Sleep(0.001f);
					
				}

				PinData.bIsPinned = false;
			}
		});
	});

	return LambdaId;
}

void UJavascriptAsync::CallScriptFunction(int32 InLambdaId, const FString& FunctionName, const FString& Args, int32 CallbackId)
{
	FJavascriptRemoteFunctionData FunctionData;
	FunctionData.Event = FunctionName;
	FunctionData.Args = Args;
	FunctionData.CallbackId = CallbackId;
	FunctionData.bIsFunctionCall = true;

	//Queue up the function to be pulled on next poll by the pinned lambda
	LambdaMapData.DataForId(InLambdaId).MessageQueue->Enqueue(FunctionData);
}

void UJavascriptAsync::RunScriptInLambda(int32 InLambdaId, const FString& Script)
{
	FJavascriptRemoteFunctionData MessageData;
	MessageData.Event = Script;
	MessageData.bIsFunctionCall = false;

	LambdaMapData.DataForId(InLambdaId).MessageQueue->Enqueue(MessageData);
}


void UJavascriptAsync::ExposeObjectInLambda(int32 InLambdaId, UObject* Object, const FString& Name)
{
	FJavascriptRemoteFunctionData MessageData;
	MessageData.bHasExposedObject = true;
	MessageData.ExposedObject = Object;
	MessageData.Args = Name;
	MessageData.bIsFunctionCall = false;

	LambdaMapData.DataForId(InLambdaId).MessageQueue->Enqueue(MessageData);
}

void UJavascriptAsync::StopLambda(int32 InLambdaId)
{
	LambdaMapData.DataForId(InLambdaId).bShouldPin = false;

	//todo: signal lazy cleanup of isolate, otherwise handler will cleanup later
}

void UJavascriptAsync::BeginDestroy()
{
	//NB: this doesn't get called early enough... todo: cleanup all lambdas on game world exit
	TArray<int32> LambdaIds = LambdaMapData.LambdaIds();
	for (int32 LambdaId : LambdaIds)
	{
		StopLambda(LambdaId);
	}

	Super::BeginDestroy();
}

UJavascriptCallableWrapper::UJavascriptCallableWrapper(class FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

void UJavascriptCallableWrapper::CallFunction(FString FunctionName, FString Args, int32 LambdaId, int32 CallbackId)
{
	//UE_LOG(LogTemp, Log, TEXT("Received %s, %s"), *FunctionName, *Args);
	Async(EAsyncExecution::TaskGraphMainThread, [this, FunctionName, Args, LambdaId, CallbackId] {
		LambdaLink->OnAsyncCall.ExecuteIfBound(FunctionName, Args, LambdaId, CallbackId);
		//Return handled in javascript
	});
}
