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

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void RebuildContent();

	UFUNCTION()
	void OnInvChanged();

	UInventoryComponent* GetInv() const;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UTextBlock> WeightText;
	UPROPERTY() TObjectPtr<UWrapBox> Grid;
	UPROPERTY() TObjectPtr<UHorizontalBox> HotbarBox;
	UPROPERTY() TObjectPtr<UTextBlock> SortLabel;

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;
	TWeakObjectPtr<UInventoryComponent> BoundInv;
	bool bDirty = true;
	int32 SortMode = 0; // 0=naam, 1=aantal, 2=categorie
};
