#pragma once

#include "CoreMinimal.h"
#include "JavascriptIsolate.h"
#include "JavascriptContext.h"
#include "JavascriptInstance.generated.h"

USTRUCT()
struct V8_API FJavascriptAsset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Javascript Asset")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Javascript Asset")
	FSoftObjectPath Asset;
};

USTRUCT()
struct V8_API FJavascriptClassAsset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Javascript Class Asset")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Javascript Class Asset")
	TSubclassOf<UObject> Class;
};

USTRUCT(BlueprintType)
struct V8_API FJavascriptFeatures
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript Features")
	TMap<FString, FString> FeatureMap;

	FJavascriptFeatures();

	//C++ functions
	void AddDefaultIsolateFeatures();
	void AddDefaultContextFeatures();
	void ClearFeatures();
	bool IsEmpty() const;
};

USTRUCT(BlueprintType)
struct V8_API FJSInstanceOptions
{
	GENERATED_USTRUCT_BODY();

	//Quick disable option. Can only change on startup
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript Instance Options")
	bool bEnabled;

	//Will determine isolate/context domains if non-unique option is set
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript Instance Options")
	FString IsolateDomain;

	//If using non-default/non-gamethread keep in mind that most features have to be off
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript Instance Options")
	EJavascriptAsyncOption ThreadOption;

	//Exposed feature map
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript Instance Options")
	FJavascriptFeatures Features;

	//If false: same domain will re-use isolate
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript Instance Options")
	bool bUseUniqueIsolate;

	//If false: same domain will re-use context
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript Instance Options")
	bool bUseUniqueContext;

	//if true this will receive tick events
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript Instance Options")
	bool bAttachToTick;

	//UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Javascript Instance Options")
	//bool bAttachInspector;

	bool UsesGameThread() const;

	FJSInstanceOptions();
};

struct FJSInstanceContextSettings
{
	TSharedPtr<FJavascriptIsolate> Isolate;
	TSharedPtr<FJavascriptContext> Context;
	int32 ThreadId;

	FJSInstanceContextSettings();
};

/**
 * Holds a unreal wrapped context + features running on specified isolates.
 * Refactored to be main wrapper for a JS instance. Isolates are root attached
 */
class V8_API FJavascriptInstance
{
public:
	//Settings have to be set by handler
	FJavascriptInstance(const FJSInstanceOptions& InOptions, const FJSInstanceContextSettings& DerivedSettings);
	~FJavascriptInstance();

	/** To re-use an isolated, grab a shared pointer and pass it into another instance */
	TSharedPtr<FJavascriptIsolate> GetSharedIsolate();

	FString IsolateDomain;

	//Encompasses Features, thread ids etc
	FJSInstanceOptions Options;
	FJSInstanceContextSettings ContextSettings;

protected:

	//Thread/threadid?
	TArray<FString> Paths;
	TSharedPtr<FString> ContextId;
};
