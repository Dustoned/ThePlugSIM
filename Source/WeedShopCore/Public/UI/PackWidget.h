// UPackWidget — verpak-menu van de bench. Kies een GEDROOGDE strain + een bakje/jar dat je hebt, klik
// Pack: er gaat tot de container-capaciteit gram in een zakje en dat wordt verkoopbare Bag_<strain>-
// voorraad. Losse/natte wiet kun je niet aan klanten verkopen — alleen verpakt.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "PackWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UVerticalBox;
class UWrapBox;
class UWeedActionButton;
class UWeedItemPickGrid;
class UPackWidget;

// Sleep-payload: BudId = gedroogde strain die je op een container sleept (packen); BagId = verpakte zak die
// je op de uitpak-zone sleept (unpacken). Er is er telkens één gezet.
UCLASS()
class WEEDSHOPCORE_API UPackDragOp : public UDragDropOperation
{
	GENERATED_BODY()
public:
	UPROPERTY() FName BudId;
	UPROPERTY() FName BagId;
};

// Sleepbare gedroogde-wiet-cel (de BRON): sleep 'm op een container om 1 volle container te pakken. Gepoold
// + in-place via SetInner (geen ClearChildren -> geen flash bij een voorraad-wijziging).
UCLASS()
class WEEDSHOPCORE_API UPackWeedCell : public UUserWidget
{
	GENERATED_BODY()
public:
	FName BudId; int32 GramsAvail = 0;
	UPROPERTY() TObjectPtr<UWidget> Content; // icoon+labels; RebuildWidget wrapt dit in de grijpbare cel
	void SetInner(UWidget* W);
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	UPROPERTY() TObjectPtr<class UBorder> Frame;
};

// Rechts-kolom-cel. Lege container (BagId leeg): sleep wiet erop -> nieuw packen. Verpakte zak (BagId gezet):
// SLEEPBAAR -> uitpakken (drop op de wiet-kolom links); niet-vol = ook een bijvul-doel (drop weed erop).
UCLASS()
class WEEDSHOPCORE_API UPackContCell : public UUserWidget
{
	GENERATED_BODY()
public:
	FName ContId;          // container-type (icoon/noun; drop weed = nieuw packen als BagId leeg is)
	FName BagId;           // gezet = deze cel is een verpakte zak (sleepbaar = uitpakken)
	bool bBagFull = false; // volle zak -> geen bijvul-doel (wel sleepbaar)
	TWeakObjectPtr<UPackWidget> Owner;
	UPROPERTY() TObjectPtr<UWidget> Content; // icoon+labels; RebuildWidget wrapt dit in de cel
	void SetInner(UWidget* W);               // in-place cel-inhoud vervangen (gepoold)
	void SetDragHover(bool bHover);          // accent-highlight tijdens een drag-over
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override; // bag -> sleep (uitpak)
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	UPROPERTY() TObjectPtr<class UBorder> Frame; // de cel-rand (voor hover-highlight)
};

// Sleepbare VERPAKTE-zak-cel (unpack-bron): sleep 'm op de uitpak-zone om er grammen uit te halen. Wordt
// GEPOOLD (in-place hergebruikt) door RefreshUnpack -> SetInner swapt alleen de zichtbare inhoud, geen herbouw.
UCLASS()
class WEEDSHOPCORE_API UPackBagCell : public UUserWidget
{
	GENERATED_BODY()
public:
	FName BagId; int32 CountAvail = 0;
	UPROPERTY() TObjectPtr<UWidget> Content;
	void SetInner(UWidget* W); // in-place de cel-inhoud (icoon+labels) vervangen
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	UPROPERTY() TObjectPtr<class UBorder> Frame;
};

// Uitpak-DROPZONE: sleep een verpakte zak erop -> vraag hoeveel gram eruit (via Owner->UnpackDropped).
UCLASS()
class WEEDSHOPCORE_API UPackUnwrapCell : public UUserWidget
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UPackWidget> Owner;
	UPROPERTY() TObjectPtr<UWidget> Content;
	void SetDragHover(bool bHover);
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	UPROPERTY() TObjectPtr<class UBorder> Frame;
};

// Een lopende inpak-job (channel-model): 1 container die over een timer volloopt. Client-cosmetisch; op
// voltooiing pakt de bestaande server-auth RequestPackGrams de container echt in (per-speler inventory).
// Welke actie de gedeelde hoeveelheid-popup nu aanstuurt.
enum class EPackAmountMode : uint8 { Pack, Unpack, TopUp };

struct FPackJob
{
	FName BudId;
	FName ContId;
	FName TargetBag;   // gezet = een bestaande niet-volle zak BIJVULLEN (merge)
	FName UnpackBag;   // gezet = UITPAKKEN (grammen uit deze zak halen); dan zijn BudId/ContId leeg/alleen-voor-icoon
	int32 Grams = 0;
	float Elapsed = 0.f;
	float Duration = 1.f;
};

UCLASS()
class WEEDSHOPCORE_API UPackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);
	void PackOne(FName BudId, FName ContId); // drag-drop van wiet op een lege container: popup (2 sliders) of direct
	void TopUpDropped(FName BudId, FName BagId); // drag-drop van wiet op een niet-volle zak: bijvullen (merge)
	void UnpackDropped(FName BagId);         // drag-drop van een zak op de uitpak-zone: vraag hoeveel gram (popup)

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	// "Hoeveel inpakken?"-popup (modal, gespiegeld van de deal-AmountPopup). Bij het slepen van wiet op een
	// container waar meer dan 1 van kan: vraag het aantal; anders meteen 1 job.
	void BuildAmountPopup(UCanvasPanel* Root);
	void OpenPackPopup(FName Bud, FName Cont);                 // pack: 2 sliders (gram/zakje + aantal zakjes)
	void OpenUnpackPopup(FName Bag, int32 MaxGrams);           // unpack: 1 slider (grammen eruit)
	void OpenTopUpPopup(FName Bud, FName Bag, int32 MaxAdd);   // bijvullen: 1 slider (grammen erbij), default vol
	UFUNCTION() void OnAmountChanged(float V);                 // slider 1 (gram/zakje bij pack; grammen bij unpack)
	UFUNCTION() void OnAmount2Changed(float V);                // slider 2 (aantal zakjes, alleen pack)
	void RefreshPackPopupLabels();                            // beide labels + samenvatting herberekenen (pack)
	void ConfirmAmount();
	void CancelAmount();
	void EnqueueGrams(FName Bud, FName Cont, int32 Grams);    // Grams gram inpakken: vul containers tot cap (direct-pad)
	void EnqueueBags(FName Bud, FName Cont, int32 GramsPerBag, int32 Count); // Count zakjes van GramsPerBag inpakken (queue)
	void EnqueueTopUp(FName Bud, FName Bag, int32 Grams);     // 1 bijvul-job: voeg Grams gram toe aan de niet-volle zak
	void EnqueueUnpack(FName Bag, int32 Grams);              // 1 unpack-job: haal Grams gram uit de zak (halve pack-tijd)

	// PERSISTENT UI: het volledige pack- én unpack-scherm wordt ÉÉN keer opgebouwd (BuildPackPane /
	// BuildUnpackPane). Daarna updaten Refresh* alleen waardes/zichtbaarheid/rij-pools IN PLACE — nooit
	// ClearChildren op een klik (geen flash/scroll-sprong). FillBody is enkel nog de dispatcher.
	void BuildPackPane(UVerticalBox* Parent);
	void BuildUnpackPane(UVerticalBox* Parent);
	void FillBody();       // dispatcher: kiest de juiste pane + roept de juiste Refresh
	void RefreshPack();    // pack-flow in place bijwerken (sleep-cellen + drop-cellen + lanes), geen teardown
	void RefreshUnpack();  // unpack-lijst in place bijwerken, geen teardown

	// Channel-inpakken: sleep-drop start een job; de tick laat de balken vollopen en pakt op voltooiing echt in.
	void TickJobs(float Dt);       // jobs voortduwen (alleen terwijl de bench open is -> weglopen = pauze) + voltooien
	void RefreshLanes();           // lane-rij-pool groeien/krimpen naar het aantal jobs (geen ClearChildren)
	void UpdateLaneProgress();     // per-lane balk-percentage + resttijd in place bijwerken (elke tick)

	// Zet het aantal bags EN werkt slider + labels meteen bij (geen herbouw -> slider springt niet).
	void SetB(int32 N);   // pack-flow
	void SetUB(int32 N);  // unpack-flow

	// Highlight een keuze-knop (grams-per-bag / bags) als actief: accent-vlak + accent-outline (idioom uit
	// UWeedItemPickGrid::StyleCell). In place SetStyle - geen herbouw.
	void StyleChoiceBtn(UWeedActionButton* B, bool bActive);

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;

	// Twee wederzijds-uitsluitende panes: alleen hun Visibility flipt op de tab-toggle (geen ClearChildren).
	UPROPERTY() TObjectPtr<UVerticalBox> PackPane;
	UPROPERTY() TObjectPtr<UVerticalBox> UnpackPane;

	// --- Pack-flow persistente widgets (één keer gebouwd) ---
	// Drag-drop-inpakken (ND: "sleep wiet in de jar"): sleepbare wiet-cellen (bron) -> drop op container-cellen.
	// Beide GEPOOLD (per-cel change-sig, geen ClearChildren -> geen flash bij een pack-voltooiing).
	UPROPERTY() TObjectPtr<UWrapBox> WeedDragBox;      // sleepbare gedroogde-wiet-cellen (per strain)
	UPROPERTY() TObjectPtr<UWrapBox> ContDropBox;      // drop-doel container-cellen (per container)
	UPROPERTY() TArray<TObjectPtr<UPackWeedCell>> WeedCellPool;
	TArray<FString> WeedCellSig;
	UPROPERTY() TArray<TObjectPtr<UPackContCell>> ContCellPool;
	TArray<FString> ContCellSig;
	UPROPERTY() TObjectPtr<class UTextBlock> NoBudLabel;
	UPROPERTY() TObjectPtr<UVerticalBox> ContSection;  // "2. Pick a bag/jar"-blok (titel + lijst + geen-melding)
	UPROPERTY() TObjectPtr<UWidget> ContCard;
	UPROPERTY() TObjectPtr<class UTextBlock> NoContLabel;
	UPROPERTY() TObjectPtr<class UTextBlock> PackHintLabel; // "Drag weed onto a container to pack it"

	// --- Channel-lanes ("PACKING"-blok): gepoolde progress-rijen (patroon uit UDryingRackWidget). ---
	UPROPERTY() TObjectPtr<UVerticalBox> LaneSection;          // titel + rij-pool
	UPROPERTY() TObjectPtr<class UTextBlock> LaneTitle;        // "PACKING (n/lanes)"
	UPROPERTY() TObjectPtr<class UTextBlock> LaneIdleLabel;    // "Nothing packing - drag weed onto a container."
	UPROPERTY() TArray<TObjectPtr<class UBorder>> LaneRows;    // rij-container (Border)
	UPROPERTY() TArray<TObjectPtr<class USizeBox>> LaneIcons;  // icoon-slot (swap bij nieuwe container)
	UPROPERTY() TArray<TObjectPtr<class UTextBlock>> LaneNames;// "50g Silver Haze jar"
	UPROPERTY() TArray<TObjectPtr<class UTextBlock>> LaneTimes;// "3.2s left"
	UPROPERTY() TArray<TObjectPtr<class UProgressBar>> LaneBars;
	TArray<FName> LaneShownCont; // welke container het icoon in lane i nu toont (alleen swappen bij wijziging)

	TArray<FPackJob> Jobs;       // inpak-jobs (queue): de eerste N=lanes draaien, de rest wacht

	// --- "Hoeveel inpakken?"-popup (modal op de root-canvas) ---
	UPROPERTY() TObjectPtr<UWidget> AmountRoot;              // de hele popup-overlay (Visibility togglet)
	UPROPERTY() TObjectPtr<class USlider> AmountSlider;      // slider 1: gram/zakje (pack) of grammen-eruit (unpack)
	UPROPERTY() TObjectPtr<class USlider> AmountSlider2;     // slider 2: aantal zakjes (alleen pack)
	UPROPERTY() TObjectPtr<class UTextBlock> AmountTitle;    // "How many to pack?" / "How many grams to take out?"
	UPROPERTY() TObjectPtr<class UTextBlock> AmountLabel;    // slider 1-label
	UPROPERTY() TObjectPtr<class UTextBlock> AmountLabel2;   // slider 2-label (pack)
	UPROPERTY() TObjectPtr<class UTextBlock> AmountSummary;  // "= 120g total" (pack)
	UPROPERTY() TObjectPtr<UWidget> AmountPackRow2;          // slider 2 + summary-blok (verborgen bij unpack)
	UPROPERTY() TObjectPtr<class UTextBlock> AmountConfirmText; // confirm-knoplabel ("Pack" / "Take out")
	EPackAmountMode AmountMode = EPackAmountMode::Pack; // welke actie de popup nu aanstuurt
	FName PendingBud;    // strain waarvoor de pack-popup open staat
	FName PendingCont;   // container waarvoor de pack-popup open staat
	FName PendingBag;    // verpakte zak waarvoor de unpack-popup open staat
	int32 PendingMax = 0;    // unpack: max grammen
	int32 PendingCap = 1;    // pack: container-capaciteit (max gram/zakje)
	int32 PendingWeed = 0;   // pack: beschikbare wiet (gram)
	int32 PendingContOwned = 0; // pack: beschikbare containers
	int32 PendingRoom = 0;   // pack: vrije plek in de queue
	UPROPERTY() TObjectPtr<UVerticalBox> GpbSection;   // "2.b Grams per bag"-blok
	UPROPERTY() TObjectPtr<class UTextBlock> GpbLabel;
	UPROPERTY() TObjectPtr<UVerticalBox> BagsSection;  // "3. How many bags?"-blok
	UPROPERTY() TObjectPtr<class UTextBlock> PackSummaryLabel;
	UPROPERTY() TObjectPtr<class USlider> GramSlider;
	UPROPERTY() TObjectPtr<class UTextBlock> GramLabel;
	UPROPERTY() TObjectPtr<UWeedActionButton> PackButton;
	UPROPERTY() TObjectPtr<class UTextBlock> PackBtnLabel;

	// Keuze-knoppen die gehighlight worden (actief = accent). Grams-per-bag (1g/Max) + aantal-bags (Half/Max),
	// zowel pack- als unpack-flow. De -/+ steppers krijgen GEEN highlight (dat zijn geen vaste keuzes).
	UPROPERTY() TObjectPtr<UWeedActionButton> GpbOneBtn;
	UPROPERTY() TObjectPtr<UWeedActionButton> GpbMaxBtn;
	UPROPERTY() TObjectPtr<UWeedActionButton> BagsHalfBtn;
	UPROPERTY() TObjectPtr<UWeedActionButton> BagsMaxBtn;
	UPROPERTY() TObjectPtr<UWeedActionButton> UnpackHalfBtn;
	UPROPERTY() TObjectPtr<UWeedActionButton> UnpackMaxBtn;

	// --- Unpack-flow (drag-drop, consistent met packen): sleepbare bag-cellen -> uitpak-dropzone -> grammen-popup ---
	UPROPERTY() TObjectPtr<UWrapBox> UnpackBagBox;        // sleepbare verpakte-zak-cellen (nette namen)
	UPROPERTY() TArray<TObjectPtr<UPackBagCell>> UnpackCellPool; // GEPOOLD (geen ClearChildren -> geen flash)
	TArray<FString> UnpackCellSig;                        // per-cel change-sig (BagId|count|gram): alleen bij wijziging updaten
	UPROPERTY() TObjectPtr<UWeedItemPickGrid> UnpackGrid; // (legacy, niet meer gebouwd)
	UPROPERTY() TObjectPtr<class UTextBlock> UnpackEmptyLabel;
	UPROPERTY() TObjectPtr<UVerticalBox> UnpackControls; // slider/steppers/knop-blok (verborgen als geen bags)
	UPROPERTY() TObjectPtr<class USlider> UnpackSlider;
	UPROPERTY() TObjectPtr<class UTextBlock> UnpackLabel;
	UPROPERTY() TObjectPtr<UWeedActionButton> UnpackButton;
	UPROPERTY() TObjectPtr<class UTextBlock> UnpackBtnLabel;

	// (De strain/container/unpack-keuze draait nu op UWeedItemPickGrid — SetItems diff't intern, dus
	//  hier zijn geen losse rij-pools/signatures meer nodig.)

	UPROPERTY() TObjectPtr<class UTextBlock> TabBtnLabel; // header-knop naast Close: "Unpack bags" / "Back to packing"
	bool bUnpackTab = false; // true = aparte unpack-tab (alleen de uitpak-lijst); false = de normale pack-flow

	FName SelStrain;       // gekozen gedroogde Bud_<strain>
	FName SelContainer;    // gekozen container (Cont_*)
	int32 SelBags = 1;     // gekozen aantal bags (1..MaxBags)
	int32 SelGrams = 0;    // gram per zakje (0 = volle container-cap); kleiner = bv. 1g-zakjes
	int32 MaxBags = 1;     // max bags = containers die je hebt vs wiet die je hebt
	int32 PackCap = 1;     // capaciteit (gram) van de gekozen container
	int32 PackBudHave = 0; // beschikbare gram van de gekozen strain

	// Unpack-tab hergebruikt SelBags/MaxBags (de tabs zijn wederzijds uitsluitend).
	FName SelUnpackBag;     // gekozen verpakte Bag_<strain> om uit te pakken
	int32 UnpackPerBag = 1; // gram per zakje van de gekozen bag (voor de labels)

	FString LastSig;       // herbouw alleen bij wijziging
};
