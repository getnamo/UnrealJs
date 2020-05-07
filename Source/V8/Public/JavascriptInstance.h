#pragma once

#include "CoreMinimal.h"
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

/**
 * Holds a unreal wrapped context + features running on specified isolates.
 * Refactored to be main wrapper for a JS instance. Isolates are root attached
 */
class V8_API FJavascriptInstance
{
public:
	FJavascriptInstance();
	~FJavascriptInstance();
};
