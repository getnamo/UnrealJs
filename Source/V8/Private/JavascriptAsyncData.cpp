#pragma once

#include "JavascriptAsyncData.h"

bool FJavascriptAsyncUtil::IsBgThread(EJavascriptAsyncOption Option)
{
	return (Option != EJavascriptAsyncOption::TaskGraphMainThread);
}

EAsyncExecution FJavascriptAsyncUtil::ToAsyncExecution(EJavascriptAsyncOption Option)
{
	if (Option == EJavascriptAsyncOption::TaskGraphMainThread)
	{
		return EAsyncExecution::TaskGraphMainThread;
	}
	else if (Option == EJavascriptAsyncOption::TaskGraph)
	{
		return EAsyncExecution::TaskGraph;
	}
	else if (Option == EJavascriptAsyncOption::Thread)
	{
		return EAsyncExecution::Thread;
	}
	else if (Option == EJavascriptAsyncOption::ThreadPool)
	{
		return EAsyncExecution::ThreadPool;
	}
	else
	{
		//Default any remaining options to threadpool
		return EAsyncExecution::ThreadPool;
	}
}
