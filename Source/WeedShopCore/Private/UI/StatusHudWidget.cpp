#include "UI/StatusHudWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"
#include "World/HeatComponent.h"
#include "Progression/LevelComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "GameFramework/Pawn.h"

TSharedRef<SWidget> UStatusHudWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

UHorizontalBox* UStatusHudWidget::MakeRow(UVerticalBox* Parent, int32 IconType, const FLinearColor& IconCol)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

	USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>();
	IcoSz->SetWidthOverride(24.f); IcoSz->SetHeightOverride(24.f);
	IcoSz->SetContent(WeedUI::Icon(WidgetTree, (WeedUI::EIcon)IconType, 24.f, IconCol));
	UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(IcoSz);
	IS->SetVerticalAlignment(VAlign_Center);
	IS->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));

	UVerticalBoxSlot* RS = Parent->AddChildToVerticalBox(Row);
	RS->SetPadding(FMargin(0.f, 5.f, 0.f, 5.f));
	return Row;
}

void UStatusHudWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
	Card->SetBrush(WeedUI::Rounded(FLinearColor(0.04f, 0.05f, 0.07f, 0.82f), 18.f));
	Card->SetPadding(FMargin(14.f, 12.f, 16.f, 12.f));
	Card->SetVisibility(ESlateVisibility::HitTestInvisible);

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Card);
	CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(20.f, 18.f));

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(VB);

	auto RightText = [this](UHorizontalBox* Row, UTextBlock* T)
	{
		UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(T);
		S->SetVerticalAlignment(VAlign_Center);
	};
	// Een gelabelde bar: tekst BOVEN een grote, brede progress bar (niet erop).
	auto MakeBar = [this](TObjectPtr<UProgressBar>& OutBar, TObjectPtr<UTextBlock>& OutText, const FLinearColor& Fill) -> UWidget*
	{
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();

		OutText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.9f, 0.93f, 1.f), false, true);
		Col->AddChildToVerticalBox(OutText);

		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(224.f); Sz->SetHeightOverride(18.f);
		OutBar = WidgetTree->ConstructWidget<UProgressBar>();
		OutBar->SetFillColorAndOpacity(Fill);
		Sz->SetContent(OutBar);
		UVerticalBoxSlot* BS = Col->AddChildToVerticalBox(Sz);
		BS->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		return Col;
	};

	// 1) Geld.
	{
		UHorizontalBox* Row = MakeRow(VB, (int32)WeedUI::EIcon::Coin, FLinearColor(1.f, 0.85f, 0.35f));
		CashText = WeedUI::Text(WidgetTree, TEXT("EUR 0"), 15, FLinearColor(0.75f, 1.f, 0.75f), false, true);
		RightText(Row, CashText);
	}
	// 2) Tijd.
	{
		UHorizontalBox* Row = MakeRow(VB, (int32)WeedUI::EIcon::Clock, FLinearColor(0.45f, 0.7f, 1.f));
		TimeText = WeedUI::Text(WidgetTree, TEXT("Day 00:00"), 14, FLinearColor(0.85f, 0.9f, 1.f));
		RightText(Row, TimeText);
	}
	// 3) Heat.
	{
		UHorizontalBox* Row = MakeRow(VB, (int32)WeedUI::EIcon::Flame, FLinearColor(1.f, 0.5f, 0.35f));
		UWidget* Bar = MakeBar(HeatBar, HeatText, FLinearColor(1.f, 0.45f, 0.3f));
		UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Bar); S->SetVerticalAlignment(VAlign_Center);
	}
	// 4) Level.
	{
		UHorizontalBox* Row = MakeRow(VB, (int32)WeedUI::EIcon::Level, FLinearColor(0.6f, 0.5f, 1.f));
		UWidget* Bar = MakeBar(LevelBar, LevelText, FLinearColor(0.35f, 0.7f, 1.f));
		UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Bar); S->SetVerticalAlignment(VAlign_Center);
	}
	// 5) Stoned (verborgen tot je high bent) — toont de XP-bonus.
	{
		UHorizontalBox* Row = MakeRow(VB, (int32)WeedUI::EIcon::Leaf, FLinearColor(0.4f, 0.9f, 0.5f));
		UWidget* Bar = MakeBar(StonedBar, StonedText, FLinearColor(0.4f, 0.9f, 0.5f));
		UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Bar); S->SetVerticalAlignment(VAlign_Center);
		StonedRow = Row;
		StonedRow->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UStatusHudWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (!GS) { return; }

	if (CashText && GS->GetEconomy())
	{
		const int64 Euros = (int64)GS->GetEconomy()->GetBalanceEuros();
		FString Raw = FString::Printf(TEXT("%lld"), Euros), Grp;
		const int32 Ln = Raw.Len();
		for (int32 i = 0; i < Ln; ++i) { if (i > 0 && (Ln - i) % 3 == 0) { Grp.AppendChar(TEXT('.')); } Grp.AppendChar(Raw[i]); }
		CashText->SetText(FText::FromString(FString::Printf(TEXT("EUR %s"), *Grp)));
	}
	if (TimeText && GS->GetDayCycle())
	{
		const int32 T = FMath::RoundToInt(GS->GetDayCycle()->GetCycleFraction() * 24.f * 60.f);
		TimeText->SetText(FText::FromString(FString::Printf(TEXT("%s  %02d:%02d"),
			GS->GetDayCycle()->IsNight() ? TEXT("Night") : TEXT("Day"), (T / 60) % 24, T % 60)));
	}
	if (HeatBar && GS->GetHeat())
	{
		const float H = GS->GetHeat()->GetHeat();
		HeatBar->SetPercent(H / 100.f);
		HeatBar->SetFillColorAndOpacity(H >= 75.f ? FLinearColor(1.f, 0.25f, 0.2f) : (H >= 40.f ? FLinearColor(1.f, 0.6f, 0.2f) : FLinearColor(0.5f, 0.8f, 0.4f)));
		if (HeatText) { HeatText->SetText(FText::FromString(FString::Printf(TEXT("Heat  %.0f%%"), H))); }
	}
	if (LevelBar && GS->GetLeveling())
	{
		const ULevelComponent* Lv = GS->GetLeveling();
		LevelBar->SetPercent(Lv->GetLevelFraction());
		if (LevelText)
		{
			LevelText->SetText(FText::FromString(Lv->GetLevel() >= ULevelComponent::MaxLevel
				? FString::Printf(TEXT("Lv %d  MAX"), Lv->GetLevel())
				: FString::Printf(TEXT("Lv %d   %d/%d XP"), Lv->GetLevel(), Lv->GetCurrentXP(), Lv->GetXPToNext())));
		}
	}

	// Stoned-rij volgt de telefoon-carrier op de pawn.
	UPhoneClientComponent* Phone = nullptr;
	if (APawn* P = GetOwningPlayerPawn()) { Phone = P->FindComponentByClass<UPhoneClientComponent>(); }
	const float SF = Phone ? Phone->GetStonedHudFrac() : 0.f;
	if (StonedRow)
	{
		StonedRow->SetVisibility(SF > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (SF > 0.f && StonedBar)
	{
		StonedBar->SetPercent(SF);
		if (StonedText)
		{
			const int32 Secs = FMath::CeilToInt(Phone->GetStonedHudSecs());
			// XP-bonus is gebaseerd op de THC% van de gerookte wiet (17%-joint -> +17% XP, max +50%).
			const int32 XpBoost = FMath::RoundToInt(Phone->GetStonedHudXpFrac() * 100.f);
			StonedText->SetText(FText::FromString(FString::Printf(TEXT("Stoned %d:%02d   XP +%d%%"),
				Secs / 60, Secs % 60, XpBoost)));
		}
	}
}
