#include "UI/PlantInfoWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Interaction/Interactable.h"
#include "Cultivation/GrowPlant.h"
#include "Cultivation/SoilTypes.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Pawn.h"

TSharedRef<SWidget> UPlantInfoWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UPlantInfoWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PlantCard"));
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.95f), 18.f);
		Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f));
		CardB->SetBrush(Br);
	}
	CardB->SetPadding(FMargin(30.f, 22.f, 30.f, 24.f));
	CardB->SetVisibility(ESlateVisibility::HitTestInvisible);
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	// Stats-kaart ONDERAAN (boven de hotbar): zo blijft de plant zelf in beeld vrij. De losse interactie-prompt
	// ("Water the plant") blijft wel bij het crosshair/object (zit in HotkeyHintWidget).
	CS->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
	CS->SetAlignment(FVector2D(0.5f, 1.f)); // onderrand = ankerpunt
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, -118.f)); // ~118px boven de schermrand, net boven de hotbar

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(VB);

	TitleText = WeedUI::Text(WidgetTree, TEXT("Pot"), 18, WeedUI::ColAccent(), false, true);
	VB->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));

	// Kleine kit-icoon (vast 18px) voor de rijen.
	auto Ico = [this](const FString& Name, const FLinearColor& Tint) -> UWidget*
	{
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(30.f); Sz->SetHeightOverride(30.f);
		Sz->SetContent(WeedUI::KitIcon(WidgetTree, Name, 30.f, Tint));
		return Sz;
	};

	// Radiaal-materiaal voor de ring-gauges (Percent + Color params).
	RadialMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/UI/M_RadialProgress.M_RadialProgress"));

	// Ring-gauge: radiale ring + kit-icoon in 't midden + waarde-tekst eronder.
	auto MakeGauge = [this](const FString& IcoName, const FLinearColor& IcoTint, UImage*& OutRing, UTextBlock*& OutVal) -> UWidget*
	{
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(96.f); Sz->SetHeightOverride(96.f);
		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>(); Sz->SetContent(Ov);
		OutRing = WidgetTree->ConstructWidget<UImage>();
		if (RadialMat) { OutRing->SetBrushFromMaterial(RadialMat); }
		OutRing->SetBrushSize(FVector2D(96.f, 96.f));
		UOverlaySlot* ROS = Ov->AddChildToOverlay(OutRing); ROS->SetHorizontalAlignment(HAlign_Fill); ROS->SetVerticalAlignment(VAlign_Fill);
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(44.f); IcoSz->SetHeightOverride(44.f);
		IcoSz->SetContent((IcoName.StartsWith(TEXT("t_")) || IcoName.StartsWith(TEXT("/")))
			? WeedUI::KitIcon(WidgetTree, IcoName, 44.f, IcoTint)
			: WeedUI::UiGlyph(WidgetTree, IcoName, 44.f, IcoTint, WeedUI::EIcon::Leaf));
		UOverlaySlot* IS = Ov->AddChildToOverlay(IcoSz); IS->SetHorizontalAlignment(HAlign_Center); IS->SetVerticalAlignment(VAlign_Center);
		UVerticalBoxSlot* SzS = Box->AddChildToVerticalBox(Sz); SzS->SetHorizontalAlignment(HAlign_Center);
		OutVal = WeedUI::Text(WidgetTree, TEXT(""), 17, WeedUI::ColText(), true, true);
		UVerticalBoxSlot* VS = Box->AddChildToVerticalBox(OutVal); VS->SetHorizontalAlignment(HAlign_Center); VS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		return Box;
	};

	// Rij van 3 ring-gauges: water / health / groei.
	UHorizontalBox* Gauges = WidgetTree->ConstructWidget<UHorizontalBox>();
	UImage* GrW = nullptr; UTextBlock* GrWt = nullptr;
	Gauges->AddChildToHorizontalBox(MakeGauge(TEXT("t_drop_blue_128"), FLinearColor::White, GrW, GrWt))->SetPadding(FMargin(0.f, 0.f, 30.f, 0.f));
	WaterRing = GrW; WaterText = GrWt;
	UImage* GrH = nullptr; UTextBlock* GrHt = nullptr;
	Gauges->AddChildToHorizontalBox(MakeGauge(TEXT("t_heart_red_128"), FLinearColor::White, GrH, GrHt))->SetPadding(FMargin(0.f, 0.f, 30.f, 0.f));
	HealthRing = GrH; HealthText = GrHt;
	UImage* GrG = nullptr; UTextBlock* GrGt = nullptr;
	Gauges->AddChildToHorizontalBox(MakeGauge(TEXT("weedleaf"), FLinearColor::White, GrG, GrGt)); // transparante gekleurde cannabis-leaf (runtime-PNG) i.p.v. T_WeedLeaf-met-bg
	GrowthRing = GrG; GrowthTimeText = GrGt;
	UVerticalBoxSlot* GRS = VB->AddChildToVerticalBox(Gauges); GRS->SetHorizontalAlignment(HAlign_Center); GRS->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
	RingRow = Gauges;

	// Oogst: [weegschaal] gram   [THC-molecuul] thc%.
	UHorizontalBox* HRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	HRow->AddChildToHorizontalBox(Ico(TEXT("t_weight_128"), WeedUI::ColText()))->SetVerticalAlignment(VAlign_Center);
	YieldText = WeedUI::Text(WidgetTree, TEXT(""), 16, WeedUI::ColText(), false, true);
	UHorizontalBoxSlot* YS = HRow->AddChildToHorizontalBox(YieldText);
	YS->SetVerticalAlignment(VAlign_Center); YS->SetPadding(FMargin(8.f, 0.f, 18.f, 0.f));
	USizeBox* TIco = WidgetTree->ConstructWidget<USizeBox>(); TIco->SetWidthOverride(44.f); TIco->SetHeightOverride(44.f);
	TIco->SetContent(WeedUI::UiGlyph(WidgetTree, TEXT("thc"), 44.f, FLinearColor::White, WeedUI::EIcon::Leaf));
	HRow->AddChildToHorizontalBox(TIco)->SetVerticalAlignment(VAlign_Center);
	ThcText = WeedUI::Text(WidgetTree, TEXT(""), 16, WeedUI::ColGood(), false, true);
	UHorizontalBoxSlot* TS = HRow->AddChildToHorizontalBox(ThcText);
	TS->SetVerticalAlignment(VAlign_Center); TS->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	YieldRow = HRow;
	{ UVerticalBoxSlot* HRS = VB->AddChildToVerticalBox(HRow); HRS->SetHorizontalAlignment(HAlign_Center); HRS->SetPadding(FMargin(0.f, 7.f, 0.f, 2.f)); } // yield+thc-rij gecentreerd

	// --- Conditie-badges (mold / pest): vast gebouwd, alleen zichtbaar zodra een plek besmet is. ---
	// Icoon (drop-in PNG uit Icons/<key>.png) + korte label. Pest gebruikt de bestaande spray-icon;
	// mold valt terug op een glyph tot er een 'mold.png' in Icons staat.
	ConditionRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto MakeBadge = [this](TObjectPtr<UHorizontalBox>& Out, const FString& IcoKey, const FString& Label, const FLinearColor& Tint)
	{
		Out = WidgetTree->ConstructWidget<UHorizontalBox>();
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(24.f); Sz->SetHeightOverride(24.f);
		Sz->SetContent(WeedUI::UiGlyph(WidgetTree, IcoKey, 24.f, Tint, WeedUI::EIcon::Leaf));
		Out->AddChildToHorizontalBox(Sz)->SetVerticalAlignment(VAlign_Center);
		UTextBlock* T = WeedUI::Text(WidgetTree, Label, 13, Tint, false, true);
		UHorizontalBoxSlot* BS = Out->AddChildToHorizontalBox(T);
		BS->SetVerticalAlignment(VAlign_Center); BS->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
		Out->SetVisibility(ESlateVisibility::Collapsed);
	};
	MakeBadge(MoldBadge, TEXT("spray"), TEXT("Mold"), FLinearColor(0.78f, 0.86f, 0.55f)); // schimmel = spray-icon (zelfde als shop), groengrijze tint om van pest te scheiden
	MakeBadge(PestBadge, TEXT("spray"), TEXT("Pest"), FLinearColor(1.f, 0.55f, 0.4f));     // ongedierte = oranjerood
	ConditionRow->AddChildToHorizontalBox(MoldBadge)->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));
	ConditionRow->AddChildToHorizontalBox(PestBadge);
	{ UVerticalBoxSlot* CRS = VB->AddChildToVerticalBox(ConditionRow); CRS->SetHorizontalAlignment(HAlign_Center); CRS->SetPadding(FMargin(0.f, 3.f, 0.f, 1.f)); }
	ConditionRow->SetVisibility(ESlateVisibility::Collapsed);

	// Aarde + upgrades (klein, compact).
	// Upgrades/hint staan NIET meer op de kaart (info komt bij de pot-interact); members blijven voor NativeTick.
	// SoilText hangt WEL in de kaart, maar alleen zichtbaar bij een LEGE pot: dan wil je zien hoeveel
	// harvests de soil nog kan ("Soil: 3 harvests left" / "No soil") voordat je plant.
	SoilText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColTextDim(), false, true);
	{
		UVerticalBoxSlot* SS = VB->AddChildToVerticalBox(SoilText);
		SS->SetHorizontalAlignment(HAlign_Center); SS->SetPadding(FMargin(0.f, 4.f, 0.f, 2.f));
	}
	SoilText->SetVisibility(ESlateVisibility::Collapsed);
	UpgradesText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim());
	HintText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColTextDim());
}

void UPlantInfoWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	APawn* P = GetOwningPlayerPawn();
	if (!P)
	{
		if (Card && LastCardVis != 0) { LastCardVis = 0; Card->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}

	// Component-cache (weak + pawn-check): niet 2x FindComponentByClass per tick.
	if (CachedCompPawn.Get() != P || !CachedPhone.IsValid() || !CachedInteract.IsValid())
	{
		CachedCompPawn = P;
		CachedPhone = P->FindComponentByClass<UPhoneClientComponent>();
		CachedInteract = P->FindComponentByClass<UInteractionComponent>();
	}

	// Geen kaart als er een UI open is.
	bool bUiOpen = false;
	if (const UPhoneClientComponent* Ph = CachedPhone.Get())
	{
		bUiOpen = Ph->IsOpen() || Ph->IsDealOpen() || Ph->IsInventoryOpen() || Ph->IsRollOpen() || Ph->IsPotUpgradeOpen();
	}

	AGrowPlant* Plant = nullptr;
	if (const UInteractionComponent* IC = CachedInteract.Get())
	{
		Plant = Cast<AGrowPlant>(IC->GetFocusedActor());
	}

	const bool bShow = Plant && !bUiOpen;
	if (Card)
	{
		const int32 VisNow = bShow ? 1 : 0;
		if (VisNow != LastCardVis) { LastCardVis = VisNow; Card->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	}
	if (!bShow) { LastKey.Reset(); return; }

	const bool bPlanted = Plant->IsPlanted();
	const int32 NumSlots = Plant->GetNumSlots();

	// Zet een ring (Percent + Color) via z'n dynamische materiaal — alleen bij delta > 0.001 of kleur-wissel.
	auto SetRing = [](UImage* Ring, float Frac, const FLinearColor& Col, float& LastFrac, FLinearColor& LastCol)
	{
		if (!Ring) { return; }
		const float F = FMath::Clamp(Frac, 0.f, 1.f);
		if (FMath::Abs(F - LastFrac) <= 0.001f && Col == LastCol) { return; }
		LastFrac = F; LastCol = Col;
		if (UMaterialInstanceDynamic* MID = Ring->GetDynamicMaterial())
		{
			MID->SetScalarParameterValue(TEXT("Percent"), F);
			MID->SetVectorParameterValue(TEXT("Color"), Col);
		}
	};

	// Alle getoonde (afgeronde) waarden verzamelen -> change-key; tekst/vis-secties alleen bij wijziging.
	// De ringen worden hierbuiten gehouden: die updaten elke tick (met de delta-gate hierboven).
	int32 Rem = 0, SickN = 0;
	float Wtr = 0.f, Care = 0.f;
	bool bMold = false, bPest = false;
	FString Key;
	if (bPlanted)
	{
		// Groei-ring = gemiddelde van de geplante plekken; besmettings-vlaggen in dezelfde loop.
		float GrowSum = 0.f; int32 GrowN = 0;
		for (int32 i = 0; i < NumSlots; ++i)
		{
			if (Plant->IsSlotPlanted(i)) { GrowSum += Plant->GetSlotFraction(i); ++GrowN; }
			const uint8 A = Plant->GetSlotAfflict(i);
			if (A == 1) { bMold = true; } else if (A == 2) { bPest = true; }
		}
		Rem = FMath::CeilToInt(Plant->GetSecondsRemaining());
		Wtr = Plant->GetWaterLevel();
		Care = Plant->GetCareAvg();
		SickN = Plant->GetAfflictedCount();
		const FLinearColor HCol = (SickN > 0 || Care < 0.5f) ? FLinearColor(1.f, 0.4f, 0.4f) : (Care >= 0.8f ? FLinearColor(0.4f, 0.9f, 0.4f) : FLinearColor(1.f, 0.7f, 0.2f));

		SetRing(GrowthRing, (GrowN > 0) ? (GrowSum / GrowN) : 0.f, FLinearColor(0.45f, 0.9f, 0.4f), LastGrowFrac, LastGrowCol);
		SetRing(WaterRing, Wtr, FLinearColor(0.3f, 0.7f, 1.f), LastWaterFrac, LastWaterCol);
		SetRing(HealthRing, Care, HCol, LastHealthFrac, LastHealthCol);

		Key = FString::Printf(TEXT("P|%s|%s|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
			*Plant->GetPrimaryStrainName().ToString(), *Plant->GetPrimaryStrainId().ToString(),
			Plant->GetPlantedCount(), NumSlots, Rem,
			(int32)FMath::RoundHalfToEven(Wtr * 100.f), (int32)FMath::RoundHalfToEven(Care * 100.f),
			SickN > 0 ? 1 : 0, bMold ? 1 : 0, bPest ? 1 : 0,
			(int32)FMath::RoundHalfToEven(Plant->GetEstimatedTotalYield()),
			(int32)FMath::RoundHalfToEven(Plant->GetEstimatedThcPercent()));
	}
	else
	{
		Key = FString(TEXT("E|")) + WeedUI::PrettyItemName(Plant->GetPotTier());
	}

	// Soil + upgrades tellen ook mee in de key (worden in beide takken getoond/gezet).
	const bool bHasSoil = Plant->HasSoil();
	FString SoilName; int32 SoilUses = 0;
	if (bHasSoil)
	{
		FSoilDef Sd; SoilName = GetSoilDef(Plant->GetSoilId(), Sd) ? Sd.DisplayName : Plant->GetSoilId().ToString();
		SoilUses = Plant->GetSoilUsesLeft();
	}
	const FString Ups = Plant->GetActiveUpgradesLabel();
	Key += FString::Printf(TEXT("|S%d|%s|%d|U|%s"), bHasSoil ? 1 : 0, *SoilName, SoilUses, *Ups);

	if (Key == LastKey) { return; } // niets zichtbaars gewijzigd -> teksten/visibility overslaan
	LastKey = Key;

	if (bPlanted)
	{
		// Titel = naam van de plant + basis-THC%, met aantal plekken als die >1 is.
		const FString StrainName = Plant->GetPrimaryStrainName().ToString();
		FString Title = StrainName; // THC staat duidelijk bij de opbrengst (geen dubbele weergave)
		if (NumSlots > 1) { Title += FString::Printf(TEXT("   (%d/%d)"), Plant->GetPlantedCount(), NumSlots); }
		TitleText->SetText(FText::FromString(Title));
		if (SoilText) { SoilText->SetVisibility(ESlateVisibility::Collapsed); } // geplant: soil-info hoort bij de pot-interact, kaart blijft compact
		// Naam in de per-strain tag-kleur (zelfde hue als de strain-tags in de inventory, iets helderder voor leesbaarheid).
		{
			const FName StrainId = Plant->GetPrimaryStrainId();
			if (!StrainId.IsNone())
			{
				const FString Code = WeedUI::ItemTagShort(FName(*(FString(TEXT("Bud_")) + StrainId.ToString())));
				TitleText->SetColorAndOpacity(FSlateColor(WeedUI::TagColor(Code, 0.9f, 0.72f)));
			}
			else { TitleText->SetColorAndOpacity(FSlateColor(WeedUI::ColAccent())); }
		}
		if (RingRow) { RingRow->SetVisibility(ESlateVisibility::HitTestInvisible); }

		if (GrowthTimeText)
		{
			GrowthTimeText->SetText(FText::FromString(Rem > 0 ? FString::Printf(TEXT("%d:%02d"), Rem / 60, Rem % 60) : TEXT("READY")));
			GrowthTimeText->SetColorAndOpacity(FSlateColor(Rem > 0 ? WeedUI::ColText() : WeedUI::ColGood()));
		}
		if (WaterText) { WaterText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Wtr * 100.f))); }
		if (HealthText)
		{
			HealthText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Care * 100.f)));
			HealthText->SetColorAndOpacity(FSlateColor(SickN > 0 ? WeedUI::ColWarn() : WeedUI::ColText()));
		}

		// Conditie-badges: toon mold en/of pest zodra een plek besmet is (1 = mold, 2 = pest).
		if (MoldBadge)      { MoldBadge->SetVisibility(bMold ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
		if (PestBadge)      { PestBadge->SetVisibility(bPest ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
		if (ConditionRow)   { ConditionRow->SetVisibility((bMold || bPest) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }

		if (YieldRow) { YieldRow->SetVisibility(ESlateVisibility::HitTestInvisible); } // wiet erin: hele rij (weegschaal+THC-iconen + waarden) tonen
		YieldText->SetText(FText::FromString(FString::Printf(TEXT("~%.0f g"), Plant->GetEstimatedTotalYield())));
		if (ThcText) { ThcText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Plant->GetEstimatedThcPercent()))); }
	}
	else
	{
		// Toon WELKE pot het is (bv. "Clay pot"), niet alleen "Empty pot".
		const FString PotName = WeedUI::PrettyItemName(Plant->GetPotTier());
		TitleText->SetText(FText::FromString(PotName.IsEmpty() ? TEXT("Empty pot") : (PotName + TEXT("  (empty)"))));
		TitleText->SetColorAndOpacity(FSlateColor(WeedUI::ColTextDim())); // lege pot = geen strain-kleur
		if (RingRow) { RingRow->SetVisibility(ESlateVisibility::Collapsed); }
		if (ConditionRow) { ConditionRow->SetVisibility(ESlateVisibility::Collapsed); }
		if (YieldRow) { YieldRow->SetVisibility(ESlateVisibility::Collapsed); } // lege pot: geen weegschaal/THC-iconen tonen
		if (SoilText) { SoilText->SetVisibility(ESlateVisibility::HitTestInvisible); } // lege pot: toon hoeveel harvests de soil nog kan
	}

	if (bHasSoil)
	{
		SoilText->SetColorAndOpacity(FSlateColor(WeedUI::ColGood()));
		SoilText->SetText(FText::FromString(FString::Printf(TEXT("Soil: %s  (%d harvests left)"), *SoilName, SoilUses)));
	}
	else
	{
		SoilText->SetColorAndOpacity(FSlateColor(WeedUI::ColWarn()));
		SoilText->SetText(FText::FromString(TEXT("No soil")));
	}

	// Actieve gear-upgrades op deze pot (altijd tonen, met of zonder soil/plant).
	if (UpgradesText)
	{
		UpgradesText->SetText(FText::FromString(Ups.IsEmpty() ? TEXT("Upgrades: none") : FString::Printf(TEXT("Upgrades: %s"), *Ups)));
		UpgradesText->SetColorAndOpacity(FSlateColor(Ups.IsEmpty() ? WeedUI::ColTextDim() : WeedUI::ColAccent()));
	}

	// (De interactie-tekst staat nu rechtsonder bij Controls; geen hint-regel meer op de plantkaart.)
	if (HintText) { HintText->SetVisibility(ESlateVisibility::Collapsed); }
}
