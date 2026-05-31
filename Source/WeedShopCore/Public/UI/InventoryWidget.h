// UInventoryWidget — inventory-scherm (UMG): item-tegels + gewicht/slots + hotbar-rij. Klik een
// item om 'm op de hotbar te zetten, klik een hotbar-slot om 'm eraf te halen. Weed met meerdere
// batches krijgt een Merge-knop. Leest/zet de InventoryComponent op de pawn.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryWidget.generated.h"

class UPhoneClientComponent;
class UInventoryComponent;
class UCanvasPanel;
class UWidget;
class UTextBlock;
class UWrapBox;
class UHorizontalBox;

UCLASS()
class WEEDSHOPCORE_API UInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

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

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;
	TWeakObjectPtr<UInventoryComponent> BoundInv;
	bool bDirty = true;
};
