#pragma once

#include "Components/ActorComponent.h"
#include "JavascriptInstanceHandler.h"
#include "JavascriptInstanceComponent.generated.h"


UCLASS(BlueprintType, ClassGroup = Script, Blueprintable, hideCategories = (ComponentReplication), meta = (BlueprintSpawnableComponent))
class V8_API UJavascriptInstanceComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

};