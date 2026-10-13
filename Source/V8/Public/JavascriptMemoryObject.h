#pragma once

#include "JavascriptMemoryObject.generated.h"

/**
 * Native memory that scripts can use in place: memory.access(object) returns an ArrayBuffer over
 * GetMemory()/GetSize() without copying.
 *
 * Those ArrayBuffers are detached (they read as empty) by ReleaseMemoryViews(), so a script can't
 * reach memory that is gone. Subclasses call it before their memory is freed, moved or reused; it
 * also runs on destruction.
 */
UCLASS(BlueprintType, Abstract)
class V8_API UJavascriptMemoryObject : public UObject
{
	GENERATED_BODY()

public:
	virtual void* GetMemory() { return nullptr; }
	virtual int32 GetSize() { return 0; }

	/** Detaches every ArrayBuffer memory.access made over this object. Call it on the thread of the script that made them. */
	UFUNCTION(BlueprintCallable, Category = "Javascript")
	void ReleaseMemoryViews();

	virtual void BeginDestroy() override;
};

/**
 * Memory owned elsewhere, for C++ code that lends it to scripts (a mapped GPU readback, a decoder's
 * frame): SetMemory points the object at it, a script reads it through memory.access, and Release()
 * detaches the script's views and gives the memory back through the release callback.
 */
UCLASS(BlueprintType)
class V8_API UJavascriptExternalMemory : public UJavascriptMemoryObject
{
	GENERATED_BODY()

public:
	/** Lends Size bytes at Data. OnRelease runs once, after the views are detached, from Release() or destruction. */
	void SetMemory(void* InData, int32 InSize, TUniqueFunction<void()>&& InOnRelease = nullptr);

	/** Detaches the script's views of the memory and releases it. */
	UFUNCTION(BlueprintCallable, Category = "Javascript")
	void Release();

	/** False once released. */
	UFUNCTION(BlueprintPure, Category = "Javascript")
	bool HasMemory() const { return Data != nullptr; }

	virtual void* GetMemory() override { return Data; }
	virtual int32 GetSize() override { return Size; }
	virtual void BeginDestroy() override;

private:
	void* Data = nullptr;
	int32 Size = 0;
	TUniqueFunction<void()> OnRelease;
};
