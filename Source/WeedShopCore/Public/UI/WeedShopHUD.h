// AWeedShopHUD — on-screen overlay puur in C++ (Canvas + hit-boxes, geen UMG nodig).
// Toont kas, klok/dag-nacht, voorraad, interactie-prompt en de klikbare telefoon (knoppen + hover).
// De telefoon-staat/-acties zitten in UPhoneClientComponent op de speler-pawn.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "WeedShopHUD.generated.h"

class UPhoneClientComponent;

UCLASS()
class WEEDSHOPCORE_API AWeedShopHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	// Hit-box callbacks (klik + hover) voor de telefoon-knoppen.
	virtual void NotifyHitBoxClick(FName BoxName) override;
	virtual void NotifyHitBoxBeginCursorOver(FName BoxName) override;
	virtual void NotifyHitBoxEndCursorOver(FName BoxName) override;

protected:
	// Tekent de klikbare telefoon voor de actieve tab.
	void DrawPhone(UPhoneClientComponent* Phone);

	// Tekent het klikbare roll-paneel (grams-keuze + kwaliteit + draai-knop).
	void DrawRollUI(UPhoneClientComponent* Phone);

	// Tekent één klikbare knop-regel (rect + tekst + hit-box) en geeft true terug bij hover.
	bool DrawButton(FName BoxName, const FString& Label, float X, float Y, float W, const FLinearColor& BaseColor);

	// De telefoon-component op de bestuurde pawn (nullptr als geen).
	UPhoneClientComponent* GetPhone() const;

	// Naam van de hit-box waar de cursor nu boven hangt.
	FName HoveredBox;

	// Slider-track (scherm-pixels) van het roll-paneel, voor klik->dichtstbijzijnde gram.
	float RollTrackX = 0.f;
	float RollTrackW = 0.f;

	// Tooltip die deze frame getekend moet worden (leeg = geen).
	FString HoverTooltip;
};
