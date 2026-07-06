// UShelfWidget — opslag-schap-menu met SLEEP-slots. Twee kolommen: links de inhoud van het schap,
// rechts je eigen voorraad. Sleep een item van de ene kolom naar de andere om op te slaan/eruit te
// halen. Stacks behouden hun THC/kwaliteit. Server-authoritative via de telefoon.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "ShelfWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UScrollBox;
class UShelfWidget;
class UWrapBox;
class USizeBox;
class UVerticalBox;
class UWeedItemPickGrid;
class AStorageShelf;

// Sleep-payload voor het schap-scherm.
UCLASS()
class WEEDSHOPCORE_API UShelfDragOp : public UDragDropOperation
{
	GENERATED_BODY()
public:
	UPROPERTY() bool bFromShelf = false; // true = uit het schap gesleept, false = uit je inventory
	UPROPERTY() int32 ShelfIndex = -1;   // schap-slot (alleen bij bFromShelf)
	UPROPERTY() FName ItemId = NAME_None;
	UPROPERTY() int32 Qty = 0;
};

// Eén sleepbare/droppbare cel in het schap-scherm.
UCLASS()
class WEEDSHOPCORE_API UShelfCell : public UUserWidget
{
	GENERATED_BODY()
public:
	bool bShelfSide = false;    // in welke kolom deze cel staat (true = schap, false = inventory)
	int32 ShelfIndex = -1;      // schap-slot-index (alleen schap-cellen)
	FName ItemId = NAME_None;   // None = lege "sleep hierheen"-cel (alleen drop-doel)
	int32 Qty = 0;
	float Thc = 0.f;
	FString Badge;
	FString Tooltip; // fallback-override; normaal zet FillBody de tooltip direct via WeedUI::ItemTooltipText (ND7.6)
	float IconSize = 46.f;
	TWeakObjectPtr<UShelfWidget> Owner;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
};

// Hele kolom als drop-zone: droppen ergens in het vak telt (niet alleen precies op een cel).
UCLASS()
class WEEDSHOPCORE_API UShelfDropZone : public UUserWidget
{
	GENERATED_BODY()
public:
	bool bShelfSide = false;
	TWeakObjectPtr<UShelfWidget> Owner;
	UPROPERTY() TObjectPtr<UWidget> Inner; // de kolom-inhoud (zet vóór toevoegen aan de tree)
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
};

UCLASS()
class WEEDSHOPCORE_API UShelfWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);
	// Afhandeling van een drop: bDroppedOnShelfSide = de kolom waar je losliet (true=schap -> opslaan).
	void HandleShelfDrop(bool bDroppedOnShelfSide, class UShelfDragOp* Op);
	// Een stapel uit je ECHTE inventory (UInvDragOp) in het schap droppen -> opslaan. (De echte inventory
	// staat ernaast open, net als bij de droogrek; uit het schap halen doe je door een schap-item op je
	// inventory te slepen - dat handelt de InventoryWidget zelf af.)
	void HandleInvStore(class UInvDragOp* Op);
	// ND7.14: shift+klik op een schap-cel = de hele stapel DIRECT terug naar je inventory (geen popup).
	void QuickTakeToInventory(int32 ShelfIndex, int32 Qty);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void FillBody();
	void RebuildFridgeSection(AStorageShelf* Shelf); // fridge-edible-knoppen (apart van de gediffte grid)

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UScrollBox> ShelfList; // links: schap-inhoud
	UPROPERTY() TObjectPtr<UScrollBox> InvList;    // rechts: je eigen voorraad
	UPROPERTY() TObjectPtr<class UTextBlock> TitleText;

	// Persistente grid + cel-pool -> per FillBody alleen gewijzigde cellen vervangen (geen ClearChildren -> geen flash).
	UPROPERTY() TObjectPtr<UWrapBox> ShelfGrid;
	UPROPERTY() TArray<TObjectPtr<USizeBox>> ShelfCellBoxes;
	TArray<FString> ShelfCellSigs;
	UPROPERTY() TObjectPtr<UVerticalBox> FridgeSection;

	// Fridge "Make edibles": icoon-grid-picker + in-place status/hint-teksten (éénmalig gebouwd, per refresh
	// alleen SetItems/SetText/SetVisibility -> geen ClearChildren-flash op een klik).
	UPROPERTY() TObjectPtr<UWeedItemPickGrid> FridgePick;
	UPROPERTY() TObjectPtr<class UTextBlock> FridgeTitleText;  // "Make edibles"-kop
	UPROPERTY() TObjectPtr<class UTextBlock> FridgeHintText;   // uitleg als er geen ButterMix ligt
	UPROPERTY() TObjectPtr<class UTextBlock> FridgeStatusText; // "Setting N batches..."-regel

	FString LastSig; // herbouw alleen bij wijziging
};
