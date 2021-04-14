#include "JavascriptInstanceComponent.h"
#include "JavascriptInstance.h"
#include "Async/Async.h"
#include "JavascriptAsync.h"
#include "JavascriptUModule.h"


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

void UJavascriptInstanceComponent::Emit(const FString& Name, const FString& Message)
{
	OnMessage.ExecuteIfBound(Name, Message);
}

void UJavascriptInstanceComponent::EmitBytes(const FString& Name, const TArray<uint8>& Bytes)
{
	OnBytesMessage.ExecuteIfBound(Name, Bytes);
}

void UJavascriptInstanceComponent::Reload()
{
	ShutDownInstance();
	StartupInstanceAndRun();
}

void UJavascriptInstanceComponent::RunFile(const FString& FilePath)
{
	if (InstanceOptions.Features.FeatureMap.Contains("RunFile")) 
	{
		Instance->ContextSettings.Context->Public_RunFile(FilePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UJavascriptInstanceComponent::RunFile permission denied. Add 'RunFile' to features."));
	}
}

UClass* UJavascriptInstanceComponent::ClassByName(const FString& ClassName)
{
	UObject* ClassPackage = ANY_PACKAGE;
	return FindObject<UClass>(ClassPackage, *ClassName);
}

void UJavascriptInstanceComponent::StartupInstanceAndRun()
{
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

				//Safe to expose some basics
				if (InstanceOptions.Features.FeatureMap.Contains("Context"))
				{
					Expose(TEXT("Context"), this);
				}
				if (InstanceOptions.Features.FeatureMap.Contains("Root"))
				{
					Expose(TEXT("Root"), GetOwner());
				}
				if (InstanceOptions.Features.FeatureMap.Contains("Async"))
				{
					Expose(TEXT("Async"), NewObject<UJavascriptAsync>(this));
					//Run javascript wrapping code for this exposure. Allows raw function passing
					FString Content = Instance->ContextSettings.Context->ReadScriptFile(TEXT("async.js"));
					Instance->ContextSettings.Context->Public_RunScript(Content);
					//Instance->ContextSettings.Context->Public_RunFile(TEXT("async.js"));
				}
				if (InstanceOptions.Features.FeatureMap.Contains("UModule"))
				{
					Instance->ContextSettings.Context->ExposeUModule();
				}
				if (InstanceOptions.Features.FeatureMap.Contains("Http"))
				{
					//TODO: expose class not instance...
					//Expose(TEXT("Http"), NewObject<UJavascriptHttpRequest>(this));
				}
				if (InstanceOptions.Features.FeatureMap.Contains(TEXT("World")))
				{
					Expose("GWorld", GetWorld());
				}
				if (InstanceOptions.Features.FeatureMap.Contains(TEXT("Engine")))
				{
					Expose("GEngine", GEngine);
				}
				if (InstanceOptions.Features.FeatureMap.Contains(TEXT("Globals")))
				{
					Instance->ContextSettings.Context->ExposeGlobals();
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
					EAsyncExecution Execution = FJavascriptAsyncUtil::ToAsyncExecution(InstanceOptions.ThreadOption);
					Async(Execution, [this, RunDefaultScript]()
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

void UJavascriptInstanceComponent::ShutDownInstance()
{
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
}

void UJavascriptInstanceComponent::InitializeComponent()
{
	Super::InitializeComponent();
	StartupInstanceAndRun();
}

void UJavascriptInstanceComponent::UninitializeComponent()
{
	bool bValidWorld = GetWorld() && ((GetWorld()->IsGameWorld() && !GetWorld()->IsPreviewWorld()));
	if (!bValidWorld)
	{
		Super::UninitializeComponent();
		return;
	}

	ShutDownInstance();
	
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


