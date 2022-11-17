#pragma once

#include "JavascriptInstance.h"
#include "JavascriptInstanceHandler.generated.h"

UENUM()
enum EJSInstanceResult
{
	RESULT_ERROR = 0,
	RESULT_INSTANT,
	RESULT_DELAYED
};

UENUM()
enum EJSInstanceResultType
{
	RESULT_NONE_ERROR = 0,
	RESULT_NEW,
	RESULT_REUSE
};

class FJavascriptInstanceHandler
{
public:
	FJavascriptInstanceHandler();
	~FJavascriptInstanceHandler();

	static TWeakPtr<FJavascriptInstanceHandler> GetMainHandler();

	//Main way to get an instance, may be instant or delayed depending on whether instance is ready. Callback is on GT
	EJSInstanceResult RequestInstance(const FJSInstanceOptions& InOptions, TFunction<void(TSharedPtr<FJavascriptInstance>, EJSInstanceResultType)> OnDelayedResult = nullptr);
	void ReleaseInstance(TSharedPtr<FJavascriptInstance> Instance);

	//Utility
	bool IsInGameThread();

	//If it has no features exposed in the beginning, no matter the target thread, allocate on bg
	bool bAllocateEmptyIsolatesOnBgThread;

private:
	TMap<int32, TArray<TSharedPtr<FJavascriptInstance>>> InstanceThreadMap;
	int32 HandlerThreadId;
};