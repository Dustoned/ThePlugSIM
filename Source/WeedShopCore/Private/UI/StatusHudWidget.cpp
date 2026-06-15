#include "UI/StatusHudWidget.h"

#include "WeedShopCore.h"
#include "GameFramework/Pawn.h"
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

UHorizontalBox* UStatusHudWidget::MakeRow(UVerticalBox* Parent, int32 IconType, const FLinearColor& IconCol, const FString& IconKey)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

	USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>();
	IcoSz->SetWidthOverride(24.f); IcoSz->SetHeightOverride(24.f);
	IcoSz->SetContent(IconKey.IsEmpty()
		? WeedUI::Icon(WidgetTree, (WeedUI::EIcon)IconType, 24.f, IconCol)
		: WeedUI::UiGlyph(WidgetTree, IconKey, 24.f, IconCol, (WeedUI::EIcon)IconType));
	LastRowIcon = IcoSz;
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
	// Onzichtbare achtergrond (zoals gevraagd): geen paneel, geen kader. Leesbaar via een sterke tekst-schaduw.
	Card->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.f), 14.f));
	Card->SetPadding(FMargin(2.f, 2.f, 2.f, 2.f));
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

	// 1) Cash (money-stack icoon).
	{
		UHorizontalBox* Row = MakeRow(VB, (int32)WeedUI::EIcon::Coin, FLinearColor(1.f, 0.85f, 0.35f));
		CashText = WeedUI::Text(WidgetTree, TEXT("Cash EUR 0"), 15, FLinearColor(0.8f, 1.f, 0.8f), false, true);
		RightText(Row, CashText);
	}
	// 1b) Bank (eigen bank-icoon).
	{
		UHorizontalBox* Row = MakeRow(VB, (int32)WeedUI::EIcon::Coin, FLinearColor(0.45f, 0.75f, 1.f), TEXT("bank"));
		BankText = WeedUI::Text(WidgetTree, TEXT("Bank EUR 0"), 15, FLinearColor(0.7f, 0.88f, 1.f), false, true);
		RightText(Row, BankText);
	}
	// 2) Tijd.
	{
		UHorizontalBox* Row = MakeRow(VB, (int32)WeedUI::EIcon::Clock, FLinearColor(0.45f, 0.7f, 1.f));
		TimeIcon = LastRowIcon; // dag/nacht-swap (zon/maan) gebeurt in de tick
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

	// Schaduw op alle HUD-tekst -> leesbaar zonder paneel-achtergrond.
	auto Shade = [](UTextBlock* T)
	{
		if (T) { T->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 1.f)); T->SetShadowOffset(FVector2D(1.6f, 2.f)); }
	};
	Shade(CashText); Shade(BankText); Shade(TimeText);
	Shade(HeatText); Shade(LevelText); Shade(StonedText);
}

void UStatusHudWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (!GS) { return; }

	if (CashText && GS->GetEconomy())
	{
		// Compact (1.0M / 12k / 523) zodat de kaart smal blijft en niets overlapt.
		auto Compact = [](double E) { const double A = FMath::Abs(E); if (A >= 1000000.0) { return FString::Printf(TEXT("%.1fM"), E / 1000000.0); } if (A >= 1000.0) { return FString::Printf(TEXT("%.1fk"), E / 1000.0); } return FString::Printf(TEXT("%.0f"), E); };
		CashText->SetText(FText::FromString(FString::Printf(TEXT("Cash  EUR %s"), *Compact(GS->GetEconomy()->GetBalanceEuros()))));
		if (BankText) { BankText->SetText(FText::FromString(FString::Printf(TEXT("Bank  EUR %s"), *Compact(GS->GetEconomy()->GetBankEuros())))); }
	}
	if (TimeText && GS->GetDayCycle())
	{
		const float Hour = GS->GetDayCycle()->GetClockHour();
		const int32 H = FMath::Clamp((int32)Hour, 0, 23);
		const int32 M = FMath::Clamp((int32)((Hour - H) * 60.f), 0, 59);
		const bool bNight = GS->GetDayCycle()->IsNight();
		TimeText->SetText(FText::FromString(FString::Printf(TEXT("Day %d   %02d:%02d %s"),
			GS->GetDayCycle()->GetDayNumber(), H, M, bNight ? TEXT("(night)") : TEXT(""))));

		// Icoon volgt dag/nacht: zon overdag, maan 's nachts (alleen wisselen bij een overgang).
		if (TimeIcon && bTimeNightShown != (int32)bNight)
		{
			bTimeNightShown = (int32)bNight;
			const FLinearColor Col = bNight ? FLinearColor(0.7f, 0.78f, 1.f) : FLinearColor(1.f, 0.82f, 0.3f);
			TimeIcon->SetContent(WeedUI::UiGlyph(WidgetTree, bNight ? TEXT("ui_moon") : TEXT("ui_sun"), 24.f, Col, WeedUI::EIcon::Clock));
		}
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
