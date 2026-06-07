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
	FString Badge;                                   // bv. "x5" of "12g" — getoond als pill rechtsboven
	FString Tooltip;                                 // volledige naam + details (hover)
	FLinearColor Accent = FLinearColor(0.3f, 0.34f, 0.42f);
	bool bSlotNumber = false;                        // hotbar: toon het slotnummer linksboven
	int32 SlotNumber = 0;
	TWeakObjectPtr<UInventoryComponent> Inv;
	TWeakObjectPtr<UInventoryWidget> Owner;
	// Optionele Merge-knop (voor weed met meerdere batches).
	bool bShowMerge = false;
	TFunction<void()> MergeFn;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
};

UCLASS()
class WEEDSHOPCORE_API UInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);
	void MarkDirty() { bDirty = true; }
	// Een KLARE droogrek-batch in de inventory laten droppen = oogsten (drag i.p.v. klik).
	bool AcceptDryBatchDrop(int32 EntryIndex);
	// Shift+klik op een stapel -> open de split-popup (slider: hoeveel afsplitsen).
	void OpenSplitPopup(int32 StackId);
	// Voeg alle stapels van dit item samen (sleep een stapel op een gelijke -> mergen).
	void MergeItemNow(FName ItemId);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void RebuildContent();

	UFUNCTION()
	void OnInvChanged();

	UInventoryComponent* GetInv() const;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<class UCanvasPanelSlot> CardSlot; // om de inventory naast een paneel (droogrek) te schuiven
	UPROPERTY() TObjectPtr<class USizeBox> StashBox;         // HOME STASH-kolom (verbergen in een machine)
	UPROPERTY() TObjectPtr<UTextBlock> WeightText;
	UPROPERTY() TObjectPtr<UWrapBox> Grid;
	UPROPERTY() TObjectPtr<UHorizontalBox> HotbarBox;
	UPROPERTY() TObjectPtr<UTextBlock> SortLabel;
	UPROPERTY() TObjectPtr<class UScrollBox> StashList; // thuis-voorraad uit shelves/chests

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

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;
	TWeakObjectPtr<UInventoryComponent> BoundInv;
	bool bDirty = true;
	int32 SortMode = 0; // 0=naam, 1=aantal, 2=categorie
};
