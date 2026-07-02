// UStatusHudWidget — altijd zichtbaar status-paneel linksboven (UMG), zelfde clean stijl als de
// telefoon: afgeronde kaart, flat-iconen en echte progress bars voor heat, level en stoned.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StatusHudWidget.generated.h"

class UTextBlock;
class UProgressBar;
class UWidget;
class UCanvasPanel;
class UVerticalBox;
class USizeBox;

UCLASS()
class WEEDSHOPCORE_API UStatusHudWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	// Bouwt één rij (icoon + inhoud) en levert de inhoud-container terug om in te vullen.
	// IconKey (optioneel) = PNG-sleutel; leeg = gebruik het EIcon-vormicoon.
	class UHorizontalBox* MakeRow(UVerticalBox* Parent, int32 IconType, const FLinearColor& IconCol, const FString& IconKey = FString());

	// De icoon-container van de zojuist gebouwde rij (om naderhand te kunnen vervangen).
	UPROPERTY() TObjectPtr<USizeBox> LastRowIcon;
	// Tijd-rij-icoon: wisselt tussen zon (dag) en maan (nacht).
	UPROPERTY() TObjectPtr<USizeBox> TimeIcon;
	int32 bTimeNightShown = -1; // -1 = nog niet gezet

	UPROPERTY() TObjectPtr<UTextBlock> CashText;
	UPROPERTY() TObjectPtr<UTextBlock> BankText;
	UPROPERTY() TObjectPtr<UTextBlock> TimeText;
	UPROPERTY() TObjectPtr<UTextBlock> DayText;   // header: "Day X" (links), TimeText = "HH:MM" (rechts)
	UPROPERTY() TObjectPtr<UWidget> MaxBadge;     // "MAX"-badge naast het level, verborgen tot max-level
	UPROPERTY() TObjectPtr<UProgressBar> HeatBar;
	UPROPERTY() TObjectPtr<UTextBlock> HeatText;
	UPROPERTY() TObjectPtr<UProgressBar> LevelBar;
	UPROPERTY() TObjectPtr<UTextBlock> LevelText;
	UPROPERTY() TObjectPtr<UWidget> StonedRow;
	UPROPERTY() TObjectPtr<UProgressBar> StonedBar;
	UPROPERTY() TObjectPtr<UTextBlock> StonedText;

	// Laadscherm-dismiss: het laadscherm blijft staan tot de speler stil in de kamer staat (of een safety-cap).
	float LoadShownTime = 0.f;
	float LoadStillTime = 0.f;
	bool bLoadStopped = false;

	// Perf: laatst-gezette waarden -> SetText alleen bij verschil (changed-check-idioom).
	double LastCashShown = -1.0e18;
	double LastBankShown = -1.0e18;
	int32 LastDayShown = -1;
	int32 LastMinuteShown = -1;   // H*60+M
	int32 LastHeatShown = -1;
	int32 LastLevelShown = -1;
	int32 LastStonedKey = -1;     // Secs*1000 + XpBoost
	int32 LastStonedVisible = -1; // -1 onbekend
	// Phone-component-cache (weak + pawn-check): FindComponentByClass niet elke tick.
	TWeakObjectPtr<class UPhoneClientComponent> CachedPhone;
	TWeakObjectPtr<APawn> CachedPhonePawn;
};
