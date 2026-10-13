#pragma once

#include "CoreMinimal.h"
#include "V8PCH.h"

class UJavascriptMemoryObject;

/**
 * The ArrayBuffers memory.access made over each memory object, held weakly, so they can be detached
 * when the memory goes away (UJavascriptMemoryObject::ReleaseMemoryViews).
 */
struct FJavascriptMemoryViews
{
	static void Register(const UJavascriptMemoryObject* Source, v8::Isolate* Isolate, v8::Local<v8::ArrayBuffer> Buffer);
	static void Release(const UJavascriptMemoryObject* Source);

	/** Before an isolate is disposed: forget its views. On the isolate's thread. */
	static void ForgetIsolate(v8::Isolate* Isolate);
};
