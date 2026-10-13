#include "JavascriptFileWatcherComponent.h"
#include "DirectoryWatcher.h"
#include "V8PCH.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#if V8_ENABLE_DIRECTORY_WATCHER
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#endif

UJavascriptFileWatcherComponent::UJavascriptFileWatcherComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	Extensions.Add(TEXT("js"));
	DebounceSeconds = 0.25f;
	bWatchOnBeginPlay = true;
}

void UJavascriptFileWatcherComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bWatchOnBeginPlay)
	{
		StartWatching();
	}
}

void UJavascriptFileWatcherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopWatching();

	Super::EndPlay(EndPlayReason);
}

FString UJavascriptFileWatcherComponent::ResolveDirectory(const FString& Directory) const
{
	const FString ScriptsDirectory = FPaths::ProjectContentDir() / TEXT("Scripts");
	FString Resolved = Directory.IsEmpty() ? ScriptsDirectory : (FPaths::IsRelative(Directory) ? ScriptsDirectory / Directory : Directory);
	Resolved = FPaths::ConvertRelativePathToFull(Resolved);
	FPaths::NormalizeDirectoryName(Resolved);
	return Resolved;
}

TArray<FString> UJavascriptFileWatcherComponent::GetWatchedDirectories() const
{
	TArray<FString> Result;
	for (const FRegistration& Registration : Registrations)
	{
		Result.Add(Registration.Directory);
	}
	return Result;
}

bool UJavascriptFileWatcherComponent::IsWatching() const
{
	return Registrations.Num() > 0;
}

void UJavascriptFileWatcherComponent::StartWatching()
{
	StopWatching();

#if V8_ENABLE_DIRECTORY_WATCHER
	IDirectoryWatcher* DirectoryWatcher = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")).Get();
	if (!DirectoryWatcher)
	{
		return;
	}

	TArray<FString> ToWatch;
	if (Directories.Num() == 0)
	{
		ToWatch.Add(ResolveDirectory(FString()));
	}
	for (const FString& Directory : Directories)
	{
		ToWatch.AddUnique(ResolveDirectory(Directory));
	}

	TWeakObjectPtr<UJavascriptFileWatcherComponent> WeakThis(this);
	for (const FString& Directory : ToWatch)
	{
		if (!IFileManager::Get().DirectoryExists(*Directory))
		{
			UE_LOG(Javascript, Warning, TEXT("JavascriptFileWatcher: '%s' does not exist; not watching it"), *Directory);
			continue;
		}

		FRegistration Registration;
		Registration.Directory = Directory;
		DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(Directory,
			IDirectoryWatcher::FDirectoryChanged::CreateLambda([WeakThis](const TArray<FFileChangeData>& Changes)
			{
				if (UJavascriptFileWatcherComponent* This = WeakThis.Get())
				{
					TArray<FString> Files;
					for (const FFileChangeData& Change : Changes)
					{
						Files.Add(Change.Filename);
					}
					This->HandleChanges(Files);
				}
			}),
			Registration.Handle);

		if (Registration.Handle.IsValid())
		{
			Registrations.Add(Registration);
		}
	}
#endif
}

void UJavascriptFileWatcherComponent::StopWatching()
{
#if V8_ENABLE_DIRECTORY_WATCHER
	if (Registrations.Num() > 0)
	{
		if (FDirectoryWatcherModule* Module = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
		{
			if (IDirectoryWatcher* DirectoryWatcher = Module->Get())
			{
				for (const FRegistration& Registration : Registrations)
				{
					DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(Registration.Directory, Registration.Handle);
				}
			}
		}
	}
#endif
	Registrations.Empty();
	PendingFiles.Empty();
	SetComponentTickEnabled(false);
}

void UJavascriptFileWatcherComponent::HandleChanges(const TArray<FString>& Files)
{
	bool bAnyMatched = false;
	for (FString File : Files)
	{
		FPaths::NormalizeFilename(File);
		const FString Extension = FPaths::GetExtension(File);
		const bool bMatches = Extensions.Num() == 0 || Extensions.ContainsByPredicate([&Extension](const FString& Wanted)
		{
			return Extension.Equals(Wanted, ESearchCase::IgnoreCase);
		});

		if (bMatches)
		{
			PendingFiles.AddUnique(File);
			bAnyMatched = true;
		}
	}

	if (bAnyMatched)
	{
		LastChangeSeconds = FPlatformTime::Seconds();
		SetComponentTickEnabled(true);
	}
}

void UJavascriptFileWatcherComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (PendingFiles.Num() == 0)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (FPlatformTime::Seconds() - LastChangeSeconds >= DebounceSeconds)
	{
		TArray<FString> Changed = MoveTemp(PendingFiles);
		PendingFiles.Reset();
		SetComponentTickEnabled(false);
		OnFilesChanged.Broadcast(Changed);
	}
}
