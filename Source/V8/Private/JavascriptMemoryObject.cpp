#include "JavascriptMemoryObject.h"
#include "JavascriptMemoryViews.h"
#include "HAL/PlatformTLS.h"
#include "Misc/ScopeLock.h"

namespace
{
	struct FScriptMemoryView
	{
		v8::Isolate* Isolate = nullptr;
		uint32 ThreadId = 0;
		TSharedPtr<v8::Global<v8::ArrayBuffer>> Buffer;
	};

	FCriticalSection& ViewsLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}

	TMap<const UJavascriptMemoryObject*, TArray<FScriptMemoryView>>& AllViews()
	{
		static TMap<const UJavascriptMemoryObject*, TArray<FScriptMemoryView>> Views;
		return Views;
	}
}

void FJavascriptMemoryViews::Register(const UJavascriptMemoryObject* Source, v8::Isolate* Isolate, v8::Local<v8::ArrayBuffer> Buffer)
{
	FScopeLock Lock(&ViewsLock());
	TArray<FScriptMemoryView>& Views = AllViews().FindOrAdd(Source);

	// Weak handles empty themselves once the script drops the buffer
	Views.RemoveAll([](const FScriptMemoryView& View) { return View.Buffer->IsEmpty(); });

	FScriptMemoryView& View = Views.AddDefaulted_GetRef();
	View.Isolate = Isolate;
	View.ThreadId = FPlatformTLS::GetCurrentThreadId();
	View.Buffer = MakeShared<v8::Global<v8::ArrayBuffer>>(Isolate, Buffer);
	View.Buffer->SetWeak();
}

void FJavascriptMemoryViews::Release(const UJavascriptMemoryObject* Source)
{
	TArray<FScriptMemoryView> Views;
	{
		FScopeLock Lock(&ViewsLock());
		AllViews().RemoveAndCopyValue(Source, Views);
	}

	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();
	for (FScriptMemoryView& View : Views)
	{
		if (View.Buffer->IsEmpty())
		{
			continue;
		}
		if (View.ThreadId != ThreadId)
		{
			// Another thread's isolate can't be entered from here; its handle is left as it is
			UE_LOG(Javascript, Warning, TEXT("%s: a script on another thread still views this memory; release it on that thread"), *GetNameSafe(Source));
			continue;
		}
		v8::Isolate::Scope IsolateScope(View.Isolate);
		v8::HandleScope HandleScope(View.Isolate);
		v8::Local<v8::ArrayBuffer> Buffer = View.Buffer->Get(View.Isolate);
		if (Buffer->IsDetachable())
		{
			(void)Buffer->Detach(v8::Local<v8::Value>());
		}
		View.Buffer->Reset();
	}
}

void FJavascriptMemoryViews::ForgetIsolate(v8::Isolate* Isolate)
{
	FScopeLock Lock(&ViewsLock());
	for (auto It = AllViews().CreateIterator(); It; ++It)
	{
		It.Value().RemoveAll([Isolate](FScriptMemoryView& View)
		{
			if (View.Isolate != Isolate)
			{
				return false;
			}
			View.Buffer->Reset();
			return true;
		});
		if (It.Value().Num() == 0)
		{
			It.RemoveCurrent();
		}
	}
}

void UJavascriptMemoryObject::ReleaseMemoryViews()
{
	FJavascriptMemoryViews::Release(this);
}

void UJavascriptMemoryObject::BeginDestroy()
{
	ReleaseMemoryViews();
	Super::BeginDestroy();
}

void UJavascriptExternalMemory::SetMemory(void* InData, int32 InSize, TUniqueFunction<void()>&& InOnRelease)
{
	Release();
	Data = InData;
	Size = InData ? InSize : 0;
	OnRelease = MoveTemp(InOnRelease);
}

void UJavascriptExternalMemory::Release()
{
	ReleaseMemoryViews();
	Data = nullptr;
	Size = 0;
	if (OnRelease)
	{
		TUniqueFunction<void()> Callback = MoveTemp(OnRelease);
		OnRelease = nullptr;
		Callback();
	}
}

void UJavascriptExternalMemory::BeginDestroy()
{
	Release();
	Super::BeginDestroy();
}
