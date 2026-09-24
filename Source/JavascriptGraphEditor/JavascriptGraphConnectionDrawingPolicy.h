#pragma once

#include "ConnectionDrawingPolicy.h"

class FJavascriptGraphConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
protected:
	UEdGraph* GraphObj;
	TMap<UEdGraphNode*, int32> NodeWidgetMap;
public:
	FJavascriptGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);
	~FJavascriptGraphConnectionDrawingPolicy();

	// FConnectionDrawingPolicy interface 
	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FVector2f& StartPoint, const FVector2f& EndPoint, const FConnectionParams& Params) override;
	virtual void DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2f& StartPoint, const FVector2f& EndPoint, UEdGraphPin* Pin) override;
	virtual FVector2f ComputeSplineTangent(const FVector2f& Start, const FVector2f& End) const override;
	virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& PinGeometries, FArrangedChildren& ArrangedNodes) override;
	virtual void DetermineLinkGeometry(
		FArrangedChildren& ArrangedNodes,
		TSharedRef<SWidget>& OutputPinWidget,
		UEdGraphPin* OutputPin,
		UEdGraphPin* InputPin,
		/*out*/ FArrangedWidget*& StartWidgetGeometry,
		/*out*/ FArrangedWidget*& EndWidgetGeometry
		) override;
	// End of FConnectionDrawingPolicy interface
public:
	void MakeRotatedBox(FVector2D ArrowDrawPos, float AngleInRadians, FLinearColor WireColor);
public:
	const TSet< FEdGraphPinReference > GetHoveredPins() { return HoveredPins; };
	const int32 GetWireLayerID() { return WireLayerID; };
};
