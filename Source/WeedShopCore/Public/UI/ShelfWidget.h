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
	FString Tooltip;
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

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void FillBody();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UScrollBox> ShelfList; // links: schap-inhoud
	UPROPERTY() TObjectPtr<UScrollBox> InvList;    // rechts: je eigen voorraad
	UPROPERTY() TObjectPtr<class UTextBlock> TitleText;

	FString LastSig; // herbouw alleen bij wijziging
};
