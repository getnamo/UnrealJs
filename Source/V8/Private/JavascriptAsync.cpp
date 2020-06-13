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

int32 UJavascriptAsync::RunScript(const FString& Script, EJavascriptAsyncOption ExecutionContext)
{
	FJSInstanceOptions InstanceOptions;
	InstanceOptions.ThreadOption = ExecutionContext;
	const int32 LambdaId = ++IdCounter;

	

	const FString SafeScript = Script;
	FJavascriptInstanceHandler::GetMainHandler().Pin()->RequestInstance(InstanceOptions, [SafeScript, LambdaId, this](TSharedPtr<FJavascriptInstance> NewInstance)
	{
		//run script
		const EAsyncExecution AsyncExecutionContext = FJavascriptAsyncUtil::ToAsyncExecution(NewInstance->Options.ThreadOption);

		Async(AsyncExecutionContext, [NewInstance, SafeScript, LambdaId, this]()
		{
			FString ReturnValue = NewInstance->ContextSettings.Context->Public_RunScript(SafeScript);
			OnLambdaComplete.ExecuteIfBound(ReturnValue, LambdaId);
		});
	});

	return LambdaId;
}