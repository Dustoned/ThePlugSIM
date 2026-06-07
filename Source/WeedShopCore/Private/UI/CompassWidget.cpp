#include "UI/CompassWidget.h"

#include "UI/WeedUiStyle.h"
#include "Customer/CustomerBase.h"
#include "Customer/CustomerSpawner.h"
#include "Phone/PhoneClientComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"

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

	// Achtergrondbalk bovenaan, gecentreerd.
	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CompassBg"));
	Bg->SetBrush(WeedUI::Rounded(FLinearColor(0.04f, 0.05f, 0.07f, 0.7f), 10.f));
	Bg->SetVisibility(ESlateVisibility::HitTestInvisible);
	Bg->SetClipping(EWidgetClipping::ClipToBounds);
	UCanvasPanelSlot* BgS = Root->AddChildToCanvas(Bg);
	BgS->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
	BgS->SetAlignment(FVector2D(0.5f, 0.f));
	BgS->SetAutoSize(false);
	BgS->SetSize(FVector2D(BandW, 34.f));
	BgS->SetPosition(FVector2D(0.f, 12.f));

	Band = WidgetTree->ConstructWidget<UCanvasPanel>();
	Band->SetVisibility(ESlateVisibility::HitTestInvisible);
	Bg->SetContent(Band);

	// Middenstreepje (jouw kijkrichting).
	UBorder* Center = WidgetTree->ConstructWidget<UBorder>();
	Center->SetBrush(WeedUI::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.85f), 1.f));
	Center->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* CenS = Band->AddChildToCanvas(Center);
	CenS->SetAutoSize(false); CenS->SetSize(FVector2D(2.f, 30.f));
	CenS->SetAlignment(FVector2D(0.5f, 0.5f)); CenS->SetPosition(FVector2D(BandW * 0.5f, 17.f));

	// Windstreken.
	static const TCHAR* Names[8] = { TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW") };
	for (int32 i = 0; i < 8; ++i)
	{
		UTextBlock* T = WeedUI::Text(WidgetTree, Names[i], (i % 2 == 0) ? 14 : 10, (i % 2 == 0) ? FLinearColor::White : FLinearColor(0.6f, 0.65f, 0.75f), true);
		Band->AddChildToCanvas(T);
		CardinalLabels.Add(T);
		CardinalYaws.Add(i * 45.f);
	}

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

	// Marker-pool voor mede-spelers (blauw poppetje).
	for (int32 i = 0; i < 4; ++i)
	{
		USizeBox* CB = WidgetTree->ConstructWidget<USizeBox>();
		CB->SetWidthOverride(18.f); CB->SetHeightOverride(18.f);
		CB->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Person, 18.f, FLinearColor(0.3f, 0.6f, 1.f)));
		CB->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* CS = Band->AddChildToCanvas(CB);
		CS->SetAutoSize(false); CS->SetSize(FVector2D(18.f, 18.f)); CS->SetAlignment(FVector2D(0.5f, 0.5f));
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

	// Windstreken.
	for (int32 i = 0; i < CardinalLabels.Num(); ++i)
	{
		const float Rel = FRotator::NormalizeAxis(CardinalYaws[i] - PlayerYaw);
		PlaceOnBand(CardinalLabels[i], Rel, 11.f);
	}

	// Alléén een poppetje voor klanten die je NU nodig hebt (afspraak / staat te wachten). Gewone
	// roamende NPC's staan niet op de kompas (wel als gekleurde puntjes op de kaart).
	int32 m = 0;
	for (TActorIterator<ACustomerBase> It(GetWorld()); It && m < Markers.Num(); ++It)
	{
		if (!IsValid(*It) || !It->bNeedsPlayer) { continue; }
		const FVector D = It->GetActorLocation() - PL;
		if (D.SizeSquared2D() < 100.f) { continue; }
		const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		const float Rel = FRotator::NormalizeAxis(Bearing - PlayerYaw);
		PlaceOnBand(Markers[m], Rel, 22.f);
		++m;
	}
	for (; m < Markers.Num(); ++m) { Markers[m]->SetVisibility(ESlateVisibility::Collapsed); }

	// Mede-spelers (blauw poppetje) -> zie waar je co-op maatje is.
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
			PlaceOnBand(CoopMarkers[cm], FRotator::NormalizeAxis(Bearing - PlayerYaw), 22.f);
			++cm;
		}
	}
	for (; cm < CoopMarkers.Num(); ++cm) { CoopMarkers[cm]->SetVisibility(ESlateVisibility::Collapsed); }

	// Home = JOUW woning (gekocht/starter). Alleen tonen als je er een hebt — niet het park-centrum.
	bool bHaveHome = false;
	if (APawn* OwnerPawn = GetOwningPlayerPawn())
	{
		if (UPhoneClientComponent* Ph = OwnerPawn->FindComponentByClass<UPhoneClientComponent>())
		{
			bHaveHome = Ph->GetActiveHomeLocation(HomeWorld);
		}
	}
	if (HomeMarker)
	{
		if (bHaveHome)
		{
			const FVector D = HomeWorld - PL;
			const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			PlaceOnBand(HomeMarker, FRotator::NormalizeAxis(Bearing - PlayerYaw), 22.f);
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
			PlaceOnBand(WaypointMarker, FRotator::NormalizeAxis(Bearing - PlayerYaw), 22.f);
		}
		else
		{
			WaypointMarker->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
