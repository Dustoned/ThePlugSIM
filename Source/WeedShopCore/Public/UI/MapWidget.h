// UMapWidget — schematische top-down stadskaart (in C++ opgebouwd). Toont de blokken (winkels met
// naam, woonblokken met huisnummer-reeks, park), je eigen positie en alle klanten/NPC's als marker.
// Wordt zowel klein in de telefoon-Map-app getoond als fullscreen via de M-toets.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MapWidget.generated.h"

class UCanvasPanel;
class UBorder;
class UTextBlock;
class ACityGenerator;

UCLASS()
class WEEDSHOPCORE_API UMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetFullscreen(bool b) { bFullscreen = b; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;
	// Klik op de kaart = waypoint zetten (links) / wissen (rechts).
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY() TObjectPtr<class UImage> MapImage; // echte top-down render van de stad
	bool bImageSet = false;
	UPROPERTY() TObjectPtr<UWidget> PlayerDot; // poppetje (zelfde icoon als de kompas)
	UPROPERTY() TArray<TObjectPtr<UWidget>> CoopDots; // mede-spelers (blauw poppetje)
	UPROPERTY() TObjectPtr<UBorder> WaypointDot;
	UPROPERTY() TObjectPtr<class UWidget> HomeIcon; // goud huisje op JOUW woning
	UPROPERTY() TArray<TObjectPtr<UBorder>> NpcDots;        // roamers (cyaan stipjes)
	UPROPERTY() TArray<TObjectPtr<class UWidget>> ShopIcons; // winkels (geel winkel-icoon)
	UPROPERTY() TArray<TObjectPtr<class UWidget>> NeedIcons; // klanten-voor-jou (groen poppetje)
	UPROPERTY() TArray<TObjectPtr<class UWidget>> DeliveryIcons; // bezorgingen (oranje pakket-icoon bij de voordeur)
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> NpcLabels;

	FVector2D CanvasToWorld(FVector2D Local) const; // inverse van WorldToCanvas
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	class UPhoneClientComponent* GetPhone() const;

	bool bFullscreen = false;
	bool bBuiltBlocks = false;
	FVector2D CenterXY = FVector2D::ZeroVector;
	float Scale = 0.05f;
	// ZOOM (pack-map): standaard dicht op de speler, scrollwiel zoomt uit tot de hele grens-ring.
	FVector2D MapCenterFull = FVector2D::ZeroVector; // centrum van de volledige capture
	float Scale0 = 0.05f;                            // schaal bij zoom 1 (hele kaart in beeld)
	float Zoom = 1.f;
	bool bZoomable = false;
	// Pannen: RMB-slepen verschuift de view; kaart openen centreert altijd weer op de speler.
	bool bPanning = false;
	bool bDragged = false;
	bool bManualPan = false;
	bool bWasVisible = false;
	FVector2D LastDragLocal = FVector2D::ZeroVector;
	FVector2D PanCenter = FVector2D::ZeroVector;
	void UpdateView();

	TWeakObjectPtr<ACityGenerator> City;
	TWeakObjectPtr<class ADoorRetrofitter> PackMap; // pack-map-adapter (kaart zonder CityGenerator)

	FVector2D WorldToCanvas(float Wx, float Wy) const;
	void BuildBlocks();
	UBorder* AddDot(const FLinearColor& Col, float Sz, int32 ZOrder);
	class UWidget* AddPersonIcon(const FLinearColor& Tint = FLinearColor(0.3f, 1.f, 0.4f), float Sz = 20.f, int32 ZOrder = 21); // poppetje-icoon op het canvas
	class UWidget* AddPlayerMarker(); // groot speler-baken: witte cirkel + blauw poppetje, boven alles
	UTextBlock* AddCanvasText(const FString& T, FVector2D Pos, float W, int32 Size, const FLinearColor& Col, int32 ZOrder);
	// Nummer/label op een klein donker afgerond chip-vlak (altijd leesbaar, ongeacht het dak eronder).
	class UWidget* AddPill(const FString& T, FVector2D Pos, int32 Size, const FLinearColor& TextCol, int32 ZOrder);
};
