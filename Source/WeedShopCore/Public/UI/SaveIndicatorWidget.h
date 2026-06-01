// USaveIndicatorWidget — kleine save-melding rechtsboven: een draaiend icoon + "Saving..." en daarna
// "Saved", zichtbaar voor iedereen in de game (leest de gerepliceerde SaveCounter op de GameState).
// Werkt zowel tijdens het spelen als over menu's heen (hoge z-order).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SaveIndicatorWidget.generated.h"

class UWidget;
class UTextBlock;

UCLASS()
class WEEDSHOPCORE_API USaveIndicatorWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(class UCanvasPanel* Root);

	UPROPERTY() TObjectPtr<UWidget> Box;        // het hele melding-vakje (tonen/verbergen)
	UPROPERTY() TObjectPtr<UWidget> Spinner;    // draaiend icoon
	UPROPERTY() TObjectPtr<UTextBlock> Label;

	int32 LastCounter = -1;   // laatst geziene SaveCounter
	float Timer = -1.f;       // < 0 = inactief; anders sec sinds de save begon
	bool bInit = false;
};
