#include "JavascriptInstanceComponent.h"

UJavascriptInstanceComponent::UJavascriptInstanceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}