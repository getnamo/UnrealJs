#pragma once

#include "JavascriptInstanceHandler.h"
#include "JavascriptLambda.generated.h"

DECLARE_DYNAMIC_DELEGATE(FJsLambdaNoParamSignature);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FJsLambdaMessageSignature, FString, Message, int32, LambdaId);

UCLASS(BlueprintType, ClassGroup = Script, Blueprintable)
class V8_API UJavascriptLambda : public UObject
{
	GENERATED_UCLASS_BODY()
public:

	static UJavascriptLambda* GetLambda();

	UPROPERTY()
	FJsLambdaNoParamSignature OnLambdaComplete;

	UPROPERTY()
	FJsLambdaMessageSignature OnMessage;

	/** RunLambda script on background thread*/
	UFUNCTION(BlueprintCallable)
	static int32 AsyncRun(const FString& Script);

	TSharedPtr<FJavascriptInstanceHandler> MainHandler;
	TSharedPtr<FJavascriptInstance> LambdaInstance;
};