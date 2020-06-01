#include "JavascriptInstanceComponent.h"

UJavascriptInstanceComponent::UJavascriptInstanceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	MainHandler = FJavascriptInstanceHandler::GetMainHandler();
}

void UJavascriptInstanceComponent::InitializeComponent()
{
	Super::InitializeComponent();
	auto ContextOwner = GetOuter();
	if (ContextOwner && !HasAnyFlags(RF_ClassDefaultObject) && !ContextOwner->HasAnyFlags(RF_ClassDefaultObject))
	{
		bool bValidWorld = GetWorld() && ((GetWorld()->IsGameWorld() && !GetWorld()->IsPreviewWorld()));
		if (!bValidWorld)
		{
			return;
		}

		//initialize
	}
}

void UJavascriptInstanceComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
}

void UJavascriptInstanceComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
