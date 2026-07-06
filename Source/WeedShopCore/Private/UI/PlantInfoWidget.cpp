#include "UI/PlantInfoWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Interaction/Interactable.h"
#include "Cultivation/GrowPlant.h"
#include "Cultivation/SoilTypes.h"
#include "Save/AssetKeepAliveSubsystem.h"

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
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColPanel(0.96f), 14.f);
		Br.OutlineSettings.Width = 1.f; Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.52f));
		CardB->SetBrush(Br);
	}
	CardB->SetPadding(FMargin(18.f, 14.f, 18.f, 15.f));
	CardB->SetVisibility(ESlateVisibility::HitTestInvisible);
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	// Stats-kaart ONDERAAN (boven de hotbar): zo blijft de plant zelf in beeld vrij. De losse interactie-prompt
	// ("Water the plant") blijft wel bij het crosshair/object (zit in HotkeyHintWidget).
	CS->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
	CS->SetAlignment(FVector2D(0.5f, 1.f)); // onderrand = ankerpunt
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(0.f, -108.f)); // compact boven de hotbar, plant/interact blijft vrij

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(VB);

	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	TitleText = WeedUI::Text(WidgetTree, TEXT("Pot"), 17, WeedUI::ColAccent(), false, true);
	UHorizontalBoxSlot* NameS = Head->AddChildToHorizontalBox(TitleText);
	NameS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	NameS->SetVerticalAlignment(VAlign_Center);
	UBorder* StatusPill = WidgetTree->ConstructWidget<UBorder>();
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColSlotEmpty(0.72f), 6.f);
		Br.OutlineSettings.Width = 1.f;
		Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.36f));
		StatusPill->SetBrush(Br);
	}
	StatusPill->SetPadding(FMargin(8.f, 2.f, 8.f, 2.f));
	StatusText = WeedUI::Text(WidgetTree, TEXT("EMPTY"), 9, WeedUI::ColTextDim(), true, true);
	StatusPill->SetContent(StatusText);
	UHorizontalBoxSlot* StS = Head->AddChildToHorizontalBox(StatusPill);
	StS->SetHorizontalAlignment(HAlign_Right);
	StS->SetVerticalAlignment(VAlign_Center);
	VB->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Radiaal-materiaal voor de ring-gauges (Percent + Color params). Function-local static cache +
	// keep-alive: de LoadObject-hitch (disk + shader-keten) hoort maar 1x per proces te gebeuren, niet
	// bij elke widget-bouw (GC purget string-geladen ketens anders per LoadMap).
	{
		static TWeakObjectPtr<UMaterialInterface> CachedRadialMat;
		if (!CachedRadialMat.IsValid())
		{
			UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/UI/M_RadialProgress.M_RadialProgress"));
			if (M) { UAssetKeepAliveSubsystem::Keep(this, M); }
			CachedRadialMat = M;
		}
		RadialMat = CachedRadialMat.Get();
	}

	// Ring-gauge: radiale ring + kit-icoon in 't midden + waarde + label eronder.
	auto MakeGauge = [this](const FString& IcoName, const FLinearColor& IcoTint, const FString& Label, UImage*& OutRing, UTextBlock*& OutVal) -> UWidget*
	{
		const float RingPx = 78.f;
		const float IconPx = 34.f;
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>(); Sz->SetWidthOverride(RingPx); Sz->SetHeightOverride(RingPx);
		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>(); Sz->SetContent(Ov);
		OutRing = WidgetTree->ConstructWidget<UImage>();
		if (RadialMat) { OutRing->SetBrushFromMaterial(RadialMat); }
		OutRing->SetDesiredSizeOverride(FVector2D(RingPx, RingPx));
		UOverlaySlot* ROS = Ov->AddChildToOverlay(OutRing); ROS->SetHorizontalAlignment(HAlign_Fill); ROS->SetVerticalAlignment(VAlign_Fill);
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>(); IcoSz->SetWidthOverride(IconPx); IcoSz->SetHeightOverride(IconPx);
		IcoSz->SetContent((IcoName.StartsWith(TEXT("t_")) || IcoName.StartsWith(TEXT("/")))
			? WeedUI::KitIcon(WidgetTree, IcoName, IconPx, IcoTint)
			: WeedUI::UiGlyph(WidgetTree, IcoName, IconPx, IcoTint, WeedUI::EIcon::Leaf));
		UOverlaySlot* IS = Ov->AddChildToOverlay(IcoSz); IS->SetHorizontalAlignment(HAlign_Center); IS->SetVerticalAlignment(VAlign_Center);
		UVerticalBoxSlot* SzS = Box->AddChildToVerticalBox(Sz); SzS->SetHorizontalAlignment(HAlign_Center);
		OutVal = WeedUI::Text(WidgetTree, TEXT(""), 16, WeedUI::ColText(), true, true);
		UVerticalBoxSlot* VS = Box->AddChildToVerticalBox(OutVal); VS->SetHorizontalAlignment(HAlign_Center); VS->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		UTextBlock* Lbl = WeedUI::Text(WidgetTree, Label, 9, WeedUI::ColTextDim(0.82f), true, true);
		UVerticalBoxSlot* LS = Box->AddChildToVerticalBox(Lbl); LS->SetHorizontalAlignment(HAlign_Center);
		return Box;
	};

	// Rij van 3 ring-gauges: water / health / groei.
	UHorizontalBox* Gauges = WidgetTree->ConstructWidget<UHorizontalBox>();
	UImage* GrW = nullptr; UTextBlock* GrWt = nullptr;
	Gauges->AddChildToHorizontalBox(MakeGauge(TEXT("t_drop_blue_128"), FLinearColor::White, TEXT("Water"), GrW, GrWt))->SetPadding(FMargin(0.f, 0.f, 26.f, 0.f));
	WaterRing = GrW; WaterText = GrWt;
	UImage* GrH = nullptr; UTextBlock* GrHt = nullptr;
	Gauges->AddChildToHorizontalBox(MakeGauge(TEXT("t_heart_red_128"), FLinearColor::White, TEXT("Care"), GrH, GrHt))->SetPadding(FMargin(0.f, 0.f, 26.f, 0.f));
	HealthRing = GrH; HealthText = GrHt;
	UImage* GrG = nullptr; UTextBlock* GrGt = nullptr;
	Gauges->AddChildToHorizontalBox(MakeGauge(TEXT("weedleaf"), FLinearColor::White, TEXT("Grow"), GrG, GrGt)); // transparante gekleurde cannabis-leaf (runtime-PNG) i.p.v. T_WeedLeaf-met-bg
	GrowthRing = GrG; GrowthTimeText = GrGt;
	UVerticalBoxSlot* GRS = VB->AddChildToVerticalBox(Gauges); GRS->SetHorizontalAlignment(HAlign_Center); GRS->SetPadding(FMargin(0.f, 1.f, 0.f, 8.f));
	RingRow = Gauges;

	// Oogst-metrics als compacte pills.
	UHorizontalBox* HRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto MakeMetric = [this](const FString& Label, const FString& Icon, const FLinearColor& Tint, TObjectPtr<UTextBlock>& OutValue) -> UWidget*
	{
		UBorder* B = WidgetTree->ConstructWidget<UBorder>();
		FSlateBrush Br = WeedUI::StorageSlotBrushWithFill(WeedUI::ColSlotEmpty(0.46f), true, false, Tint.CopyWithNewOpacity(0.35f), 8.f);
		Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.34f));
		B->SetBrush(Br);
		B->SetPadding(FMargin(8.f, 5.f, 10.f, 5.f));
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		B->SetContent(Row);
		USizeBox* I = WidgetTree->ConstructWidget<USizeBox>(); I->SetWidthOverride(24.f); I->SetHeightOverride(24.f);
		I->SetContent(Icon.StartsWith(TEXT("t_"))
			? WeedUI::KitIcon(WidgetTree, Icon, 24.f, Tint)
			: WeedUI::UiGlyph(WidgetTree, Icon, 24.f, Tint, WeedUI::EIcon::Leaf));
		UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(I);
		IS->SetVerticalAlignment(VAlign_Center); IS->SetPadding(FMargin(0.f, 0.f, 7.f, 0.f));
		UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>();
		Row->AddChildToHorizontalBox(Texts)->SetVerticalAlignment(VAlign_Center);
		Texts->AddChildToVerticalBox(WeedUI::Text(WidgetTree, Label, 8, WeedUI::ColTextDim(0.78f), false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 0.f));
		OutValue = WeedUI::Text(WidgetTree, TEXT(""), 15, WeedUI::ColText(), false, true);
		Texts->AddChildToVerticalBox(OutValue);
		return B;
	};
	HRow->AddChildToHorizontalBox(MakeMetric(TEXT("YIELD"), TEXT("t_weight_128"), WeedUI::ColText(), YieldText))->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	HRow->AddChildToHorizontalBox(MakeMetric(TEXT("THC"), TEXT("thc"), WeedUI::ColGood(), ThcText));
	YieldRow = HRow;
	{ UVerticalBoxSlot* HRS = VB->AddChildToVerticalBox(HRow); HRS->SetHorizontalAlignment(HAlign_Center); HRS->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f)); }

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

	// Aarde-info voor lege potten: geen lange debug-zin, maar een compacte row met soil + harvest-count.
	SoilRow = WidgetTree->ConstructWidget<UBorder>();
	{
		FSlateBrush Br = WeedUI::Rounded(WeedUI::ColSlotEmpty(0.40f), 7.f);
		Br.OutlineSettings.Width = 0.6f;
		Br.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.22f));
		SoilRow->SetBrush(Br);
	}
	SoilRow->SetPadding(FMargin(8.f, 5.f, 8.f, 5.f));
	UHorizontalBox* SoilLine = WidgetTree->ConstructWidget<UHorizontalBox>();
	SoilRow->SetContent(SoilLine);
	UTextBlock* SoilLabel = WeedUI::Text(WidgetTree, TEXT("SOIL"), 8, WeedUI::ColTextDim(0.78f), true, true);
	SoilLine->AddChildToHorizontalBox(SoilLabel)->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	SoilText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColTextDim(), false, true);
	UHorizontalBoxSlot* SoilNameS = SoilLine->AddChildToHorizontalBox(SoilText);
	SoilNameS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SoilNameS->SetVerticalAlignment(VAlign_Center);
	SoilUsesText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColGood(), true, true);
	UHorizontalBoxSlot* SoilUsesS = SoilLine->AddChildToHorizontalBox(SoilUsesText);
	SoilUsesS->SetVerticalAlignment(VAlign_Center);
	SoilUsesS->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	{ UVerticalBoxSlot* SS = VB->AddChildToVerticalBox(SoilRow); SS->SetHorizontalAlignment(HAlign_Fill); SS->SetPadding(FMargin(0.f, 2.f, 0.f, 1.f)); }
	SoilRow->SetVisibility(ESlateVisibility::Collapsed);
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
	int32 Rem = 0, SickN = 0, ReadyN = 0;
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
		ReadyN = Plant->GetReadyCount();
		const FLinearColor HCol = (SickN > 0 || Care < 0.5f) ? FLinearColor(1.f, 0.4f, 0.4f) : (Care >= 0.8f ? FLinearColor(0.4f, 0.9f, 0.4f) : FLinearColor(1.f, 0.7f, 0.2f));
		const FLinearColor WCol = Wtr < 0.25f ? WeedUI::ColWarn() : (Wtr < 0.50f ? FLinearColor(1.f, 0.75f, 0.25f) : FLinearColor(0.3f, 0.7f, 1.f));

		SetRing(GrowthRing, (GrowN > 0) ? (GrowSum / GrowN) : 0.f, ReadyN > 0 ? WeedUI::ColGood() : FLinearColor(0.45f, 0.9f, 0.4f), LastGrowFrac, LastGrowCol);
		SetRing(WaterRing, Wtr, WCol, LastWaterFrac, LastWaterCol);
		SetRing(HealthRing, Care, HCol, LastHealthFrac, LastHealthCol);

		Key = FString::Printf(TEXT("P|%s|%s|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
			*Plant->GetPrimaryStrainName().ToString(), *Plant->GetPrimaryStrainId().ToString(),
			Plant->GetPlantedCount(), NumSlots, Rem,
			(int32)FMath::RoundHalfToEven(Wtr * 100.f), (int32)FMath::RoundHalfToEven(Care * 100.f),
			SickN > 0 ? 1 : 0, bMold ? 1 : 0, bPest ? 1 : 0, ReadyN,
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

	// Per-label changed-checks: de key wijzigt elke seconde (groei-teller), maar de meeste labels blijven
	// dan gelijk -> alleen ECHT gewijzigde tekst/kleur naar Slate sturen. Visibility idem: alleen zetten
	// bij een daadwerkelijke wissel (identiek werk overslaan, nooit een update vertragen).
	auto SetTxt = [](UTextBlock* T, FLabelCache& C, const FString& S)
	{
		if (T && S != C.Str) { C.Str = S; T->SetText(FText::FromString(S)); }
	};
	auto SetTxtCol = [](UTextBlock* T, FLabelCache& C, const FString& S, const FLinearColor& Col)
	{
		if (!T) { return; }
		if (S != C.Str) { C.Str = S; T->SetText(FText::FromString(S)); }
		if (!Col.Equals(C.Col)) { C.Col = Col; T->SetColorAndOpacity(FSlateColor(Col)); }
	};
	auto SetVis = [](UWidget* W, ESlateVisibility V)
	{
		if (W && W->GetVisibility() != V) { W->SetVisibility(V); }
	};

	if (bPlanted)
	{
		// Titel = naam van de plant + basis-THC%, met aantal plekken als die >1 is.
		const FString StrainName = Plant->GetPrimaryStrainName().ToString();
		FString Title = StrainName; // THC staat duidelijk bij de opbrengst (geen dubbele weergave)
		if (NumSlots > 1) { Title += FString::Printf(TEXT("   (%d/%d)"), Plant->GetPlantedCount(), NumSlots); }
		// Naam in de per-strain tag-kleur (zelfde hue als de strain-tags in de inventory, iets helderder voor leesbaarheid).
		FLinearColor TitleCol = WeedUI::ColAccent();
		{
			const FName StrainId = Plant->GetPrimaryStrainId();
			if (!StrainId.IsNone())
			{
				const FString Code = WeedUI::ItemTagShort(FName(*(FString(TEXT("Bud_")) + StrainId.ToString())));
				TitleCol = WeedUI::TagColor(Code, 0.9f, 0.72f);
			}
		}
		SetTxtCol(TitleText, TitleCache, Title, TitleCol);
		SetVis(SoilRow, ESlateVisibility::Collapsed); // geplant: soil-info hoort bij de pot-interact, kaart blijft compact
		{
			const bool bSick = SickN > 0 || Care < 0.50f || Wtr < 0.25f;
			SetTxtCol(StatusText, StatusCache,
				ReadyN > 0 ? TEXT("READY") : (bSick ? TEXT("ATTENTION") : TEXT("GROWING")),
				ReadyN > 0 ? WeedUI::ColGood() : (bSick ? WeedUI::ColWarn() : WeedUI::ColTextDim()));
		}
		SetVis(RingRow, ESlateVisibility::HitTestInvisible);

		SetTxtCol(GrowthTimeText, GrowTimeCache,
			ReadyN > 0 ? TEXT("READY") : (Rem > 0 ? FString::Printf(TEXT("%d:%02d"), Rem / 60, Rem % 60) : TEXT("SOON")),
			ReadyN > 0 ? WeedUI::ColGood() : WeedUI::ColText());
		SetTxtCol(WaterText, WaterTxtCache,
			FString::Printf(TEXT("%.0f%%"), Wtr * 100.f),
			Wtr < 0.25f ? WeedUI::ColWarn() : WeedUI::ColText());
		SetTxtCol(HealthText, HealthTxtCache,
			FString::Printf(TEXT("%.0f%%"), Care * 100.f),
			SickN > 0 ? WeedUI::ColWarn() : WeedUI::ColText());

		// Conditie-badges: toon mold en/of pest zodra een plek besmet is (1 = mold, 2 = pest).
		SetVis(MoldBadge, bMold ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		SetVis(PestBadge, bPest ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		SetVis(ConditionRow, (bMold || bPest) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		SetVis(YieldRow, ESlateVisibility::HitTestInvisible); // wiet erin: hele rij (weegschaal+THC-iconen + waarden) tonen
		SetTxt(YieldText, YieldCache, FString::Printf(TEXT("~%.0f g"), Plant->GetEstimatedTotalYield()));
		SetTxt(ThcText, ThcCache, FString::Printf(TEXT("%.0f%%"), Plant->GetEstimatedThcPercent()));
	}
	else
	{
		// Toon WELKE pot het is (bv. "Clay pot"), niet alleen "Empty pot".
		const FString PotName = WeedUI::PrettyItemName(Plant->GetPotTier());
		SetTxtCol(TitleText, TitleCache, PotName.IsEmpty() ? FString(TEXT("Empty pot")) : PotName, WeedUI::ColTextDim()); // lege pot = geen strain-kleur
		SetTxtCol(StatusText, StatusCache,
			bHasSoil ? TEXT("READY") : TEXT("EMPTY"),
			bHasSoil ? WeedUI::ColGood() : WeedUI::ColTextDim());
		SetVis(RingRow, ESlateVisibility::Collapsed);
		SetVis(ConditionRow, ESlateVisibility::Collapsed);
		SetVis(YieldRow, ESlateVisibility::Collapsed); // lege pot: geen weegschaal/THC-iconen tonen
		SetVis(SoilRow, ESlateVisibility::HitTestInvisible); // lege pot: toon hoeveel harvests de soil nog kan
	}

	if (bHasSoil)
	{
		SetTxtCol(SoilText, SoilTxtCache, SoilName, WeedUI::ColTextDim(0.96f));
		SetTxtCol(SoilUsesText, SoilUsesCache,
			SoilUses == 1 ? FString::Printf(TEXT("%d harvest"), SoilUses) : FString::Printf(TEXT("%d harvests"), SoilUses),
			WeedUI::ColGood());
	}
	else
	{
		SetTxtCol(SoilText, SoilTxtCache, TEXT("No soil"), WeedUI::ColTextDim(0.80f));
		SetTxtCol(SoilUsesText, SoilUsesCache, TEXT("Add soil"), WeedUI::ColWarn());
	}

	// Actieve gear-upgrades op deze pot (altijd tonen, met of zonder soil/plant).
	SetTxtCol(UpgradesText, UpgradesCache,
		Ups.IsEmpty() ? FString(TEXT("Upgrades: none")) : FString::Printf(TEXT("Upgrades: %s"), *Ups),
		Ups.IsEmpty() ? WeedUI::ColTextDim() : WeedUI::ColAccent());

	// (De interactie-tekst staat nu rechtsonder bij Controls; geen hint-regel meer op de plantkaart.)
	SetVis(HintText, ESlateVisibility::Collapsed);
}
