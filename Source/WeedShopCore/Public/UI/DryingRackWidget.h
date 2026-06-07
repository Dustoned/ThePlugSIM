// UDryingRackWidget — droogrek-scherm met SLEEP-slots. Links de batches die hangen te drogen (icon-cel
// met progress-balkje); rechts je natte wiet uit je inventory. Sleep natte wiet -> het rek = ophangen;
// sleep een KLARE batch -> je inventory = oogsten. Server-authoritative via de telefoon.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "DryingRackWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UScrollBox;
class UTextBlock;
class UProgressBar;
class UDryingRackWidget;

// Sleep-payload voor het droogrek.
UCLASS()
class WEEDSHOPCORE_API UDryDragOp : public UDragDropOperation
{
	GENERATED_BODY()
public:
	UPROPERTY() bool bWet = false;       // true = natte wiet uit inventory (ophangen), false = drogende batch (oogsten)
	UPROPERTY() int32 EntryIndex = -1;   // rack-batch-index (alleen bij !bWet)
	UPROPERTY() FName ItemId = NAME_None;
	UPROPERTY() int32 Qty = 0;
};

// Eén sleepbare cel (de visuele inhoud wordt los meegegeven via Inner zodat een drogende cel een
// progress-balkje kan tonen). Drogende cellen zijn alleen sleepbaar als ze klaar zijn.
UCLASS()
class WEEDSHOPCORE_API UDryCell : public UUserWidget
{
	GENERATED_BODY()
public:
	bool bWet = false;          // welke kolom: true = inventory (nat), false = drogende batch
	int32 EntryIndex = -1;
	FName ItemId = NAME_None;
	int32 Qty = 0;
	bool bReady = false;        // drogende cel: klaar om te oogsten (alleen dan sleepbaar)
	bool bDraggableAlways = false; // inventory-cel: altijd sleepbaar (ook niet-natte items)
	TWeakObjectPtr<UDryingRackWidget> Owner;
	UPROPERTY() TObjectPtr<UWidget> Inner;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
};

// Hele kolom als drop-zone.
UCLASS()
class WEEDSHOPCORE_API UDryDropZone : public UUserWidget
{
	GENERATED_BODY()
public:
	bool bDryingSide = false; // true = de rek-kolom (drop natte wiet hier = ophangen)
	TWeakObjectPtr<UDryingRackWidget> Owner;
	UPROPERTY() TObjectPtr<UWidget> Inner;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
};

UCLASS()
class WEEDSHOPCORE_API UDryingRackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);
	// bDroppedOnDryingSide = de kolom waar je losliet (true=rek -> ophangen, false=inventory -> oogsten).
	void HandleDryDrop(bool bDroppedOnDryingSide, class UDryDragOp* Op);
	// Drop vanuit de inventory/hotbar (UInvDragOp) op het rek: alleen natte wiet wordt opgehangen.
	void HandleInvDrop(bool bDroppedOnDryingSide, class UInvDragOp* Op);
	// Klik op een klare batch -> oogsten naar je inventory.
	void CollectReady(int32 EntryIndex);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void FillBody();
	void UpdateProgress(); // elke tick: bars + resterende-tijd labels bijwerken

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UScrollBox> DryList; // links: drogende + klare batches
	UPROPERTY() TObjectPtr<UScrollBox> WetList; // rechts: natte wiet uit inventory
	UPROPERTY() TObjectPtr<UTextBlock> TitleText;

	// Per drogende-batch-cel: progress-bar + statuslabel + de batch-index in de rack-Entries.
	UPROPERTY() TArray<TObjectPtr<UProgressBar>> RowBars;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> RowStatus;
	TArray<int32> RowEntryIndex;

	FString LastSig; // herbouw alleen bij wijziging
};
