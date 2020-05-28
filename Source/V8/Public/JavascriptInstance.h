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
	bool IsEmpty() const;
};

struct FJSInstanceOptions
{
	FString IsolateDomain;
	TSharedPtr<FJavascriptIsolate> Isolate;
	EUJSThreadOption ThreadOption;
	FJavascriptFeatures Features;

	FJSInstanceOptions();
};

/**
 * Holds a unreal wrapped context + features running on specified isolates.
 * Refactored to be main wrapper for a JS instance. Isolates are root attached
 */
class V8_API FJavascriptInstance
{
public:
	FJavascriptInstance(const FJSInstanceOptions& InOptions);
	~FJavascriptInstance();

	/** To re-use an isolated, grab a shared pointer and pass it into another instance */
	TSharedPtr<FJavascriptIsolate> GetSharedIsolate();

	FString IsolateDomain;

protected:
	/** This specifies the available exposures to the context */
	FJavascriptFeatures Features;
	bool bIsolateIsUnique;

	//Thread/threadid?
	int32 ThreadId;

	TSharedPtr<FJavascriptIsolate> Isolate;
	TSharedPtr<FJavascriptContext> Context;
	TArray<FString> Paths;
	TSharedPtr<FString> ContextId;
};
