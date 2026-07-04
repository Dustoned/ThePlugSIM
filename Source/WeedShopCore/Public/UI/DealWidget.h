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
class UWeedItemPickGrid;
class UBorder;
class USizeBox;
class UImage;
class UMaterialInterface;

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
	UPROPERTY() TObjectPtr<UTextBlock> WantsText;        // prefix "Wants Xg " (normale tekstkleur)
	UPROPERTY() TObjectPtr<UTextBlock> WantsStrainText;  // alleen de strain-naam (in de strain-tagkleur)
	UPROPERTY() TObjectPtr<class UHorizontalBox> WantsRow; // rij die beide bevat (visibility togglen)
	UPROPERTY() TObjectPtr<UTextBlock> SubText;
	UPROPERTY() TObjectPtr<UTextBlock> PriceText;
	UPROPERTY() TObjectPtr<USlider> PriceSlider;
	UPROPERTY() TObjectPtr<UTextBlock> StockText;
	UPROPERTY() TObjectPtr<UTextBlock> ChanceText;
	UPROPERTY() TObjectPtr<UProgressBar> ChanceBar;
	UPROPERTY() TObjectPtr<UTextBlock> RelationText;

	// C.4 — respect/loyaliteit/addiction als 3 ring-gauges (radiaal-materiaal, PlantInfoWidget-mechanisme).
	UPROPERTY() TObjectPtr<UMaterialInterface> RadialMat;       // /Game/_Project/UI/M_RadialProgress (1x geladen in BuildShell)
	UPROPERTY() TObjectPtr<UHorizontalBox> StatGaugeRow;        // rij van 3 gauges (respect/loyalty/addiction)
	UPROPERTY() TObjectPtr<UImage> RespectRing;                 // radiale ring-image (dynamisch materiaal)
	UPROPERTY() TObjectPtr<UImage> LoyaltyRing;
	UPROPERTY() TObjectPtr<UImage> AddictRing;
	UPROPERTY() TObjectPtr<UTextBlock> RespectVal;             // waarde-tekst midden onder de ring
	UPROPERTY() TObjectPtr<UTextBlock> LoyaltyVal;
	UPROPERTY() TObjectPtr<UTextBlock> AddictVal;
	UPROPERTY() TObjectPtr<UTextBlock> RespectSub;             // D13a — mini-label "to contact" onder de respect-waarde (zichtbaar tot unlock)
	// Delta-caches per ring: sla de MID-update over als de fractie/kleur nauwelijks wijzigt.
	float LastRespFrac = -1.f, LastLoyalFrac = -1.f, LastAddictFrac = -1.f;
	FLinearColor LastRespCol = FLinearColor(0.f, 0.f, 0.f, 0.f);
	FLinearColor LastLoyalCol = FLinearColor(0.f, 0.f, 0.f, 0.f);
	FLinearColor LastAddictCol = FLinearColor(0.f, 0.f, 0.f, 0.f);

	UPROPERTY() TObjectPtr<UTextBlock> TierText;     // klant-tier-NAAM (in de pill rechts van de naam)
	UPROPERTY() TObjectPtr<UBorder>    TierPill;     // pill-kader om TierText (accent-vlak)
	UPROPERTY() TObjectPtr<USizeBox>   TierBar;      // dunne accent-balk onder de kop-rij (3px)
	UPROPERTY() TObjectPtr<UTextBlock> PreviewText;
	UPROPERTY() TObjectPtr<UTextBlock> OfferLabel;
	UPROPERTY() TObjectPtr<UVerticalBox> StrainBox;      // container voor het strain-keuze-grid
	UPROPERTY() TObjectPtr<UVerticalBox> JointPickerBox; // container voor het joint-keuze-grid (welke joint geef je)
	UPROPERTY() TObjectPtr<UTextBlock> NoWeedText;

	// Keuze-grids (B.11): strain-picker (met selectie) + joint-picker (zonder selectie). Persistent, diffen intern.
	UPROPERTY() TObjectPtr<UWeedItemPickGrid> StrainGrid;
	UPROPERTY() TObjectPtr<UWeedItemPickGrid> JointGrid;
	UPROPERTY() TObjectPtr<UTextBlock> StrainEmptyText;              // "(no weed in your inventory)" melding
	UPROPERTY() TObjectPtr<UTextBlock> JointEmptyText;              // "No joints - roll one first (R)."

	// Signatuur-gate: wanneer roepen we StrainGrid->SetItems aan (lijst-wijziging) vs enkel SetSelected (selectie).
	FName StrainSelectedId = NAME_None;                             // welke id nu de "geselecteerd"-stijl heeft
	FString StrainListSig;                                          // signatuur van de strain-LIJST (set Bag-ids + grammen/thc)

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;
	TWeakObjectPtr<ACustomerBase> LastCustomer;
	FName LastOffered = NAME_None;
	bool bSliderHeld = false;

	// Perf: value-key over alle bron-waarden van de UpdateLive-TEKSTEN (State/R/L/A/Offered/Ask/Stock/...);
	// gelijk = alle SetText/visibility-calls overslaan. De gameplay-regels in NativeTick blijven elke tick.
	FString LastLiveKey;
};
