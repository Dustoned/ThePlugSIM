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

// Eén cel in de geef-interactie. Mode 0 = bron-zakje in de sell-grid (sleepbaar, canonieke inventory-look),
// mode 1 = geef-zone (drop-DOEL: bevat de geef-grid als content, klik = niks meer -> zie mode 2),
// mode 2 = een GEGEVEN bag in de geef-grid (klik = 1 terug naar de sell-grid; overschrijft NativeOnDrop NIET,
// zodat een drop OP zo'n cel naar de container-zone bubbelt). KRITISCH: RebuildWidget MOET SetVisibility(Visible)
// zetten, anders is de cel niet hit-testbaar en start de sleep/klik nooit (dat was de bug van de eerste poging).
UCLASS()
class WEEDSHOPCORE_API UDealBagCell : public UUserWidget
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UDealWidget> Owner;
	int32 Mode = 0;          // 0 = bron-zakje (sleepbaar), 1 = geef-zone (drop-doel), 2 = gegeven bag (klik = -1)
	FName Strain;
	int32 Gram = 0;
	int32 Avail = 0;
	FString Title;           // grote regel (ongebruikt in de icon-cel-modi, blijft voor back-compat)
	FString SubLabel;        // kleine regel eronder (idem)
	bool bWide = false;      // geef-zone = brede balk i.p.v. vierkant

	// Optionele expliciete content (canonieke icon-cel): de DealWidget bouwt 'm via WeedItemPickGrid-recept
	// en zet 'm hier; RebuildWidget gebruikt 'm dan i.p.v. de oude tekst-cel. WrapBox voor de geef-zone (mode 1).
	UPROPERTY() TObjectPtr<UWidget> Content;
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

	// Aangeroepen door UDealBagCell (de geef-cellen). Publiek zodat de cel-klasse ze kan callen.
	void OnBagDroppedOnGive(FName Strain, int32 Gram, int32 Avail); // bag uit de sell-grid op de geef-zone -> +1 in de pile
	void OnGiveZoneClicked();                                       // tik op de LEGE geef-zone -> pile leegmaken (fallback)
	void OnGivenBagClicked(FName Strain, int32 Gram);              // klik op een gegeven bag-cel -> 1 terug naar de sell-grid
	int32 PileAvailFor(FName Strain, int32 Gram) const;             // voorraad van die maat MINUS wat al in de pile ligt

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	UFUNCTION()
	void OnPriceSlider(float Value);
	UFUNCTION()
	void OnAmountSlider(float Value);

	void BuildShell(UCanvasPanel* Root);
	void UpdateLive();
	void GiveJointPressed();   // "Give joint" geklikt: 0 -> melding, 1 -> direct geven, >=2 -> kiezer
	void RebuildJointPicker(); // vult de joint-kiezer (strain - gram - kwaliteit per joint)

	// --- Geef-interactie (bag-offers): een SELL-GRID (al je bags, sleepbaar) + een GEEF-ZONE (drop-doel dat de
	//     geef-grid met echte bag-iconen bevat). Sleep bag -> geef-zone = +1 in de pile; klik op een gegeven bag =
	//     1 terug. De pile-som = DealGiveGrams (drijft kans/preview/verkoop). ---
	void BuildGiveTray(UVerticalBox* VB);       // bouwt de geef-zone + de sell-grid 1x in BuildShell
	void RebuildSellGrid();                     // sig-gated: vult de sell-grid met ALLE bag-stacks (inv + hotbar)
	void RefreshGiveZone();                     // (her)vult de geef-grid met bag-iconen uit de pile; zet DealGiveGrams
	void SyncPileToOffered();                   // reset de pile bij een strain-wissel (leeg beginnen met de nieuwe strain)
	int32 PileTotalGrams() const;

	// Canonieke inventory-cel-CONTENT (bag-icoon + count-badge rechtsboven + strain-tag onderaan), zoals
	// UWeedItemPickGrid::MakeCellContent. Gedeeld door de sell-grid (mode 0) en de geef-grid (mode 2).
	UWidget* MakeBagCellContent(FName Strain, int32 Gram, int32 Count) const;

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

	// --- Geef-interactie (bag-offers) ---
	UPROPERTY() TObjectPtr<UVerticalBox> TrayBox;        // hele blok (geef-zone + sell-grid), togglen met de amount-zone
	UPROPERTY() TObjectPtr<UDealBagCell> GiveZone;       // drop-DOEL (mode 1): bevat de geef-grid + de lege-hint
	UPROPERTY() TObjectPtr<UWrapBox> GiveGrid;           // de gegeven bags als icon-cellen (mode 2, klik = -1)
	UPROPERTY() TObjectPtr<UTextBlock> GiveHint;         // "Drop bags here to give <naam>" als de pile leeg is
	UPROPERTY() TObjectPtr<UWrapBox> SellGrid;           // ALLE bags die je bij hebt (inv + hotbar), sleepbaar (mode 0)
	TMap<int32, int32> PileCounts;                       // gram-maat -> aantal zakjes in de pile
	FName PileStrain = NAME_None;                        // strain waarvoor de pile geldt (reset bij strain-wissel)
	FString SellGridSig;                                 // sig van de sell-grid (alleen bij bag-voorraad-wijziging vullen)

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
	// LIVE deal-delta's onder elke waarde ("+N", groen): de +respect/+loyalty/+hooked die deze deal oplevert
	// (uit PreviewDealOutcome met de huidige GiveG). 0 of geen deal -> leeg/verborgen. Vervangt de oude PreviewText-regel.
	UPROPERTY() TObjectPtr<UTextBlock> RespectDelta;
	UPROPERTY() TObjectPtr<UTextBlock> LoyaltyDelta;
	UPROPERTY() TObjectPtr<UTextBlock> AddictDelta;
	UPROPERTY() TObjectPtr<UTextBlock> RespectSub;             // D13a — mini-label "to contact" onder de respect-waarde (zichtbaar tot unlock)
	// Delta-caches per ring: sla de MID-update over als de fractie/kleur nauwelijks wijzigt.
	float LastRespFrac = -1.f, LastLoyalFrac = -1.f, LastAddictFrac = -1.f;
	FLinearColor LastRespCol = FLinearColor(0.f, 0.f, 0.f, 0.f);
	FLinearColor LastLoyalCol = FLinearColor(0.f, 0.f, 0.f, 0.f);
	FLinearColor LastAddictCol = FLinearColor(0.f, 0.f, 0.f, 0.f);

	UPROPERTY() TObjectPtr<UTextBlock> TierText;     // klant-tier-NAAM (in de pill rechts van de naam)
	UPROPERTY() TObjectPtr<UBorder>    TierPill;     // pill-kader om TierText (accent-vlak)
	UPROPERTY() TObjectPtr<USizeBox>   TierBar;      // dunne accent-balk onder de kop-rij (3px)
	UPROPERTY() TObjectPtr<UTextBlock> PreviewText;      // dood (spec): permanent Collapsed + leeg; de gauge-delta's tonen dit nu
	UPROPERTY() TObjectPtr<UVerticalBox> JointPickerBox; // container voor het joint-keuze-grid (welke joint geef je)
	UPROPERTY() TObjectPtr<UTextBlock> NoWeedText;

	// Joint-picker (zonder selectie). Persistent, diff't intern. (De strain-picker is vervangen door de
	// strain-uit-bag-logica: de aangeboden strain volgt uit welke bag je in de geef-zone legt.)
	UPROPERTY() TObjectPtr<UWeedItemPickGrid> JointGrid;
	UPROPERTY() TObjectPtr<UTextBlock> JointEmptyText;              // "No joints - roll one first (R)."

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;
	TWeakObjectPtr<ACustomerBase> LastCustomer;
	FName LastOffered = NAME_None;
	bool bSliderHeld = false;
	bool bAmountHeld = false;

	// Perf: value-key over alle bron-waarden van de UpdateLive-TEKSTEN (State/R/L/A/Offered/Ask/Stock/...);
	// gelijk = alle SetText/visibility-calls overslaan. De gameplay-regels in NativeTick blijven elke tick.
	FString LastLiveKey;
};
