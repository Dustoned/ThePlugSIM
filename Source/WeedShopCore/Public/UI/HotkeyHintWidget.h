// UHotkeyHintWidget — vast hulplijstje rechtsonder met de toetsen die je NU kunt gebruiken (zoals
// het Fortnite-bouwmenu, maar rechtsonder). De lijst past zich aan de context aan: lopen, een item
// in de hand, mikken op een pot/klant, plaats-modus, of een open UI. Alleen-lezen (niet klikbaar).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HotkeyHintWidget.generated.h"

class UCanvasPanel;
class UVerticalBox;

UCLASS()
class WEEDSHOPCORE_API UHotkeyHintWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	// Voegt een rij (toets-chip + omschrijving) toe aan de lijst.
	void AddRow(const FString& Key, const FString& Action);

	UPROPERTY() TObjectPtr<UVerticalBox> List;

	// Laatst getoonde set (om alleen te herbouwen als de context wijzigt -> geen flicker).
	FString LastSig;
};
