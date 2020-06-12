#pragma once

#include "Async/Async.h"

/** NB: ordering is different from async, default is game thread */
UENUM(BlueprintType)
enum class EJavascriptAsyncOption : uint8
{
	TaskGraphMainThread = 0,//main thread, short task (~<2sec)
	TaskGraph,				//background on taskgraph, short task (~<2sec)
	Thread,					//background thread
	ThreadPool				//background in threadpool
};

struct FJavascriptAsyncUtil
{
	static bool IsBgThread(EJavascriptAsyncOption Option);

	static EAsyncExecution ToAsyncExecution(EJavascriptAsyncOption Option);
};