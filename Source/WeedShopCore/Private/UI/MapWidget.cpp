#include "UI/MapWidget.h"
#include "World/CityDoor.h" // FriendlyNpcName fallback

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
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/WeedUiStyle.h"
#include "Styling/CoreStyle.h"
#include "World/CityGenerator.h"
#include "World/DoorRetrofitter.h"
#include "Customer/CustomerBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Game/WeedShopGameState.h"
#include "Npc/NpcRegistryComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "EngineUtils.h"

namespace
{
	constexpr float GMapDS = 1000.f;  // ontwerp-grootte van het kaartvlak (px) — groot genoeg dat alle nummers passen
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

	// Rechtermuisknop: slepen = kaart pannen; klik zonder slepen = waypoint wissen (op mouse-up).
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		bPanning = true;
		bDragged = false;
		LastDragLocal = Canvas ? Canvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()) : FVector2D::ZeroVector;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	// Links = waypoint op de aangeklikte plek (klik-pixel -> kaart-lokaal -> wereld).
	const FVector2D Local = Canvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	// Alleen binnen het kaartvlak: klikken op het zijpaneel zet geen waypoint.
	if (Local.X < 0.f || Local.Y < 0.f || Local.X > GMapDS || Local.Y > GMapDS) { return FReply::Unhandled(); }
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

UWidget* UMapWidget::AddPill(const FString& T, FVector2D Pos, int32 Size, const FLinearColor& TextCol, int32 ZOrder)
{
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrush(WeedUI::Rounded(FLinearColor(0.01f, 0.02f, 0.03f, 0.8f), 4.f));
	B->SetPadding(FMargin(5.f, 1.f, 5.f, 2.f));
	UTextBlock* Tb = WidgetTree->ConstructWidget<UTextBlock>();
	Tb->SetText(FText::FromString(T));
	Tb->SetFont(WeedUI::Font(Size, true));
	Tb->SetColorAndOpacity(FSlateColor(TextCol));
	Tb->SetJustification(ETextJustify::Center);
	B->SetContent(Tb);
	UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(B);
	Cs->SetAutoSize(true);
	Cs->SetAlignment(FVector2D(0.5f, 0.5f));
	Cs->SetPosition(Pos);
	Cs->SetZOrder(ZOrder);
	B->SetVisibility(ESlateVisibility::HitTestInvisible);
	return B;
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

UWidget* UMapWidget::AddPersonIcon(const FLinearColor& Tint, float Sz, int32 ZOrder)
{
	USizeBox* SB = WidgetTree->ConstructWidget<USizeBox>();
	SB->SetWidthOverride(Sz); SB->SetHeightOverride(Sz);
	SB->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Person, Sz, Tint));
	UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(SB);
	Cs->SetAutoSize(false);
	Cs->SetSize(FVector2D(Sz, Sz));
	Cs->SetAlignment(FVector2D(0.5f, 0.5f));
	Cs->SetZOrder(ZOrder);
	return SB;
}

TSharedRef<SWidget> UMapWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MapRoot"));
		WidgetTree->RootWidget = Root;

		// Hele widget hit-testbaar -> een klik op de kaart bereikt NativeOnMouseButtonDown (waypoint zetten).
		SetVisibility(ESlateVisibility::Visible);

		// Donkere achtergrond (dekkend -> HUD erachter komt er niet doorheen).
		UBorder* Bg = WidgetTree->ConstructWidget<UBorder>();
		Bg->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.07f, 1.f));
		Root->AddChildToOverlay(Bg);

		// Hoofd-layout: kaart links/midden, info-zijpaneel rechts. Zo staat er NIETS over de kaart heen.
		UHorizontalBox* Main = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (UOverlaySlot* MOS = Root->AddChildToOverlay(Main))
		{
			MOS->SetHorizontalAlignment(HAlign_Fill);
			MOS->SetVerticalAlignment(VAlign_Fill);
			MOS->SetPadding(FMargin(24.f, 18.f));
		}

		// Vaste-grootte kaartvlak, geschaald naar de beschikbare ruimte (vierkant).
		UScaleBox* SB = WidgetTree->ConstructWidget<UScaleBox>();
		SB->SetStretch(EStretch::ScaleToFit);
		if (UHorizontalBoxSlot* MapSlot = Main->AddChildToHorizontalBox(SB))
		{
			MapSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			MapSlot->SetHorizontalAlignment(HAlign_Center);
			MapSlot->SetVerticalAlignment(VAlign_Center);
		}
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(GMapDS);
		Box->SetHeightOverride(GMapDS);
		SB->AddChild(Box);

		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MapCanvas"));
		Canvas->SetClipping(EWidgetClipping::ClipToBounds); // nummers/markers blijven binnen het kaartvlak
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

		// --- Info-zijpaneel rechts (naast de kaart, niet erover) ---
		UBorder* Side = WidgetTree->ConstructWidget<UBorder>();
		Side->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.08f, 0.11f, 0.97f), 12.f));
		Side->SetPadding(FMargin(16.f, 14.f));
		if (UHorizontalBoxSlot* SS = Main->AddChildToHorizontalBox(Side))
		{
			SS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			SS->SetVerticalAlignment(VAlign_Center);
			SS->SetPadding(FMargin(18.f, 0.f, 0.f, 0.f));
		}
		USizeBox* SideW = WidgetTree->ConstructWidget<USizeBox>();
		SideW->SetWidthOverride(240.f);
		Side->SetContent(SideW);
		UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
		SideW->SetContent(Info);

		auto AddInfo = [&](const FString& T, int32 Size, const FLinearColor& Col, float TopPad)
		{
			UTextBlock* Tb = WidgetTree->ConstructWidget<UTextBlock>();
			Tb->SetText(FText::FromString(T));
			Tb->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", Size));
			Tb->SetColorAndOpacity(FSlateColor(Col));
			Tb->SetAutoWrapText(true);
			if (UVerticalBoxSlot* Vs = Info->AddChildToVerticalBox(Tb)) { Vs->SetPadding(FMargin(0.f, TopPad, 0.f, 0.f)); }
		};
		AddInfo(TEXT("CITY - MAP"), 22, FLinearColor(0.8f, 0.9f, 1.f), 0.f);
		AddInfo(TEXT("Noord is boven"), 12, FLinearColor(0.6f, 0.65f, 0.75f), 6.f);
		AddInfo(TEXT("Klik = waypoint zetten"), 13, FLinearColor(0.75f, 0.85f, 1.f), 22.f);
		AddInfo(TEXT("Rechtsklik = waypoint wissen"), 13, FLinearColor(0.75f, 0.85f, 1.f), 3.f);
		AddInfo(TEXT("M = kaart sluiten"), 12, FLinearColor(0.6f, 0.65f, 0.75f), 3.f);
		AddInfo(TEXT("Legenda"), 14, FLinearColor(0.85f, 0.9f, 1.f), 24.f);
		AddInfo(TEXT("cyan dot = you"), 12, FLinearColor(0.4f, 0.9f, 1.f), 6.f);
		AddInfo(TEXT("geel = waypoint"), 12, FLinearColor(1.f, 0.85f, 0.3f), 3.f);
		AddInfo(TEXT("blauw = NPC"), 12, FLinearColor(0.45f, 0.6f, 1.f), 3.f);
		AddInfo(TEXT("green figure = customer for you"), 12, FLinearColor(0.5f, 1.f, 0.6f), 3.f);
		AddInfo(TEXT("gold house = your home"), 12, FLinearColor(1.f, 0.82f, 0.3f), 3.f);
	}
	return Super::RebuildWidget();
}

void UMapWidget::BuildBlocks()
{
	if (!Canvas) { return; }

	// Pack-map (geen CityGenerator): kale top-down kaart via de DoorRetrofitter-capture - geen
	// blok-labels/huisnummers, wel speler/waypoint/NPC-dots + klik-voor-waypoint.
	if (!City.IsValid() && PackMap.IsValid())
	{
		CenterXY = PackMap->GetMapCenter();
		Scale = GMapDS / FMath::Max(1.f, PackMap->GetMapOrthoWidth());
		// Zoombaar: standaard DICHT op de speler (view ~3,5km breed), scrollwiel tot de hele ring.
		MapCenterFull = CenterXY;
		Scale0 = Scale;
		bZoomable = true;
		Zoom = FMath::Clamp((GMapDS / Scale0) / 350000.f * 20.f, 6.f, 40.f);
		if (Canvas) { Canvas->SetClipping(EWidgetClipping::ClipToBounds); }
		PackMap->CaptureMapNow();
		PlayerDot = AddPersonIcon(FLinearColor(0.2f, 0.9f, 1.f), 24.f, 26); // jij: cyaan poppetje
		WaypointDot = AddDot(FLinearColor(1.f, 0.85f, 0.15f), 18.f, 27);
		if (WaypointDot) { WaypointDot->SetVisibility(ESlateVisibility::Collapsed); }
		bBuiltBlocks = true;
		return;
	}
	if (!City.IsValid()) { return; }

	const FVector C = City->GetCityCenter();
	CenterXY = FVector2D(C.X, C.Y);
	// Schaal afgestemd op de top-down render: het 760px-vlak = de OrthoWidth-brede wereldzone.
	const float Ortho = FMath::Max(1.f, City->GetMapOrthoWidth());
	Scale = GMapDS / Ortho;

	// Bloklabels: winkelnaam (geel), park (groen) of flat-reeks "32 1-20" (wit). Rijtjeshuizen worden
	// hieronder apart getekend (oriëntatie-bewust: nummers naast OF onder elkaar).
	TArray<FCityMapBlock> Blocks;
	City->GetMapBlocks(Blocks);
	for (const FCityMapBlock& Bk : Blocks)
	{
		const bool bRowBlock = !Bk.bShop && Bk.Label != TEXT("Park") && !Bk.Label.Contains(TEXT("-"));
		if (bRowBlock) { continue; } // rijtjeshuizen -> oriëntatie-bewuste chip hieronder
		const FVector2D P = WorldToCanvas(Bk.Center.X, Bk.Center.Y);

		if (Bk.bShop)
		{
			// Winkel = passend icoontje op het blok (GEEN nummer), met de naam eronder.
			WeedUI::EIcon Ico = WeedUI::EIcon::Shop;
			FLinearColor Tint(1.f, 0.86f, 0.35f);
			if (Bk.Label == TEXT("GAS"))            { Ico = WeedUI::EIcon::Flame; Tint = FLinearColor(1.f, 0.45f, 0.35f); }
			else if (Bk.Label == TEXT("GROW"))      { Ico = WeedUI::EIcon::Leaf;  Tint = FLinearColor(0.5f, 1.f, 0.5f); }
			else if (Bk.Label == TEXT("FURNITURE")) { Ico = WeedUI::EIcon::Home;  Tint = FLinearColor(0.8f, 0.65f, 1.f); }
			else if (Bk.Label == TEXT("SUPPLIES"))  { Ico = WeedUI::EIcon::Shop;  Tint = FLinearColor(0.5f, 0.78f, 1.f); }
			if (UWidget* IcoW = WeedUI::Icon(WidgetTree, Ico, 34.f, Tint))
			{
				if (UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(IcoW))
				{
					Cs->SetAutoSize(false); Cs->SetSize(FVector2D(34.f, 34.f));
					Cs->SetAlignment(FVector2D(0.5f, 0.5f)); Cs->SetPosition(P + FVector2D(0.f, -10.f)); Cs->SetZOrder(24);
				}
				IcoW->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			AddPill(Bk.Label, P + FVector2D(0.f, 16.f), 11, Tint, 24);
		}
		else
		{
			const FLinearColor Col = (Bk.Label == TEXT("Park")) ? FLinearColor(0.55f, 1.f, 0.6f) : FLinearColor(1.f, 1.f, 0.96f);
			AddPill(Bk.Label, P, 11, Col, 24);
		}
	}

	// Rijtjeshuizen: groepeer de ECHTE huizen per blok, bepaal de rij-oriëntatie uit hun posities en
	// toon één nette chip met de nummers NAAST elkaar (rij langs X) of ONDER elkaar (rij langs Y),
	// zodat het aansluit op hoe de huisjes op de kaart staan.
	{
		const float Pitch = FMath::Max(1.f, City->GetPitch());
		// Posities meteen naar CANVAS-ruimte (de kaart-render draait de assen), zodat de oriëntatie klopt
		// met wat je op het scherm ziet i.p.v. met de wereld-assen.
		struct FRowGrp { TArray<TPair<FVector2D, FString>> Items; };
		TMap<FIntPoint, FRowGrp> Groups;
		for (const FApartmentHome& H : City->GetApartmentHomes())
		{
			if (H.bApartment || H.Number.IsEmpty() || H.Number.Contains(TEXT("-"))) { continue; }
			const FIntPoint Key(FMath::RoundToInt((H.InteriorPos.X - C.X) / Pitch),
								FMath::RoundToInt((H.InteriorPos.Y - C.Y) / Pitch));
			const FVector2D Cv = WorldToCanvas(H.InteriorPos.X, H.InteriorPos.Y);
			Groups.FindOrAdd(Key).Items.Add(TPair<FVector2D, FString>(Cv, H.Number));
		}
		for (TPair<FIntPoint, FRowGrp>& GP : Groups)
		{
			TArray<TPair<FVector2D, FString>>& It = GP.Value.Items;
			if (It.Num() == 0) { continue; }
			FVector2D Lo = It[0].Key, Hi = It[0].Key, Sum(0.f, 0.f);
			for (const TPair<FVector2D, FString>& E : It)
			{
				Lo.X = FMath::Min(Lo.X, E.Key.X); Lo.Y = FMath::Min(Lo.Y, E.Key.Y);
				Hi.X = FMath::Max(Hi.X, E.Key.X); Hi.Y = FMath::Max(Hi.Y, E.Key.Y);
				Sum += E.Key;
			}
			// Spreiding in SCHERM-Y groter dan in scherm-X -> huisjes staan onder elkaar -> nummers stacken.
			const bool bVertical = (Hi.Y - Lo.Y) > (Hi.X - Lo.X);
			It.Sort([bVertical](const TPair<FVector2D, FString>& A, const TPair<FVector2D, FString>& B)
				{ return bVertical ? (A.Key.Y < B.Key.Y) : (A.Key.X < B.Key.X); });
			FString Lbl;
			for (int32 i = 0; i < It.Num(); ++i) { if (i > 0) { Lbl += bVertical ? TEXT("\n") : TEXT(" "); } Lbl += It[i].Value; }
			AddPill(Lbl, Sum / It.Num(), 9, FLinearColor(1.f, 1.f, 0.96f), 24);
		}
	}

	City->CaptureMapNow(); // verse top-down render zodra de kaart opent

	// Speler-marker (boven alles) + waypoint-marker (geel, verborgen tot je 'm zet).
	PlayerDot = AddPersonIcon(FLinearColor(0.2f, 0.9f, 1.f), 24.f, 26); // jij: cyaan poppetje
	WaypointDot = AddDot(FLinearColor(1.f, 0.85f, 0.15f), 18.f, 27);
	if (WaypointDot) { WaypointDot->SetVisibility(ESlateVisibility::Collapsed); }
	// (Titel/uitleg/legenda staan in het zijpaneel rechts, niet meer over de kaart.)
	// Goud huisje op JOUW woning (alleen zichtbaar als je er een bezit).
	{
		USizeBox* HB = WidgetTree->ConstructWidget<USizeBox>();
		HB->SetWidthOverride(22.f); HB->SetHeightOverride(22.f);
		HB->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Home, 22.f, FLinearColor(1.f, 0.82f, 0.25f)));
		if (UCanvasPanelSlot* Cs = Canvas->AddChildToCanvas(HB))
		{
			Cs->SetAutoSize(false); Cs->SetSize(FVector2D(22.f, 22.f)); Cs->SetAlignment(FVector2D(0.5f, 0.5f)); Cs->SetZOrder(28);
		}
		HB->SetVisibility(ESlateVisibility::Collapsed);
		HomeIcon = HB;
	}
	bBuiltBlocks = true;
}

FReply UMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bPanning || !Canvas || Scale <= 0.f) { return FReply::Unhandled(); }
	const FVector2D Local = Canvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D D = Local - LastDragLocal;
	if (!D.IsNearlyZero())
	{
		if (D.Size() > 3.f) { bDragged = true; }
		if (!bManualPan) { bManualPan = true; PanCenter = CenterXY; }
		// canvas-x = wereld +Y, canvas-y = wereld -X; slepen verschuift de view tegengesteld.
		PanCenter.Y -= D.X / Scale;
		PanCenter.X += D.Y / Scale;
		LastDragLocal = Local;
	}
	return FReply::Handled();
}

FReply UMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && bPanning)
	{
		bPanning = false;
		if (!bDragged)
		{
			if (UPhoneClientComponent* Ph = GetPhone()) { Ph->ClearWaypoint(); }
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void UMapWidget::UpdateView()
{
	if (!bZoomable || !MapImage || !MapImage->Slot) { return; }
	// View-centrum = de speler, geklemd zodat de view binnen de kaart blijft.
	FVector2D VC = MapCenterFull;
	if (bManualPan) { VC = PanCenter; }
	else if (APawn* P = GetOwningPlayerPawn()) { VC = FVector2D(P->GetActorLocation().X, P->GetActorLocation().Y); }
	const float HalfWorld = (GMapDS / FMath::Max(0.0001f, Scale0)) * 0.5f;
	const float HalfView = HalfWorld / FMath::Max(1.f, Zoom);
	VC.X = FMath::Clamp(VC.X, MapCenterFull.X - (HalfWorld - HalfView), MapCenterFull.X + (HalfWorld - HalfView));
	VC.Y = FMath::Clamp(VC.Y, MapCenterFull.Y - (HalfWorld - HalfView), MapCenterFull.Y + (HalfWorld - HalfView));
	if (bManualPan) { PanCenter = VC; } // geklemd terugschrijven
	CenterXY = VC;
	Scale = Scale0 * Zoom;
	// Kaart-afbeelding schalen en schuiven zodat VC in het canvas-midden ligt.
	const float rx = VC.X - MapCenterFull.X;
	const float ry = VC.Y - MapCenterFull.Y;
	const FVector2D PFull(GMapDS * 0.5f + ry * Scale0, GMapDS * 0.5f - rx * Scale0);
	if (UCanvasPanelSlot* Is = Cast<UCanvasPanelSlot>(MapImage->Slot))
	{
		Is->SetSize(FVector2D(GMapDS * Zoom, GMapDS * Zoom));
		Is->SetPosition(FVector2D(GMapDS * 0.5f, GMapDS * 0.5f) - PFull * Zoom);
	}
}

FReply UMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bZoomable) { return FReply::Unhandled(); }
	Zoom = FMath::Clamp(Zoom * (InMouseEvent.GetWheelDelta() > 0.f ? 1.3f : 0.77f), 1.f, 60.f);
	return FReply::Handled();
}

void UMapWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	const bool bVisNow = IsVisible();
	if (bVisNow && !bWasVisible) { bManualPan = false; bPanning = false; } // openen = centreren op de speler
	bWasVisible = bVisNow;
	UpdateView();
	Super::NativeTick(MyGeometry, DeltaTime);

	if (!City.IsValid())
	{
		for (TActorIterator<ACityGenerator> It(GetWorld()); It; ++It) { City = *It; break; }
	}
	if (!City.IsValid() && !PackMap.IsValid())
	{
		for (TActorIterator<ADoorRetrofitter> It(GetWorld()); It; ++It) { PackMap = *It; break; }
	}
	if (!bBuiltBlocks && (City.IsValid() || PackMap.IsValid())) { BuildBlocks(); }
	if (!bBuiltBlocks || !Canvas) { return; }

	// Echte top-down render dekkend tonen: M_MapDisplay (UI/opaque) sampelt de SceneCapture-RT.
	// De BaseColor-capture heeft alpha=0; dit materiaal negeert die alpha -> geen doorzichtige kaart.
	if (!bImageSet && MapImage && (City.IsValid() || PackMap.IsValid()))
	{
		UTextureRenderTarget2D* RT = City.IsValid() ? City->GetMapRenderTarget()
			: (PackMap.IsValid() ? PackMap->GetMapRenderTarget() : nullptr);
		if (RT)
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
	const APawn* LocalPawn = GetOwningPlayerPawn();
	if (PlayerDot)
	{
		if (LocalPawn)
		{
			const FVector L = LocalPawn->GetActorLocation();
			if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(PlayerDot->Slot)) { Cs->SetPosition(WorldToCanvas(L.X, L.Y)); }
			PlayerDot->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}

	// Mede-spelers (blauw) -> zie waar je co-op maatje is.
	{
		int32 Used = 0;
		if (const AGameStateBase* GSb = GetWorld() ? GetWorld()->GetGameState() : nullptr)
		{
			for (APlayerState* PS : GSb->PlayerArray)
			{
				const APawn* Pw = PS ? PS->GetPawn() : nullptr;
				if (!Pw || Pw == LocalPawn) { continue; }
				while (CoopDots.Num() <= Used) { CoopDots.Add(AddPersonIcon(FLinearColor(0.3f, 0.55f, 1.f), 24.f, 26)); } // co-op maatje: blauw poppetje
				if (UWidget* Dot = CoopDots[Used])
				{
					const FVector L = Pw->GetActorLocation();
					if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(Dot->Slot)) { Cs->SetPosition(WorldToCanvas(L.X, L.Y)); }
					Dot->SetVisibility(ESlateVisibility::HitTestInvisible);
				}
				++Used;
			}
		}
		for (int32 k = Used; k < CoopDots.Num(); ++k) { if (CoopDots[k]) { CoopDots[k]->SetVisibility(ESlateVisibility::Collapsed); } }
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

	// Home-icoon op je eigen woning (verborgen als je er geen hebt).
	if (HomeIcon)
	{
		FVector HW;
		UPhoneClientComponent* Ph = GetPhone();
		if (Ph && Ph->GetActiveHomeLocation(HW))
		{
			if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(HomeIcon->Slot)) { Cs->SetPosition(WorldToCanvas(HW.X, HW.Y)); }
			HomeIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else { HomeIcon->SetVisibility(ESlateVisibility::Collapsed); }
	}

	// Klanten/NPC's verzamelen.
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr;

	int32 NRoam = 0, NNeed = 0;
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		ACustomerBase* C = *It;
		if (!IsValid(C)) { continue; }
		// Bewoners in binnen-/home-transitie niet als straat-NPC tonen: anders lijken ze vast te staan op woonblokken.
		if (!C->ShouldShowOnCityMap()) { continue; }

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
				FString Name = ACityDoor::FriendlyNpcName(C->NpcId);
				if (Reg && !C->NpcId.IsNone()) { float r, l, a; FText Nm; if (Reg->GetStats(C->NpcId, r, l, a, Nm) && !Nm.IsEmpty()) { Name = Nm.ToString(); } }
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
