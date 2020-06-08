#include "JavascriptInstanceComponent.h"

UJavascriptInstanceComponent::UJavascriptInstanceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	Instance = nullptr;
	MainHandler = FJavascriptInstanceHandler::GetMainHandler().Pin();
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

		//Initialize, happens on a callback due to possible delay
		EJSInstanceResult Result = MainHandler->RequestInstance(InstanceOptions, [this](TSharedPtr<FJavascriptInstance> NewInstance)
		{
			Instance = NewInstance;
		});
	}
}

void UJavascriptInstanceComponent::UninitializeComponent()
{
	MainHandler->ReleaseInstance(Instance);
	Instance = nullptr;
	Super::UninitializeComponent();
}

void UJavascriptInstanceComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
