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

FJavascriptInstance::FJavascriptInstance(const FJSInstanceOptions& InOptions)
{
	//Todo: initialize on bg thread option
	Features = InOptions.Features;
	ThreadId = 0;

	bIsolateIsUnique = true;
	IsolateDomain = InOptions.IsolateDomain;
	//todo use domain to fetch same isolates from stack

	//Create Isolate unless passed in
	if (InOptions.Isolate.IsValid())
	{
		Isolate = InOptions.Isolate;
		bIsolateIsUnique = false;
	}
	else
	{
		Isolate = TSharedPtr<FJavascriptIsolate>(FJavascriptIsolate::Create(false));
		Isolate->SetAvailableFeatures(Features.FeatureMap);
	}

	//Create context on Isolate
	Context = TSharedPtr<FJavascriptContext>(FJavascriptContext::Create(Isolate, Paths));
	if (Features.FeatureMap.Contains(TEXT("Context")))
	{
		//Can't be exposed in instance variant
		//Context->Expose("Context", this);
	}

	if (Features.FeatureMap.Contains(TEXT("UnrealGlobals")))
	{
		Context->ExposeGlobals();
	}
	if (Features.FeatureMap.Contains(TEXT("Engine")))
	{
		Context->Expose("GEngine", GEngine);
	}

	//World and Root need to be expose in a UObject
}

FJavascriptInstance::~FJavascriptInstance()
{
	Context.Reset();
	if (bIsolateIsUnique)
	{
		Isolate.Reset();
	}
}

TSharedPtr<FJavascriptIsolate> FJavascriptInstance::GetSharedIsolate()
{
	return Isolate;
}

FJSInstanceOptions::FJSInstanceOptions()
{
	IsolateDomain = TEXT("default");
	Isolate = nullptr;
	ThreadOption = EUJSThreadOption::USE_DEFAULT;
	Features = FJavascriptFeatures();	//no features is default
}
