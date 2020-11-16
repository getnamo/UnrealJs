#pragma once

#include "Components/ActorComponent.h"
#include "JavascriptInstanceHandler.h"
#include "JavascriptInstanceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FJsInstBPNoParamDelegate);

DECLARE_DYNAMIC_DELEGATE(FJsInstNoParamDelegate);
DECLARE_DYNAMIC_DELEGATE_OneParam(FJsInstTickSignature, float, DeltaSeconds);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FJsInstMessageSignature, FString, Name, FString, Message);

//A javascript instance may re-use isolates and gives greater control over expose vs classical javascript component.
UCLASS(BlueprintType, ClassGroup = Script, meta = (BlueprintSpawnableComponent))
class V8_API UJavascriptInstanceComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:



	//Blueprint notifications
	//Called before script start
	UPROPERTY(BlueprintAssignable)
	FJsInstBPNoParamDelegate OnInstanceReady;

	//Called just before a script gets run
	UPROPERTY(BlueprintAssignable)
	FJsInstBPNoParamDelegate OnScriptBegin;

	//Called just after a script finished
	UPROPERTY(BlueprintAssignable)
	FJsInstBPNoParamDelegate OnScriptInitPassEnd;

	//These are only called on javascript side
	UPROPERTY()
	FJsInstTickSignature OnTick;

	UPROPERTY()
	FJsInstNoParamDelegate OnBeginPlay;

	UPROPERTY()
	FJsInstNoParamDelegate OnEndPlay;

	//Linked with EmitToJs
	UPROPERTY()
	FJsInstMessageSignature OnMessage;

	//Specify common domain/uniqueness etc of instance
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category= "Javascript Instance Component")
	FJSInstanceOptions InstanceOptions;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Javascript Instance Component")
	FString DefaultScript;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Javascript Instance Inspector")
	bool bCreateInspectorOnInstanceStartup;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Javascript Instance Inspector")
	int32 InspectorPort;

	UFUNCTION(BlueprintCallable, Category = "Js Functions")
	void Expose(const FString& JsName, UObject* ObjectToExpose);

	UFUNCTION(BlueprintCallable, Category = "Javascript")
	void Emit(const FString& Name, const FString& Message);

	// Begin UActorComponent interface.
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// Begin UActorComponent interface.	

protected:
	TSharedPtr<FJavascriptInstanceHandler> MainHandler;
	TSharedPtr<FJavascriptInstance> Instance;
	FThreadSafeBool bShouldScriptRun;
	FThreadSafeBool bIsThreadRunning;
	bool bIsScriptRunning;

};

//hideCategories = (ComponentReplication) Blueprintable