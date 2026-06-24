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

	// Twee compacte horizontale stroken: LINKS (Day/tijd, Cash, Bank) en RECHTS (Heat, Level, Stoned).
	// Geen zware "graybox": alleen een subtiele donkere scrim (lage opacity) + tekst-schaduw.
	const FLinearColor ColScrim (0.04f, 0.045f, 0.06f, 0.34f);
	const FLinearColor ColLabel (1.f, 1.f, 1.f, 0.55f);
	const FLinearColor ColDiv   (1.f, 1.f, 1.f, 0.12f);
	const FLinearColor ColGold  (0.957f, 0.773f, 0.259f);
	const FLinearColor ColBlue  (0.325f, 0.725f, 1.f);
	const FLinearColor ColOrange(1.f, 0.55f, 0.2f);
	const FLinearColor ColPurple(0.694f, 0.424f, 1.f);
	const FLinearColor ColSun   (1.f, 0.82f, 0.3f);

	auto Shade = [](UTextBlock* T) { if (T) { T->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.95f)); T->SetShadowOffset(FVector2D(1.f, 1.5f)); } };

	// Maak een compacte strook (scrim + horizontale box) verankerd op een boven-hoek; geeft de strook terug.
	auto MakeStrip = [this, Root, ColScrim](float AnchorX, float AlignX, float PosX) -> UHorizontalBox*
	{
		USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>();
		Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
		UCanvasPanelSlot* CS = Root->AddChildToCanvas(Frame);
		CS->SetAnchors(FAnchors(AnchorX, 0.f, AnchorX, 0.f));
		CS->SetAlignment(FVector2D(AlignX, 0.f));
		CS->SetAutoSize(true);
		CS->SetPosition(FVector2D(PosX, 14.f));
		UBorder* Scrim = WidgetTree->ConstructWidget<UBorder>();
		Scrim->SetBrush(WeedUI::Rounded(ColScrim, 11.f));
		Scrim->SetPadding(FMargin(13.f, 7.f, 14.f, 8.f));
		Scrim->SetVisibility(ESlateVisibility::HitTestInvisible);
		Frame->SetContent(Scrim);
		UHorizontalBox* Strip = WidgetTree->ConstructWidget<UHorizontalBox>();
		Scrim->SetContent(Strip);
		return Strip;
	};

	UHorizontalBox* StripL = MakeStrip(0.f, 0.f, 16.f);    // linksboven
	UHorizontalBox* StripR = MakeStrip(1.f, 1.f, -16.f);   // rechtsboven

	// Eén compacte chip in een gekozen strook (of sub-container): [icoon] + [ label / waarde ].
	auto AddChip = [this, ColLabel, Shade](UHorizontalBox* Strip, WeedUI::EIcon Ico, const FLinearColor& IcoCol, const FString& IcoKey, const FString& Label, const FString& Val, TObjectPtr<UTextBlock>& OutVal) -> UHorizontalBox*
	{
		UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(26.f); IcoSz->SetHeightOverride(26.f);
		IcoSz->SetContent(IcoKey.IsEmpty() ? WeedUI::Icon(WidgetTree, Ico, 26.f, IcoCol) : WeedUI::UiGlyph(WidgetTree, IcoKey, 26.f, IcoCol, Ico));
		UHorizontalBoxSlot* IS = Chip->AddChildToHorizontalBox(IcoSz); IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
		UTextBlock* Lbl = WeedUI::Text(WidgetTree, Label, 11, ColLabel, false, false); Shade(Lbl);
		Col->AddChildToVerticalBox(Lbl);
		OutVal = WeedUI::Text(WidgetTree, Val, 20, FLinearColor::White, false, true); Shade(OutVal);
		Col->AddChildToVerticalBox(OutVal)->SetPadding(FMargin(0.f, -1.f, 0.f, 0.f));
		Chip->AddChildToHorizontalBox(Col)->SetVerticalAlignment(VAlign_Center);
		Strip->AddChildToHorizontalBox(Chip)->SetVerticalAlignment(VAlign_Center);
		return Chip;
	};

	auto AddDivider = [this, ColDiv](UHorizontalBox* Strip)
	{
		UBorder* D = WidgetTree->ConstructWidget<UBorder>();
		D->SetBrush(WeedUI::Rounded(ColDiv, 1.f));
		D->SetVisibility(ESlateVisibility::HitTestInvisible);
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(1.f); Sz->SetHeightOverride(30.f); Sz->SetContent(D);
		UHorizontalBoxSlot* DS = Strip->AddChildToHorizontalBox(Sz); DS->SetVerticalAlignment(VAlign_Center); DS->SetPadding(FMargin(13.f, 0.f, 13.f, 0.f));
	};

	// === LINKS: Time-chip (zon/maan + Day / HH:MM; TimeIcon = dag/nacht-swap), Cash, Bank ===
	{
		UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(26.f); IcoSz->SetHeightOverride(26.f);
		IcoSz->SetContent(WeedUI::UiGlyph(WidgetTree, TEXT("ui_sun"), 26.f, ColSun, WeedUI::EIcon::Clock));
		TimeIcon = IcoSz;
		UHorizontalBoxSlot* IS = Chip->AddChildToHorizontalBox(IcoSz); IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
		DayText = WeedUI::Text(WidgetTree, TEXT("Day 1"), 11, ColLabel, false, false); Shade(DayText);
		Col->AddChildToVerticalBox(DayText);
		TimeText = WeedUI::Text(WidgetTree, TEXT("09:04"), 20, FLinearColor::White, false, true); Shade(TimeText);
		Col->AddChildToVerticalBox(TimeText)->SetPadding(FMargin(0.f, -1.f, 0.f, 0.f));
		Chip->AddChildToHorizontalBox(Col)->SetVerticalAlignment(VAlign_Center);
		StripL->AddChildToHorizontalBox(Chip)->SetVerticalAlignment(VAlign_Center);
	}
	AddDivider(StripL);
	AddChip(StripL, WeedUI::EIcon::Coin, ColGold, FString(),    TEXT("Cash"), TEXT("EUR 0"), CashText);
	AddDivider(StripL);
	AddChip(StripL, WeedUI::EIcon::Coin, ColBlue, TEXT("bank"), TEXT("Bank"), TEXT("EUR 0"), BankText);

	// === RECHTS: Heat, Level (+MAX-badge), Stoned (verborgen tot je high bent) ===
	AddChip(StripR, WeedUI::EIcon::Flame, ColOrange, FString(), TEXT("Heat"), TEXT("0%"), HeatText);
	AddDivider(StripR);
	{
		UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(26.f); IcoSz->SetHeightOverride(26.f);
		IcoSz->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Level, 26.f, ColPurple));
		UHorizontalBoxSlot* IS = Chip->AddChildToHorizontalBox(IcoSz); IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
		UTextBlock* Lbl = WeedUI::Text(WidgetTree, TEXT("Level"), 11, ColLabel, false, false); Shade(Lbl);
		Col->AddChildToVerticalBox(Lbl);
		UHorizontalBox* ValRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		LevelText = WeedUI::Text(WidgetTree, TEXT("1"), 20, FLinearColor::White, false, true); Shade(LevelText);
		ValRow->AddChildToHorizontalBox(LevelText)->SetVerticalAlignment(VAlign_Center);
		UBorder* Badge = WidgetTree->ConstructWidget<UBorder>();
		Badge->SetBrush(WeedUI::Rounded(ColPurple, 6.f));
		Badge->SetPadding(FMargin(6.f, 0.f, 6.f, 1.f));
		Badge->SetContent(WeedUI::Text(WidgetTree, TEXT("MAX"), 10, FLinearColor(0.10f, 0.05f, 0.16f), true, true));
		UHorizontalBoxSlot* BgS = ValRow->AddChildToHorizontalBox(Badge); BgS->SetVerticalAlignment(VAlign_Center); BgS->SetPadding(FMargin(7.f, 0.f, 0.f, 0.f));
		MaxBadge = Badge; MaxBadge->SetVisibility(ESlateVisibility::Collapsed);
		Col->AddChildToVerticalBox(ValRow)->SetPadding(FMargin(0.f, -1.f, 0.f, 0.f));
		Chip->AddChildToHorizontalBox(Col)->SetVerticalAlignment(VAlign_Center);
		StripR->AddChildToHorizontalBox(Chip)->SetVerticalAlignment(VAlign_Center);
	}
	{
		// Stoned: divider + chip in een wrapper die mee verbergt (zo geen los hangend streepje als 't weg is).
		UHorizontalBox* StWrap = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddDivider(StWrap);
		AddChip(StWrap, WeedUI::EIcon::Leaf, FLinearColor(0.4f, 0.9f, 0.5f), FString(), TEXT("Stoned"), TEXT("0:00"), StonedText);
		StripR->AddChildToHorizontalBox(StWrap)->SetVerticalAlignment(VAlign_Center);
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
		// Compact (1.0M / 12k / 523) zodat de waarden netjes in de strook passen.
		auto Compact = [](double E) { const double A = FMath::Abs(E); if (A >= 1000000.0) { return FString::Printf(TEXT("%.1fM"), E / 1000000.0); } if (A >= 1000.0) { return FString::Printf(TEXT("%.1fk"), E / 1000.0); } return FString::Printf(TEXT("%.0f"), E); };
		CashText->SetText(FText::FromString(FString::Printf(TEXT("EUR %s"), *Compact(GS->GetEconomy()->GetBalanceEuros()))));
		if (BankText) { BankText->SetText(FText::FromString(FString::Printf(TEXT("EUR %s"), *Compact(GS->GetEconomy()->GetBankEuros())))); }
	}
	if (GS->GetDayCycle())
	{
		const float Hour = GS->GetDayCycle()->GetClockHour();
		const int32 H = FMath::Clamp((int32)Hour, 0, 23);
		const int32 M = FMath::Clamp((int32)((Hour - H) * 60.f), 0, 59);
		const bool bNight = GS->GetDayCycle()->IsNight();
		if (DayText) { DayText->SetText(FText::FromString(FString::Printf(TEXT("Day %d"), GS->GetDayCycle()->GetDayNumber()))); }
		if (TimeText) { TimeText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), H, M))); }

		// Header-icoon volgt dag/nacht: zon overdag, maan 's nachts (alleen wisselen bij een overgang).
		if (TimeIcon && bTimeNightShown != (int32)bNight)
		{
			bTimeNightShown = (int32)bNight;
			const FLinearColor Col = bNight ? FLinearColor(0.7f, 0.78f, 1.f) : FLinearColor(1.f, 0.82f, 0.3f);
			TimeIcon->SetContent(WeedUI::UiGlyph(WidgetTree, bNight ? TEXT("ui_moon") : TEXT("ui_sun"), 26.f, Col, WeedUI::EIcon::Clock));
		}
	}
	if (GS->GetHeat())
	{
		const float H = GS->GetHeat()->GetHeat();
		if (HeatText) { HeatText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), H))); }
		if (HeatBar)
		{
			HeatBar->SetPercent(H / 100.f);
			HeatBar->SetFillColorAndOpacity(H >= 75.f ? FLinearColor(1.f, 0.25f, 0.2f) : (H >= 40.f ? FLinearColor(1.f, 0.42f, 0.f) : FLinearColor(1.f, 0.55f, 0.2f)));
		}
	}
	if (GS->GetLeveling())
	{
		const ULevelComponent* Lv = GS->GetLeveling();
		const bool bMax = Lv->GetLevel() >= ULevelComponent::MaxLevel;
		if (LevelText) { LevelText->SetText(FText::FromString(FString::Printf(TEXT("%d"), Lv->GetLevel()))); }
		if (MaxBadge) { MaxBadge->SetVisibility(bMax ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
		if (LevelBar) { LevelBar->SetPercent(Lv->GetLevelFraction()); }
	}

	// Stoned-chip volgt de telefoon-carrier op de pawn.
	UPhoneClientComponent* Phone = nullptr;
	if (APawn* P = GetOwningPlayerPawn()) { Phone = P->FindComponentByClass<UPhoneClientComponent>(); }
	const float SF = Phone ? Phone->GetStonedHudFrac() : 0.f;
	if (StonedRow)
	{
		StonedRow->SetVisibility(SF > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (SF > 0.f && Phone)
	{
		if (StonedBar) { StonedBar->SetPercent(SF); }
		if (StonedText)
		{
			const int32 Secs = FMath::CeilToInt(Phone->GetStonedHudSecs());
			const int32 XpBoost = FMath::RoundToInt(Phone->GetStonedHudXpFrac() * 100.f);
			StonedText->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d  +%d%%"), Secs / 60, Secs % 60, XpBoost)));
		}
	}
}
