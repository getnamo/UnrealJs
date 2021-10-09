#include "JavascriptInstance.h"

#include "JavascriptIsolate_Private.h"
#include "JavascriptContext_Private.h"

FJavascriptFeatures::FJavascriptFeatures()
{
	ClearFeatures();
}

void FJavascriptFeatures::AddDefaultIsolateFeatures()
{
	FeatureMap.Add(TEXT("UnrealClasses"), TEXT("default"));
	FeatureMap.Add(TEXT("UnrealMemory"), TEXT("default"));
	FeatureMap.Add(TEXT("UnrealGlobals"), TEXT("default"));
	FeatureMap.Add(TEXT("UnrealMisc"), TEXT("default"));
}

void FJavascriptFeatures::AddDefaultContextFeatures()
{
	FeatureMap.Add(TEXT("Root"), TEXT("default"));
	FeatureMap.Add(TEXT("World"), TEXT("default"));
	FeatureMap.Add(TEXT("Engine"), TEXT("default"));

	FeatureMap.Add(TEXT("Context"), TEXT("default"));
}

void FJavascriptFeatures::ClearFeatures()
{
	FeatureMap.Empty();
}

bool FJavascriptFeatures::IsEmpty() const
{
	return FeatureMap.Num() == 0;
}

FJavascriptInstance::FJavascriptInstance(const FJSInstanceOptions& InOptions, const FJSInstanceContextSettings& InContextSettings)
{
	Options = InOptions;
	ContextSettings = InContextSettings;

	IsolateDomain = InOptions.IsolateDomain;
	//todo use domain to fetch same isolates from stack

	//Create Isolate unless passed in
	if (Options.bUseUniqueIsolate && ContextSettings.Isolate.IsValid())
	{
	
	}
	else
	{
		ContextSettings.Isolate = TSharedPtr<FJavascriptIsolate>(FJavascriptIsolate::Create(false));
		ContextSettings.Isolate->SetAvailableFeatures(Options.Features.FeatureMap);
	}

	//Create context on Isolate
	ContextSettings.Context = TSharedPtr<FJavascriptContext>(FJavascriptContext::Create(ContextSettings.Isolate, Paths));
	if (Options.Features.FeatureMap.Contains(TEXT("Context")))
	{
		//Can't be exposed in instance variant
		//Context->Expose("Context", this);
	}

	if (Options.Features.FeatureMap.Contains(TEXT("UnrealGlobals")))
	{
		ContextSettings.Context->ExposeGlobals();
	}
	if (Options.Features.FeatureMap.Contains(TEXT("Engine")))
	{
		ContextSettings.Context->Expose("GEngine", GEngine);
	}

	//World and Root need to be expose in a UObject
}

FJavascriptInstance::~FJavascriptInstance()
{
	ContextSettings.Context.Reset();
	if (Options.bUseUniqueIsolate)
	{
		ContextSettings.Isolate.Reset();
	}
}

TSharedPtr<FJavascriptIsolate> FJavascriptInstance::GetSharedIsolate()
{
	return ContextSettings.Isolate;
}

bool FJSInstanceOptions::UsesGameThread() const
{
	return !FJavascriptAsyncUtil::IsBgThread(ThreadOption);
}

FJSInstanceOptions::FJSInstanceOptions()
{
	bEnabled = true;
	IsolateDomain = TEXT("default");
	ThreadOption = EJavascriptAsyncOption::TaskGraphMainThread;
	Features = FJavascriptFeatures();	//no features is default
	
	//Default isolate features
	bUseUniqueIsolate = false;
	bUseUniqueContext = true;	//should be non-unique if using same
	bAttachToTick = true;
	//bAttachInspector = false;
}

FJSInstanceContextSettings::FJSInstanceContextSettings()
{
	Isolate = nullptr;
	Context = nullptr;
	ThreadId = 0;
}
