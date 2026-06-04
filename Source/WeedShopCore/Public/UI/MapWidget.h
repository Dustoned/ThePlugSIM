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
	UPROPERTY() TObjectPtr<UBorder> PlayerDot;
	UPROPERTY() TObjectPtr<UBorder> WaypointDot;
	UPROPERTY() TArray<TObjectPtr<UBorder>> NpcDots;        // roamers (cyaan stipjes)
	UPROPERTY() TArray<TObjectPtr<class UWidget>> NeedIcons; // klanten-voor-jou (groen poppetje)
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> NpcLabels;

	FVector2D CanvasToWorld(FVector2D Local) const; // inverse van WorldToCanvas
	class UPhoneClientComponent* GetPhone() const;

	bool bFullscreen = false;
	bool bBuiltBlocks = false;
	FVector2D CenterXY = FVector2D::ZeroVector;
	float Scale = 0.05f;

	TWeakObjectPtr<ACityGenerator> City;

	FVector2D WorldToCanvas(float Wx, float Wy) const;
	void BuildBlocks();
	UBorder* AddDot(const FLinearColor& Col, float Sz, int32 ZOrder);
	class UWidget* AddPersonIcon(); // groen poppetje-icoon op het canvas
	UTextBlock* AddCanvasText(const FString& T, FVector2D Pos, float W, int32 Size, const FLinearColor& Col, int32 ZOrder);
	// Nummer/label op een klein donker afgerond chip-vlak (altijd leesbaar, ongeacht het dak eronder).
	class UWidget* AddPill(const FString& T, FVector2D Pos, int32 Size, const FLinearColor& TextCol, int32 ZOrder);
};
