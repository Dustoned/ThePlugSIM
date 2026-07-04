// UDealWidget — het deal-scherm als UMG (clean): klantkaart, prijs-slider, substituut-strain-keuze,
// live acceptatiekans + R/L/A-preview. Leest de actieve deal van UPhoneClientComponent op de pawn.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
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
class UWrapBox;
class UDealWidget;

// Sleep-payload voor de geef-tray: welk zakje (strain + gram-maat) sleep je, en hoeveel heb je ervan.
UCLASS()
class WEEDSHOPCORE_API UDealBagDragOp : public UDragDropOperation
{
	GENERATED_BODY()
public:
	UPROPERTY() FName Strain;
	UPROPERTY() int32 Gram = 0;
	UPROPERTY() int32 Avail = 0;
};

// Eén cel in de geef-tray. Mode 0 = bron-zakje (sleepbaar), mode 1 = geef-zone (drop-doel). KRITISCH:
// RebuildWidget MOET SetVisibility(Visible) zetten, anders is de cel niet hit-testbaar en start de sleep nooit
// (dat was de bug van de eerste poging - UInvCell doet dit ook).
UCLASS()
class WEEDSHOPCORE_API UDealBagCell : public UUserWidget
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UDealWidget> Owner;
	int32 Mode = 0;          // 0 = bron (sleepbaar), 1 = geef-zone (drop-doel)
	FName Strain;
	int32 Gram = 0;
	int32 Avail = 0;
	FString Title;           // grote regel
	FString SubLabel;        // kleine regel eronder
	bool bWide = false;      // geef-zone = brede balk i.p.v. vierkant
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
};

UCLASS()
class WEEDSHOPCORE_API UDealWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

	// Aangeroepen door UDealBagCell (de geef-tray-cellen). Publiek zodat de cel-klasse ze kan callen.
	void OnBagDroppedOnGive(FName Strain, int32 Gram, int32 Avail); // zakje op de geef-zone -> stack? popup, anders +1
	void OnGiveZoneClicked();                                       // tik op de geef-zone -> pile leegmaken
	int32 PileAvailFor(FName Strain, int32 Gram) const;             // voorraad van die maat MINUS wat al in de pile ligt

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	UFUNCTION()
	void OnPriceSlider(float Value);
	UFUNCTION()
	void OnAmountSlider(float Value);

	void BuildShell(UCanvasPanel* Root);
	void RebuildStrains();          // vult/diff de strain-cel-pool (list-change) + zet de selectie-highlight
	void RefreshStrainSelection();  // pure selectie-wissel: alleen de 2 betrokken knoppen herstylen
	FString ComputeStrainListSig() const; // signatuur van de aangeboden strain-lijst (set + grammen/thc/wanted)
	void UpdateLive();
	void GiveJointPressed();   // "Give joint" geklikt: 0 -> melding, 1 -> direct geven, >=2 -> kiezer
	void RebuildJointPicker(); // vult de joint-kiezer (strain - gram - kwaliteit per joint)

	// --- Geef-tray ---
	void BuildGiveTray(UVerticalBox* VB);       // bouwt de tray 1x in BuildShell (geef-zone + bag-strip)
	void BuildHowManyPopup(UCanvasPanel* Root); // de "hoeveel zakjes?"-slider-popup (hoge ZOrder)
	void RebuildBagStrip();                     // vult de voorraad-strip (per maat) van de aangeboden strain
	void RefreshGiveZone();                     // toont de pile-combo + totaal op de geef-zone; zet DealGiveGrams
	void SyncPileToOffered();                   // reset+autofill de pile bij strain-wissel
	void OpenHowMany(int32 Gram, int32 Avail);
	UFUNCTION() void OnHowManySlider(float V);
	void ConfirmHowMany();
	void CancelHowMany();
	int32 PileTotalGrams() const;

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
	UPROPERTY() TObjectPtr<USlider> AmountSlider;
	UPROPERTY() TObjectPtr<UTextBlock> AmountText;

	// --- Geef-tray (bag-offers): je zakjes als sleepbare cellen + een geef-zone (drop-doel). Sleep zakjes
	//     erin, combineer maten; stack -> "hoeveel?"-popup. De pile-som = DealGiveGrams. ---
	UPROPERTY() TObjectPtr<UVerticalBox> TrayBox;        // hele tray-blok (togglen met de amount-zone)
	UPROPERTY() TObjectPtr<UDealBagCell> GiveZone;       // transparante drop-cel (hit-test) boven de balk
	UPROPERTY() TObjectPtr<UTextBlock> GiveTotalText;    // "Geeft: 5g + 2g = 7g" (SetText-baar, op de zichtbare balk)
	UPROPERTY() TObjectPtr<UWrapBox> BagStrip;           // je voorraad-zakjes van de aangeboden strain (sleepbaar)
	UPROPERTY() TArray<TObjectPtr<UDealBagCell>> BagStripPool;
	TMap<int32, int32> PileCounts;                       // gram-maat -> aantal zakjes in de pile
	FName PileStrain = NAME_None;                        // strain waarvoor de pile geldt (reset bij strain-wissel)
	FString BagStripSig;                                 // sig van de voorraad-strip (alleen bij wijziging vullen)
	UPROPERTY() TObjectPtr<UWidget> HowManyRoot;         // "hoeveel zakjes?"-popup
	UPROPERTY() TObjectPtr<USlider> HowManySlider;
	UPROPERTY() TObjectPtr<UTextBlock> HowManyLabel;
	int32 HowManyGram = 0;
	int32 HowManyMax = 0;

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
	bool bAmountHeld = false;

	// Perf: value-key over alle bron-waarden van de UpdateLive-TEKSTEN (State/R/L/A/Offered/Ask/Stock/...);
	// gelijk = alle SetText/visibility-calls overslaan. De gameplay-regels in NativeTick blijven elke tick.
	FString LastLiveKey;
};
