// UInventoryWidget — inventory-scherm (UMG) met ECHTE drag-and-drop: sleep een item uit het rooster
// naar een hotbar-slot om 'm toe te wijzen, en sleep een hotbar-slot terug naar het rooster om 'm eraf
// te halen. Weed met meerdere batches krijgt een Merge-knop. Leest/zet de InventoryComponent op de pawn.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "InventoryWidget.generated.h"

class UPhoneClientComponent;
class UInventoryComponent;
struct FInventoryStack;
class UCanvasPanel;
class UWidget;
class UTextBlock;
class UWrapBox;
class UHorizontalBox;
class UInventoryWidget;

// Sleep-payload: welke stapel wordt versleept en waarvandaan (hotbar-slot of -1 = uit het rooster).
UCLASS()
class WEEDSHOPCORE_API UInvDragOp : public UDragDropOperation
{
	GENERATED_BODY()
public:
	UPROPERTY() int32 StackId = 0;
	UPROPERTY() int32 FromSlot = -1; // >=0 = vanaf hotbar-slot
	UPROPERTY() int32 FromCell = -1; // >=0 = vanuit een rooster-cel
	UPROPERTY() bool bSplit = false; // true = Shift ingedrukt -> de helft afsplitsen bij drop

	// Sleep je 'm BUITEN de inventory (op niks) los -> hele stapel op de grond droppen (OnDragCancelled).
	TWeakObjectPtr<UInventoryComponent> DropInv;
	UFUNCTION() void HandleDroppedOutside(UDragDropOperation* Operation);
};

// Eén sleepbare/droppbare cel. Bouwt zijn eigen visuele inhoud uit de meegegeven velden, zodat hij als
// los UUserWidget drag/drop-events kan ontvangen. SlotIndex >=0 = hotbar-slot (drop = toewijzen),
// SlotIndex -1 = rooster-cel (drop vanaf hotbar = eraf halen).
UCLASS()
class WEEDSHOPCORE_API UInvCell : public UUserWidget
{
	GENERATED_BODY()
public:
	int32 StackId = 0;        // 0 = leeg
	int32 SlotIndex = -1;     // >=0 = hotbar-slot
	int32 GridCell = -1;      // >=0 = rooster-cel
	bool bDraggable = false;
	FString Line1, Line2;
	FLinearColor Bg = FLinearColor(0.11f, 0.12f, 0.16f, 0.95f);
	float Radius = 8.f;
	// Icoon + accent + aantal-badge (rechtsboven). IconId None = geen icoon (bv. lege cel).
	FName IconId = NAME_None;
	float IconSize = 38.f;
	int32 WaterOverride = -1;                         // >=0 = water van DEZE fles (per-slot vol/leeg); -1 = n.v.t.
	FString Badge;                                   // bv. "x5" of "12g" — getoond als pill rechtsboven
	FString Tag;                                     // korte strain/variant-tag onderaan (OG, Silver Haze, ...)
	FString Tooltip;                                 // info-BODY zonder naam (details-paneel; de zwevende hover-tooltip prefixt de naam zelf)
	FLinearColor Accent = FLinearColor(0.3f, 0.34f, 0.42f);
	bool bSlotNumber = false;                        // hotbar: toon het slotnummer linksboven
	int32 SlotNumber = 0;
	TWeakObjectPtr<UInventoryComponent> Inv;
	TWeakObjectPtr<UInventoryWidget> Owner;
	// Alleen voor de hover-details: een hotbar-DropCell heeft geen Owner (die hoort bij de HotbarWidget) maar
	// kan zo tóch het inventory-details-paneel vullen. Verandert NIETS aan drag/klik/drop (die blijven op Owner).
	TWeakObjectPtr<UInventoryWidget> DetailsOwner;
	UPROPERTY() TObjectPtr<class UBorder> HoverGlow; // glow-overlay bovenop de cel, zichtbaar bij hover
	// Optionele Merge-knop (voor weed met meerdere batches).
	bool bShowMerge = false;
	TFunction<void()> MergeFn;

	// De volledige drop-afhandeling van deze cel (NativeOnDrop roept dit aan). PUBLIEK zodat de container
	// (inventory-grid/hotbar) een drop die in de GAPS tussen de cellen landde naar de dichtstbijzijnde cel
	// kan doorsturen — die gedraagt zich dan exact alsof je er direct op dropte.
	bool HandleDropOp(UDragDropOperation* InOperation);

	// Dichtstbijzijnde cel binnen de snap-drempel (MaxEdgeFrac x cel-maat BUITEN de celrand) van een
	// drop-positie in schermruimte; nullptr = te ver van elke cel (bestaand gedrag laten gelden).
	static UInvCell* FindNearestCell(const TArray<UInvCell*>& Cells, const FVector2D& ScreenPos, float MaxEdgeFrac = 0.45f);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override; // hover: opschalen + details tonen
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
};

UCLASS()
class WEEDSHOPCORE_API UInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);
	void ShowItemDetails(class UInvCell* Cell); // vult het details-paneel links (aangeroepen vanuit UInvCell-hover)
	void BeginDragGhost(class UInvCell* Cell);  // dimt de bron-cel tijdens slepen ("opgepakt"-look); hersteld in NativeTick
	// Optimistische grid-swap bij een drop: wissel twee cel-widgets DIRECT om zodat de move zonder server-round-
	// trip-flikker meteen zichtbaar is. RebuildContent reconcilieert later (signaturen meegewisseld -> no-op).
	void OptimisticGridSwap(int32 CellA, int32 CellB);
	// Optimistische drop-update voor cross-widget moves (bv. hotbar->grid): zet METEEN een item-cel (Fill) of lege
	// cel (Clear) neer zonder op de server-round-trip te wachten; RebuildContent reconcilieert later (sig matcht -> no-op).
	void OptimisticFillCell(int32 cell, int32 StackId);
	void OptimisticClearCell(int32 cell);
	void MarkDirty() { bDirty = true; }
	// Een KLARE droogrek-batch in de inventory laten droppen = oogsten (drag i.p.v. klik).
	bool AcceptDryBatchDrop(int32 EntryIndex);
	// Shift+klik op een stapel -> open de split-popup (slider: hoeveel afsplitsen).
	void OpenSplitPopup(int32 StackId);
	// Voeg alle stapels van dit item samen (sleep een stapel op een gelijke -> mergen).
	void MergeItemNow(FName ItemId);
	// Sleep-merge van TWEE stapels: gelijke kwaliteit -> direct mergen; verschillende THC%/kwaliteit -> eerst bevestigen.
	// Return true als de merge is afgehandeld (direct of via de bevestig-popup).
	bool TryMergeOrConfirm(int32 IntoStackId, int32 FromStackId);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;
	// Container-vangnet: een drop in de gaps/padding TUSSEN de rooster-cellen snapt naar de dichtstbijzijnde
	// cel i.p.v. te "missen" (drag-cancel zou de stapel op de grond droppen). Cellen die de drop zelf al
	// afhandelden consumeren het event — dit vuurt dus nooit dubbel.
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	void BuildShell(UCanvasPanel* Root);
	void RebuildContent();
	// Gedeelde cel-bouw + signatuur (RebuildContent én de optimistische drop-update gebruiken ze, zodat een
	// optimistisch geplaatste cel identiek is aan de na-server-rebuild -> de reconcile slaat 'm naadloos over).
	class UInvCell* BuildGridCellWidget(int32 cell, int32 StackId, bool bShowItem, const FInventoryStack* SPtr, UInventoryComponent* Inv, UPhoneClientComponent* Ph);
	FString GridCellSig(int32 StackId, bool bShowItem, const FInventoryStack* SPtr, UInventoryComponent* Inv) const;

	UFUNCTION()
	void OnInvChanged();

	UInventoryComponent* GetInv() const;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<class UCanvasPanelSlot> CardSlot; // om de inventory naast een paneel (droogrek) te schuiven
	UPROPERTY() TObjectPtr<class USizeBox> StashBox;         // HOME STASH-kolom (verbergen in een machine)
	UPROPERTY() TObjectPtr<UTextBlock> WeightText;
	UPROPERTY() TObjectPtr<class UProgressBar> WeightBar;   // dunne weight-bar in de header (vult op gewicht/max, rood bij bijna-vol)
	UPROPERTY() TObjectPtr<class UBackgroundBlur> BlurBg;   // background-blur achter het venster (premium focus)
	UPROPERTY() TObjectPtr<UWidget> DimBg;                  // donkere dim-overlay achter het venster
	UPROPERTY() TObjectPtr<UWrapBox> Grid;
	UPROPERTY() TObjectPtr<UHorizontalBox> HotbarBox;
	UPROPERTY() TObjectPtr<UTextBlock> SortLabel;
	UPROPERTY() TObjectPtr<class UScrollBox> StashList; // thuis-voorraad uit shelves/chests
	// Item-details-paneel (hover over een slot vult dit; anders toont het de HOME STASH).
	UPROPERTY() TObjectPtr<UWidget> StashContent;
	UPROPERTY() TObjectPtr<UWidget> DetailsContent;
	UPROPERTY() TObjectPtr<class USizeBox> DetailsIconBox;
	UPROPERTY() TObjectPtr<UTextBlock> DetailsName;
	UPROPERTY() TObjectPtr<UTextBlock> DetailsBody;
	UPROPERTY() TObjectPtr<UWidget> DetailsSplitBtn;
	int32 DetailsStackId = 0;
	TWeakObjectPtr<class UInvCell> DragGhostCell; // bron-cel die gedimd is tijdens slepen

	// Vult het thuis-voorraad-lijstje (alle shelves/chests samengeteld per strain).
	void RebuildStash();

	// --- Split-popup (slider) ---
	void BuildSplitPopup(UCanvasPanel* Root);
	UFUNCTION() void OnSplitSliderChanged(float V);
	void ConfirmSplit();
	void CancelSplit();
	UPROPERTY() TObjectPtr<UWidget> SplitRoot;          // hele overlay (Collapsed als dicht)
	UPROPERTY() TObjectPtr<class USlider> SplitSlider;
	UPROPERTY() TObjectPtr<UTextBlock> SplitLabel;
	int32 SplitStackId = 0;
	int32 SplitTotal = 0;

	// --- Merge-confirm-popup (alleen als de twee stapels VERSCHILLENDE THC%/kwaliteit% hebben;
	//     gelijke kwaliteit merget direct, want dat is verliesvrij) ---
	void BuildMergePopup(UCanvasPanel* Root);
	void ConfirmMerge();
	void CancelMerge();
	UPROPERTY() TObjectPtr<UWidget> MergeRoot;     // hele overlay (Collapsed als dicht)
	UPROPERTY() TObjectPtr<UTextBlock> MergeLabel; // toont het gewogen-gemiddelde-resultaat
	int32 PendingMergeInto = 0;
	int32 PendingMergeFrom = 0;

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;
	TWeakObjectPtr<UInventoryComponent> BoundInv;
	bool bDirty = true;
	FString LastStashSig = TEXT("\x01"); // HOME STASH alleen herbouwen als de shelf-inhoud ECHT wijzigt (niet bij elke backpack-sleep)
	// Vaste cel-slots: éénmalig gebouwd, daarna vervangen we alleen de inhoud van cellen die ECHT wijzigden
	// (per-cel signatuur). Geen ClearChildren meer -> geen grid-flikker bij elke sleep.
	UPROPERTY() TArray<TObjectPtr<class USizeBox>> CellBoxes;
	TArray<FString> CellSigs;
	int32 SortMode = 0; // 0=naam, 1=aantal, 2=categorie
};
