// UCrosshairWidget — klein, schoon wit stipje midden in beeld als crosshair. Verbergt zich als er
// UI/menu open is (telefoon, inventory, pauze, titelscherm).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairWidget.generated.h"

class UCanvasPanel;
class UWidget;
class UBorder;
class APawn;
class UPhoneClientComponent;
class UInteractionComponent;
class UBuildComponent;

UCLASS()
class WEEDSHOPCORE_API UCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	UPROPERTY() TObjectPtr<UWidget> Dot;
	UPROPERTY() TObjectPtr<UBorder> DotBorder;
	UPROPERTY() TObjectPtr<UWidget> Ring;
	UPROPERTY() TObjectPtr<UBorder> RingBorder;
	int32 LastVisualState = -1;

	// Gecachte pawn-componenten: 1x opzoeken i.p.v. FindComponentByClass per frame;
	// herzoeken alleen bij pawn-wissel of stale pointer.
	TWeakObjectPtr<APawn> CachedPawn;
	TWeakObjectPtr<UPhoneClientComponent> CachedPhone;
	TWeakObjectPtr<UInteractionComponent> CachedInteraction;
	TWeakObjectPtr<UBuildComponent> CachedBuild;
};
