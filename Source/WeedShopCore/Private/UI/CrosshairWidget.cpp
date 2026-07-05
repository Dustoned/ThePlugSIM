#include "UI/CrosshairWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Placement/BuildComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "GameFramework/Pawn.h"

TSharedRef<SWidget> UCrosshairWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		Canvas->SetVisibility(ESlateVisibility::HitTestInvisible);

		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();

		USizeBox* RingSz = WidgetTree->ConstructWidget<USizeBox>();
		RingSz->SetWidthOverride(20.f); RingSz->SetHeightOverride(20.f);
		RingBorder = WidgetTree->ConstructWidget<UBorder>();
		FSlateBrush RingBrush = WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.f), 10.f);
		RingBrush.OutlineSettings.Width = 1.f;
		RingBrush.OutlineSettings.Color = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.55f));
		RingBorder->SetBrush(RingBrush);
		RingBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
		RingSz->SetContent(RingBorder);
		RingSz->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* RS = Ov->AddChildToOverlay(RingSz);
		RS->SetHorizontalAlignment(HAlign_Center); RS->SetVerticalAlignment(VAlign_Center);
		Ring = RingSz;

		// Klein wit rond stipje, exact in het midden.
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(6.f); Sz->SetHeightOverride(6.f);
		UBorder* DotB = WidgetTree->ConstructWidget<UBorder>();
		DotB->SetBrush(WeedUI::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.85f), 3.f));
		DotB->SetVisibility(ESlateVisibility::HitTestInvisible);
		Sz->SetContent(DotB);
		Dot = Sz;
		DotBorder = DotB;
		UOverlaySlot* DS = Ov->AddChildToOverlay(Sz);
		DS->SetHorizontalAlignment(HAlign_Center); DS->SetVerticalAlignment(VAlign_Center);

		UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(Ov);
		CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CS->SetAlignment(FVector2D(0.5f, 0.5f));
		CS->SetAutoSize(true);
		CS->SetPosition(FVector2D(0.f, 0.f));
	}
	return Super::RebuildWidget();
}

void UCrosshairWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!Dot) { return; }

	// Verberg het stipje zodra er UI/menu open is.
	bool bHide = false;
	bool bInteract = false;
	bool bPickable = false;
	bool bPlacing = false;
	bool bPlacementValid = false;
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UPhoneClientComponent* Ph = P->FindComponentByClass<UPhoneClientComponent>())
		{
			bHide = Ph->IsAnyGameUIOpen() || Ph->IsMainMenuOpen();
		}
		if (const UInteractionComponent* IC = P->FindComponentByClass<UInteractionComponent>())
		{
			AActor* Focus = IC->GetFocusedActor();
			bInteract = (Focus != nullptr);
			if (Focus)
			{
				if (const UBuildComponent* BC = P->FindComponentByClass<UBuildComponent>())
				{
					bPickable = BC->IsPickable(Focus);
				}
			}
		}
		if (const UBuildComponent* BC = P->FindComponentByClass<UBuildComponent>())
		{
			bPlacing = BC->IsPlacing();
			bPlacementValid = BC->IsPlacementValid();
		}
	}
	const int32 State = bHide ? 0 : (bPlacing ? (bPlacementValid ? 4 : 5) : (bPickable ? 3 : (bInteract ? 2 : 1)));
	if (State == LastVisualState) { return; }
	LastVisualState = State;

	Dot->SetVisibility(State == 0 ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (Ring) { Ring->SetVisibility((State >= 2) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	const FLinearColor Accent = (State == 4) ? WeedUI::ColGood(0.9f)
		: (State == 5) ? FLinearColor(1.f, 0.72f, 0.25f, 0.9f)
		: (State == 3) ? FLinearColor(0.72f, 1.f, 0.56f, 0.92f)
		: (State == 2) ? WeedUI::ColAccent(0.9f)
		: FLinearColor(1.f, 1.f, 1.f, 0.85f);
	if (DotBorder) { DotBorder->SetBrush(WeedUI::Rounded(Accent, 3.f)); }
	if (RingBorder)
	{
		FSlateBrush RB = WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.f), 10.f);
		RB.OutlineSettings.Width = 1.f;
		RB.OutlineSettings.Color = FSlateColor(Accent);
		RingBorder->SetBrush(RB);
	}
}
