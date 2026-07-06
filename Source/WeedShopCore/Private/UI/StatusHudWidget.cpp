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
#include "Progression/UpgradeComponent.h"

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
	const FLinearColor ColScrim = WeedUI::ColPanel(0.34f);
	const FLinearColor ColLabel = WeedUI::ColTextDim(0.55f);
	const FLinearColor ColDiv   = WeedUI::ColStroke(0.12f);
	const FLinearColor ColGold  (0.957f, 0.773f, 0.259f);   // cash/geld-cue (semantisch) -> behouden
	const FLinearColor ColBlue  = WeedUI::ColAccent();
	const FLinearColor ColOrange(1.f, 0.55f, 0.2f);         // heat-signaal (semantisch) -> behouden
	const FLinearColor ColPurple= WeedUI::ColAccent();
	const FLinearColor ColSun   (1.f, 0.82f, 0.3f);         // zon/dag-nacht-tint (semantisch) -> behouden

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
		(void)Label; (void)ColLabel; // labels weg: alleen icoon + waarde (compacter, minder tekst, meer game-feel)
		OutVal = WeedUI::Text(WidgetTree, Val, 21, WeedUI::ColText(), false, true); Shade(OutVal);
		Chip->AddChildToHorizontalBox(OutVal)->SetVerticalAlignment(VAlign_Center);
		Strip->AddChildToHorizontalBox(Chip)->SetVerticalAlignment(VAlign_Center);
		return Chip;
	};

	auto AddDivider = [this, ColDiv](UHorizontalBox* Strip) -> UWidget*
	{
		UBorder* D = WidgetTree->ConstructWidget<UBorder>();
		D->SetBrush(WeedUI::Rounded(ColDiv, 1.f));
		D->SetVisibility(ESlateVisibility::HitTestInvisible);
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(1.f); Sz->SetHeightOverride(30.f); Sz->SetContent(D);
		UHorizontalBoxSlot* DS = Strip->AddChildToHorizontalBox(Sz); DS->SetVerticalAlignment(VAlign_Center); DS->SetPadding(FMargin(13.f, 0.f, 13.f, 0.f));
		return Sz;
	};

	// === LINKS: Time-chip (zon/maan + Day / HH:MM), Cash, Bank ===
	{
		UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(26.f); IcoSz->SetHeightOverride(26.f);
		// Zon EN maan eenmalig bouwen in een Overlay; de dag/nacht-overgang toggelt straks alleen
		// visibility (geen SetContent -> geen 1-frame her-layout rond de dag-teller-wissel).
		UOverlay* IcoOv = WidgetTree->ConstructWidget<UOverlay>();
		TimeSunGlyph = WeedUI::UiGlyph(WidgetTree, TEXT("ui_sun"), 26.f, ColSun, WeedUI::EIcon::Clock);
		TimeMoonGlyph = WeedUI::UiGlyph(WidgetTree, TEXT("ui_moon"), 26.f, FLinearColor(0.7f, 0.78f, 1.f), WeedUI::EIcon::Clock);
		TimeMoonGlyph->SetVisibility(ESlateVisibility::Hidden);
		UOverlaySlot* SunS = IcoOv->AddChildToOverlay(TimeSunGlyph); SunS->SetHorizontalAlignment(HAlign_Fill); SunS->SetVerticalAlignment(VAlign_Fill);
		UOverlaySlot* MoonS = IcoOv->AddChildToOverlay(TimeMoonGlyph); MoonS->SetHorizontalAlignment(HAlign_Fill); MoonS->SetVerticalAlignment(VAlign_Fill);
		IcoSz->SetContent(IcoOv);
		UHorizontalBoxSlot* IS = Chip->AddChildToHorizontalBox(IcoSz); IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
		DayText = WeedUI::Text(WidgetTree, TEXT("Day 1"), 11, ColLabel, false, false); Shade(DayText);
		DayText->SetMinDesiredWidth(58.f);  // dekt "Day 999"
		Col->AddChildToVerticalBox(DayText);
		TimeText = WeedUI::Text(WidgetTree, TEXT("09:04"), 20, WeedUI::ColText(), false, true); Shade(TimeText);
		TimeText->SetMinDesiredWidth(58.f); // dekt "88:88" -> kolom-breedte stabiel per minuut-tick
		Col->AddChildToVerticalBox(TimeText)->SetPadding(FMargin(0.f, -1.f, 0.f, 0.f));
		Chip->AddChildToHorizontalBox(Col)->SetVerticalAlignment(VAlign_Center);
		StripL->AddChildToHorizontalBox(Chip)->SetVerticalAlignment(VAlign_Center);
		// Horloge-gate (ND7.16): de klok-chip start verborgen; NativeTick toont 'm (incl. divider)
		// zodra de Wristwatch-upgrade gekocht/gerepliceerd is. Zelfde Collapsed-patroon als HeatRow.
		TimeChip = Chip;
		TimeChip->SetVisibility(ESlateVisibility::Collapsed);
	}
	TimeDivider = AddDivider(StripL);
	if (TimeDivider) { TimeDivider->SetVisibility(ESlateVisibility::Collapsed); }
	AddChip(StripL, WeedUI::EIcon::Coin, ColGold, FString(),    TEXT("Cash"), TEXT("EUR 0"), CashText);
	AddDivider(StripL);
	AddChip(StripL, WeedUI::EIcon::Coin, ColBlue, TEXT("bank"), TEXT("Bank"), TEXT("EUR 0"), BankText);
	// De stroken zijn auto-sized: zonder minimum-breedtes verspringt de HELE strook bij elke
	// waarde-wissel (EUR 999 -> EUR 1.0k). Minima dekken de normale compact-range ("EUR 999.9k").
	if (CashText) { CashText->SetMinDesiredWidth(108.f); }
	if (BankText) { BankText->SetMinDesiredWidth(108.f); }

	// === RECHTS: Heat, Level (+MAX-badge), Stoned (verborgen tot je high bent) ===
	HeatRow = AddChip(StripR, WeedUI::EIcon::Flame, ColOrange, FString(), TEXT("Heat"), TEXT("0%"), HeatText);
	if (HeatText) { HeatText->SetMinDesiredWidth(48.f); } // dekt "100%" -> strook verspringt niet mee
	HeatDivider = AddDivider(StripR);
	if (HeatRow) { HeatRow->SetVisibility(ESlateVisibility::Collapsed); }
	if (HeatDivider) { HeatDivider->SetVisibility(ESlateVisibility::Collapsed); }
	{
		UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(26.f); IcoSz->SetHeightOverride(26.f);
		IcoSz->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Level, 26.f, ColPurple));
		UHorizontalBoxSlot* IS = Chip->AddChildToHorizontalBox(IcoSz); IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBox* ValRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		LevelText = WeedUI::Text(WidgetTree, TEXT("1"), 20, WeedUI::ColText(), false, true); Shade(LevelText);
		ValRow->AddChildToHorizontalBox(LevelText)->SetVerticalAlignment(VAlign_Center);
		UBorder* Badge = WidgetTree->ConstructWidget<UBorder>();
		Badge->SetBrush(WeedUI::Rounded(ColPurple, 6.f));
		Badge->SetPadding(FMargin(6.f, 0.f, 6.f, 1.f));
		Badge->SetContent(WeedUI::Text(WidgetTree, TEXT("MAX"), 10, WeedUI::ColAccentDim(), true, true));
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
		AddChip(StWrap, WeedUI::EIcon::Leaf, WeedUI::ColGood(), FString(), TEXT("Stoned"), TEXT("0:00"), StonedText);
		if (StonedText) { StonedText->SetMinDesiredWidth(100.f); } // dekt "9:59  +25%" -> geen jitter per seconde
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
		// Changed-check: alleen formatteren + SetText als het bedrag echt wijzigde.
		auto Compact = [](double E) { const double A = FMath::Abs(E); if (A >= 1000000.0) { return FString::Printf(TEXT("%.1fM"), E / 1000000.0); } if (A >= 1000.0) { return FString::Printf(TEXT("%.1fk"), E / 1000.0); } return FString::Printf(TEXT("%.0f"), E); };
		const double CashE = GS->GetEconomy()->GetBalanceEuros();
		if (CashE != LastCashShown)
		{
			LastCashShown = CashE;
			CashText->SetText(FText::FromString(FString::Printf(TEXT("EUR %s"), *Compact(CashE))));
		}
		if (BankText)
		{
			const double BankE = GS->GetEconomy()->GetBankEuros();
			if (BankE != LastBankShown)
			{
				LastBankShown = BankE;
				BankText->SetText(FText::FromString(FString::Printf(TEXT("EUR %s"), *Compact(BankE))));
			}
		}
	}
	// Horloge-gate (ND7.16): de klok/dag-chip linksboven alleen tonen met de gekochte Wristwatch-
	// upgrade (gedeeld op de GameState, gerepliceerd -> klopt voor host en joiner). Changed-check:
	// visibility alleen zetten bij een echte wissel; de tekst-updates eronder blijven gewoon lopen.
	{
		const int32 WatchNow = (GS->GetUpgrades() && GS->GetUpgrades()->HasWatch(GetOwningPlayerPawn())) ? 1 : 0;
		if (WatchNow != LastWatchShown)
		{
			LastWatchShown = WatchNow;
			const ESlateVisibility V = WatchNow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
			if (TimeChip) { TimeChip->SetVisibility(V); }
			if (TimeDivider) { TimeDivider->SetVisibility(V); }
		}
	}
	if (GS->GetDayCycle())
	{
		const float Hour = GS->GetDayCycle()->GetClockHour();
		const int32 H = FMath::Clamp((int32)Hour, 0, 23);
		const int32 M = FMath::Clamp((int32)((Hour - H) * 60.f), 0, 59);
		const bool bNight = GS->GetDayCycle()->IsNight();
		if (DayText)
		{
			const int32 DayN = GS->GetDayCycle()->GetDayNumber();
			if (DayN != LastDayShown) { LastDayShown = DayN; DayText->SetText(FText::FromString(FString::Printf(TEXT("Day %d"), DayN))); }
		}
		if (TimeText)
		{
			const int32 MinKey = H * 60 + M;
			if (MinKey != LastMinuteShown) { LastMinuteShown = MinKey; TimeText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), H, M))); }
		}

		// Header-icoon volgt dag/nacht: zon overdag, maan 's nachts. Beide glyphs staan al gebouwd in
		// de Overlay -> alleen visibility togglen (geen SetContent, dus geen 1-frame her-layout).
		if (TimeSunGlyph && TimeMoonGlyph && bTimeNightShown != (int32)bNight)
		{
			bTimeNightShown = (int32)bNight;
			TimeSunGlyph->SetVisibility(bNight ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
			TimeMoonGlyph->SetVisibility(bNight ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
	}
	// Co-op: heat + level/XP van de LOKALE speler (eigenaar van deze HUD), niet van de host.
	APawn* OwnerPawn = GetOwningPlayerPawn();
	if (GS->GetHeat())
	{
		const float H = GS->GetHeat()->GetHeatFor(OwnerPawn);
		HeatVisibleTimer = FMath::Max(0.f, HeatVisibleTimer - DeltaTime);
		if (HeatText)
		{
			// RoundHalfToEven = zelfde afronding als printf %.0f -> getoonde int als changed-check.
			const int32 HShown = (int32)FMath::RoundHalfToEven(H);
			if (HShown != LastHeatShown)
			{
				if (LastHeatShown >= 0) { HeatVisibleTimer = 4.f; }
				LastHeatShown = HShown;
				HeatText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), H)));
			}
			const int32 VisNow = (HShown > 0 || HeatVisibleTimer > 0.f) ? 1 : 0;
			if (VisNow != LastHeatVisible)
			{
				LastHeatVisible = VisNow;
				const ESlateVisibility V = VisNow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
				if (HeatRow) { HeatRow->SetVisibility(V); }
				if (HeatDivider) { HeatDivider->SetVisibility(V); }
			}
		}
		if (HeatBar)
		{
			// Delta-gate: SetPercent alleen bij een echte wijziging; de fill-kleur alleen bij een band-wissel
			// (identiek werk overslaan -> geen redundante Slate-invalidaties per tick).
			const float Pct = H / 100.f;
			if (FMath::Abs(Pct - LastHeatPct) > 0.004f)
			{
				LastHeatPct = Pct;
				HeatBar->SetPercent(Pct);
			}
			const int32 Band = (H >= 75.f) ? 2 : ((H >= 40.f) ? 1 : 0);
			if (Band != LastHeatBand)
			{
				LastHeatBand = Band;
				HeatBar->SetFillColorAndOpacity(Band == 2 ? FLinearColor(1.f, 0.25f, 0.2f) : (Band == 1 ? FLinearColor(1.f, 0.42f, 0.f) : FLinearColor(1.f, 0.55f, 0.2f)));
			}
		}
	}
	if (GS->GetLeveling())
	{
		const ULevelComponent* Lv = GS->GetLeveling();
		const int32 Level = Lv->GetLevelFor(OwnerPawn);
		if (Level != LastLevelShown)
		{
			LastLevelShown = Level;
			const bool bMax = Level >= ULevelComponent::MaxLevel;
			if (LevelText) { LevelText->SetText(FText::FromString(FString::Printf(TEXT("%d"), Level))); }
			if (MaxBadge) { MaxBadge->SetVisibility(bMax ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
		}
		if (LevelBar) { LevelBar->SetPercent(Lv->GetLevelFractionFor(OwnerPawn)); }
	}

	// Stoned-chip volgt de telefoon-carrier op de pawn (component gecachet: weak + pawn-check).
	APawn* P = OwnerPawn;
	if (CachedPhonePawn.Get() != P || (P && !CachedPhone.IsValid()))
	{
		CachedPhonePawn = P;
		CachedPhone = P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;
	}
	UPhoneClientComponent* Phone = CachedPhone.Get();
	const float SF = Phone ? Phone->GetStonedHudFrac() : 0.f;
	if (StonedRow)
	{
		const int32 VisNow = (SF > 0.f) ? 1 : 0;
		if (VisNow != LastStonedVisible)
		{
			LastStonedVisible = VisNow;
			StonedRow->SetVisibility(SF > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
	if (SF > 0.f && Phone)
	{
		if (StonedBar) { StonedBar->SetPercent(SF); }
		if (StonedText)
		{
			const int32 Secs = FMath::CeilToInt(Phone->GetStonedHudSecs());
			const int32 XpBoost = FMath::RoundToInt(Phone->GetStonedHudXpFrac() * 100.f);
			const int32 Key = Secs * 1000 + XpBoost;
			if (Key != LastStonedKey)
			{
				LastStonedKey = Key;
				StonedText->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d  +%d%%"), Secs / 60, Secs % 60, XpBoost)));
			}
		}
	}
}
