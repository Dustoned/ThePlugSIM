// UWardrobeWidget - outfit-menu van de kledingkast (Wardrobe-placeable). Kies je body (Male/Female/
// Girl 1-3) en voor de Casual-meisjes per slot (Top/Pants/Shoes/Hair) een kledingstuk uit de
// OutfitCatalog. Wijzigingen worden direct toegepast (gerepliceerd) en opgeslagen in de save.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WardrobeWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UVerticalBox;

UCLASS()
class WEEDSHOPCORE_API UWardrobeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void FillBody();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	// Live preview: SceneCapture die ALLEEN jouw character rendert (vooraanzicht) naar een render target.
	UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> PreviewRT;
	UPROPERTY() TObjectPtr<class UImage> PreviewImage;
	TWeakObjectPtr<class ASceneCapture2D> PreviewCapture;
	void EnsurePreview();   // capture spawnen/positioneren (elke tick zolang open)
	void ReleasePreview();  // capture opruimen bij sluiten

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;

	FString LastSig; // herbouw alleen bij wijziging (geen flicker)
};
