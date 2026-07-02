// UCompassWidget — strakke kompasbalk bovenaan (UMG): N/O/Z/W draaien mee met je kijkrichting,
// mensen die buiten lopen verschijnen als stipjes, en later kunnen waypoints worden toegevoegd.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CompassWidget.generated.h"

class UCanvasPanel;
class UBorder;
class UTextBlock;

UCLASS()
class WEEDSHOPCORE_API UCompassWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Optioneel doel-waypoint (wereldlocatie). Zet bRelevant=false om te verbergen. Voor later gebruik.
	void SetWaypoint(const FVector& World, bool bActive) { WaypointWorld = World; bHasWaypoint = bActive; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void PlaceOnBand(class UWidget* W, float RelAngleDeg, float Y);

	UPROPERTY() TObjectPtr<UCanvasPanel> Band;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> CardinalLabels;
	UPROPERTY() TArray<TObjectPtr<UWidget>> Markers;       // mensen buiten (groen poppetje)
	UPROPERTY() TArray<TObjectPtr<UWidget>> CoopMarkers;   // mede-spelers (blauw poppetje)
	UPROPERTY() TArray<TObjectPtr<UWidget>> DeliveryMarkers; // bezorgingen (oranje pakket-icoon)
	UPROPERTY() TObjectPtr<UWidget> HomeMarker;            // je basis (goud huisje)
	UPROPERTY() TObjectPtr<UBorder> WaypointMarker;        // generiek waypoint (blauw)

	TArray<float> CardinalYaws;
	FVector WaypointWorld = FVector::ZeroVector;
	bool bHasWaypoint = false;
	FVector HomeWorld = FVector::ZeroVector;
	bool bHomeFound = false;

	// Perf: klant-SET + home-locatie worden elke 0.25s ververst; bearing/PlaceOnBand blijft per tick.
	TArray<TWeakObjectPtr<class ACustomerBase>> CachedCustomers;
	float CustomerCacheAge = 1000.f;
	TWeakObjectPtr<class UPhoneClientComponent> CachedPhone;
	TWeakObjectPtr<APawn> CachedPhonePawn;
	bool bCachedHaveHome = false;
	float HomeCacheAge = 1000.f;

	static constexpr float BandW = 540.f;
	static constexpr float HalfFov = 90.f; // toont 180 graden over de balk
};
