#include "JavascriptAsync.h"
#include "JavascriptWorker.h"
#include "UObject/Package.h"

UJavascriptAsync::UJavascriptAsync(class FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		Host = MakeShared<FJavascriptWorkerHost>();

		TWeakObjectPtr<UJavascriptAsync> WeakThis(this);
		Host->OnMessage = [WeakThis](const FJavascriptWorkerMessage& Message)
		{
			if (UJavascriptAsync* This = WeakThis.Get())
			{
				This->HandleWorkerMessage(Message);
			}
		};
	}
}

UJavascriptAsync* UJavascriptAsync::StaticInstance(UObject* Owner)
{
	return NewObject<UJavascriptAsync>(Owner ? Owner : GetTransientPackage());
}

void UJavascriptAsync::PreExposeObject(UObject* Object, const FString& Name)
{
	PreExposures.Emplace(Name, Object);
}

void UJavascriptAsync::ClearPreExposures()
{
	PreExposures.Empty();
}

int32 UJavascriptAsync::SpawnWorker(const FString& Source, bool bEnableRequire)
{
	if (!Host.IsValid())
	{
		return 0;
	}

	FJavascriptWorkerSpec Spec;
	Spec.Source = Source;
	Spec.bEnableRequire = bEnableRequire;

	// Exposures are captured per worker here, on the game thread
	for (const TPair<FString, TWeakObjectPtr<UObject>>& Exposure : PreExposures)
	{
		if (UObject* Object = Exposure.Value.Get())
		{
			Spec.Exposures.Emplace(Exposure.Key, Object);
		}
	}
	PreExposures.Empty();

	return Host->Spawn(MoveTemp(Spec));
}

bool UJavascriptAsync::PostToWorker(int32 WorkerId, const FString& Kind, int32 RequestId, const FString& Name, const FString& Payload)
{
	if (!Host.IsValid())
	{
		return false;
	}

	FJavascriptWorkerMessage Message;
	Message.Kind = Kind;
	Message.RequestId = RequestId;
	Message.Name = Name;
	Message.Payload = Payload;
	return Host->Post(WorkerId, MoveTemp(Message));
}

bool UJavascriptAsync::ExposeObjectInWorker(int32 WorkerId, UObject* Object, const FString& Name)
{
	return Host.IsValid() && Host->Expose(WorkerId, Object, Name);
}

void UJavascriptAsync::StopWorker(int32 WorkerId)
{
	if (Host.IsValid())
	{
		Host->Stop(WorkerId);
	}
}

void UJavascriptAsync::StopAllWorkers()
{
	if (Host.IsValid())
	{
		Host->StopAll();
	}
}

int32 UJavascriptAsync::NumWorkers() const
{
	return Host.IsValid() ? Host->NumWorkers() : 0;
}

void UJavascriptAsync::ProcessWorkerMessages()
{
	if (Host.IsValid())
	{
		Host->ProcessMessages();
	}
}

int32 UJavascriptAsync::NumLiveWorkerThreads()
{
	return FJavascriptWorkerHost::NumLiveWorkerThreads();
}

void UJavascriptAsync::Shutdown()
{
	// Unbind first: nothing may reach the owning context while it is being torn down
	OnWorkerMessage.Unbind();
	PreExposures.Empty();

	if (Host.IsValid())
	{
		Host->Shutdown();
	}
}

void UJavascriptAsync::BeginDestroy()
{
	Shutdown();
	Host.Reset();

	Super::BeginDestroy();
}

void UJavascriptAsync::HandleWorkerMessage(const FJavascriptWorkerMessage& Message)
{
	OnWorkerMessage.ExecuteIfBound(Message.Kind, Message.WorkerId, Message.RequestId, Message.Name, Message.Payload);
}
