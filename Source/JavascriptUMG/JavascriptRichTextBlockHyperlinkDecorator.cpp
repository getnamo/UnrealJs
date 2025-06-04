#include "JavascriptRichTextBlockHyperlinkDecorator.h"
#include "Widgets/Text/SRichTextBlock.h"

TSharedPtr<ITextDecorator> UJavascriptRichTextBlockHyperlinkDecorator::CreateDecorator(URichTextBlock* InOwner)
{
	return SRichTextBlock::HyperlinkDecorator(HyperlinkId, FSlateHyperlinkRun::FOnClick::CreateLambda([this](const FSlateHyperlinkRun::FMetadata& Metadata) {
		Current = Metadata;
		this->OnClick.Broadcast(this);
	}));
}

FString UJavascriptRichTextBlockHyperlinkDecorator::GetMetadata(const FString& Key)
{
	auto Value = Current.Find(Key);
	if (Value)
	{
		return *Value;
	}
	
	return TEXT("");
}