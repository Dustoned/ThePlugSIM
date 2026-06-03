// UCrosshairWidget — klein, schoon wit stipje midden in beeld als crosshair. Verbergt zich als er
// UI/menu open is (telefoon, inventory, pauze, titelscherm).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairWidget.generated.h"

class UCanvasPanel;
class UWidget;

UCLASS()
class WEEDSHOPCORE_API UCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	UPROPERTY() TObjectPtr<UWidget> Dot;
};
