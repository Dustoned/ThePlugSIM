// UDealWidget — het deal-scherm als UMG (clean): klantkaart, prijs-slider, substituut-strain-keuze,
// live acceptatiekans + R/L/A-preview. Leest de actieve deal van UPhoneClientComponent op de pawn.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DealWidget.generated.h"

class UPhoneClientComponent;
class ACustomerBase;
class UInventoryComponent;
class UCanvasPanel;
class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UProgressBar;
class USlider;
class UWeedActionButton;

UCLASS()
class WEEDSHOPCORE_API UDealWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	UFUNCTION()
	void OnPriceSlider(float Value);

	void BuildShell(UCanvasPanel* Root);
	void RebuildStrains();          // vult/diff de strain-cel-pool (list-change) + zet de selectie-highlight
	void RefreshStrainSelection();  // pure selectie-wissel: alleen de 2 betrokken knoppen herstylen
	FString ComputeStrainListSig() const; // signatuur van de aangeboden strain-lijst (set + grammen/thc/wanted)
	void UpdateLive();
	void GiveJointPressed();   // "Give joint" geklikt: 0 -> melding, 1 -> direct geven, >=2 -> kiezer
	void RebuildJointPicker(); // vult de joint-kiezer (strain - gram - kwaliteit per joint)

	UPhoneClientComponent* GetPhone() const;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UTextBlock> NameText;     // NPC-naam (groot, bovenaan)
	UPROPERTY() TObjectPtr<UTextBlock> StateText;    // status (wil kopen / prospect / tevreden)
	UPROPERTY() TObjectPtr<UTextBlock> DialogueText; // wat de NPC zegt
	UPROPERTY() TObjectPtr<UWidget>    DialogueBox;  // kader rond de dialoog
	UPROPERTY() TObjectPtr<UWidget>    GiveBtn;      // "Give joint"-knop
	UPROPERTY() TObjectPtr<UWidget>    OfferBtn;     // "Offer deal"-knop (alleen kopers)
	UPROPERTY() TObjectPtr<UTextBlock> WantsText;
	UPROPERTY() TObjectPtr<UTextBlock> SubText;
	UPROPERTY() TObjectPtr<UTextBlock> PriceText;
	UPROPERTY() TObjectPtr<USlider> PriceSlider;
	UPROPERTY() TObjectPtr<UTextBlock> StockText;
	UPROPERTY() TObjectPtr<UTextBlock> ChanceText;
	UPROPERTY() TObjectPtr<UProgressBar> ChanceBar;
	UPROPERTY() TObjectPtr<UTextBlock> RelationText;
	UPROPERTY() TObjectPtr<UTextBlock> TierText;     // klant-tier (Casual -> Whale)
	UPROPERTY() TObjectPtr<UProgressBar> TierBar;    // XP-voortgang naar de volgende tier
	UPROPERTY() TObjectPtr<UTextBlock> PreviewText;
	UPROPERTY() TObjectPtr<UTextBlock> OfferLabel;
	UPROPERTY() TObjectPtr<UVerticalBox> StrainBox;
	UPROPERTY() TObjectPtr<UVerticalBox> JointPickerBox; // joint-kiezer (welke joint geef je deze NPC)
	UPROPERTY() TObjectPtr<UTextBlock> NoWeedText;

	// Persistente strain-cel-pool: geen ClearChildren meer bij een klik. De pool groeit/krimpt naar het
	// aantal strains; per cel houden we een content-signatuur (grammen/thc/wanted) + welke id 'ie toont, zodat
	// alleen echt-gewijzigde cellen opnieuw gevuld worden en een pure selectie-wissel enkel 2 knoppen herstyled.
	UPROPERTY() TArray<TObjectPtr<UWeedActionButton>> StrainCells;   // de knoppen zelf (persistent)
	UPROPERTY() TArray<TObjectPtr<UHorizontalBox>> StrainRows;       // rij-containers (2 cellen per rij)
	UPROPERTY() TObjectPtr<UTextBlock> StrainEmptyText;              // "(no weed in your inventory)" melding
	TArray<FName> StrainCellIds;                                    // welke Bag_<strain>-id elke cel toont
	TArray<FString> StrainCellSigs;                                 // per-cel content-signatuur
	FName StrainSelectedId = NAME_None;                             // welke cel nu de "geselecteerd"-stijl heeft
	FString StrainListSig;                                          // signatuur van de strain-LIJST (set Bag-ids + grammen/thc)

	// Persistente joint-kiezer-pool (zelfde idee; laag geprioriteerd, box is meestal Collapsed).
	UPROPERTY() TArray<TObjectPtr<UWeedActionButton>> JointCells;
	UPROPERTY() TObjectPtr<UTextBlock> JointPickerTitle;            // "Give which joint?"
	UPROPERTY() TObjectPtr<UTextBlock> JointEmptyText;              // "No joints - roll one first (R)."
	TArray<FName> JointCellIds;                                    // welke Joint_<...>-id elke cel geeft
	TArray<FString> JointCellSigs;                                  // per-joint content-signatuur

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;
	TWeakObjectPtr<ACustomerBase> LastCustomer;
	FName LastOffered = NAME_None;
	bool bSliderHeld = false;

	// Zet de "geselecteerd/niet-geselecteerd"-stijl op één strain-cel (in-place restyle, geen rebuild).
	void StyleStrainCell(UWeedActionButton* B, bool bSelected);
};
