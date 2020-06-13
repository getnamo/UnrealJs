#pragma once

#include "JavascriptInstanceHandler.h"
#include "JavascriptContext.h"
#include "JavascriptAsync.generated.h"

DECLARE_DYNAMIC_DELEGATE(FJsLambdaNoParamSignature);
DECLARE_DYNAMIC_DELEGATE_OneParam(FJsLambdaIdSignature, int32, LambdaId);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FJsLambdaMessageSignature, FString, Message, int32, LambdaId);

UCLASS(BlueprintType, ClassGroup = Script, Blueprintable)
class V8_API UJavascriptAsync : public UObject
{
	GENERATED_UCLASS_BODY()
public:

	UFUNCTION(BlueprintCallable)
	static UJavascriptAsync* StaticInstance(UObject* Owner = nullptr);

	UPROPERTY()
	FJsLambdaMessageSignature OnLambdaComplete;

	UPROPERTY()
	FJsLambdaMessageSignature OnMessage;

	/** Run script on background thread, returns unique id for this run*/
	UFUNCTION(BlueprintCallable)
	int32 RunScript(const FString& Script, EJavascriptAsyncOption ExecutionContext = EJavascriptAsyncOption::ThreadPool);

	/**
	To allow raw function passing we will load in a script that 
	wraps functions with the necessary
	*/

protected:
	static EAsyncExecution ToAsyncExecution(EJavascriptAsyncOption ExecutionContext);

	static int32 IdCounter;
	static TSharedPtr<FJavascriptInstanceHandler> MainHandler;
	TSharedPtr<FJavascriptInstance> LambdaInstance;
};