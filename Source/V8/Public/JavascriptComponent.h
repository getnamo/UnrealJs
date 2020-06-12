#pragma once

#include "Templates/SubclassOf.h"
#include "Components/ActorComponent.h"
#include "JavascriptContext.h"
#include "HAL/ThreadSafeBool.h"
#include "JavascriptInstance.h"
#include "JavascriptComponent.generated.h"

/**
 * 
 */
UCLASS(BlueprintType, ClassGroup = Script, Blueprintable, hideCategories = (ComponentReplication), editinlinenew, meta = (BlueprintSpawnableComponent))
class V8_API UJavascriptComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	DECLARE_DYNAMIC_DELEGATE_OneParam(FJavascriptTickSignature, float, DeltaSeconds);	
	DECLARE_DYNAMIC_DELEGATE_OneParam(FJavascriptNameSignature, FName, Name);
	DECLARE_DYNAMIC_DELEGATE(FJavascriptNoParamSignature);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript")
	FString ScriptSourceFile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript")
	bool bActiveWithinEditor;

	UPROPERTY(EditAnywhere, Category = "Javascript")
	TArray<FJavascriptAsset> Assets;

	UPROPERTY(EditAnywhere, Category = "Javascript")
	TArray<FJavascriptClassAsset> ClassAssets;

	/** If not, we have a clean v8 instance with only what we pass into it using expose */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript")
	bool bEnableFeatures;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript")
	FString IsolateDomain;

	//static TMap<FString, UJavascriptIsolate*> IsolateMap;

	/** Root, Engine, World, UnrealClasses, FileSystem, Networking, etc, all expose features should be enumerated here */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript")
	TMap<FString, FString> Features;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript")
	EJavascriptAsyncOption JavascriptThread;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript")
	bool bCreateInspectorOnStartup;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript")
	int32 InspectorPort;
	

	UFUNCTION(BlueprintCallable, Category = "Javascript")
	void ForceGC();

	UFUNCTION(BlueprintCallable, Category = "Javascript")
	void Expose(FString ExposedAs, UObject* Object);

	UFUNCTION(BlueprintCallable, Category = "Javascript")
	void Invoke(FName Name);
	

	UFUNCTION(BlueprintCallable, Category = "Javascript")
	UObject* ResolveAsset(FName Name, bool bTryLoad = true);

	UFUNCTION(BlueprintCallable, Category = "Javascript")
	UClass* ResolveClass(FName Name);

	/** TBC: wrapper of function caller for BG thread.*/
	UFUNCTION(BlueprintCallable, Category = "Javascript")
	void RunOnBGThread(const FString& Function, const FString& CaptureParams = TEXT(""), const FString& CallbackContext = TEXT(""));

	//C++ only
	UPROPERTY(transient)
	UJavascriptContext* JavascriptContext;

	UPROPERTY(transient)
	UJavascriptIsolate* JavascriptIsolate;

	UPROPERTY()
	FJavascriptTickSignature OnTick;

	UPROPERTY()
	FJavascriptNoParamSignature OnBeginPlay;

	UPROPERTY()
	FJavascriptNoParamSignature OnEndPlay;

	UPROPERTY()
	FJavascriptNameSignature OnInvoke;

	virtual void ProcessEvent(UFunction* Function, void* Parms) override;

	// Begin UActorComponent interface.
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void BeginDestroy() override;
	// Begin UActorComponent interface.	

private:
	//for background variant
	FThreadSafeBool bShouldRun;
	FThreadSafeBool bIsRunning;
};
