#pragma once

#include "JavascriptUModule.generated.h"

//Not currently used as this, see 
//JavascriptContext_Private ExposeUModule instead

//disabled from blueprint type atm
//UCLASS(BlueprintType, ClassGroup = Script, Blueprintable)
UCLASS()
class V8_API UJavascriptUModule : public UObject
{
	GENERATED_UCLASS_BODY()
public:

	//Should be only be used by
	//UFUNCTION(BlueprintCallable)
	//static UJavascriptUModule* StaticInstance(UObject* Owner = nullptr);


	/** The function that should be called on import */
	//UFUNCTION(BlueprintCallable)
	//void ModuleImport(const FString& TargetModule);


	//virtual void BeginDestroy() override;

protected:

};