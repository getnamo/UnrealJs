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

class FJavascriptInstanceHandler
{
public:
	FJavascriptInstanceHandler();
	~FJavascriptInstanceHandler();

	static FJavascriptInstanceHandler* GetMainHandler();

	//Main way to get an instance, may be instant or delayed depending on whether instance is ready. Callback is on GT
	EJSInstanceResult RequestInstance(const FJSInstanceOptions& InOptions, FJavascriptInstance*& OutInstance, TFunction<void(FJavascriptInstance*)> OnDelayedResult = nullptr);
	void ReleaseInstance(FJavascriptInstance*);

	//Utility
	bool IsInGameThread();

	//If it has no features exposed in the beginning, no matter the target thread, allocate on bg
	bool bAllocateEmptyIsolatesOnBgThread;

private:
	TMap<int32, TArray<TSharedPtr<FJavascriptInstance>>> InstanceThreadMap;
	int32 HandlerThreadId;
};