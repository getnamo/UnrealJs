#include "JavascriptInstance.h"
#include "Async/Async.h"
#include "JavascriptInstanceComponent.h"

UJavascriptInstanceComponent::UJavascriptInstanceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	bWantsInitializeComponent = true;
	bCreateInspectorOnInstanceStartup = true;
	InspectorPort = 9229;

	Instance = nullptr;
	MainHandler = FJavascriptInstanceHandler::GetMainHandler().Pin();
}

void UJavascriptInstanceComponent::Expose(const FString& JsName, UObject* ObjectToExpose)
{
	Instance->ContextSettings.Context->Expose(JsName, ObjectToExpose);
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

			if (InstanceOptions.Features.FeatureMap.Contains("Context"))
			{
				Expose(TEXT("Context"), this);
			}
			if (InstanceOptions.Features.FeatureMap.Contains("Root"))
			{
				Expose(TEXT("Root"), GetOwner());
			}
			TFunction<void()> RunDefaultScript = [this]
			{
				OnInstanceReady.Broadcast();

				OnScriptBegin.Broadcast();

				Instance->ContextSettings.Context->Public_RunFile(DefaultScript);

				OnBeginPlay.ExecuteIfBound();

				OnScriptInitPassEnd.Broadcast();

				bShouldScriptRun = true;
				bIsScriptRunning = true;
			};

			if (InstanceOptions.UsesGameThread())
			{
				if (bCreateInspectorOnInstanceStartup)
				{
					Instance->ContextSettings.Context->CreateInspector(InspectorPort);
				}

				RunDefaultScript();
			}
			else
			{
				Async(EAsyncExecution::ThreadPool, [this, RunDefaultScript]()
				{
					RunDefaultScript();

					bIsThreadRunning = true;

					if (InstanceOptions.bAttachToTick)
					{

						while (bShouldScriptRun)
						{
							//Todo: check MT data in

							OnTick.ExecuteIfBound(0.001f);	//todo: feed in actual deltatime

							//Todo: process MT data out
							FPlatformProcess::Sleep(0.001f);
						}
					}

					OnEndPlay.ExecuteIfBound();

					//request garbage collection on the same thread we're on
					if (Instance->ContextSettings.Context.IsValid())
					{
						Instance->ContextSettings.Context->RequestV8GarbageCollection();
					}

					bIsThreadRunning = false;
					bIsScriptRunning = false;
				});
			}
			
		});
	}
}

void UJavascriptInstanceComponent::UninitializeComponent()
{
	bool bValidWorld = GetWorld() && ((GetWorld()->IsGameWorld() && !GetWorld()->IsPreviewWorld()));
	if (!bValidWorld)
	{
		Super::UninitializeComponent();
		return;
	}

	bShouldScriptRun = false;

	OnEndPlay.ExecuteIfBound();

	//Close thread loop cleanly
	if (!InstanceOptions.UsesGameThread())
	{
		bShouldScriptRun = false;

		while (bIsThreadRunning)
		{
			//10micron sleep while waiting
			FPlatformProcess::Sleep(0.0001f);
		}
	}
	else
	{
		bIsThreadRunning = false;
		if (Instance->ContextSettings.Context.IsValid())
		{
			Instance->ContextSettings.Context->RequestV8GarbageCollection();
		}
	}

	MainHandler->ReleaseInstance(Instance);
	Instance = nullptr;
	Super::UninitializeComponent();
}

void UJavascriptInstanceComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (InstanceOptions.bAttachToTick &&
		(InstanceOptions.UsesGameThread()) &&
		bIsScriptRunning)
	{
		OnTick.ExecuteIfBound(DeltaTime);
	}
}
