#include "JavascriptLambda.h"
#include "JavascriptInstance.h"
#include "JavascriptInstanceHandler.h"

UJavascriptLambda::UJavascriptLambda(class FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MainHandler = FJavascriptInstanceHandler::GetMainHandler().Pin();
}

UJavascriptLambda* UJavascriptLambda::GetLambda()
{
	return NewObject<UJavascriptLambda>();
}

int32 UJavascriptLambda::AsyncRun(const FString& Script)
{
	FJSInstanceOptions InstanceOptions;
	InstanceOptions.ThreadOption = EUJSThreadOption::USE_BACKGROUND_THREAD;

	const FString SafeScript = Script;
	FJavascriptInstanceHandler::GetMainHandler().Pin()->RequestInstance(InstanceOptions, [SafeScript](TSharedPtr<FJavascriptInstance> NewInstance)
	{
		//run script
		NewInstance->ContextSettings.Context->Public_RunScript(SafeScript);
	});

	//todo: return some valid monotonically increasing id
	return 1;
}
