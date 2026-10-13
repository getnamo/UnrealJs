#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JavascriptFileWatcherComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FJsFileWatcherChangedSignature, const TArray<FString>&, ChangedFiles);

/**
 * Watches script directories (by default every script search path) and fires OnFilesChanged once
 * per save, e.g. to Reload() a UJavascriptInstanceComponent when a script changes. Changes are
 * collected until no new one arrives for DebounceSeconds, so an editor's write-then-rename save
 * reports once.
 *
 * Editor builds only (uses the DirectoryWatcher module); in packaged builds it never fires.
 */
UCLASS(BlueprintType, ClassGroup = Script, meta = (BlueprintSpawnableComponent))
class V8_API UJavascriptFileWatcherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJavascriptFileWatcherComponent();

	/**
	 * Directories to watch, including subdirectories. Relative paths resolve against the project's
	 * Content/Scripts. Empty = every script search path (the project's and each plugin's Content/Scripts)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Javascript File Watcher")
	TArray<FString> Directories;

	/** Only report files with these extensions (no dot, case-insensitive). Empty = every file */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Javascript File Watcher")
	TArray<FString> Extensions;

	/** Quiet period after the last change before OnFilesChanged fires */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Javascript File Watcher", meta = (ClampMin = "0"))
	float DebounceSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Javascript File Watcher")
	bool bWatchOnBeginPlay;

	/** Full paths of the files that changed since the last notification */
	UPROPERTY(BlueprintAssignable, Category = "Javascript File Watcher")
	FJsFileWatcherChangedSignature OnFilesChanged;

	UFUNCTION(BlueprintCallable, Category = "Javascript File Watcher")
	void StartWatching();

	UFUNCTION(BlueprintCallable, Category = "Javascript File Watcher")
	void StopWatching();

	UFUNCTION(BlueprintPure, Category = "Javascript File Watcher")
	bool IsWatching() const;

	/** Directories actually watched (resolved, absolute) */
	UFUNCTION(BlueprintPure, Category = "Javascript File Watcher")
	TArray<FString> GetWatchedDirectories() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void HandleChanges(const TArray<FString>& Files);
	FString ResolveDirectory(const FString& Directory) const;

	struct FRegistration
	{
		FString Directory;
		FDelegateHandle Handle;
	};
	TArray<FRegistration> Registrations;

	TArray<FString> PendingFiles;
	double LastChangeSeconds = 0.0;
};
