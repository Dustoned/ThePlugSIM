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
#include "Components/Spacer.h"
#include "Components/BackgroundBlur.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Styling/SlateTypes.h"

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

// (Legacy-helper, niet meer gebruikt door BuildShell maar bewaard voor compat.)
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
	Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 5.f, 0.f, 5.f));
	return Row;
}

void UStatusHudWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	// --- Kleuren (uit de design-spec) ---
	const FLinearColor ColPanel (0.055f, 0.057f, 0.075f, 0.80f); // rgba(14,15,19,~0.80) - donker glas
	const FLinearColor ColBorder(0.62f, 0.66f, 0.82f, 0.18f);    // subtiele rand, lage opacity
	const FLinearColor ColLabel (1.f, 1.f, 1.f, 0.58f);          // secundaire tekst ~58%
	const FLinearColor ColTrack (0.20f, 0.20f, 0.25f, 1.00f);    // bar-achtergrond (track) - duidelijk zichtbaar
	const FLinearColor ColGold  (0.957f, 0.773f, 0.259f);        // Cash  #F4C542
	const FLinearColor ColBlue  (0.325f, 0.725f, 1.f);           // Bank  #53B9FF
	const FLinearColor ColOrange(1.f, 0.416f, 0.f);              // Heat  #FF6A00
	const FLinearColor ColPurple(0.694f, 0.424f, 1.f);           // Level #B16CFF

	// --- Frame: vaste breedte, linksboven ---
	USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>();
	Frame->SetWidthOverride(395.f);
	Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Frame);
	CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(18.f, 16.f));

	// Subtiele rand-rim: buitenste border met 1.5px padding -> dunne lichte rand rond het paneel.
	UBorder* Rim = WidgetTree->ConstructWidget<UBorder>();
	Rim->SetBrush(WeedUI::Rounded(ColBorder, 18.f));
	Rim->SetPadding(FMargin(1.5f));
	Rim->SetVisibility(ESlateVisibility::HitTestInvisible);
	Frame->SetContent(Rim);

	// Gelaagd paneel: blur (frosted) achter + donkere tint + content.
	UOverlay* Stack = WidgetTree->ConstructWidget<UOverlay>();
	Rim->SetContent(Stack);

	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(7.f);
	Blur->SetCornerRadius(FVector4(17.f, 17.f, 17.f, 17.f));
	Blur->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* BS = Stack->AddChildToOverlay(Blur)) { BS->SetHorizontalAlignment(HAlign_Fill); BS->SetVerticalAlignment(VAlign_Fill); }

	UBorder* Tint = WidgetTree->ConstructWidget<UBorder>();
	Tint->SetBrush(WeedUI::Rounded(ColPanel, 17.f));
	Tint->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* TS = Stack->AddChildToOverlay(Tint)) { TS->SetHorizontalAlignment(HAlign_Fill); TS->SetVerticalAlignment(VAlign_Fill); }

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UOverlaySlot* VS = Stack->AddChildToOverlay(VB)) { VS->SetPadding(FMargin(20.f, 18.f, 20.f, 20.f)); }

	// --- helpers ---
	auto Shade = [](UTextBlock* T) { if (T) { T->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f)); T->SetShadowOffset(FVector2D(1.f, 1.5f)); } };

	// Afgeronde track + afgeronde fill voor een nette, "glowende" bar.
	auto StyleBar = [](UProgressBar* Bar, const FLinearColor& Track)
	{
		if (!Bar) { return; }
		FProgressBarStyle St;
		St.BackgroundImage = WeedUI::Rounded(Track, 6.f);
		St.FillImage = WeedUI::Rounded(FLinearColor::White, 6.f); // wit -> wordt getint door SetFillColorAndOpacity
		Bar->WidgetStyle = St;
	};

	auto AddDivider = [this, VB]()
	{
		UBorder* D = WidgetTree->ConstructWidget<UBorder>();
		D->SetBrush(WeedUI::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.07f), 1.f));
		D->SetVisibility(ESlateVisibility::HitTestInvisible);
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetHeightOverride(1.f); Sz->SetContent(D);
		VB->AddChildToVerticalBox(Sz)->SetPadding(FMargin(0.f, 13.f, 0.f, 13.f));
	};

	// Geld-rij: icoon links, label (klein) boven, waarde (groot) onder.
	auto MoneyRow = [this, VB, ColLabel, Shade](WeedUI::EIcon Ico, const FLinearColor& IcoCol, const FString& IcoKey, const FString& Label, TObjectPtr<UTextBlock>& OutVal)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(46.f); IcoSz->SetHeightOverride(46.f);
		IcoSz->SetContent(IcoKey.IsEmpty() ? WeedUI::Icon(WidgetTree, Ico, 46.f, IcoCol) : WeedUI::UiGlyph(WidgetTree, IcoKey, 46.f, IcoCol, Ico));
		UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(IcoSz); IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));

		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
		UTextBlock* Lbl = WeedUI::Text(WidgetTree, Label, 16, ColLabel, false, false); Shade(Lbl);
		Col->AddChildToVerticalBox(Lbl);
		OutVal = WeedUI::Text(WidgetTree, TEXT("EUR 0"), 32, FLinearColor::White, false, true); Shade(OutVal);
		Col->AddChildToVerticalBox(OutVal)->SetPadding(FMargin(0.f, 1.f, 0.f, 0.f));
		Row->AddChildToHorizontalBox(Col)->SetVerticalAlignment(VAlign_Center);

		VB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));
	};

	// Bar-rij: kop (icoon + label + waarde rechts) en daaronder een brede afgeronde progress bar.
	auto BarRow = [this, VB, ColLabel, ColTrack, Shade, StyleBar](WeedUI::EIcon Ico, const FLinearColor& IcoCol, const FString& Label, const FLinearColor& Fill, float BarH, TObjectPtr<UTextBlock>& OutVal, TObjectPtr<UProgressBar>& OutBar) -> UVerticalBox*
	{
		UVerticalBox* Wrap = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBox* Hdr = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(38.f); IcoSz->SetHeightOverride(38.f);
		IcoSz->SetContent(WeedUI::Icon(WidgetTree, Ico, 38.f, IcoCol));
		UHorizontalBoxSlot* IS = Hdr->AddChildToHorizontalBox(IcoSz); IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 15.f, 0.f));
		UTextBlock* Lbl = WeedUI::Text(WidgetTree, Label, 18, ColLabel, false, true); Shade(Lbl);
		Hdr->AddChildToHorizontalBox(Lbl)->SetVerticalAlignment(VAlign_Center);
		USpacer* Sp = WidgetTree->ConstructWidget<USpacer>();
		Hdr->AddChildToHorizontalBox(Sp)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		OutVal = WeedUI::Text(WidgetTree, TEXT("0%"), 22, FLinearColor::White, false, true); Shade(OutVal);
		Hdr->AddChildToHorizontalBox(OutVal)->SetVerticalAlignment(VAlign_Center);
		Wrap->AddChildToVerticalBox(Hdr);

		USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>(); BarSz->SetHeightOverride(BarH);
		OutBar = WidgetTree->ConstructWidget<UProgressBar>();
		StyleBar(OutBar, ColTrack);
		OutBar->SetFillColorAndOpacity(Fill);
		BarSz->SetContent(OutBar);
		Wrap->AddChildToVerticalBox(BarSz)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
		return Wrap;
	};

	// === HEADER: [zon] Day X (links)  ...  [klok] HH:MM (rechts) ===
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* SunSz = WidgetTree->ConstructWidget<USizeBox>(); SunSz->SetWidthOverride(28.f); SunSz->SetHeightOverride(28.f);
		SunSz->SetContent(WeedUI::UiGlyph(WidgetTree, TEXT("ui_sun"), 28.f, FLinearColor(1.f, 0.82f, 0.3f), WeedUI::EIcon::Clock));
		TimeIcon = SunSz; // dag/nacht-swap in de tick
		UHorizontalBoxSlot* SunS = Row->AddChildToHorizontalBox(SunSz); SunS->SetVerticalAlignment(VAlign_Center); SunS->SetPadding(FMargin(0.f, 0.f, 11.f, 0.f));
		DayText = WeedUI::Text(WidgetTree, TEXT("Day 1"), 23, FLinearColor::White, false, true); Shade(DayText);
		Row->AddChildToHorizontalBox(DayText)->SetVerticalAlignment(VAlign_Center);

		// Verticale scheidingsstreep tussen Day en tijd (zoals de mockup).
		UBorder* Div = WidgetTree->ConstructWidget<UBorder>();
		Div->SetBrush(WeedUI::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.22f), 1.f));
		Div->SetVisibility(ESlateVisibility::HitTestInvisible);
		USizeBox* DivSz = WidgetTree->ConstructWidget<USizeBox>(); DivSz->SetWidthOverride(1.5f); DivSz->SetHeightOverride(24.f); DivSz->SetContent(Div);
		UHorizontalBoxSlot* DivS = Row->AddChildToHorizontalBox(DivSz); DivS->SetVerticalAlignment(VAlign_Center); DivS->SetPadding(FMargin(16.f, 0.f, 16.f, 0.f));

		USizeBox* ClkSz = WidgetTree->ConstructWidget<USizeBox>(); ClkSz->SetWidthOverride(24.f); ClkSz->SetHeightOverride(24.f);
		ClkSz->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Clock, 24.f, FLinearColor(0.7f, 0.8f, 1.f)));
		UHorizontalBoxSlot* ClkS = Row->AddChildToHorizontalBox(ClkSz); ClkS->SetVerticalAlignment(VAlign_Center); ClkS->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		TimeText = WeedUI::Text(WidgetTree, TEXT("09:23"), 23, FLinearColor::White, false, true); Shade(TimeText);
		Row->AddChildToHorizontalBox(TimeText)->SetVerticalAlignment(VAlign_Center);
		VB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	}

	AddDivider();
	MoneyRow(WeedUI::EIcon::Coin, ColGold, FString(),     TEXT("Cash"), CashText);
	MoneyRow(WeedUI::EIcon::Coin, ColBlue, TEXT("bank"),  TEXT("Bank"), BankText);

	AddDivider();
	VB->AddChildToVerticalBox(BarRow(WeedUI::EIcon::Flame, ColOrange, TEXT("Heat"), ColOrange, 18.f, HeatText, HeatBar));

	AddDivider();
	{
		// Level: icoon + "Level"-label + GROOT level-getal + MAX-badge, met de bar eronder (zoals de mockup).
		UVerticalBox* Wrap = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBox* Hdr = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(46.f); IcoSz->SetHeightOverride(46.f);
		IcoSz->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Level, 46.f, ColPurple));
		UHorizontalBoxSlot* IS = Hdr->AddChildToHorizontalBox(IcoSz); IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));

		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
		UTextBlock* Lbl = WeedUI::Text(WidgetTree, TEXT("Level"), 16, ColLabel, false, false); Shade(Lbl);
		Col->AddChildToVerticalBox(Lbl);
		UHorizontalBox* ValRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		LevelText = WeedUI::Text(WidgetTree, TEXT("1"), 34, FLinearColor::White, false, true); Shade(LevelText);
		ValRow->AddChildToHorizontalBox(LevelText)->SetVerticalAlignment(VAlign_Center);
		UBorder* Badge = WidgetTree->ConstructWidget<UBorder>();
		Badge->SetBrush(WeedUI::Rounded(ColPurple, 7.f));
		Badge->SetPadding(FMargin(9.f, 2.f, 9.f, 3.f));
		Badge->SetContent(WeedUI::Text(WidgetTree, TEXT("MAX"), 13, FLinearColor(0.10f, 0.05f, 0.16f), true, true));
		UHorizontalBoxSlot* BgS = ValRow->AddChildToHorizontalBox(Badge); BgS->SetVerticalAlignment(VAlign_Center); BgS->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		MaxBadge = Badge; MaxBadge->SetVisibility(ESlateVisibility::Collapsed);
		Col->AddChildToVerticalBox(ValRow)->SetPadding(FMargin(0.f, 1.f, 0.f, 0.f));
		Hdr->AddChildToHorizontalBox(Col)->SetVerticalAlignment(VAlign_Center);
		Wrap->AddChildToVerticalBox(Hdr);

		USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>(); BarSz->SetHeightOverride(24.f);
		LevelBar = WidgetTree->ConstructWidget<UProgressBar>();
		StyleBar(LevelBar, ColTrack);
		LevelBar->SetFillColorAndOpacity(ColPurple);
		BarSz->SetContent(LevelBar);
		Wrap->AddChildToVerticalBox(BarSz)->SetPadding(FMargin(0.f, 9.f, 0.f, 0.f));
		VB->AddChildToVerticalBox(Wrap);
	}

	// STONED (verborgen tot je high bent) - zelfde stijl, groene bar.
	{
		UVerticalBox* StWrap = BarRow(WeedUI::EIcon::Leaf, FLinearColor(0.4f, 0.9f, 0.5f), TEXT("Stoned"), FLinearColor(0.4f, 0.9f, 0.5f), 16.f, StonedText, StonedBar);
		VB->AddChildToVerticalBox(StWrap)->SetPadding(FMargin(0.f, 13.f, 0.f, 0.f));
		StonedRow = StWrap;
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
		// Compact (1.0M / 12k / 523) zodat de waarden netjes in het paneel passen.
		auto Compact = [](double E) { const double A = FMath::Abs(E); if (A >= 1000000.0) { return FString::Printf(TEXT("%.1fM"), E / 1000000.0); } if (A >= 1000.0) { return FString::Printf(TEXT("%.1fk"), E / 1000.0); } return FString::Printf(TEXT("%.0f"), E); };
		CashText->SetText(FText::FromString(FString::Printf(TEXT("EUR %s"), *Compact(GS->GetEconomy()->GetBalanceEuros()))));
		if (BankText) { BankText->SetText(FText::FromString(FString::Printf(TEXT("EUR %s"), *Compact(GS->GetEconomy()->GetBankEuros())))); }
	}
	if (TimeText && GS->GetDayCycle())
	{
		const float Hour = GS->GetDayCycle()->GetClockHour();
		const int32 H = FMath::Clamp((int32)Hour, 0, 23);
		const int32 M = FMath::Clamp((int32)((Hour - H) * 60.f), 0, 59);
		const bool bNight = GS->GetDayCycle()->IsNight();
		if (DayText) { DayText->SetText(FText::FromString(FString::Printf(TEXT("Day %d"), GS->GetDayCycle()->GetDayNumber()))); }
		TimeText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), H, M)));

		// Header-icoon volgt dag/nacht: zon overdag, maan 's nachts (alleen wisselen bij een overgang).
		if (TimeIcon && bTimeNightShown != (int32)bNight)
		{
			bTimeNightShown = (int32)bNight;
			const FLinearColor Col = bNight ? FLinearColor(0.7f, 0.78f, 1.f) : FLinearColor(1.f, 0.82f, 0.3f);
			TimeIcon->SetContent(WeedUI::UiGlyph(WidgetTree, bNight ? TEXT("ui_moon") : TEXT("ui_sun"), 26.f, Col, WeedUI::EIcon::Clock));
		}
	}
	if (HeatBar && GS->GetHeat())
	{
		const float H = GS->GetHeat()->GetHeat();
		HeatBar->SetPercent(H / 100.f);
		HeatBar->SetFillColorAndOpacity(H >= 75.f ? FLinearColor(1.f, 0.25f, 0.2f) : (H >= 40.f ? FLinearColor(1.f, 0.42f, 0.f) : FLinearColor(1.f, 0.55f, 0.2f)));
		if (HeatText) { HeatText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), H))); }
	}
	if (LevelBar && GS->GetLeveling())
	{
		const ULevelComponent* Lv = GS->GetLeveling();
		LevelBar->SetPercent(Lv->GetLevelFraction());
		const bool bMax = Lv->GetLevel() >= ULevelComponent::MaxLevel;
		if (LevelText) { LevelText->SetText(FText::FromString(FString::Printf(TEXT("%d"), Lv->GetLevel()))); }
		if (MaxBadge) { MaxBadge->SetVisibility(bMax ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
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
			const int32 XpBoost = FMath::RoundToInt(Phone->GetStonedHudXpFrac() * 100.f);
			StonedText->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d  +%d%%"), Secs / 60, Secs % 60, XpBoost)));
		}
	}
}
