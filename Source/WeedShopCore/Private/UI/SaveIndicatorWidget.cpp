#include "UI/SaveIndicatorWidget.h"

#include "UI/WeedUiStyle.h"
#include "Game/WeedShopGameState.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"

TSharedRef<SWidget> USaveIndicatorWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void USaveIndicatorWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Pil rechtsboven.
	UBorder* Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SavePill"));
	Pill->SetBrush(WeedUI::Rounded(FLinearColor(0.04f, 0.05f, 0.07f, 0.82f), 10.f));
	Pill->SetPadding(FMargin(12.f, 7.f, 14.f, 7.f));
	Box = Pill;
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Pill);
	CS->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f)); // rechtsboven
	CS->SetAlignment(FVector2D(1.f, 0.f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(-18.f, 18.f));

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Pill->SetContent(Row);

	// Draaiend "laad"-icoon (gear).
	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(18.f); Sz->SetHeightOverride(18.f);
	UWidget* Icon = WeedUI::Icon(WidgetTree, WeedUI::EIcon::Gear, 18.f, FLinearColor(0.7f, 0.85f, 1.f));
	Sz->SetContent(Icon);
	Spinner = Sz;
	UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(Sz);
	IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

	Label = WeedUI::Text(WidgetTree, TEXT("Saving..."), 12, FLinearColor(0.9f, 0.92f, 1.f), false, true);
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(Label);
	LS->SetVerticalAlignment(VAlign_Center);

	Box->SetVisibility(ESlateVisibility::Collapsed); // start verborgen
}

void USaveIndicatorWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (!GS) { return; }

	const int32 C = GS->GetSaveCounter();
	if (!bInit) { bInit = true; LastCounter = C; } // bij start niet meteen flashen
	if (C != LastCounter)
	{
		LastCounter = C;
		Timer = 0.f; // nieuwe save -> melding starten
	}

	if (Timer < 0.f)
	{
		if (Box && Box->GetVisibility() != ESlateVisibility::Collapsed) { Box->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}

	Timer += DeltaTime;
	if (Box) { Box->SetVisibility(ESlateVisibility::HitTestInvisible); }

	const bool bSaving = (Timer < 0.6f);
	if (Label)
	{
		Label->SetText(FText::FromString(bSaving ? TEXT("Saving...") : TEXT("Saved")));
		Label->SetColorAndOpacity(FSlateColor(bSaving ? FLinearColor(0.9f, 0.92f, 1.f) : FLinearColor(0.5f, 1.f, 0.6f)));
	}
	// Spinner draait tijdens "Saving...", staat stil bij "Saved".
	if (Spinner)
	{
		Spinner->SetRenderTransformAngle(bSaving ? (Timer * 540.f) : 0.f);
	}

	// Na ~2.4s weer verbergen (Saving 0.6s + Saved ~1.8s).
	if (Timer > 2.4f)
	{
		Timer = -1.f;
		if (Box) { Box->SetVisibility(ESlateVisibility::Collapsed); }
	}
}
