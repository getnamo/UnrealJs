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

FJavascriptAsyncLambdaPinData& FJavascriptAsyncLambdaMapData::DataForId(int32 LambdaId)
{
	if (!PinData.Contains(LambdaId))
	{
		FJavascriptAsyncLambdaPinData NewPinData;
		NewPinData.MessageQueue = MakeShareable(new TQueue<FJavascriptRemoteFunctionData>());
		PinData.Add(LambdaId, NewPinData);
	}
	return PinData[LambdaId];
}

TQueue<FJavascriptRemoteFunctionData>* FJavascriptAsyncLambdaMapData::MessageQueue(int32 LambdaId)
{
	return DataForId(LambdaId).MessageQueue.Get();
}

TArray<int32> FJavascriptAsyncLambdaMapData::LambdaIds()
{
	TArray<int32> Keys;
	PinData.GetKeys(Keys);
	return Keys;
}
