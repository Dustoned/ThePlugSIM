#include "UI/CrosshairWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "GameFramework/Pawn.h"

TSharedRef<SWidget> UCrosshairWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		Canvas->SetVisibility(ESlateVisibility::HitTestInvisible);

		// Klein wit rond stipje, exact in het midden.
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(6.f); Sz->SetHeightOverride(6.f);
		UBorder* DotB = WidgetTree->ConstructWidget<UBorder>();
		DotB->SetBrush(WeedUI::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.85f), 3.f));
		DotB->SetVisibility(ESlateVisibility::HitTestInvisible);
		Sz->SetContent(DotB);
		Dot = Sz;

		UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(Sz);
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
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UPhoneClientComponent* Ph = P->FindComponentByClass<UPhoneClientComponent>())
		{
			bHide = Ph->IsAnyGameUIOpen() || Ph->IsMainMenuOpen();
		}
	}
	Dot->SetVisibility(bHide ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}
