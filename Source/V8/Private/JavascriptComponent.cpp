#include "JavascriptComponent.h"
#include "JavascriptIsolate.h"
#include "JavascriptContext.h"
#include "JavascriptStats.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Async/Async.h"
#include "V8PCH.h"
#include "IV8.h"


DECLARE_CYCLE_STAT(TEXT("Javascript Component Tick Time"), STAT_JavascriptComponentTickTime, STATGROUP_Javascript);

//IsolateMap = TMap<FString, UJavascriptIsolate*>();

UJavascriptComponent::UJavascriptComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = false;
	bAutoActivate = true;
	bWantsInitializeComponent = true;

	JavascriptThread = EJavascriptAsyncOption::TaskGraphMainThread;
	IsolateDomain = TEXT("default");

	bEnableFeatures = true;
	bCreateInspectorOnStartup = false;
	InspectorPort = 9229;
	
	//Start with default isolate features
	Features = UJavascriptIsolate::DefaultIsolateFeatures();

	//Add default context feature exposures
	Features.Add(TEXT("Root"), TEXT("default"));
	Features.Add(TEXT("World"), TEXT("default"));
	Features.Add(TEXT("Engine"), TEXT("default"));

	Features.Add(TEXT("Context"), TEXT("default"));

	//Add external features, these don't exist yet!
	//Features.Add(TEXT("FileSystem"), TEXT("default"));
	//Features.Add(TEXT("Networking"), TEXT("default"));
}

void UJavascriptComponent::OnRegister()
{
	auto ContextOwner = GetOuter();

	if (ContextOwner && !HasAnyFlags(RF_ClassDefaultObject) && !ContextOwner->HasAnyFlags(RF_ClassDefaultObject))
	{
		if (GetWorld() && ((GetWorld()->IsGameWorld() && !GetWorld()->IsPreviewWorld()) || bActiveWithinEditor))
		{
			UJavascriptIsolate* Isolate = nullptr;
			if (!IsRunningCommandlet())
			{
				//new isolate if using bg threads
				if (FJavascriptAsyncUtil::IsBgThread(JavascriptThread))
				{
					Isolate = nullptr;
				}
				else {
					UJavascriptStaticCache* StaticGameData = Cast<UJavascriptStaticCache>(GEngine->GameSingleton);
					if (StaticGameData)
					{
						if (StaticGameData->Isolates.Num() > 0)
							Isolate = StaticGameData->Isolates.Pop();
					}
				}
			}
			//new isolate if features are different
			if (Isolate)
			{
				if (Isolate->Features.Num() != Features.Num())
				{
					Isolate = nullptr;
				}
			}

			if (!Isolate)
			{
				Isolate = NewObject<UJavascriptIsolate>();
				Isolate->Init(false, Features);
				Isolate->AddToRoot();
			}

			auto* Context = Isolate->CreateContext();
			JavascriptContext = Context;
			JavascriptIsolate = Isolate;

			//Ensure our context knows which thread it should be in
			JavascriptContext->Thread = JavascriptThread;

			if (Features.Contains(TEXT("Root")))
			{
				Context->Expose("Root", this);
			}
			if (Features.Contains(TEXT("World")))
			{
				Context->Expose("GWorld", GetWorld());
			}
			if (Features.Contains(TEXT("Engine")))
			{
				Context->Expose("GEngine", GEngine);
			}

			if (bCreateInspectorOnStartup && JavascriptThread == EJavascriptAsyncOption::TaskGraphMainThread)
			{
				Context->CreateInspector(InspectorPort);
			}
		}
	}

	Super::OnRegister();
}

void UJavascriptComponent::Activate(bool bReset)
{
	Super::Activate(bReset);

	if (JavascriptContext)
	{
		//Background thread type javascript
		if (FJavascriptAsyncUtil::IsBgThread(JavascriptThread))
		{
			
			EAsyncExecution ExecType = FJavascriptAsyncUtil::ToAsyncExecution(JavascriptThread);
			
			Async(ExecType, [this]
			{
				bShouldRun = true;
				bIsRunning = true;

				JavascriptContext->RunFile(*ScriptSourceFile);
				OnBeginPlay.ExecuteIfBound();

				while (bShouldRun)
				{
					//Todo: check MT data in

					OnTick.ExecuteIfBound(0.001f);	//todo: feed in actual deltatime
					
					//Todo: process MT data out
					FPlatformProcess::Sleep(0.001f);
				}
				//OnEndPlay.ExecuteIfBound();

				//request garbage collection on the same thread we're on
				if (JavascriptContext)
				{
					JavascriptContext->RequestV8GarbageCollection();
				}

				bIsRunning = false;
			});
			
		}
		else
		{
			JavascriptContext->RunFile(*ScriptSourceFile);
			
			SetComponentTickEnabled(OnTick.IsBound());
		}
	}

	if (JavascriptThread == EJavascriptAsyncOption::TaskGraphMainThread)
	{
		OnBeginPlay.ExecuteIfBound();
	}
}

void UJavascriptComponent::Deactivate()
{
	if (JavascriptThread == EJavascriptAsyncOption::TaskGraphMainThread)
	{
		OnEndPlay.ExecuteIfBound();
	}
	else
	{	
		
	}
	Super::Deactivate();
}

void UJavascriptComponent::BeginDestroy()
{
	if (IsValid(GEngine) && !IsRunningCommandlet())
	{
		if (FJavascriptAsyncUtil::IsBgThread(JavascriptThread))
		{
			bShouldRun = false;
			//OnTick.Clear();

			while (bIsRunning)
			{
				//10micron sleep while waiting
				FPlatformProcess::Sleep(0.0001f);
			}
		}

		auto* StaticGameData = Cast<UJavascriptStaticCache>(GEngine->GameSingleton);
		if (StaticGameData)
		{
			StaticGameData->Isolates.Add(JavascriptIsolate);
		}
		else if (JavascriptIsolate)
		{
			JavascriptIsolate->RemoveFromRoot();
			JavascriptIsolate = nullptr;
			JavascriptContext = nullptr;
		}
	}
	if (IsActive())
	{
		Deactivate();
	}

	Super::BeginDestroy();
}

void UJavascriptComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	check(bRegistered);

	SCOPE_CYCLE_COUNTER(STAT_JavascriptComponentTickTime);

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (JavascriptThread == EJavascriptAsyncOption::TaskGraphMainThread)
	{
		OnTick.ExecuteIfBound(DeltaTime);
	}
}

void UJavascriptComponent::ForceGC()
{
	JavascriptContext->RequestV8GarbageCollection();
}

void UJavascriptComponent::Expose(FString ExposedAs, UObject* Object)
{
	JavascriptContext->Expose(ExposedAs, Object);
}

void UJavascriptComponent::Invoke(FName Name)
{
	OnInvoke.ExecuteIfBound(Name);
}

void UJavascriptComponent::ProcessEvent(UFunction* Function, void* Parms)
{
	if (JavascriptContext && JavascriptContext->CallProxyFunction(this, this, Function, Parms))
	{
		return;
	}

	Super::ProcessEvent(Function, Parms);
}

UObject* UJavascriptComponent::ResolveAsset(FName Name, bool bTryLoad)
{
	for (const auto& Item : Assets)
	{
		if (Item.Name == Name)
		{
			return bTryLoad ? Item.Asset.TryLoad() : Item.Asset.ResolveObject();
		}
	}

	return nullptr;
}

UClass* UJavascriptComponent::ResolveClass(FName Name)
{
	for (const auto& Item : ClassAssets)
	{
		if (Item.Name == Name)
		{
			return Item.Class;
		}
	}

	return nullptr;
}

void UJavascriptComponent::RunOnBGThread(const FString& Function, const FString& CaptureParams /*= TEXT("")*/, const FString& CallbackContext /*= TEXT("")*/)
{
	const FString SafeFunction = Function;
	const FString SafeParams = CaptureParams;
	const FString SafeCallback = CallbackContext;

	//todo: fetch fresh 'component', schedule run, and callback on calling component context

	Async(EAsyncExecution::Thread, [SafeFunction, SafeParams, SafeCallback]()
	{
		FString Result = TEXT("");
	});

	//We need a FStyle wrapper for Javascript component so we can maintain sub-isolates for this callback setup
}

