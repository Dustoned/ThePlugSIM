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

	UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY() TObjectPtr<UBorder> PlayerDot;
	UPROPERTY() TArray<TObjectPtr<UBorder>> NpcDots;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> NpcLabels;

	bool bFullscreen = false;
	bool bBuiltBlocks = false;
	FVector2D CenterXY = FVector2D::ZeroVector;
	float Scale = 0.05f;

	TWeakObjectPtr<ACityGenerator> City;

	FVector2D WorldToCanvas(float Wx, float Wy) const;
	void BuildBlocks();
	UBorder* AddDot(const FLinearColor& Col, float Sz, int32 ZOrder);
	UTextBlock* AddCanvasText(const FString& T, FVector2D Pos, float W, int32 Size, const FLinearColor& Col, int32 ZOrder);
};
