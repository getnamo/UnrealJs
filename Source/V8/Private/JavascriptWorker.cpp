#include "JavascriptWorker.h"
#include "JavascriptIsolate_Private.h"
#include "JavascriptContext_Private.h"
#include "Translator.h"
#include "Exception.h"
#include "IV8.h"
#include "HAL/Event.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "UObject/GarbageCollection.h"
#include <libplatform/libplatform.h>

namespace
{
	/** How long an idle worker sleeps before pumping its isolate's V8 tasks again */
	constexpr uint32 IdlePumpIntervalMs = 100;

	/** Stack kept below the JS stack limit for native frames (UE reflection, V8 runtime) */
	constexpr uint32 NativeStackReserve = 1024 * 1024;

	/** Upper bound on V8 tasks run per pump, so a self-reposting task can't starve messages */
	constexpr int32 MaxTasksPerPump = 64;

	constexpr uint32 ShutdownJoinTimeoutMs = 5000;

	std::atomic<int32> GLiveWorkerThreads{ 0 };
	std::atomic<int32> GWorkerThreadSerial{ 0 };

	/** Process-wide view of workers, for module shutdown and for workers that outlived a stop timeout */
	struct FWorkerRegistry
	{
		FCriticalSection Lock;
		TArray<TWeakPtr<FJavascriptWorker>> Live;

		/** Workers whose host gave up waiting on them; destroyed once their thread exits */
		TArray<TSharedPtr<FJavascriptWorker>> Orphans;
	};

	// Intentionally never destroyed: a worker stuck at exit must not be joined from a static destructor
	FWorkerRegistry& GetRegistry()
	{
		static FWorkerRegistry* Registry = new FWorkerRegistry();
		return *Registry;
	}

	void RegisterLiveWorker(const TSharedPtr<FJavascriptWorker>& Worker)
	{
		FWorkerRegistry& Registry = GetRegistry();
		FScopeLock Lock(&Registry.Lock);
		Registry.Live.RemoveAll([](const TWeakPtr<FJavascriptWorker>& Entry) { return !Entry.IsValid(); });
		Registry.Live.Add(Worker);
	}

	void AdoptOrphan(const TSharedPtr<FJavascriptWorker>& Worker)
	{
		FWorkerRegistry& Registry = GetRegistry();
		FScopeLock Lock(&Registry.Lock);
		Registry.Orphans.Add(Worker);
	}

	/** Game thread: destroys orphans whose threads have exited */
	void ReapOrphans()
	{
		TArray<TSharedPtr<FJavascriptWorker>> Exited;
		{
			FWorkerRegistry& Registry = GetRegistry();
			FScopeLock Lock(&Registry.Lock);
			for (int32 Index = Registry.Orphans.Num() - 1; Index >= 0; --Index)
			{
				if (Registry.Orphans[Index]->HasExited())
				{
					Exited.Add(Registry.Orphans[Index]);
					Registry.Orphans.RemoveAtSwap(Index);
				}
			}
		}
		// Exited goes out of scope outside the lock: destroying a worker joins its (finished) thread
	}

	FString ToJsonString(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len() + 2);
		Out.AppendChar(TEXT('"'));
		for (const TCHAR Char : In)
		{
			switch (Char)
			{
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (Char < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Char));
				}
				else
				{
					Out.AppendChar(Char);
				}
			}
		}
		Out.AppendChar(TEXT('"'));
		return Out;
	}
}

// FJavascriptWorkerOutbox

void FJavascriptWorkerOutbox::Post(FJavascriptWorkerMessage&& Message)
{
	if (!bClosed)
	{
		Queue.Enqueue(MoveTemp(Message));
	}
}

bool FJavascriptWorkerOutbox::Dequeue(FJavascriptWorkerMessage& OutMessage)
{
	return Queue.Dequeue(OutMessage);
}

void FJavascriptWorkerOutbox::Empty()
{
	FJavascriptWorkerMessage Discarded;
	while (Queue.Dequeue(Discarded))
	{
	}
}

// FJavascriptWorker

FJavascriptWorker::FJavascriptWorker(int32 InId, FJavascriptWorkerSpec&& InSpec, TSharedRef<FJavascriptWorkerOutbox> InOutbox)
	: Id(InId)
	, Spec(MoveTemp(InSpec))
	, Outbox(InOutbox)
	, Platform(IV8::Get().GetV8Platform())
	, ScriptPaths(IV8::Get().GetGlobalScriptSearchPaths())
	, WakeEvent(FPlatformProcess::GetSynchEventFromPool(false))
	, ExitedEvent(FPlatformProcess::GetSynchEventFromPool(true))
{
	check(IsInGameThread());
	for (const TPair<FString, UObject*>& Exposure : Spec.Exposures)
	{
		AddStrongReference(Exposure.Value);
	}
}

FJavascriptWorker::~FJavascriptWorker()
{
	if (Thread)
	{
		// Owners only destroy workers whose thread has exited (or at shutdown after a stop), so this join is short
		RequestStop();
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
	FPlatformProcess::ReturnSynchEventToPool(ExitedEvent);
}

bool FJavascriptWorker::Start()
{
	check(IsInGameThread());
	const FString ThreadName = FString::Printf(TEXT("JsWorker_%d"), ++GWorkerThreadSerial);
	Thread = FRunnableThread::Create(this, *ThreadName, Spec.StackSize, TPri_BelowNormal);
	if (!Thread)
	{
		bHasExited = true;
		ExitedEvent->Trigger();
		return false;
	}
	return true;
}

void FJavascriptWorker::Post(FJavascriptWorkerMessage&& Message)
{
	// Spsc: the game thread is the only producer
	Inbox.Enqueue(MoveTemp(Message));
	WakeEvent->Trigger();
}

void FJavascriptWorker::AddStrongReference(UObject* Object)
{
	check(IsInGameThread());
	if (Object)
	{
		StrongReferences.Emplace(Object);
	}
}

void FJavascriptWorker::RequestStop()
{
	bStopRequested = true;
	{
		// TerminateExecution is callable from any thread without a Locker; it interrupts runaway scripts
		FScopeLock Lock(&IsolateLock);
		if (LiveIsolate)
		{
			LiveIsolate->TerminateExecution();
		}
	}
	WakeEvent->Trigger();
}

bool FJavascriptWorker::WaitForExit(uint32 TimeoutMs)
{
	return bHasExited || ExitedEvent->Wait(TimeoutMs);
}

uint32 FJavascriptWorker::Run()
{
	++GLiveWorkerThreads;

	uint8 StackMarker = 0;
	if (!bStopRequested)
	{
		RunIsolate(reinterpret_cast<uintptr_t>(&StackMarker));
	}

	FJavascriptWorkerMessage Stopped;
	Stopped.Kind = TEXT("stopped");
	Stopped.WorkerId = Id;
	Outbox->Post(MoveTemp(Stopped));

	--GLiveWorkerThreads;
	bHasExited = true;
	ExitedEvent->Trigger();
	return 0;
}

void FJavascriptWorker::RunIsolate(uintptr_t StackTop)
{
	// Everything V8 below is created, used and destroyed on this thread, so no v8::Locker is needed
	TSharedPtr<FJavascriptIsolate> Isolate(FJavascriptIsolate::Create(false, /*bIsWorker=*/true));
	v8::Isolate* V8Isolate = Isolate->isolate_;

	if (Spec.StackSize > NativeStackReserve * 2)
	{
		// V8's default limit is computed for this thread already (it created the isolate);
		// raise it to the stack we asked for, minus room for native frames
		v8::Isolate::Scope IsolateScope(V8Isolate);
		V8Isolate->SetStackLimit(StackTop - (Spec.StackSize - NativeStackReserve));
	}

	// Plain-data worker: no Unreal class/global exposure
	TMap<FString, FString> NoFeatures;
	Isolate->SetAvailableFeatures(NoFeatures);

	{
		TSharedPtr<FJavascriptContext> Context(FJavascriptContext::Create(Isolate, ScriptPaths));
		{
			FScopeLock Lock(&IsolateLock);
			LiveIsolate = V8Isolate;
		}

		if (!bStopRequested)
		{
			InstallBridge(*Context);

			if (Spec.bEnableRequire)
			{
				Context->ExposeUModule();
			}

			if (Spec.Exposures.Num() > 0)
			{
				// Exporting a UObject walks its class; keep GC from running underneath us
				FGCScopeGuard GCGuard;
				for (const TPair<FString, UObject*>& Exposure : Spec.Exposures)
				{
					Context->Expose(Exposure.Key, Exposure.Value);
				}
			}

			const FString PreludeName = TEXT("async-worker.js");
			const FString Prelude = Context->ReadScriptFile(PreludeName);
			bool bBootstrapped = false;
			if (Prelude.IsEmpty())
			{
				FJavascriptWorkerMessage Error;
				Error.Kind = TEXT("error");
				Error.WorkerId = Id;
				Error.Name = TEXT("__main");
				Error.Payload = FString::Printf(TEXT("{\"message\":%s}"), *ToJsonString(TEXT("async worker prelude 'async-worker.js' not found in script paths")));
				Outbox->Post(MoveTemp(Error));
			}
			else if (RunScript(*Context, Context->GetScriptFileFullPath(PreludeName), Prelude))
			{
				bBootstrapped = RunScript(*Context, FString::Printf(TEXT("async-lambda-%d.js"), Id), Spec.Source);
			}

			// A failed bootstrap has nothing to serve. (A one-shot may also have closed itself already:
			// microtasks run at the end of the bootstrap script, so never clear bCloseRequested here.)
			if (!bBootstrapped)
			{
				bCloseRequested = true;
			}

			v8::Platform* V8Platform = static_cast<v8::Platform*>(Platform);
			while (!bStopRequested && !bCloseRequested)
			{
				FJavascriptWorkerMessage Message;
				while (!bStopRequested && !bCloseRequested && Inbox.Dequeue(Message))
				{
					Dispatch(*Context, Message);
				}

				if (bStopRequested || bCloseRequested)
				{
					break;
				}

				// setTimeout/setInterval callbacks that are due (they may close a one-shot worker)
				const int32 NextTimerMs = RunTimers(*Context);
				if (bStopRequested || bCloseRequested)
				{
					break;
				}

				{
					// This isolate's foreground tasks (GC jobs, finalization, ...) run here, on its own thread
					v8::Isolate::Scope IsolateScope(V8Isolate);
					for (int32 TaskCount = 0; TaskCount < MaxTasksPerPump && v8::platform::PumpMessageLoop(V8Platform, V8Isolate); ++TaskCount)
					{
					}
					V8Isolate->PerformMicrotaskCheckpoint();
				}

				// Woken by Post/RequestStop or the next timer; the idle timeout only bounds how stale V8's own tasks can get
				const uint32 WaitMs = NextTimerMs < 0 ? IdlePumpIntervalMs : FMath::Min(static_cast<uint32>(NextTimerMs), IdlePumpIntervalMs);
				if (WaitMs > 0)
				{
					WakeEvent->Wait(WaitMs);
				}
			}
		}

		{
			FScopeLock Lock(&IsolateLock);
			LiveIsolate = nullptr;
		}

		BridgeObject.Reset();
		Context.Reset();
	}

	// isolate->Dispose() happens here, on the thread that created it
	Isolate.Reset();
}

void FJavascriptWorker::InstallBridge(FJavascriptContext& Context)
{
	v8::Isolate* V8Isolate = Context.isolate();
	v8::Isolate::Scope IsolateScope(V8Isolate);
	v8::HandleScope HandleScope(V8Isolate);
	v8::Local<v8::Context> V8Context = Context.context();
	v8::Context::Scope ContextScope(V8Context);

	v8::Local<v8::External> Self = v8::External::New(V8Isolate, this);

	// __worker.post(kind, requestId, name, payload, bytes?): queue a message for the game thread.
	// bytes (ArrayBuffer or a view) is copied, so the script may reuse its buffer right away.
	auto PostCallback = [](const v8::FunctionCallbackInfo<v8::Value>& Info)
	{
		FJavascriptWorker* Worker = static_cast<FJavascriptWorker*>(Info.Data().As<v8::External>()->Value());
		v8::Isolate* CallIsolate = Info.GetIsolate();
		auto StringArg = [&Info, CallIsolate](int32 Index)
		{
			return (Info.Length() > Index && Info[Index]->IsString()) ? StringFromV8(CallIsolate, Info[Index]) : FString();
		};

		FJavascriptWorkerMessage Message;
		Message.Kind = StringArg(0);
		Message.WorkerId = Worker->Id;
		Message.RequestId = Info.Length() > 1 ? Info[1]->Int32Value(CallIsolate->GetCurrentContext()).FromMaybe(0) : 0;
		Message.Name = StringArg(2);
		Message.Payload = StringArg(3);

		if (Info.Length() > 4)
		{
			const v8::Local<v8::Value> BytesArg = Info[4];
			if (BytesArg->IsArrayBufferView())
			{
				const v8::Local<v8::ArrayBufferView> View = BytesArg.As<v8::ArrayBufferView>();
				Message.Bytes.SetNumUninitialized(static_cast<int32>(View->ByteLength()));
				View->CopyContents(Message.Bytes.GetData(), Message.Bytes.Num());
			}
			else if (BytesArg->IsArrayBuffer())
			{
				const v8::Local<v8::ArrayBuffer> Buffer = BytesArg.As<v8::ArrayBuffer>();
				Message.Bytes.SetNumUninitialized(static_cast<int32>(Buffer->ByteLength()));
				FMemory::Memcpy(Message.Bytes.GetData(), Buffer->Data(), Message.Bytes.Num());
			}
		}

		Worker->Outbox->Post(MoveTemp(Message));
	};

	// __worker.close(): a one-shot lambda is done; the worker thread exits after this message
	auto CloseCallback = [](const v8::FunctionCallbackInfo<v8::Value>& Info)
	{
		static_cast<FJavascriptWorker*>(Info.Data().As<v8::External>()->Value())->bCloseRequested = true;
	};

	v8::Local<v8::Object> Bridge = v8::Object::New(V8Isolate);
	(void)Bridge->Set(V8Context, V8_KeywordString(V8Isolate, "post"), v8::Function::New(V8Context, PostCallback, Self).ToLocalChecked());
	(void)Bridge->Set(V8Context, V8_KeywordString(V8Isolate, "close"), v8::Function::New(V8Context, CloseCallback, Self).ToLocalChecked());
	(void)Bridge->Set(V8Context, V8_KeywordString(V8Isolate, "id"), v8::Integer::New(V8Isolate, Id));
	(void)V8Context->Global()->Set(V8Context, V8_KeywordString(V8Isolate, "__worker"), Bridge);

	BridgeObject.Reset(V8Isolate, Bridge);
}

bool FJavascriptWorker::RunScript(FJavascriptContext& Context, const FString& FileName, const FString& Source)
{
	v8::Isolate* V8Isolate = Context.isolate();
	v8::Isolate::Scope IsolateScope(V8Isolate);
	v8::HandleScope HandleScope(V8Isolate);
	v8::Local<v8::Context> V8Context = Context.context();
	v8::Context::Scope ContextScope(V8Context);
	v8::TryCatch TryCatch(V8Isolate);

	v8::ScriptOrigin Origin(V8_String(V8Isolate, FileName));
	v8::Local<v8::Script> CompiledScript;
	v8::Local<v8::Value> Result;
	if (!v8::Script::Compile(V8Context, V8_String(V8Isolate, Source), &Origin).ToLocal(&CompiledScript) ||
		!CompiledScript->Run(V8Context).ToLocal(&Result))
	{
		ReportScriptError(V8Isolate, TryCatch, 0, TEXT("__main"));
		return false;
	}
	return true;
}

void FJavascriptWorker::Dispatch(FJavascriptContext& Context, const FJavascriptWorkerMessage& Message)
{
	if (Message.Kind == TEXT("expose"))
	{
		if (Message.Object)
		{
			FGCScopeGuard GCGuard;
			Context.Expose(Message.Name, Message.Object);
		}
		return;
	}

	v8::Isolate* V8Isolate = Context.isolate();
	v8::Isolate::Scope IsolateScope(V8Isolate);
	v8::HandleScope HandleScope(V8Isolate);
	v8::Local<v8::Context> V8Context = Context.context();
	v8::Context::Scope ContextScope(V8Context);
	v8::TryCatch TryCatch(V8Isolate);

	v8::Local<v8::Object> Bridge = BridgeObject.Get(V8Isolate);
	v8::Local<v8::Value> DispatchValue;
	if (Bridge.IsEmpty() ||
		!Bridge->Get(V8Context, V8_KeywordString(V8Isolate, "dispatch")).ToLocal(&DispatchValue) ||
		!DispatchValue->IsFunction())
	{
		UE_LOG(Javascript, Error, TEXT("Async worker %d: async-worker.js did not install __worker.dispatch; dropped '%s'"), Id, *Message.Kind);
		return;
	}

	// Bytes arrive as a Uint8Array over a fresh buffer this isolate owns
	v8::Local<v8::Value> BytesArg = v8::Undefined(V8Isolate);
	if (Message.Bytes.Num() > 0)
	{
		const v8::Local<v8::ArrayBuffer> Buffer = v8::ArrayBuffer::New(V8Isolate, Message.Bytes.Num());
		FMemory::Memcpy(Buffer->Data(), Message.Bytes.GetData(), Message.Bytes.Num());
		BytesArg = v8::Uint8Array::New(Buffer, 0, Message.Bytes.Num());
	}

	// Payload travels as a string argument; it is never spliced into source
	v8::Local<v8::Value> Args[] = {
		V8_String(V8Isolate, Message.Kind),
		v8::Integer::New(V8Isolate, Message.RequestId),
		V8_String(V8Isolate, Message.Name),
		V8_String(V8Isolate, Message.Payload),
		BytesArg
	};

	v8::Local<v8::Value> Result;
	if (!DispatchValue.As<v8::Function>()->Call(V8Context, Bridge, UE_ARRAY_COUNT(Args), Args).ToLocal(&Result))
	{
		ReportScriptError(V8Isolate, TryCatch, Message.RequestId, Message.Name);
	}
}

int32 FJavascriptWorker::RunTimers(FJavascriptContext& Context)
{
	v8::Isolate* V8Isolate = Context.isolate();
	v8::Isolate::Scope IsolateScope(V8Isolate);
	v8::HandleScope HandleScope(V8Isolate);
	v8::Local<v8::Context> V8Context = Context.context();
	v8::Context::Scope ContextScope(V8Context);
	v8::TryCatch TryCatch(V8Isolate);

	v8::Local<v8::Object> Bridge = BridgeObject.Get(V8Isolate);
	v8::Local<v8::Value> TickValue;
	if (Bridge.IsEmpty() ||
		!Bridge->Get(V8Context, V8_KeywordString(V8Isolate, "tick")).ToLocal(&TickValue) ||
		!TickValue->IsFunction())
	{
		return -1;
	}

	v8::Local<v8::Value> Result;
	if (!TickValue.As<v8::Function>()->Call(V8Context, Bridge, 0, nullptr).ToLocal(&Result))
	{
		ReportScriptError(V8Isolate, TryCatch, 0, TEXT("__timer"));
		return -1;
	}
	return Result->Int32Value(V8Context).FromMaybe(-1);
}

void FJavascriptWorker::ReportScriptError(v8::Isolate* V8Isolate, v8::TryCatch& TryCatch, int32 RequestId, const FString& Name)
{
	// Termination from RequestStop unwinds as an uncatchable exception; it is not a script error
	if (bStopRequested || TryCatch.HasTerminated() || !TryCatch.HasCaught())
	{
		return;
	}

	// Report() also writes the error and stack to the log
	const FString Report = FV8Exception::Report(V8Isolate, TryCatch);
	FString FirstLine = Report;
	int32 NewLineIndex = INDEX_NONE;
	if (Report.FindChar(TEXT('\n'), NewLineIndex))
	{
		FirstLine = Report.Left(NewLineIndex);
	}

	FJavascriptWorkerMessage Error;
	Error.Kind = TEXT("error");
	Error.WorkerId = Id;
	Error.RequestId = RequestId;
	Error.Name = Name;
	Error.Payload = FString::Printf(TEXT("{\"message\":%s,\"stack\":%s,\"logged\":true}"), *ToJsonString(FirstLine.TrimEnd()), *ToJsonString(Report));
	Outbox->Post(MoveTemp(Error));
}

// FJavascriptWorkerHost

FJavascriptWorkerHost::FJavascriptWorkerHost()
	: Outbox(MakeShared<FJavascriptWorkerOutbox>())
{
}

FJavascriptWorkerHost::~FJavascriptWorkerHost()
{
	Shutdown();
	OnMessage.Reset();
}

void FJavascriptWorkerHost::EnsureTicker()
{
	if (!TickHandle.IsValid())
	{
		TWeakPtr<FJavascriptWorkerHost> WeakHost = AsShared();
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakHost](float DeltaTime)
		{
			if (TSharedPtr<FJavascriptWorkerHost> PinnedHost = WeakHost.Pin())
			{
				PinnedHost->ProcessMessages();
				return true;
			}
			return false;
		}));
	}
}

int32 FJavascriptWorkerHost::Spawn(FJavascriptWorkerSpec&& Spec)
{
	check(IsInGameThread());
	if (bIsShutdown)
	{
		UE_LOG(Javascript, Warning, TEXT("Async: ignoring worker spawn after its owner shut down (stale script?)"));
		return 0;
	}

	ReapOrphans();
	EnsureTicker();

	const int32 WorkerId = NextWorkerId++;
	TSharedPtr<FJavascriptWorker> Worker = MakeShared<FJavascriptWorker>(WorkerId, MoveTemp(Spec), Outbox);
	if (!Worker->Start())
	{
		UE_LOG(Javascript, Error, TEXT("Async: failed to create a thread for worker %d"), WorkerId);
		return 0;
	}

	Workers.Add(WorkerId, Worker);
	RegisterLiveWorker(Worker);
	return WorkerId;
}

bool FJavascriptWorkerHost::Post(int32 WorkerId, FJavascriptWorkerMessage&& Message)
{
	check(IsInGameThread());
	if (TSharedPtr<FJavascriptWorker>* Worker = Workers.Find(WorkerId))
	{
		Message.WorkerId = WorkerId;
		(*Worker)->Post(MoveTemp(Message));
		return true;
	}
	return false;
}

bool FJavascriptWorkerHost::Expose(int32 WorkerId, UObject* Object, const FString& Name)
{
	check(IsInGameThread());
	TSharedPtr<FJavascriptWorker>* Worker = Workers.Find(WorkerId);
	if (!Worker || !Object)
	{
		return false;
	}

	(*Worker)->AddStrongReference(Object);

	FJavascriptWorkerMessage Message;
	Message.Kind = TEXT("expose");
	Message.WorkerId = WorkerId;
	Message.Name = Name;
	Message.Object = Object;
	(*Worker)->Post(MoveTemp(Message));
	return true;
}

void FJavascriptWorkerHost::Stop(int32 WorkerId)
{
	check(IsInGameThread());
	if (TSharedPtr<FJavascriptWorker>* Worker = Workers.Find(WorkerId))
	{
		(*Worker)->RequestStop();
	}
}

void FJavascriptWorkerHost::StopAll(uint32 TimeoutMs)
{
	check(IsInGameThread());
	if (Workers.Num() == 0)
	{
		return;
	}

	TArray<TSharedPtr<FJavascriptWorker>> Stopping;
	Workers.GenerateValueArray(Stopping);
	Workers.Empty();

	for (const TSharedPtr<FJavascriptWorker>& Worker : Stopping)
	{
		Worker->RequestStop();
	}

	const double Deadline = FPlatformTime::Seconds() + TimeoutMs / 1000.0;
	for (const TSharedPtr<FJavascriptWorker>& Worker : Stopping)
	{
		const double RemainingMs = FMath::Max(0.0, (Deadline - FPlatformTime::Seconds()) * 1000.0);
		if (!Worker->WaitForExit(static_cast<uint32>(RemainingMs)))
		{
			// Most likely blocked inside a native call, which TerminateExecution can't interrupt
			UE_LOG(Javascript, Warning, TEXT("Async: worker %d did not stop within %u ms; it will be cleaned up when its thread exits"), Worker->GetId(), TimeoutMs);
			AdoptOrphan(Worker);
		}
	}

	// Anything still queued (including the workers' own "stopped") is superseded by the notifications below
	Outbox->Empty();

	if (!bIsShutdown && OnMessage)
	{
		for (const TSharedPtr<FJavascriptWorker>& Worker : Stopping)
		{
			FJavascriptWorkerMessage Stopped;
			Stopped.Kind = TEXT("stopped");
			Stopped.WorkerId = Worker->GetId();
			OnMessage(Stopped);
		}
	}

	ReapOrphans();
	// Stopping goes out of scope: exited workers are destroyed (joined) here
}

void FJavascriptWorkerHost::Shutdown(uint32 TimeoutMs)
{
	// No more dispatch into the owning context from here on
	bIsShutdown = true;

	if (IsInGameThread())
	{
		StopAll(TimeoutMs);
	}
	else
	{
		// Not expected: hosts are owned by game-thread objects. Never block another thread on joins.
		for (TPair<int32, TSharedPtr<FJavascriptWorker>>& Pair : Workers)
		{
			Pair.Value->RequestStop();
			AdoptOrphan(Pair.Value);
		}
		Workers.Empty();
	}

	Outbox->Close();
	Outbox->Empty();

	if (TickHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

void FJavascriptWorkerHost::ProcessMessages(int32 MaxMessages)
{
	check(IsInGameThread());

	// OnMessage runs script, which may release the last owner of this host
	TSharedRef<FJavascriptWorkerHost> KeepAlive = AsShared();

	FJavascriptWorkerMessage Message;
	for (int32 Count = 0; Count < MaxMessages && !bIsShutdown && Outbox->Dequeue(Message); ++Count)
	{
		if (Message.Kind == TEXT("stopped"))
		{
			// The thread has exited (or is returning from Run); dropping the worker joins it
			Workers.Remove(Message.WorkerId);
		}

		if (OnMessage)
		{
			OnMessage(Message);
		}
	}
}

bool FJavascriptWorkerHost::ShutdownAllWorkers(uint32 TimeoutMs)
{
	check(IsInGameThread());

	TArray<TSharedPtr<FJavascriptWorker>> Running;
	{
		FWorkerRegistry& Registry = GetRegistry();
		FScopeLock Lock(&Registry.Lock);
		for (const TWeakPtr<FJavascriptWorker>& Entry : Registry.Live)
		{
			if (TSharedPtr<FJavascriptWorker> Worker = Entry.Pin())
			{
				Running.Add(Worker);
			}
		}
		Running.Append(Registry.Orphans);
		Registry.Live.Empty();
		Registry.Orphans.Empty();
	}

	for (const TSharedPtr<FJavascriptWorker>& Worker : Running)
	{
		Worker->RequestStop();
	}

	bool bAllExited = true;
	const double Deadline = FPlatformTime::Seconds() + FMath::Max(TimeoutMs, ShutdownJoinTimeoutMs) / 1000.0;
	for (const TSharedPtr<FJavascriptWorker>& Worker : Running)
	{
		const double RemainingMs = FMath::Max(0.0, (Deadline - FPlatformTime::Seconds()) * 1000.0);
		if (!Worker->WaitForExit(static_cast<uint32>(RemainingMs)))
		{
			bAllExited = false;
			UE_LOG(Javascript, Error, TEXT("Async: worker %d is still running at shutdown; leaking it"), Worker->GetId());

			// Destroying it would block on its thread; keep it alive for the rest of the process
			new TSharedPtr<FJavascriptWorker>(Worker);
		}
	}
	return bAllExited;
}

int32 FJavascriptWorkerHost::NumLiveWorkerThreads()
{
	return GLiveWorkerThreads.load();
}
