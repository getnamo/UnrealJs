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
	const EAsyncExecution SafeAsyncExecutionContext = FJavascriptAsyncUtil::ToAsyncExecution(ExecutionContext);

	//if(FJavascriptAsyncUtil::IsBgThread(Sa)

	const FString SafeScript = Script;
	FJavascriptInstanceHandler::GetMainHandler().Pin()->RequestInstance(InstanceOptions, [SafeScript, SafeAsyncExecutionContext](TSharedPtr<FJavascriptInstance> NewInstance)
	{
		//run script
		Async(SafeAsyncExecutionContext, [NewInstance, SafeScript]()
		{
			NewInstance->ContextSettings.Context->Public_RunScript(SafeScript);
		});
	});

	IdCounter++;
	return IdCounter;
}
