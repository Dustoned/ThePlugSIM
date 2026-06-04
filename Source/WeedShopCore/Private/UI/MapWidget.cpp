#include "UI/MapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/WeedUiStyle.h"
#include "Styling/CoreStyle.h"
#include "World/CityGenerator.h"
#include "Customer/CustomerBase.h"
#include "Game/WeedShopGameState.h"
#include "Npc/NpcRegistryComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "EngineUtils.h"

namespace
{
	constexpr float GMapDS = 760.f;  // ontwerp-grootte van het kaartvlak (px)
}

FVector2D UMapWidget::WorldToCanvas(float Wx, float Wy) const
{
	const float rx = Wx - CenterXY.X;
	const float ry = Wy - CenterXY.Y;
	// scherm-rechts = wereld +Y, scherm-omhoog = wereld +X (noorden boven)
	return FVector2D(GMapDS * 0.5f + ry * Scale, GMapDS * 0.5f - rx * Scale);
}

FVector2D UMapWidget::CanvasToWorld(FVector2D Local) const
{
	if (Scale <= 0.f) { return CenterXY; }
	const float ry = (Local.X - GMapDS * 0.5f) / Scale;
	const float rx = -(Local.Y - GMapDS * 0.5f) / Scale;
	return FVector2D(CenterXY.X + rx, CenterXY.Y + ry);
}

UPhoneClientComponent* UMapWidget::GetPhone() const
{
	APawn* P = GetOwningPlayerPawn();
	return P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;
}

FReply UMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!Canvas || !bBuiltBlocks) { return FReply::Unhandled(); }
	UPhoneClientComponent* Ph = GetPhone();
	if (!Ph) { return FReply::Unhandled(); }

	// Rechtermuisknop = waypoint wissen.
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Ph->ClearWaypoint();
		return FReply::Handled();
	}
	// Links = waypoint op de aangeklikte plek (klik-pixel -> kaart-lokaal -> wereld).
	const FVector2D Local = Canvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D World = CanvasToWorld(Local);
	float Z = 0.f;
	if (APawn* P = GetOwningPlayerPawn()) { Z = P->GetActorLocation().Z; }
	Ph->SetWaypoint(FVector(World.X, World.Y, Z));
	return FReply::Handled();
}

UTextBlock* UMapWidget::AddCanvasText(const FString& T, FVector2D Pos, float W, int32 Size, const FLinearColor& Col, int32 ZOrder)
{
	UTextBlock* Tb = WidgetTree->ConstructWidget<UTextBlock>();
	Tb->SetText(FText::FromString(T));
	Tb->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", Size));
	Tb->SetColorAndOpacity(FSlateColor(Col));
	Tb->SetJustification(ETextJustify::Center);
	UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(Tb);
	Cs->SetAutoSize(false);
	Cs->SetSize(FVector2D(W, Size + 6.f));
	Cs->SetAlignment(FVector2D(0.5f, 0.5f));
	Cs->SetPosition(Pos);
	Cs->SetZOrder(ZOrder);
	return Tb;
}

UBorder* UMapWidget::AddDot(const FLinearColor& Col, float Sz, int32 ZOrder)
{
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrushColor(Col);
	UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(B);
	Cs->SetAutoSize(false);
	Cs->SetSize(FVector2D(Sz, Sz));
	Cs->SetAlignment(FVector2D(0.5f, 0.5f));
	Cs->SetZOrder(ZOrder);
	return B;
}

UWidget* UMapWidget::AddPersonIcon()
{
	USizeBox* SB = WidgetTree->ConstructWidget<USizeBox>();
	SB->SetWidthOverride(20.f); SB->SetHeightOverride(20.f);
	SB->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Person, 20.f, FLinearColor(0.3f, 1.f, 0.4f)));
	UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(SB);
	Cs->SetAutoSize(false);
	Cs->SetSize(FVector2D(20.f, 20.f));
	Cs->SetAlignment(FVector2D(0.5f, 0.5f));
	Cs->SetZOrder(21);
	return SB;
}

TSharedRef<SWidget> UMapWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MapRoot"));
		WidgetTree->RootWidget = Root;

		// Donkere achtergrond (dekkend -> HUD erachter komt er niet doorheen).
		UBorder* Bg = WidgetTree->ConstructWidget<UBorder>();
		Bg->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.07f, 1.f));
		Root->AddChildToOverlay(Bg);

		// Vaste-grootte kaartvlak, geschaald naar de beschikbare ruimte.
		UScaleBox* SB = WidgetTree->ConstructWidget<UScaleBox>();
		SB->SetStretch(EStretch::ScaleToFit);
		if (UOverlaySlot* OS = Root->AddChildToOverlay(SB))
		{
			OS->SetHorizontalAlignment(HAlign_Center);
			OS->SetVerticalAlignment(VAlign_Center);
		}
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(GMapDS);
		Box->SetHeightOverride(GMapDS);
		SB->AddChild(Box);

		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MapCanvas"));
		Box->SetContent(Canvas);

		// Echte top-down stad-render als achtergrond (gevuld door de SceneCapture); onderop.
		MapImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MapImage"));
		if (UCanvasPanelSlot* Is = Canvas->AddChildToCanvas(MapImage))
		{
			Is->SetAutoSize(false);
			Is->SetSize(FVector2D(GMapDS, GMapDS));
			Is->SetAlignment(FVector2D(0.f, 0.f));
			Is->SetPosition(FVector2D(0.f, 0.f));
			Is->SetZOrder(0);
		}
		MapImage->SetVisibility(ESlateVisibility::HitTestInvisible);

		// Titel + kompas.
		AddCanvasText(TEXT("STAD - KAART"), FVector2D(GMapDS * 0.5f, 20.f), GMapDS, 20, FLinearColor(0.7f, 0.85f, 1.f), 50);
		AddCanvasText(TEXT("N"), FVector2D(GMapDS * 0.5f, 44.f), 40.f, 14, FLinearColor(0.8f, 0.8f, 0.9f), 50);
	}
	return Super::RebuildWidget();
}

void UMapWidget::BuildBlocks()
{
	if (!Canvas || !City.IsValid()) { return; }

	const FVector C = City->GetCityCenter();
	CenterXY = FVector2D(C.X, C.Y);
	// Schaal afgestemd op de top-down render: het 760px-vlak = de OrthoWidth-brede wereldzone.
	const float Ortho = FMath::Max(1.f, City->GetMapOrthoWidth());
	Scale = GMapDS / Ortho;

	// Huisnummer-/winkellabels boven de ECHTE stad-render (zo zie je gebouwen ÉN welke nummers waar).
	TArray<FCityMapBlock> Blocks;
	City->GetMapBlocks(Blocks);
	const float BlkPx = City->GetMapBlockSize() * Scale;
	for (const FCityMapBlock& Bk : Blocks)
	{
		const FVector2D P = WorldToCanvas(Bk.Center.X, Bk.Center.Y);
		const int32 FontSz = Bk.bShop ? 13 : 11;
		// Wit met donkere "schaduw" eronder -> leesbaar op zowel donkere wegen als gekleurde daken.
		AddCanvasText(Bk.Label, P + FVector2D(1.f, -BlkPx * 0.30f + 1.f), BlkPx + 12.f, FontSz, FLinearColor(0.f, 0.f, 0.f, 0.85f), 5);
		AddCanvasText(Bk.Label, P + FVector2D(0.f, -BlkPx * 0.30f), BlkPx + 12.f, FontSz, FLinearColor(1.f, 1.f, 0.95f), 6);
	}

	City->CaptureMapNow(); // verse top-down render zodra de kaart opent

	// Speler-marker (boven alles) + waypoint-marker (geel, verborgen tot je 'm zet).
	PlayerDot = AddDot(FLinearColor(0.2f, 0.9f, 1.f), 16.f, 20);
	WaypointDot = AddDot(FLinearColor(1.f, 0.85f, 0.15f), 18.f, 22);
	if (WaypointDot) { WaypointDot->SetVisibility(ESlateVisibility::Collapsed); }
	AddCanvasText(TEXT("Klik = waypoint zetten  /  rechtsklik = wissen"),
		FVector2D(GMapDS * 0.5f, 64.f), GMapDS, 11, FLinearColor(0.7f, 0.85f, 1.f), 50);
	AddCanvasText(TEXT("cyaan stip = jij    geel = waypoint    blauw = NPC    groen poppetje = klant voor jou"),
		FVector2D(GMapDS * 0.5f, GMapDS - 18.f), GMapDS, 10, FLinearColor(0.7f, 0.72f, 0.8f), 50);

	bBuiltBlocks = true;
}

void UMapWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	if (!City.IsValid())
	{
		for (TActorIterator<ACityGenerator> It(GetWorld()); It; ++It) { City = *It; break; }
	}
	if (!bBuiltBlocks && City.IsValid()) { BuildBlocks(); }
	if (!bBuiltBlocks || !Canvas) { return; }

	// Echte top-down render dekkend tonen: M_MapDisplay (UI/opaque) sampelt de SceneCapture-RT.
	// De BaseColor-capture heeft alpha=0; dit materiaal negeert die alpha -> geen doorzichtige kaart.
	if (!bImageSet && MapImage && City.IsValid())
	{
		if (UTextureRenderTarget2D* RT = City->GetMapRenderTarget())
		{
			if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_MapDisplay.M_MapDisplay")))
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
				MID->SetTextureParameterValue(TEXT("Tex"), RT);
				MapImage->SetBrushFromMaterial(MID);
				if (UCanvasPanelSlot* Is = Cast<UCanvasPanelSlot>(MapImage->Slot)) { Is->SetSize(FVector2D(GMapDS, GMapDS)); }
				MapImage->SetDesiredSizeOverride(FVector2D(GMapDS, GMapDS));
				MapImage->SetVisibility(ESlateVisibility::HitTestInvisible);
				bImageSet = true;
			}
		}
	}

	// Speler.
	if (PlayerDot)
	{
		if (APawn* P = GetOwningPlayerPawn())
		{
			const FVector L = P->GetActorLocation();
			if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(PlayerDot->Slot)) { Cs->SetPosition(WorldToCanvas(L.X, L.Y)); }
			PlayerDot->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}

	// Waypoint-marker.
	if (WaypointDot)
	{
		UPhoneClientComponent* Ph = GetPhone();
		if (Ph && Ph->HasWaypoint())
		{
			const FVector W = Ph->GetWaypoint();
			if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(WaypointDot->Slot)) { Cs->SetPosition(WorldToCanvas(W.X, W.Y)); }
			WaypointDot->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else { WaypointDot->SetVisibility(ESlateVisibility::Collapsed); }
	}

	// Klanten/NPC's verzamelen.
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr;

	int32 NRoam = 0, NNeed = 0;
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		ACustomerBase* C = *It;
		if (!IsValid(C)) { continue; }

		const FVector L = C->GetActorLocation();
		const FVector2D Pos = WorldToCanvas(L.X, L.Y);

		if (C->bNeedsPlayer)
		{
			// Klant-die-je-nodig-hebt = altijd het GROENE poppetje-icoon + naam (duidelijk onderscheid).
			while (NeedIcons.Num() <= NNeed) { NeedIcons.Add(AddPersonIcon()); }
			while (NpcLabels.Num() <= NNeed) { NpcLabels.Add(AddCanvasText(TEXT(""), FVector2D::ZeroVector, 130.f, 11, FLinearColor(0.6f, 1.f, 0.6f), 22)); }
			if (UWidget* Ico = NeedIcons[NNeed])
			{
				if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(Ico->Slot)) { Cs->SetPosition(Pos); }
				Ico->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			if (NpcLabels[NNeed])
			{
				FString Name = C->NpcId.IsNone() ? FString(TEXT("Klant")) : C->NpcId.ToString();
				if (Reg && !C->NpcId.IsNone()) { float r, l, a; FText Nm; if (Reg->GetStats(C->NpcId, r, l, a, Nm)) { Name = Nm.ToString(); } }
				NpcLabels[NNeed]->SetText(FText::FromString(Name));
				if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(NpcLabels[NNeed]->Slot)) { Cs->SetPosition(Pos + FVector2D(0.f, -16.f)); }
				NpcLabels[NNeed]->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			++NNeed;
		}
		else
		{
			// Gewone roamer = klein cyaan puntje (geen label).
			while (NpcDots.Num() <= NRoam) { NpcDots.Add(AddDot(FLinearColor(0.25f, 0.45f, 1.f), 10.f, 18)); }
			if (UBorder* Dot = NpcDots[NRoam])
			{
				if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(Dot->Slot)) { Cs->SetPosition(Pos); }
				Dot->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			++NRoam;
		}
	}
	// Overtollige markers verbergen.
	for (int32 k = NRoam; k < NpcDots.Num(); ++k) { if (NpcDots[k]) { NpcDots[k]->SetVisibility(ESlateVisibility::Collapsed); } }
	for (int32 k = NNeed; k < NeedIcons.Num(); ++k) { if (NeedIcons[k]) { NeedIcons[k]->SetVisibility(ESlateVisibility::Collapsed); } }
	for (int32 k = NNeed; k < NpcLabels.Num(); ++k) { if (NpcLabels[k]) { NpcLabels[k]->SetVisibility(ESlateVisibility::Collapsed); } }
}
