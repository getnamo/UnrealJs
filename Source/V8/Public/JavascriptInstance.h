#pragma once

#include "CoreMinimal.h"
#include "JavascriptIsolate.h"
#include "JavascriptContext.h"
#include "JavascriptInstance.generated.h"

USTRUCT()
struct V8_API FJavascriptAsset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Javascript")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Javascript")
	FStringAssetReference Asset;
};

USTRUCT()
struct V8_API FJavascriptClassAsset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Javascript")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Javascript")
	TSubclassOf<UObject> Class;
};

USTRUCT(BlueprintType)
struct V8_API FJavascriptFeatures
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript")
	TMap<FString, FString> FeatureMap;

	FJavascriptFeatures();

	//C++ functions
	void AddDefaultIsolateFeatures();
	void AddDefaultContextFeatures();
	void ClearFeatures();
};

/**
 * Holds a unreal wrapped context + features running on specified isolates.
 * Refactored to be main wrapper for a JS instance. Isolates are root attached
 */
class V8_API FJavascriptInstance
{
public:
	FJavascriptInstance(const FJavascriptFeatures& InFeatures, TSharedPtr<FJavascriptIsolate> TargetIsolate = nullptr, FString TargetDomain = TEXT("default"));
	~FJavascriptInstance();

	/** To re-use an isolated, grab a shared pointer and pass it into another instance */
	TSharedPtr<FJavascriptIsolate> GetSharedIsolate();

protected:
	/** This specifies the available exposures to the context */
	FJavascriptFeatures Features;
	bool bIsolateIsUnique;

	//Thread/threadid?
	//Should the isolates be thread managed here or a different class?
	FString IsolateDomain;

	TSharedPtr<FJavascriptIsolate> Isolate;
	TSharedPtr<FJavascriptContext> Context;
	TArray<FString> Paths;
	TSharedPtr<FString> ContextId;
};
