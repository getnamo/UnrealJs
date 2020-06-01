#pragma once

#include "Components/ActorComponent.h"
#include "JavascriptInstanceHandler.h"
#include "JavascriptInstanceComponent.generated.h"


UCLASS(BlueprintType, ClassGroup = Script, Blueprintable, hideCategories = (ComponentReplication), meta = (BlueprintSpawnableComponent))
class V8_API UJavascriptInstanceComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:

	//Specify common domain/uniqueness etc of instance
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Javascript Instance Component")
	FJSInstanceOptions InstanceOptions;

	// Begin UActorComponent interface.
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// Begin UActorComponent interface.	

protected:
	TWeakPtr<FJavascriptInstanceHandler> MainHandler;
};