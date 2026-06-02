// ULevelUpWidget — feestelijk "LEVEL UP"-scherm dat verschijnt zodra het crew-level stijgt. Toont
// het nieuwe level + de items die op dit level zijn vrijgespeeld (icoon + naam). Fade't zachtjes
// in en weer uit (niet-blokkerend, geen input nodig). Leest het level van de GameState.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LevelUpWidget.generated.h"

class UWidget;
class UBorder;
class UTextBlock;
class UWrapBox;
class UCanvasPanel;

UCLASS()
class WEEDSHOPCORE_API ULevelUpWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	// Bouwt de inhoud voor een sprong van PrevLevel -> NewLevel (toont alle unlocks daartussen).
	void ShowForLevel(int32 PrevLevel, int32 NewLevel);

	UPROPERTY() TObjectPtr<UWidget> Card;        // gecentreerde kaart (fade)
	UPROPERTY() TObjectPtr<UWidget> Dim;         // zachte achtergrond-dim (fade)
	UPROPERTY() TObjectPtr<UTextBlock> LevelText;
	UPROPERTY() TObjectPtr<UTextBlock> SubText;
	UPROPERTY() TObjectPtr<UWrapBox> UnlockBox;

	int32 LastSeenLevel = -1; // -1 = nog niet geïnitialiseerd (eerste observatie toont niets)
	float Shown = 0.f;        // huidige fade (0..1)
	float HoldTimer = 0.f;    // resterende zichtbare tijd
};
