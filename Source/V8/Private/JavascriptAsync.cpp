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
	return NewObject<UJavascriptAsync>(Owner);
}

int32 UJavascriptAsync::RunScript(const FString& Script, EJavascriptAsyncOption ExecutionContext)
{
	FJSInstanceOptions InstanceOptions;
	InstanceOptions.ThreadOption = ExecutionContext;

	const FString SafeScript = Script;
	FJavascriptInstanceHandler::GetMainHandler().Pin()->RequestInstance(InstanceOptions, [SafeScript](TSharedPtr<FJavascriptInstance> NewInstance)
	{
		//run script
		const EAsyncExecution AsyncExecutionContext = FJavascriptAsyncUtil::ToAsyncExecution(NewInstance->Options.ThreadOption);

		Async(AsyncExecutionContext, [NewInstance, SafeScript]()
		{
			auto ReturnValue = NewInstance->ContextSettings.Context->Public_RunScript(SafeScript);
			//Todo: get return value from script run an trigger event
		});
	});

	IdCounter++;
	return IdCounter;
}
