#include "UI/DevMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Interaction/PlayerNpcActions.h"
#include "World/DayNightController.h"
#include "World/RoomStamper.h"
#include "World/CityDoor.h"
#include "WeedShopCore.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"

namespace
{
	FSlateBrush DevBrush(const FLinearColor& Color, float Radius)
	{
		FSlateBrush B;
		B.DrawAs = ESlateBrushDrawType::RoundedBox;
		B.TintColor = FSlateColor(Color);
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		B.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		return B;
	}
	FSlateFontInfo DevFont(int32 Size) { return FCoreStyle::GetDefaultFontStyle("Regular", Size); }

	const TCHAR* GCatNames[] = { TEXT("World"), TEXT("Build"), TEXT("Home"), TEXT("NPC"), TEXT("Shops"),
		TEXT("Map fix"), TEXT("Rooms"), TEXT("Doors"), TEXT("Lighting"), TEXT("Spots"), TEXT("Keys") };
}

void UDevMenuWidget::SetPhone(UPhoneClientComponent* InPhone) { Phone = InPhone; }

TSharedRef<SWidget> UDevMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DevRootCanvas"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UDevMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bContentDirty = true;
}

void UDevMenuWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	if (!Phone.IsValid()) { SetVisibility(ESlateVisibility::Collapsed); return; }
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	const bool bOpen = Phone->IsDevMenuOpen();
	if (Frame) { Frame->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { return; }
	// Alleen de DYNAMISCHE sub-lijsten verversen (per-rij diff), en enkel die van de zichtbare categorie —
	// de statische panel-inhoud is één keer gebouwd en blijft staan (geen rebuild, geen flash/scroll-sprong).
	if (bContentDirty)
	{
		bContentDirty = false;
		if (SelectedCat == 6) { RefreshRoomTemplates(); RefreshRoomPlaced(); }
		else if (SelectedCat == 9) { RefreshMarkedSpots(); }
	}
	// Sliders alléén live toepassen als hun panel de actieve view is (World=tijd, Lighting=licht) —
	// zo schrijven we DN niet elke frame vanuit een verborgen panel (behoud van oud gedrag).
	if (SelectedCat == 0 || SelectedCat == 8) { ApplyLightSliders(); }
}

void UDevMenuWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DevFrame"));
	Frame->SetBrush(DevBrush(FLinearColor(0.055f, 0.065f, 0.085f, 0.985f), 0.f));
	Frame->SetPadding(FMargin(0.f));
	Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UCanvasPanelSlot* FS = Root->AddChildToCanvas(Frame);
	FS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 1.f));
	FS->SetAlignment(FVector2D(0.f, 0.f));
	FS->SetOffsets(FMargin(0.f, 0.f, 470.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	Frame->SetContent(Outer);

	// Header-balk.
	UBorder* Head = WidgetTree->ConstructWidget<UBorder>();
	Head->SetBrush(DevBrush(FLinearColor(0.09f, 0.11f, 0.15f, 1.f), 0.f));
	Head->SetPadding(FMargin(14.f, 11.f, 14.f, 11.f));
	{
		UHorizontalBox* HRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* TS = HRow->AddChildToHorizontalBox(MakeText(TEXT("DEV MENU"), 17, FLinearColor(0.62f, 0.82f, 1.f)));
		TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* KS = HRow->AddChildToHorizontalBox(MakeText(TEXT("F10 to close"), 10, FLinearColor(0.5f, 0.55f, 0.65f)));
		KS->SetVerticalAlignment(VAlign_Center);
		Head->SetContent(HRow);
	}
	Outer->AddChildToVerticalBox(Head);

	// Twee kolommen.
	UHorizontalBox* Cols = WidgetTree->ConstructWidget<UHorizontalBox>();
	Outer->AddChildToVerticalBox(Cols)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UBorder* NavBg = WidgetTree->ConstructWidget<UBorder>();
	NavBg->SetBrush(DevBrush(FLinearColor(0.035f, 0.045f, 0.06f, 1.f), 0.f));
	NavBg->SetPadding(FMargin(8.f, 10.f, 8.f, 10.f));
	UScrollBox* NavScroll = WidgetTree->ConstructWidget<UScrollBox>();
	CatList = WidgetTree->ConstructWidget<UVerticalBox>();
	NavScroll->AddChild(CatList);
	NavBg->SetContent(NavScroll);
	USizeBox* NavSz = WidgetTree->ConstructWidget<USizeBox>();
	NavSz->SetWidthOverride(126.f);
	NavSz->SetContent(NavBg);
	Cols->AddChildToHorizontalBox(NavSz);

	UBorder* ContentBg = WidgetTree->ConstructWidget<UBorder>();
	ContentBg->SetBrush(DevBrush(FLinearColor(0.07f, 0.085f, 0.11f, 1.f), 0.f));
	ContentBg->SetPadding(FMargin(15.f, 13.f, 15.f, 14.f));
	UScrollBox* ContentScroll = WidgetTree->ConstructWidget<UScrollBox>();
	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	ContentScroll->AddChild(Body);
	ContentBg->SetContent(ContentScroll);
	Cols->AddChildToHorizontalBox(ContentBg)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	BuildNav(); // nav één keer bouwen

	// Alle 11 categorie-panels ÉÉN keer bouwen als persistente kinderen van Body.
	// Tab-klik toont alleen het gekozen panel en verbergt de rest (geen ClearChildren/rebuild).
	CatPanels.Reset();
	const int32 NumCats = UE_ARRAY_COUNT(GCatNames);
	for (int32 i = 0; i < NumCats; ++i)
	{
		UVerticalBox* Panel = WidgetTree->ConstructWidget<UVerticalBox>();
		Body->AddChildToVerticalBox(Panel);
		CatPanels.Add(Panel);
		BuildCategoryPanel(i, Panel);
	}
	ShowCategory(SelectedCat);
}

void UDevMenuWidget::BuildNav()
{
	if (!CatList) { return; }
	CatList->ClearChildren();
	CatButtons.Reset();
	const int32 N = UE_ARRAY_COUNT(GCatNames);
	for (int32 i = 0; i < N; ++i)
	{
		UWeedActionButton* CB = MakeActionBtn(GCatNames[i], FLinearColor(0.1f, 0.11f, 0.15f),
			[this, i]() { if (SelectedCat != i) { SelectedCat = i; RestyleNav(); ShowCategory(i); } }, 12);
		CatList->AddChildToVerticalBox(CB)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		CatButtons.Add(CB);
	}
	RestyleNav();
}

void UDevMenuWidget::RestyleNav()
{
	for (int32 i = 0; i < CatButtons.Num(); ++i)
	{
		if (!CatButtons[i]) { continue; }
		const bool bSel = (i == SelectedCat);
		const FLinearColor C = bSel ? FLinearColor(0.19f, 0.44f, 0.62f) : FLinearColor(0.1f, 0.11f, 0.15f);
		FButtonStyle S;
		S.Normal = DevBrush(C, 6.f);
		S.Hovered = DevBrush(C * 1.4f, 6.f);
		S.Pressed = DevBrush(C * 0.8f, 6.f);
		S.NormalPadding = FMargin(8.f, 7.f); S.PressedPadding = FMargin(8.f, 7.f);
		CatButtons[i]->SetStyle(S);
	}
}

void UDevMenuWidget::ShowCategory(int32 Cat)
{
	// Toon alleen het gekozen panel; verberg de andere 10. Geen teardown/rebuild.
	for (int32 i = 0; i < CatPanels.Num(); ++i)
	{
		if (!CatPanels[i]) { continue; }
		CatPanels[i]->SetVisibility(i == Cat ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	// Dynamische lijsten van het nu-zichtbare panel meteen up-to-date zetten (per-rij diff, geen rebuild).
	if (Cat == 6) { RefreshRoomTemplates(); RefreshRoomPlaced(); }
	else if (Cat == 9) { RefreshMarkedSpots(); }
	// World/Lighting: value-labels meteen vullen (geen lege frame) — sliders sturen daarna live.
	else if (Cat == 0 || Cat == 8) { ApplyLightSliders(); }
}

void UDevMenuWidget::BuildCategoryPanel(int32 Cat, UVerticalBox* Panel)
{
	if (!Panel) { return; }

	const FLinearColor CSave(0.22f, 0.44f, 0.32f), CClr(0.45f, 0.26f, 0.26f), CAim(0.23f, 0.37f, 0.54f), CKit(0.22f, 0.42f, 0.48f);

	auto BodyRow = [Panel](UWidget* W, const FMargin& Pad) { Panel->AddChildToVerticalBox(W)->SetPadding(Pad); };
	auto Title = [&](const TCHAR* T) { BodyRow(MakeText(T, 16, FLinearColor(0.72f, 0.86f, 1.f)), FMargin(0.f, 0.f, 0.f, 9.f)); };
	auto Head  = [&](const TCHAR* T) { BodyRow(MakeText(T, 11, FLinearColor(0.46f, 0.7f, 1.f)), FMargin(0.f, 7.f, 0.f, 3.f)); };
	auto Desc  = [&](const TCHAR* T) { UTextBlock* D = MakeText(T, 9, FLinearColor(0.54f, 0.59f, 0.68f)); D->SetAutoWrapText(true); BodyRow(D, FMargin(1.f, 1.f, 0.f, 9.f)); };
	auto Single = [&](const FString& L, const FLinearColor& C, const TCHAR* DescT, TFunction<void()> F)
	{
		BodyRow(MakeActionBtn(L, C, F, 11), FMargin(0.f, 0.f, 0.f, 2.f));
		Desc(DescT);
	};
	auto Pair = [&](const FString& L1, const FLinearColor& C1, TFunction<void()> F1, const FString& L2, const FLinearColor& C2, TFunction<void()> F2, const TCHAR* DescT)
	{
		UHorizontalBox* HB = WidgetTree->ConstructWidget<UHorizontalBox>();
		HB->AddChildToHorizontalBox(MakeActionBtn(L1, C1, F1, 11))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		if (UHorizontalBoxSlot* S2 = HB->AddChildToHorizontalBox(MakeActionBtn(L2, C2, F2, 11)))
		{
			S2->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			S2->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
		}
		BodyRow(HB, FMargin(0.f, 0.f, 0.f, 2.f));
		Desc(DescT);
	};

	switch (Cat)
	{
	case 0: // World / Time
		Title(TEXT("World / Time"));
		Pair(TEXT("Set Day"), FLinearColor(0.62f, 0.52f, 0.2f), [this]() { if (Phone.IsValid()) { Phone->RequestSetDayNight(false); } },
			 TEXT("Set Night"), FLinearColor(0.26f, 0.3f, 0.5f), [this]() { if (Phone.IsValid()) { Phone->RequestSetDayNight(true); } },
			 TEXT("Zet de klok meteen op middag of middernacht."));
			Head(TEXT("Weer (UDW - echt weer)"));
			Pair(TEXT("Clear"), FLinearColor(0.3f, 0.45f, 0.6f), [this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SetWeatherPreset(TEXT("Clear_Skies")); } },
				 TEXT("Cloudy"), FLinearColor(0.36f, 0.39f, 0.43f), [this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SetWeatherPreset(TEXT("Cloudy")); } },
				 TEXT("Helder of bewolkt."));
			Pair(TEXT("Rain"), FLinearColor(0.24f, 0.34f, 0.5f), [this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SetWeatherPreset(TEXT("Rain")); } },
				 TEXT("Thunderstorm"), FLinearColor(0.3f, 0.26f, 0.44f), [this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SetWeatherPreset(TEXT("Rain_Thunderstorm")); } },
				 TEXT("Regen of onweer (met bliksem)."));
			Pair(TEXT("Snow"), FLinearColor(0.5f, 0.55f, 0.62f), [this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SetWeatherPreset(TEXT("Snow")); } },
				 TEXT("Fog"), FLinearColor(0.42f, 0.44f, 0.46f), [this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SetWeatherPreset(TEXT("Foggy")); } },
				 TEXT("Sneeuw of mist."));
			Pair(TEXT("Random weer AAN"), FLinearColor(0.24f, 0.42f, 0.3f), [this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SetRandomWeather(true); } },
				 TEXT("UIT"), FLinearColor(0.42f, 0.3f, 0.24f), [this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SetRandomWeather(false); } },
				 TEXT("Weer vanzelf laten wisselen, of uit voor handmatige controle."));
		{
			const float Cur = GetWorld() ? UGameplayStatics::GetGlobalTimeDilation(GetWorld()) : 1.f;
			AddLightSlider(Panel, TEXT("Time speed"), (FMath::Clamp(Cur, 1.f, 8.f) - 1.f) / 7.f, TimeSpeedSlider, TimeSpeedV);
		}
		Desc(TEXT("Versnel de tijd (1x-8x) om sneller te testen."));
		Pair(TEXT("Robbery"), FLinearColor(0.5f, 0.34f, 0.18f), [this]() { if (Phone.IsValid()) { Phone->RequestDevHeatEvent(false); } },
			 TEXT("Bust"), FLinearColor(0.48f, 0.24f, 0.24f), [this]() { if (Phone.IsValid()) { Phone->RequestDevHeatEvent(true); } },
			 TEXT("Forceer nu een overval of een politie-inval."));
		Single(TEXT("Save menu cam (here)"), CAim, TEXT("Leg de huidige camera-stand vast als hoofdmenu-achtergrond voor deze map."),
			[this]() { if (IPlayerNpcActions* A = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { A->DevSaveMenuCam(); } });
		break;

	case 1: // Build & Furnish
		Title(TEXT("Build & Furnish"));
		Pair(TEXT("Build kit"), CKit, [this]() { if (Phone.IsValid()) { Phone->RequestGiveBuildKit(); } },
			 TEXT("Furniture kit"), CKit, [this]() { if (Phone.IsValid()) { Phone->RequestGiveFurnitureKit(); } },
			 TEXT("Geef jezelf alle bouw- of meubel-items in je inventory."));
		Pair(TEXT("Save furniture"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveStarterFurniture(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearStarterFurniture(); } },
			 TEXT("Sla je huidige inrichting op als standaard-layout voor dit woning-type."));
		Pair(TEXT("Save no-build"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveNoBuildZone(); } },
			 TEXT("Clear all"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearNoBuildZone(); } },
			 TEXT("Markeer 2 hoeken met F9, dan Save -> daar mag niks geplaatst worden (ook geen wall-mounts)."));
		Head(TEXT("Templates & build-area"));
		Single(TEXT("Save furniture template"), CAim, TEXT("Sla de inrichting op als sjabloon voor ALLE woningen van dit type (was F8)."),
			[this]() { if (IPlayerNpcActions* A = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { A->DevSaveFurnitureTemplate(); } });
		Single(TEXT("Mark build-area corner"), CAim, TEXT("Sta in een hoek en klik; 2 hoeken = de box waarbinnen je mag bouwen (was F11)."),
			[this]() { if (IPlayerNpcActions* A = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { A->DevMarkBuildAreaCorner(); } });
		break;

	case 2: // Home
		Title(TEXT("Home"));
		Single(TEXT("Save spawn here"), CAim, TEXT("Sla je huidige plek op als spawn-/laadpunt voor deze sessie."),
			[this]() { if (Phone.IsValid()) { Phone->SaveHomeSpawn(); } });
		Single(TEXT("Register home (stand here)"), CAim, TEXT("Registreer de kamer waar je staat als koopbare woning (meet de wanden, was F6)."),
			[this]() { if (IPlayerNpcActions* A = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { A->DevRegisterHome(); } });
		break;

	case 3: // NPC routes & spots
		Title(TEXT("NPC routes & spots"));
		Pair(TEXT("Save route"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveNpcRoute(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearNpcRoute(); } },
			 TEXT("F9-markers op volgorde = NPC-looproute (wordt een gesloten lus)."));
		Pair(TEXT("Save chill"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveChillSpots(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearChillSpots(); } },
			 TEXT("F9-markers = plekken waar NPC's blijven hangen / chillen."));
		Pair(TEXT("Save stairs"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveStairsPath(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearStairsPath(); } },
			 TEXT("F9-markers = trap-/binnenpad zodat NPC's tussen verdiepingen lopen."));
		Pair(TEXT("Show paths"), CAim, [this]() { if (Phone.IsValid()) { Phone->ShowAllPaths(); } },
			 TEXT("Hide paths"), FLinearColor(0.24f, 0.25f, 0.3f), [this]() { if (Phone.IsValid()) { Phone->HideAllPaths(); } },
			 TEXT("Toon of verberg alle opgeslagen routes als ringen/kettingen."));
		Single(TEXT("Delete path (aim at dot)"), CClr, TEXT("Richt op een route-punt en verwijder die hele route."),
			[this]() { if (Phone.IsValid()) { Phone->DeletePathInCrosshair(); } });
		Head(TEXT("Spawn points"));
		Single(TEXT("Delivery point (stand here)"), CAim, TEXT("Vaste plek waar de bezorg-drone pakketjes neerzet (was Shift+F7)."),
			[this]() { if (IPlayerNpcActions* A = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { A->DevMarkDeliveryPoint(); } });
		Single(TEXT("Add meet spot (stand here)"), CAim, TEXT("Plek waar 'come by'-afspraak-NPC's kunnen verschijnen; meerdere mogelijk (was Ctrl+F7)."),
			[this]() { if (IPlayerNpcActions* A = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { A->DevAddMeetSpot(); } });
		Single(TEXT("Activity-NPC (aim: place/edit)"), CAim, TEXT("Richt ergens = plaats een activity-NPC. Richt op een bestaande = open z'n instel-menu (was F10)."),
			[this]() { if (IPlayerNpcActions* A = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { A->DevActivityNpcAtAim(); } });
		break;

	case 4: // Shops
		Title(TEXT("Shops"));
		{
			static const TCHAR* KN[3] = { TEXT("Grow shop"), TEXT("Supplies"), TEXT("Furniture") };
			const int32 SelK = Phone.IsValid() ? FMath::Clamp(Phone->GetSelectedShopKind(), 0, 2) : 0;
			// Type-cycle: knop met een PERSISTENTE label -> tik = SetText op ShopTypeLabel, geen rebuild.
			UWeedActionButton* TypeBtn = WidgetTree->ConstructWidget<UWeedActionButton>();
			TypeBtn->OnClicked.AddDynamic(TypeBtn, &UWeedActionButton::Handle);
			TypeBtn->OnAction.BindLambda([this](int32, int32)
			{
				if (!Phone.IsValid()) { return; }
				Phone->CycleSelectedShopKind();
				static const TCHAR* KN2[3] = { TEXT("Grow shop"), TEXT("Supplies"), TEXT("Furniture") };
				const int32 K = FMath::Clamp(Phone->GetSelectedShopKind(), 0, 2);
				if (ShopTypeLabel) { ShopTypeLabel->SetText(FText::FromString(FString::Printf(TEXT("Type: %s  (tap)"), KN2[K]))); }
			});
			{
				const FLinearColor Col(0.3f, 0.34f, 0.2f);
				FButtonStyle S;
				S.Normal = DevBrush(Col, 6.f);
				S.Hovered = DevBrush(Col * 1.4f, 6.f);
				S.Pressed = DevBrush(Col * 0.8f, 6.f);
				S.NormalPadding = FMargin(8.f, 5.f); S.PressedPadding = FMargin(8.f, 5.f);
				TypeBtn->SetStyle(S);
			}
			ShopTypeLabel = MakeText(FString::Printf(TEXT("Type: %s  (tap)"), KN[SelK]), 11, FLinearColor(0.95f, 0.96f, 1.f), true);
			ShopTypeLabel->SetClipping(EWidgetClipping::Inherit);
			TypeBtn->SetContent(ShopTypeLabel);
			BodyRow(TypeBtn, FMargin(0.f, 0.f, 0.f, 2.f));
			Desc(TEXT("Kies welk winkeltype de volgende 'Save shops' krijgt."));
		}
		Pair(TEXT("Save shops"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveShopSpots(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearShopSpots(); } },
			 TEXT("F9-markers = winkels (elke krijgt een counter + ATM + verkoper)."));
		Single(TEXT("Set type (aim at counter)"), CAim, TEXT("Richt op een bestaande counter en verander z'n winkeltype."),
			[this]() { if (Phone.IsValid()) { Phone->SetShopTypeInCrosshair(); } });
		break;

	case 5: // Map fixes
		Title(TEXT("Map fixes"));
		Pair(TEXT("Walk-through (aim)"), CKit, [this]()
			{
				APlayerController* PC = GetOwningPlayer(); UWorld* DW = GetWorld();
				if (!PC || !DW || !Phone.IsValid()) { return; }
				FVector VL; FRotator VR; PC->GetPlayerViewPoint(VL, VR);
				FHitResult Hit; FCollisionQueryParams Q; Q.AddIgnoredActor(PC->GetPawn());
				if (!DW->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 2500.f, ECC_Pawn, Q) || !Hit.GetComponent())
				{ Phone->Toast(TEXT("Nothing blocking in crosshair"), FColor::Orange, 2.5f); return; }
				if (FMath::Abs(Hit.ImpactNormal.Z) > 0.6f) { Phone->Toast(TEXT("Aim at a wall/object, not the floor"), FColor::Orange, 3.f); return; }
				UPrimitiveComponent* Comp = Hit.GetComponent();
				Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				FVector SaveLoc = Comp->GetComponentLocation();
				FString MeshNm = Comp->GetName();
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp)) { if (SMC->GetStaticMesh()) { MeshNm = SMC->GetStaticMesh()->GetName(); } }
				if (UInstancedStaticMeshComponent* IC = Cast<UInstancedStaticMeshComponent>(Comp))
				{ FTransform IT; if (Hit.Item >= 0 && IC->GetInstanceTransform(Hit.Item, IT, true)) { SaveLoc = IT.GetLocation(); } }
				FString Cur; FFileHelper::LoadFileToString(Cur, *(FPaths::ProjectSavedDir() / TEXT("NoCollide.txt")));
				Cur += FString::Printf(TEXT("%s|%.1f,%.1f,%.1f\n"), *MeshNm, SaveLoc.X, SaveLoc.Y, SaveLoc.Z);
				FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("NoCollide.txt")));
				Phone->Toast(FString::Printf(TEXT("Walk-through: %s (saved)"), *MeshNm), FColor::Cyan, 3.f);
			},
			TEXT("Clear all"), CClr, [this]()
			{ WeedData::DeleteFile(TEXT("NoCollide.txt")); if (Phone.IsValid()) { Phone->Toast(TEXT("Walk-throughs cleared (restart restores)"), FColor::Orange, 4.f); } },
			TEXT("Richt op een muur/object en klik -> je loopt er voortaan doorheen (per map opgeslagen)."));
		break;

	case 6: // Rooms & stamps
		Title(TEXT("Rooms & stamps"));
		Single(TEXT("Save room build (3 markers)"), FLinearColor(0.42f, 0.34f, 0.16f),
			TEXT("3 F9-markers -> permanente kamer-build die elke sessie herbouwd wordt."),
			[this]() { if (Phone.IsValid()) { Phone->SaveRoomJob(); } });
		Single(TEXT("Save as template (2 markers)"), FLinearColor(0.25f, 0.4f, 0.5f),
			TEXT("2 F9-markers -> herbruikbaar kamer-sjabloon dat je hieronder kunt stempelen."),
			[this]() { if (Phone.IsValid()) { Phone->SaveRoomTemplateNow(); MarkDirty(); } });
		// Templates-header + persistente template-lijst-box. Rijen worden per-diff bijgewerkt in RefreshRoomTemplates().
		RoomTplHead = MakeText(TEXT("Templates  (click to place)"), 11, FLinearColor(0.46f, 0.7f, 1.f));
		BodyRow(RoomTplHead, FMargin(0.f, 7.f, 0.f, 3.f));
		RoomTplBox = WidgetTree->ConstructWidget<UVerticalBox>();
		BodyRow(RoomTplBox, FMargin(0.f, 0.f, 0.f, 0.f));
		// Placed-header + "Undo last stamp" samen in één toggle-bare box, daarna de persistente placed-lijst-box.
		RoomPlacedHeadBox = WidgetTree->ConstructWidget<UVerticalBox>();
		{
			RoomPlacedHeadBox->AddChildToVerticalBox(MakeText(TEXT("Placed  (TP / remove)"), 11, FLinearColor(0.46f, 0.7f, 1.f)))->SetPadding(FMargin(0.f, 7.f, 0.f, 3.f));
			RoomPlacedHeadBox->AddChildToVerticalBox(MakeActionBtn(TEXT("Undo last stamp"), FLinearColor(0.52f, 0.3f, 0.2f),
				[this]() { FString Info; ARoomStamper::UndoLastStamp(GetWorld(), Info); MarkDirty(); }, 11))->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
			UTextBlock* UndoDesc = MakeText(TEXT("Verwijder de laatst geplaatste (nog niet gebakken) stempel."), 9, FLinearColor(0.54f, 0.59f, 0.68f));
			UndoDesc->SetAutoWrapText(true);
			RoomPlacedHeadBox->AddChildToVerticalBox(UndoDesc)->SetPadding(FMargin(1.f, 1.f, 0.f, 9.f));
		}
		BodyRow(RoomPlacedHeadBox, FMargin(0.f, 0.f, 0.f, 0.f));
		RoomPlacedBox = WidgetTree->ConstructWidget<UVerticalBox>();
		BodyRow(RoomPlacedBox, FMargin(0.f, 0.f, 0.f, 0.f));
		break;

	case 7: // Doors
		Title(TEXT("Doors"));
		Pair(TEXT("Lock door"), FLinearColor(0.35f, 0.3f, 0.2f), [this]()
			{
				APlayerController* PC = GetOwningPlayer(); UWorld* DW = GetWorld();
				if (!PC || !DW || !Phone.IsValid()) { return; }
				FVector VL; FRotator VR; PC->GetPlayerViewPoint(VL, VR);
				FHitResult Hit; FCollisionQueryParams Q; Q.AddIgnoredActor(PC->GetPawn());
				const bool bHit = DW->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 2500.f, ECC_Visibility, Q);
				const FVector Target = bHit ? Hit.ImpactPoint : VL + VR.Vector() * 600.f;
				ACityDoor* Best = nullptr; float BestD = 250.f;
				for (TActorIterator<ACityDoor> It(DW); It; ++It) { const float Dd = FVector::Dist(It->GetActorLocation(), Target); if (Dd < BestD) { BestD = Dd; Best = *It; } }
				if (!Best) { Phone->Toast(TEXT("No door in crosshair"), FColor::Orange, 2.5f); return; }
				Best->SetResident(FString());
				const FVector DLoc = Best->GetActorLocation();
				FString Cur; FFileHelper::LoadFileToString(Cur, *(FPaths::ProjectSavedDir() / TEXT("LockedDoors.txt")));
				Cur += FString::Printf(TEXT("%.1f,%.1f,%.1f\n"), DLoc.X, DLoc.Y, DLoc.Z);
				FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("LockedDoors.txt")));
				Phone->Toast(TEXT("Door locked (saved)"), FColor::Cyan, 3.f);
			},
			TEXT("Clear locks"), CClr, [this]()
			{ WeedData::DeleteFile(TEXT("LockedDoors.txt")); if (Phone.IsValid()) { Phone->Toast(TEXT("Locked doors cleared (restart restores)"), FColor::Orange, 4.f); } },
			TEXT("Richt op een deur -> permanent op slot (prompt 'LOCKED', geen bewoner)."));
		Pair(TEXT("Snap to frame"), FLinearColor(0.25f, 0.35f, 0.3f), [this]()
			{
				APlayerController* PC = GetOwningPlayer(); UWorld* DW = GetWorld();
				if (!PC || !DW || !Phone.IsValid()) { return; }
				FVector VL; FRotator VR; PC->GetPlayerViewPoint(VL, VR);
				FHitResult Hit; FCollisionQueryParams Q; Q.AddIgnoredActor(PC->GetPawn());
				const bool bHit = DW->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 2500.f, ECC_Visibility, Q);
				const FVector Aim = bHit ? Hit.ImpactPoint : VL + VR.Vector() * 600.f;
				ACityDoor* Best = nullptr; float BestD = 250.f;
				for (TActorIterator<ACityDoor> It(DW); It; ++It) { const float Dd = FVector::Dist(It->GetActorLocation(), Aim); if (Dd < BestD) { BestD = Dd; Best = *It; } }
				if (!Best) { Phone->Toast(TEXT("No door in crosshair"), FColor::Orange, 2.5f); return; }
				const FVector DLoc = Best->GetActorLocation();
				ACityDoor::SnapToNearestFrame(DW, Best);
				FString Cur; FFileHelper::LoadFileToString(Cur, *(FPaths::ProjectSavedDir() / TEXT("DoorSnaps.txt")));
				Cur += FString::Printf(TEXT("%.1f,%.1f,%.1f\n"), DLoc.X, DLoc.Y, DLoc.Z);
				FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("DoorSnaps.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				Phone->Toast(TEXT("Door snapped to frame (saved)"), FColor::Cyan, 3.f);
			},
			TEXT("Clear snaps"), CClr, [this]()
			{ WeedData::DeleteFile(TEXT("DoorSnaps.txt")); if (Phone.IsValid()) { Phone->Toast(TEXT("Door snaps cleared (restart restores)"), FColor::Orange, 4.f); } },
			TEXT("Richt op een scheve deur -> springt in z'n dichtstbijzijnde kozijn."));
		Single(TEXT("Remove door (aim)"), FLinearColor(0.4f, 0.22f, 0.22f), TEXT("Richt op een zwevende/foute deur -> meteen weg."), [this]()
			{
				APlayerController* PC = GetOwningPlayer(); UWorld* DW = GetWorld();
				if (!PC || !DW) { return; }
				FVector VL; FRotator VR; PC->GetPlayerViewPoint(VL, VR);
				FHitResult Hit; FCollisionQueryParams Q; Q.AddIgnoredActor(PC->GetPawn());
				const bool bHit = DW->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 2500.f, ECC_Visibility, Q);
				const FVector Target = bHit ? Hit.ImpactPoint : VL + VR.Vector() * 600.f;
				ACityDoor* Best = nullptr; float BestD = 250.f;
				for (TActorIterator<ACityDoor> It(DW); It; ++It) { if (!IsValid(*It)) { continue; } const float D = FVector::Dist(It->GetActorLocation(), Target); if (D < BestD) { BestD = D; Best = *It; } }
				if (Best) { Best->Destroy(); }
			});
		break;

	case 8: // Lighting
		Title(TEXT("Lighting  (live)"));
		{
			ADayNightController* DN = ADayNightController::GetLocal(GetWorld());
			const bool bPackL = DN && DN->IsPackMinimal();
			if (bPackL)
			{
				// UDS bezit de lucht -> deze sliders sturen UDS LIVE aan (geen rebuild nodig).
				AddLightSlider(Panel, TEXT("Day exposure"),   DN ? (DN->UdsExpDay + 2.f) / 3.f : 0.5f,         LSun,   LSunV);
				AddLightSlider(Panel, TEXT("Night exposure"), DN ? (DN->UdsExpNight + 4.f) / 5.f : 0.56f,      LMoon,  LMoonV);
				AddLightSlider(Panel, TEXT("Dawn/dusk exp"),  DN ? (DN->UdsExpDawnDusk + 2.f) / 3.f : 0.57f,   LPitch, LPitchV);
				AddLightSlider(Panel, TEXT("Street lamps"),   DN ? DN->LampIntensity / 80000.f : 0.35f,        LLamp,  LLampV);
				AddLightSlider(Panel, TEXT("Stars"),          DN ? DN->UdsStars / 5.f : 0.5f,                  LSkyN,  LSkyNV);
				AddLightSlider(Panel, TEXT("Nebula"),         DN ? DN->UdsNebula / 3.f : 0.4f,                 LSkyD,  LSkyDV);
				AddLightSlider(Panel, TEXT("Night glow"),     DN ? DN->UdsNightGlow : 0.3f,                    LExp,   LExpV);
			}
			else
			{
				const float Moon = DN ? DN->MoonIntensity : 0.65f;
				const float Sun  = DN ? DN->SunIntensity  : 6.5f;
				const float Lmp  = DN ? DN->LampIntensity : 28000.f;
				AddLightSlider(Panel, TEXT("Moon (night)"), Moon / 3.f,    LMoon, LMoonV);
				AddLightSlider(Panel, TEXT("Sun (day)"),    Sun / 12.f,    LSun,  LSunV);
				AddLightSlider(Panel, TEXT("Street lamps"), Lmp / 80000.f, LLamp, LLampV);
				const float SkN = DN ? DN->SkyNight : 0.85f;
				const float SkD = DN ? DN->SkyDay : 1.0f;
				const float Pit = DN ? DN->MoonPitch : -52.f;
				const float Exp = DN ? DN->ExposureBias : 9.f;
				AddLightSlider(Panel, TEXT("Sky night"),  SkN / 2.f,           LSkyN,  LSkyNV);
				AddLightSlider(Panel, TEXT("Sky day"),    SkD / 2.f,           LSkyD,  LSkyDV);
				AddLightSlider(Panel, TEXT("Moon angle"), (Pit + 90.f) / 90.f, LPitch, LPitchV);
				AddLightSlider(Panel, TEXT("Exposure"),   Exp / 16.f,          LExp,   LExpV);
			}
			// (Value-labels worden gevuld door ShowCategory(8)/NativeTick -> ApplyLightSliders zodra de tab actief is.)
			Desc(TEXT("Sleep om het licht direct aan te passen."));
			Single(TEXT("Save light config"), CSave, TEXT("Bewaar de huidige licht-instellingen permanent voor deze map."),
				[this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SaveLightConfig(); } });
		}
		break;

	case 9: // Marked spots
		Title(TEXT("Marked spots"));
		Desc(TEXT("F9-markers: teleporteer (TP) of verwijder (X). Markeren doe je met F9 in-game."));
		// "No spots yet." (toggle-baar) + persistente spot-lijst-box; rijen worden per-diff bijgewerkt in RefreshMarkedSpots().
		SpotsEmptyText = MakeText(TEXT("No spots yet."), 11, FLinearColor(0.6f, 0.64f, 0.74f));
		BodyRow(SpotsEmptyText, FMargin(0.f, 0.f, 0.f, 4.f));
		SpotsBox = WidgetTree->ConstructWidget<UVerticalBox>();
		BodyRow(SpotsBox, FMargin(0.f, 0.f, 0.f, 0.f));
		break;

	default: // Keys
		Title(TEXT("Keys"));
		Desc(TEXT("De meeste dev-acties zijn nu knoppen. Dit zijn de losse toetsen die nog bestaan."));
		{
			auto KeyRow = [&](const TCHAR* K, const TCHAR* D)
			{
				UHorizontalBox* R = WidgetTree->ConstructWidget<UHorizontalBox>();
				USizeBox* KSz = WidgetTree->ConstructWidget<USizeBox>(); KSz->SetWidthOverride(80.f);
				KSz->SetContent(MakeText(K, 12, FLinearColor(0.85f, 0.9f, 1.f)));
				R->AddChildToHorizontalBox(KSz);
				UTextBlock* DT = MakeText(D, 10, FLinearColor(0.6f, 0.64f, 0.74f)); DT->SetAutoWrapText(true);
				UHorizontalBoxSlot* DS = R->AddChildToHorizontalBox(DT);
				DS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); DS->SetVerticalAlignment(VAlign_Center);
				BodyRow(R, FMargin(0.f, 0.f, 0.f, 5.f));
			};
			KeyRow(TEXT("F10"), TEXT("Dit dev-menu openen / sluiten"));
			KeyRow(TEXT("F9"), TEXT("Spot markeren + overlay (basis voor routes / zones / shops)"));
			KeyRow(TEXT("F7"), TEXT("Vliegen / noclip  (Space = op, Ctrl = neer)"));
		}
		break;
	}
}

void UDevMenuWidget::RefreshRoomTemplates()
{
	if (!RoomTplBox) { return; }

	const TArray<FString> Templates = ARoomStamper::ListTemplates();
	if (RoomTplHead) { RoomTplHead->SetVisibility(Templates.Num() > 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }

	// Rij-pool op maat brengen (persistent) -> geen ClearChildren van de hele lijst.
	while (RoomTplRows.Num() < Templates.Num())
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		RoomTplBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		RoomTplRows.Add(Row); RoomTplSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
	}
	while (RoomTplRows.Num() > Templates.Num())
	{
		const int32 Last = RoomTplRows.Num() - 1;
		if (RoomTplRows[Last]) { RoomTplRows[Last]->RemoveFromParent(); }
		RoomTplRows.RemoveAt(Last); RoomTplSigs.RemoveAt(Last);
	}

	// Per-rij diff: alleen een rij die ECHT wijzigde (naam) krijgt nieuwe inhoud.
	for (int32 i = 0; i < Templates.Num(); ++i)
	{
		const FString& Tpl = Templates[i];
		if (!RoomTplRows.IsValidIndex(i) || !RoomTplRows[i]) { continue; }
		if (RoomTplSigs.IsValidIndex(i) && RoomTplSigs[i] == Tpl) { continue; }
		RoomTplSigs[i] = Tpl;

		UHorizontalBox* Row = RoomTplRows[i];
		Row->ClearChildren();
		Row->AddChildToHorizontalBox(MakeActionBtn(FString::Printf(TEXT("Stamp: %s"), *Tpl), FLinearColor(0.2f, 0.35f, 0.25f),
			[this, Tpl]() { if (Phone.IsValid()) { Phone->StartRoomStamp(Tpl); } }, 11))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("X"), FLinearColor(0.45f, 0.2f, 0.2f),
			[this, Tpl]() { WeedData::DeleteFile(FString(TEXT("RoomTemplates")) / (Tpl + TEXT(".txt"))); MarkDirty(); }, 11))->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
	}
}

void UDevMenuWidget::RefreshRoomPlaced()
{
	if (!RoomPlacedBox) { return; }

	const TArray<FString> Placed = ARoomStamper::ListPlacedStamps(GetWorld());
	// Header + "Undo last stamp" alleen tonen als er iets geplaatst is (zelfde gedrag als voorheen).
	if (RoomPlacedHeadBox) { RoomPlacedHeadBox->SetVisibility(Placed.Num() > 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }

	while (RoomPlacedRows.Num() < Placed.Num())
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		RoomPlacedBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		RoomPlacedRows.Add(Row); RoomPlacedSigs.Add(TEXT("\x01"));
	}
	while (RoomPlacedRows.Num() > Placed.Num())
	{
		const int32 Last = RoomPlacedRows.Num() - 1;
		if (RoomPlacedRows[Last]) { RoomPlacedRows[Last]->RemoveFromParent(); }
		RoomPlacedRows.RemoveAt(Last); RoomPlacedSigs.RemoveAt(Last);
	}

	for (int32 i = 0; i < Placed.Num(); ++i)
	{
		const FString& SLine = Placed[i];
		if (!RoomPlacedRows.IsValidIndex(i) || !RoomPlacedRows[i]) { continue; }
		if (RoomPlacedSigs.IsValidIndex(i) && RoomPlacedSigs[i] == SLine) { continue; }
		RoomPlacedSigs[i] = SLine;

		TArray<FString> SParts; SLine.ParseIntoArray(SParts, TEXT("|"));
		FVector Pos = FVector::ZeroVector; bool bHasPos = false;
		if (SParts.Num() > 1)
		{
			TArray<FString> PP; SParts[1].ParseIntoArray(PP, TEXT(","));
			if (PP.Num() >= 3) { Pos = FVector(FCString::Atof(*PP[0]), FCString::Atof(*PP[1]), FCString::Atof(*PP[2])); bHasPos = true; }
		}
		UHorizontalBox* SRow = RoomPlacedRows[i];
		SRow->ClearChildren();
		UHorizontalBoxSlot* SLab = SRow->AddChildToHorizontalBox(MakeText(
			FString::Printf(TEXT("%s  (%.0f, %.0f)"), SParts.Num() > 0 ? *SParts[0] : *SLine, Pos.X, Pos.Y), 11, FLinearColor(0.82f, 0.88f, 1.f)));
		SLab->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); SLab->SetVerticalAlignment(VAlign_Center);
		if (bHasPos)
		{
			SRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("TP"), FLinearColor(0.2f, 0.4f, 0.55f),
				[this, Pos]() { if (APawn* Pn = GetOwningPlayerPawn()) { Pn->SetActorLocation(Pos + FVector(0.f, 0.f, 120.f)); } }, 11))->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
		}
		SRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("X"), FLinearColor(0.45f, 0.2f, 0.2f),
			[this, SLine]() { ARoomStamper::RemoveStamp(GetWorld(), SLine); MarkDirty(); }, 11))->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
	}
}

void UDevMenuWidget::RefreshMarkedSpots()
{
	if (!SpotsBox) { return; }

	TArray<FString> SpotLines;
	FFileHelper::LoadFileToStringArray(SpotLines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	SpotLines.RemoveAll([](const FString& L) { return L.TrimStartAndEnd().IsEmpty(); });
	if (SpotsEmptyText) { SpotsEmptyText->SetVisibility(SpotLines.Num() == 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }

	// Bepaal het zichtbare venster (laatste MaxShow), met bijbehorende absolute index voor delete.
	const int32 MaxShow = 24;
	const int32 First = FMath::Max(0, SpotLines.Num() - MaxShow);
	const int32 Shown = SpotLines.Num() - First;
	const FString CurMap = GetWorld() ? GetWorld()->GetOutermost()->GetName() : FString();

	while (SpotRows.Num() < Shown)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		SpotsBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		SpotRows.Add(Row); SpotSigs.Add(TEXT("\x01"));
	}
	while (SpotRows.Num() > Shown)
	{
		const int32 Last = SpotRows.Num() - 1;
		if (SpotRows[Last]) { SpotRows[Last]->RemoveFromParent(); }
		SpotRows.RemoveAt(Last); SpotSigs.RemoveAt(Last);
	}

	for (int32 r = 0; r < Shown; ++r)
	{
		const int32 SpotIdx = First + r; // absolute regel-index in het bestand (voor delete)
		const FString& Line = SpotLines[SpotIdx];
		if (!SpotRows.IsValidIndex(r) || !SpotRows[r]) { continue; }
		// Signatuur bevat de absolute index zodat na een delete de verschoven rijen mee-updaten.
		const FString Sig = FString::Printf(TEXT("%d|%s"), SpotIdx, *Line);
		if (SpotSigs.IsValidIndex(r) && SpotSigs[r] == Sig) { continue; }
		SpotSigs[r] = Sig;

		FString Label = Line; int32 Bar = INDEX_NONE;
		if (Label.FindChar(TEXT('|'), Bar)) { Label = Label.Left(Bar).TrimStartAndEnd(); }
		FVector Pos = FVector::ZeroVector; bool bHasPos = false;
		const int32 PIdx = Line.Find(TEXT("pos=("));
		if (PIdx != INDEX_NONE)
		{
			FString PosStr = Line.Mid(PIdx + 5); int32 Close = INDEX_NONE;
			if (PosStr.FindChar(TEXT(')'), Close)) { PosStr = PosStr.Left(Close); }
			TArray<FString> PP; PosStr.ParseIntoArray(PP, TEXT(","));
			if (PP.Num() >= 3) { Pos = FVector(FCString::Atof(*PP[0]), FCString::Atof(*PP[1]), FCString::Atof(*PP[2])); bHasPos = true; }
		}
		const bool bSameMap = !Line.Contains(TEXT("map=")) || Line.Contains(CurMap);

		UHorizontalBox* RowB = SpotRows[r];
		RowB->ClearChildren();
		UHorizontalBoxSlot* LS2 = RowB->AddChildToHorizontalBox(MakeText(
			FString::Printf(TEXT("%s  (%.0f, %.0f)"), *Label, Pos.X, Pos.Y), 11,
			bSameMap ? FLinearColor(0.82f, 0.88f, 1.f) : FLinearColor(0.5f, 0.52f, 0.6f)));
		LS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS2->SetVerticalAlignment(VAlign_Center);
		if (bHasPos && bSameMap)
		{
			RowB->AddChildToHorizontalBox(MakeActionBtn(TEXT("TP"), FLinearColor(0.2f, 0.4f, 0.55f),
				[this, Pos]() { if (APawn* Pn = GetOwningPlayerPawn()) { Pn->SetActorLocation(Pos + FVector(0.f, 0.f, 60.f)); } }, 11))->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
		}
		RowB->AddChildToHorizontalBox(MakeActionBtn(TEXT("X"), FLinearColor(0.45f, 0.2f, 0.2f),
			[this, SpotIdx]()
			{
				const FString F = FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt");
				TArray<FString> Ls; FFileHelper::LoadFileToStringArray(Ls, *F);
				Ls.RemoveAll([](const FString& L) { return L.TrimStartAndEnd().IsEmpty(); });
				if (Ls.IsValidIndex(SpotIdx)) { Ls.RemoveAt(SpotIdx); }
				FFileHelper::SaveStringToFile(FString::Join(Ls, TEXT("\n")) + (Ls.Num() ? TEXT("\n") : TEXT("")), *F);
				MarkDirty();
			}, 11))->SetPadding(FMargin(5.f, 0.f, 0.f, 0.f));
	}
}

UTextBlock* UDevMenuWidget::MakeText(const FString& Txt, int32 Size, const FLinearColor& Col, bool bCenter)
{
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetText(FText::FromString(Txt));
	T->SetFont(DevFont(Size));
	T->SetColorAndOpacity(FSlateColor(Col));
	if (bCenter) { T->SetJustification(ETextJustify::Center); }
	return T;
}

UWeedActionButton* UDevMenuWidget::MakeActionBtn(const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick, int32 FontSize)
{
	UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
	B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
	B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
	FButtonStyle S;
	S.Normal = DevBrush(Col, 6.f);
	S.Hovered = DevBrush(Col * 1.4f, 6.f);
	S.Pressed = DevBrush(Col * 0.8f, 6.f);
	S.NormalPadding = FMargin(8.f, 5.f); S.PressedPadding = FMargin(8.f, 5.f);
	B->SetStyle(S);
	UTextBlock* T = MakeText(Label, FontSize, FLinearColor(0.95f, 0.96f, 1.f), true);
	T->SetClipping(EWidgetClipping::Inherit);
	B->SetContent(T);
	return B;
}

USlider* UDevMenuWidget::AddLightSlider(UVerticalBox* Panel, const FString& Label, float Norm, TObjectPtr<USlider>& OutS, TObjectPtr<UTextBlock>& OutV)
{
	USlider* Slider = WidgetTree->ConstructWidget<USlider>();
	Slider->SetMinValue(0.f); Slider->SetMaxValue(1.f);
	Slider->SetValue(FMath::Clamp(Norm, 0.f, 1.f));
	Slider->SetSliderBarColor(FLinearColor(0.18f, 0.2f, 0.27f));
	Slider->SetSliderHandleColor(FLinearColor(0.55f, 0.8f, 1.f));
	OutS = Slider;

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(MakeText(Label, 11, FLinearColor(0.82f, 0.86f, 0.95f)));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);
	USizeBox* SSz = WidgetTree->ConstructWidget<USizeBox>();
	SSz->SetWidthOverride(150.f); SSz->SetHeightOverride(18.f); SSz->SetContent(Slider);
	Row->AddChildToHorizontalBox(SSz);
	OutV = MakeText(TEXT(""), 11, FLinearColor::White, true);
	USizeBox* VSz = WidgetTree->ConstructWidget<USizeBox>();
	VSz->SetWidthOverride(52.f); VSz->SetContent(OutV);
	Row->AddChildToHorizontalBox(VSz);

	if (Panel) { Panel->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f)); }
	return Slider;
}

void UDevMenuWidget::ApplyLightSliders()
{
	// Time-speed alleen op de World-tab (0); licht alleen op de Lighting-tab (8). De sliders bestaan
	// altijd (panels zijn persistent), dus we begrenzen op de actieve categorie i.p.v. op null-pointers.
	if (SelectedCat == 0 && TimeSpeedSlider && GetWorld())
	{
		const float Speed = 1.f + TimeSpeedSlider->GetValue() * 7.f;
		if (FMath::Abs(Speed - UGameplayStatics::GetGlobalTimeDilation(GetWorld())) > 0.01f)
		{
			UGameplayStatics::SetGlobalTimeDilation(GetWorld(), Speed);
		}
		if (TimeSpeedV) { TimeSpeedV->SetText(FText::FromString(FString::Printf(TEXT("%.1fx"), Speed))); }
	}

	if (SelectedCat != 8) { return; }
	ADayNightController* DN = ADayNightController::GetLocal(GetWorld());
	if (!DN || !LMoon) { return; }

	// Street lamps: zelfde slider in beide modi.
	if (LLamp)  { DN->LampIntensity = LLamp->GetValue() * 80000.f; }
	if (LLampV) { LLampV->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), DN->LampIntensity))); }

	if (DN->IsPackMinimal())
	{
		// UDS: LSun=dag-exp, LMoon=nacht-exp, LPitch=dawn-exp, LSkyN=cloud, LSkyD=fog -> live naar UDS.
		if (LSun)   { DN->UdsExpDay      = LSun->GetValue()  * 3.f - 2.f; }
		if (LMoon)  { DN->UdsExpNight    = LMoon->GetValue() * 5.f - 4.f; }
		if (LPitch) { DN->UdsExpDawnDusk = LPitch->GetValue() * 3.f - 2.f; }
		if (LSkyN)  { DN->UdsStars     = LSkyN->GetValue() * 5.f; }
		if (LSkyD)  { DN->UdsNebula    = LSkyD->GetValue() * 3.f; }
		if (LExp)   { DN->UdsNightGlow = LExp->GetValue(); }
		DN->ApplyUdsLook();
		if (LSunV)   { LSunV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->UdsExpDay))); }
		if (LMoonV)  { LMoonV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->UdsExpNight))); }
		if (LPitchV) { LPitchV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->UdsExpDawnDusk))); }
		if (LSkyNV)  { LSkyNV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->UdsStars))); }
		if (LSkyDV)  { LSkyDV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->UdsNebula))); }
		if (LExpV)   { LExpV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->UdsNightGlow))); }
	}
	else
	{
		DN->MoonIntensity = LMoon->GetValue() * 3.f;
		DN->SunIntensity  = LSun  ? LSun->GetValue()  * 12.f : DN->SunIntensity;
		if (LMoonV) { LMoonV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->MoonIntensity))); }
		if (LSunV)  { LSunV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->SunIntensity))); }
		DN->SkyNight     = LSkyN  ? LSkyN->GetValue() * 2.f          : DN->SkyNight;
		DN->SkyDay       = LSkyD  ? LSkyD->GetValue() * 2.f          : DN->SkyDay;
		DN->MoonPitch    = LPitch ? LPitch->GetValue() * 90.f - 90.f : DN->MoonPitch;
		DN->ExposureBias = LExp   ? LExp->GetValue() * 16.f          : DN->ExposureBias;
		if (LSkyNV)  { LSkyNV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->SkyNight))); }
		if (LSkyDV)  { LSkyDV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->SkyDay))); }
		if (LPitchV) { LPitchV->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), DN->MoonPitch))); }
		if (LExpV)   { LExpV->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), DN->ExposureBias))); }
	}
}
