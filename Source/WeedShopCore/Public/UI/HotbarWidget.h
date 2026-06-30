// UHotbarWidget — altijd-zichtbare hotbar (UMG) onderaan: 8 slots met item + aantal + actieve markering.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HotbarWidget.generated.h"

class UBorder;
class UTextBlock;
class UCanvasPanel;
class USizeBox;
class UInvCell;
class UInventoryComponent;

UCLASS()
class WEEDSHOPCORE_API UHotbarWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void RefreshSlots(); // werkt alle slot-iconen/tags/badges bij uit de inventory (per-tick + direct bij een mutatie)

	// Verversen ZODRA de voorraad wijzigt (zelfde frame als de drop), niet pas de volgende tick -> geen icon-pop.
	UFUNCTION() void OnInvChanged();
	TWeakObjectPtr<UInventoryComponent> BoundInv;

	UPROPERTY() TArray<TObjectPtr<UBorder>> SlotBoxes;
	UPROPERTY() TArray<TObjectPtr<USizeBox>> SlotIconBoxes; // icoon-container per slot (inhoud wisselt)
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> SlotNames;   // tag-tekst in de bubble (OG, GSC, II, ...)
	UPROPERTY() TArray<TObjectPtr<UBorder>> SlotTagPills;   // tag-bubble onderaan (verbergen als geen tag)
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> SlotBadges;  // aantal/gram-tekst
	UPROPERTY() TArray<TObjectPtr<UBorder>> SlotBadgePills; // pill-achtergrond (verbergen als leeg)
	UPROPERTY() TArray<TObjectPtr<UInvCell>> DropCells;     // transparante sleep/drop-cel per slot
	TArray<FName> SlotLastIcon;                             // laatst getoonde item-id (om icoon niet elke tick te herbouwen)
	TArray<int32> SlotLastWaterState;                      // per slot: -1 onbekend, 0 vol, 1 leeg (fles-icoon per fles)
	bool bPrevWaterEmpty = false;                           // vorige vol/leeg-staat fles -> forceert icoon-refresh bij flip

	// Telefoon-notificatie rechts van de hotbar: telefoon-icoon (trilt bij nieuw bericht) + bubble met aantal.
	UPROPERTY() TObjectPtr<UTextBlock> MsgBadge;
	UPROPERTY() TObjectPtr<UBorder> MsgBadgePill;
	UPROPERTY() TObjectPtr<USizeBox> PhoneIconBox; // het telefoon-icoon zelf (inhoud wisselt: normaal/trillend)
	int32 PhoneVibeState = -1;                     // -1 onbekend, 0 normaal, 1 trillend (om alleen bij flip te herbouwen)
	float PhoneShakeT = 0.f;                        // tijd-accumulator voor de tril-animatie
	int32 PhoneLastUnread = 0;                     // vorige ongelezen-stand (detecteert NIEUW bericht)
	float PhoneVibeTimer = 0.f;                     // resterende tril-tijd na een nieuw bericht (sec)
};
