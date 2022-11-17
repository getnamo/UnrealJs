#pragma once

#include "JavascriptInstanceHandler.h"
#include "JavascriptContext.h"
#include "JavascriptAsync.generated.h"

DECLARE_DYNAMIC_DELEGATE(FJsLambdaNoParamSignature);
DECLARE_DYNAMIC_DELEGATE_OneParam(FJsLambdaIdSignature, int32, LambdaId);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FJsLambdaMessageSignature, FString, Message, int32, LambdaId, int32, CallbackId);
DECLARE_DYNAMIC_DELEGATE_FourParams(FJsLambdaAsyncFuncSignature, FString, Name, FString, Args, int32, LambdaId, int32, CallbackId);

class UJavascriptAsync;

/** used in Js to call functions on game thread */
UCLASS(BlueprintType, ClassGroup = Script, Blueprintable)
class UJavascriptCallableWrapper : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	UFUNCTION(BlueprintCallable)
	void CallFunction(FString FunctionName, FString Args, int32 LambdaId, int32 CallbackId = -1);

	//Set
	void SetLambdaLink(UJavascriptAsync* Link) { LambdaLink = Link;};

protected:
	UJavascriptAsync* LambdaLink;
};


UCLASS(BlueprintType, ClassGroup = Script, Blueprintable)
class V8_API UJavascriptAsync : public UObject
{
	GENERATED_UCLASS_BODY()
public:

	UFUNCTION(BlueprintCallable)
	static UJavascriptAsync* StaticInstance(UObject* Owner = nullptr);

	UPROPERTY()
	FJsLambdaMessageSignature OnLambdaComplete;

	/** Lambda callback*/
	UPROPERTY()
	FJsLambdaMessageSignature OnMessage;

	/** Js Async calls from background thread*/
	UPROPERTY()
	FJsLambdaAsyncFuncSignature OnAsyncCall;

	/** sometimes we need the next lambda early */
	UFUNCTION(BlueprintCallable)
	int32 NextLambdaId();

	/** Called by Async.js to expose uobjects before main script runs */
	UFUNCTION(BlueprintCallable)
	void PreExposeObject(UObject* Object, const FString& Name);

	UFUNCTION(BlueprintCallable)
	void ClearPreExposures();

	/** Run script on background thread, returns unique id for this run*/
	UFUNCTION(BlueprintCallable)
	int32 RunScript(const FString& Script, 
					EJavascriptAsyncOption ExecutionContext = EJavascriptAsyncOption::ThreadPool, 
					bool bPinAfterRun = false,
					bool bEnableRequire = false);

	/** calls function on remote thread and gives result in 'OnMessage' */
	UFUNCTION(BlueprintCallable)
	void CallScriptFunction(int32 InLambdaId, const FString& FunctionName, const FString& Args, int32 CallbackId = 0);

	/** runs script on remote thread. ignores any result */
	UFUNCTION(BlueprintCallable)
	void RunScriptInLambda(int32 InLambdaId, const FString& Script);

	/** Attempts to expose an already created uobject on remote thread */
	UFUNCTION(BlueprintCallable)
	void ExposeObjectInLambda(int32 InLambdaId, UObject* Object, const FString& Name);

	/** if this lambda is pinned, it will unpin it*/
	UFUNCTION(BlueprintCallable)
	void StopLambda(int32 InLambdaId);

	/**
	To allow raw function passing we will load in a script that 
	wraps functions with the necessary
	*/

	virtual void BeginDestroy() override;

protected:
	static EAsyncExecution ToAsyncExecution(EJavascriptAsyncOption ExecutionContext);

	static int32 IdCounter;
	static TSharedPtr<FJavascriptInstanceHandler> MainHandler;
	TSharedPtr<FJavascriptInstance> LambdaInstance;

	TMap<FString, UObject*> PreExposures;

	UJavascriptCallableWrapper* CallableWrapper;

	FJavascriptAsyncLambdaMapData LambdaMapData;
};