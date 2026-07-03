#include "UI/CompassWidget.h"

#include "UI/WeedUiStyle.h"
#include "Customer/CustomerBase.h"
#include "Customer/CustomerSpawner.h"
#include "Phone/PhoneClientComponent.h"
#include "Game/WeedShopGameState.h"
#include "Save/SaveGameSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"

namespace
{
	// Fel poppetje MET donkere contrast-rand (goedkope outline): een iets groter bijna-zwart poppetje
	// eronder + het felle poppetje erbovenop, gecentreerd in een SizeBox. Zo springt de speler-marker
	// eruit tegen zowel een donkere als lichte kaart-achtergrond. Retourneert een UWidget-container die
	// als geheel in een canvas-slot geplaatst/verborgen kan worden (bestaande pool-logica blijft werken).
	UWidget* MakeOutlinedCompassPersonMarker(UWidgetTree* Tree, float Size, const FLinearColor& Bright)
	{
		const float Outline = Size + 5.f; // donkere rand steekt ~2,5px rondom uit
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(Outline); Box->SetHeightOverride(Outline);
		UOverlay* Ov = Tree->ConstructWidget<UOverlay>();
		Box->SetContent(Ov);
		// Donkere rand (onder): bijna-zwart, iets groter -> vormt een contrast-halo rond het felle poppetje.
		if (UOverlaySlot* OS = Ov->AddChildToOverlay(WeedUI::Icon(Tree, WeedUI::EIcon::Person, Outline, FLinearColor(0.02f, 0.02f, 0.02f, 0.95f))))
		{
			OS->SetHorizontalAlignment(HAlign_Center); OS->SetVerticalAlignment(VAlign_Center);
		}
		// Fel poppetje (boven).
		if (UOverlaySlot* OS = Ov->AddChildToOverlay(WeedUI::Icon(Tree, WeedUI::EIcon::Person, Size, Bright)))
		{
			OS->SetHorizontalAlignment(HAlign_Center); OS->SetVerticalAlignment(VAlign_Center);
		}
		Box->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Box;
	}
}

TSharedRef<SWidget> UCompassWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UCompassWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Kader weg (D.14): volledig transparante brush; UBorder blijft als onzichtbare clip-container
	// zodat markers binnen de band-breedte geclipt blijven.
	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CompassBg"));
	Bg->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.f), 12.f));
	Bg->SetVisibility(ESlateVisibility::HitTestInvisible);
	Bg->SetClipping(EWidgetClipping::ClipToBounds);
	UCanvasPanelSlot* BgS = Root->AddChildToCanvas(Bg);
	BgS->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
	BgS->SetAlignment(FVector2D(0.5f, 0.f));
	BgS->SetAutoSize(false);
	BgS->SetSize(FVector2D(BandW, 42.f));
	BgS->SetPosition(FVector2D(0.f, 12.f));

	Band = WidgetTree->ConstructWidget<UCanvasPanel>();
	Band->SetVisibility(ESlateVisibility::HitTestInvisible);
	Bg->SetContent(Band);

	// Middenstreepje (jouw kijkrichting).
	UBorder* Center = WidgetTree->ConstructWidget<UBorder>();
	Center->SetBrush(WeedUI::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.85f), 1.f));
	Center->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* CenS = Band->AddChildToCanvas(Center);
	CenS->SetAutoSize(false); CenS->SetSize(FVector2D(2.f, 38.f));
	CenS->SetAlignment(FVector2D(0.5f, 0.5f)); CenS->SetPosition(FVector2D(BandW * 0.5f, 21.f));

	// Windstreek-letters weg (D.14): alleen het midden-streepje + de markers blijven.

	// Marker-pool voor mensen buiten: een persoon-icoontje (groen), duidelijk anders dan objecten.
	for (int32 i = 0; i < 24; ++i)
	{
		USizeBox* MS2 = WidgetTree->ConstructWidget<USizeBox>();
		MS2->SetWidthOverride(16.f); MS2->SetHeightOverride(16.f);
		MS2->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Person, 16.f, FLinearColor(0.4f, 0.95f, 0.5f)));
		MS2->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* MS = Band->AddChildToCanvas(MS2);
		MS->SetAutoSize(false); MS->SetSize(FVector2D(16.f, 16.f)); MS->SetAlignment(FVector2D(0.5f, 0.5f));
		Markers.Add(MS2);
	}

	// Marker-pool voor mede-spelers: fel CYAAN poppetje MET donkere contrast-rand, groter dan de NPC-markers.
	// Cyaan (0,1,1) verschilt maximaal van de blauwe NPC-stippen op de kaart en van het groene klant-poppetje;
	// de donkere rand houdt 'm zichtbaar tegen zowel een lichte als donkere kaart-achtergrond.
	for (int32 i = 0; i < 4; ++i)
	{
		UWidget* CB = MakeOutlinedCompassPersonMarker(WidgetTree, 26.f, FLinearColor(0.f, 1.f, 1.f));
		CB->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* CS = Band->AddChildToCanvas(CB);
		CS->SetAutoSize(false); CS->SetSize(FVector2D(31.f, 31.f)); CS->SetAlignment(FVector2D(0.5f, 0.5f));
		CoopMarkers.Add(CB);
	}

	// Home-marker: goud huisje dat naar je basis wijst.
	{
		USizeBox* Hs = WidgetTree->ConstructWidget<USizeBox>();
		Hs->SetWidthOverride(18.f); Hs->SetHeightOverride(18.f);
		Hs->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Home, 18.f, FLinearColor(1.f, 0.82f, 0.25f)));
		Hs->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* HMS = Band->AddChildToCanvas(Hs);
		HMS->SetAutoSize(false); HMS->SetSize(FVector2D(18.f, 18.f)); HMS->SetAlignment(FVector2D(0.5f, 0.5f));
		HomeMarker = Hs;
	}

	// Waypoint-marker (later).
	WaypointMarker = WidgetTree->ConstructWidget<UBorder>();
	WaypointMarker->SetBrush(WeedUI::Rounded(FLinearColor(0.3f, 0.8f, 1.f, 0.98f), 3.f));
	WaypointMarker->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* WS = Band->AddChildToCanvas(WaypointMarker);
	WS->SetAutoSize(false); WS->SetSize(FVector2D(10.f, 14.f)); WS->SetAlignment(FVector2D(0.5f, 0.5f));

	// Bezorg-markers: oranje pakket-icoontje dat naar de voordeur-bezorging wijst (max 4 tegelijk).
	for (int32 i = 0; i < 4; ++i)
	{
		USizeBox* DB = WidgetTree->ConstructWidget<USizeBox>();
		DB->SetWidthOverride(18.f); DB->SetHeightOverride(18.f);
		DB->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Box, 18.f, FLinearColor(1.f, 0.6f, 0.15f)));
		DB->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* DS = Band->AddChildToCanvas(DB);
		DS->SetAutoSize(false); DS->SetSize(FVector2D(18.f, 18.f)); DS->SetAlignment(FVector2D(0.5f, 0.5f));
		DeliveryMarkers.Add(DB);
	}
}

void UCompassWidget::PlaceOnBand(UWidget* W, float RelAngleDeg, float Y)
{
	if (!W) { return; }
	if (FMath::Abs(RelAngleDeg) > HalfFov)
	{
		W->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	W->SetVisibility(ESlateVisibility::HitTestInvisible);
	const float X = BandW * 0.5f + (RelAngleDeg / HalfFov) * (BandW * 0.5f);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(W->Slot))
	{
		S->SetPosition(FVector2D(X, Y));
	}
}

void UCompassWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);

	APlayerController* PC = GetOwningPlayer();
	APawn* P = GetOwningPlayerPawn();
	if (!PC || !P) { return; }

	const float PlayerYaw = PC->GetControlRotation().Yaw;
	const FVector PL = P->GetActorLocation();

	// Windstreek-letters weg (D.14) -> geen plaatsing meer nodig.

	// AllÃ©Ã©n een poppetje voor klanten die je NU nodig hebt (afspraak / staat te wachten). Gewone
	// roamende NPC's staan niet op de kompas (wel als gekleurde puntjes op de kaart).
	// Perf: de actor-SET wordt elke 0.25s herscand (nieuwe klant verschijnt max 0.25s later);
	// flags + positie + bearing worden nog steeds ELKE tick vers gelezen.
	CustomerCacheAge += DeltaTime;
	if (CustomerCacheAge >= 0.25f)
	{
		CustomerCacheAge = 0.f;
		CachedCustomers.Reset();
		for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
		{
			if (IsValid(*It)) { CachedCustomers.Add(*It); }
		}
	}
	// Competitive: alleen JOUW afspraak-NPC's op de kompas (ApptForPlayerId leeg = gedeeld/co-op of geen
	// afspraak; anders match op de lokale speler z'n stabiele id). Buiten competitive is dit ongewijzigd.
	const AWeedShopGameState* GSComp = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const bool bCompFilter = GSComp && GSComp->IsCompetitive();
	const FString MyId = bCompFilter ? USaveGameSubsystem::StablePlayerId(P) : FString();
	int32 m = 0;
	for (const TWeakObjectPtr<ACustomerBase>& WkC : CachedCustomers)
	{
		if (m >= Markers.Num()) { break; }
		ACustomerBase* It = WkC.Get();
		if (!IsValid(It) || !It->bNeedsPlayer || !It->bShowOnCityMap) { continue; }
		// Afspraak-NPC van de tegenstander niet tonen (leeg = gedeeld, altijd tonen).
		if (bCompFilter && !It->ApptForPlayerId.IsEmpty() && It->ApptForPlayerId != MyId) { continue; }
		const FVector D = It->GetActorLocation() - PL;
		if (D.SizeSquared2D() < 100.f) { continue; }
		const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		const float Rel = FRotator::NormalizeAxis(Bearing - PlayerYaw);
		PlaceOnBand(Markers[m], Rel, 26.f);
		++m;
	}
	for (; m < Markers.Num(); ++m) { Markers[m]->SetVisibility(ESlateVisibility::Collapsed); }

	// Mede-spelers (goud poppetje) -> zie waar je co-op maatje is.
	int32 cm = 0;
	if (const AGameStateBase* GSb = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		for (APlayerState* PS : GSb->PlayerArray)
		{
			if (cm >= CoopMarkers.Num()) { break; }
			const APawn* Pw = PS ? PS->GetPawn() : nullptr;
			if (!Pw || Pw == P) { continue; }
			const FVector D = Pw->GetActorLocation() - PL;
			if (D.SizeSquared2D() < 100.f) { continue; }
			const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			PlaceOnBand(CoopMarkers[cm], FRotator::NormalizeAxis(Bearing - PlayerYaw), 26.f);
			++cm;
		}
	}
	for (; cm < CoopMarkers.Num(); ++cm) { CoopMarkers[cm]->SetVisibility(ESlateVisibility::Collapsed); }

	// Home = JOUW woning (gekocht/starter). Alleen tonen als je er een hebt â€” niet het park-centrum.
	// Perf: FindComponentByClass + GetActiveHomeLocation (kopieert homes-array) elke 0.25s i.p.v.
	// per tick; het huis staat stil, de bearing wordt nog steeds elke tick vers berekend.
	HomeCacheAge += DeltaTime;
	if (HomeCacheAge >= 0.25f)
	{
		HomeCacheAge = 0.f;
		if (APawn* OwnerPawn = GetOwningPlayerPawn())
		{
			if (!CachedPhone.IsValid() || CachedPhonePawn.Get() != OwnerPawn)
			{
				CachedPhone = OwnerPawn->FindComponentByClass<UPhoneClientComponent>();
				CachedPhonePawn = OwnerPawn;
			}
			bCachedHaveHome = CachedPhone.IsValid() && CachedPhone->GetActiveHomeLocation(HomeWorld);
		}
		else
		{
			bCachedHaveHome = false;
		}
	}
	const bool bHaveHome = bCachedHaveHome;
	if (HomeMarker)
	{
		if (bHaveHome)
		{
			const FVector D = HomeWorld - PL;
			const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			PlaceOnBand(HomeMarker, FRotator::NormalizeAxis(Bearing - PlayerYaw), 26.f);
		}
		else { HomeMarker->SetVisibility(ESlateVisibility::Collapsed); }
	}

	// Waypoint (optioneel, later).
	if (WaypointMarker)
	{
		if (bHasWaypoint)
		{
			const FVector D = WaypointWorld - PL;
			const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			PlaceOnBand(WaypointMarker, FRotator::NormalizeAxis(Bearing - PlayerYaw), 26.f);
		}
		else
		{
			WaypointMarker->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Bezorgingen: pakket-marker richting de voordeur, altijd zichtbaar tot opgehaald. In co-op gedeeld via de
	// GameState (ook de mede-speler ziet 'm); in COMPETITIVE alleen de EIGEN marker (ForPlayerId-filter, spiegelt
	// de afspraak-filter hierboven - anders verklap je de kamer van de tegenstander). Bearing vanuit de LOKALE speler.
	int32 dm = 0;
	if (const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		const bool bDelCompFilter = GS->IsCompetitive();
		const FString MyDelId = bDelCompFilter ? USaveGameSubsystem::StablePlayerId(P) : FString();
		for (const FActiveDelivery& Del : GS->GetActiveDeliveries())
		{
			if (dm >= DeliveryMarkers.Num()) { break; }
			// Bezorging van de tegenstander niet tonen (leeg = gedeeld, altijd tonen).
			if (bDelCompFilter && !Del.ForPlayerId.IsEmpty() && Del.ForPlayerId != MyDelId) { continue; }
			const FVector D = Del.World - PL;
			const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			PlaceOnBand(DeliveryMarkers[dm], FRotator::NormalizeAxis(Bearing - PlayerYaw), 26.f);
			++dm;
		}
	}
	for (; dm < DeliveryMarkers.Num(); ++dm) { DeliveryMarkers[dm]->SetVisibility(ESlateVisibility::Collapsed); }
}
