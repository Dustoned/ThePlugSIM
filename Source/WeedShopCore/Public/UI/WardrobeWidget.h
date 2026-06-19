// UWardrobeWidget - outfit-menu van de kledingkast (Wardrobe-placeable). Kies je body (Male/Female/
// Girl 1-3) en voor de Casual-meisjes per slot (Top/Pants/Shoes/Hair) een kledingstuk uit de
// OutfitCatalog. Met live studio-preview van je poppetje: slepen = draaien, scrollen = zoomen.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WardrobeWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UVerticalBox;
class USkeletalMeshComponent;

UCLASS()
class WEEDSHOPCORE_API UWardrobeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	// Muis op de preview: slepen = draaien, scrollen = zoomen.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	void BuildShell(UCanvasPanel* Root);
	void FillBody();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	// --- Live studio-preview: kloon van je poppetje (eigen verlichting, los van het FP-rendersysteem) ---
	UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> PreviewRT;
	UPROPERTY() TObjectPtr<class UImage> PreviewImage;
	TWeakObjectPtr<class ASkeletalMeshActor> PreviewActor;   // de kloon (body + outfit-parts)
	TWeakObjectPtr<class ASceneCapture2D> PreviewCapture;    // camera + lampen (orbit rond de kloon)
	FString PreviewOutfitSig;  // kloon herbouwen zodra skin/outfit wijzigt
	float PreviewYaw = 0.f;    // orbit-hoek (horizontaal slepen)
	float PreviewDist = 280.f; // camera-afstand (zoomen)
	float PreviewFocusZ = 92.f; // kijk-hoogte (verticaal slepen: hoofd <-> schoenen)
	bool bPreviewDrag = false;
	void EnsurePreview();          // capture + kloon up-to-date houden (elke tick zolang open)
	void RebuildPreviewActor();    // kloon (body + parts) opnieuw opbouwen
	void UpdatePreviewCamera();    // orbit-camera positioneren
	void ReleasePreview();         // alles opruimen bij sluiten
	bool IsOverPreview(const FPointerEvent& Ev) const;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;

	// In-place updates: Body wordt alleen herbouwd bij een STRUCTURELE wijziging (legacy/female/male). Een
	// outfit-keuze (</>) of model-wissel update alleen de betreffende tekst/knop-kleur -> geen flikker.
	TMap<int32, TWeakObjectPtr<class UTextBlock>> SlotNameTexts; // SlotIdx -> "naam (cur/count)"-tekst
	TArray<TWeakObjectPtr<class UWeedActionButton>> ModelButtons; // Girl 1/2/3-knoppen
	TArray<uint8> ModelButtonSkins;                               // parallel: skin-index per knop
	void UpdateSlotText(int32 SlotIdx);          // herbereken + SetText op de slot-naam in plaats
	void RecolorModelButtons(uint8 ActiveSkin);  // actieve model-knop oplichten zonder rebuild

	FString LastSig; // herbouw alleen bij STRUCTURELE wijziging (geen flicker)
};
