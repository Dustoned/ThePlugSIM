// UDealResultPopupWidget - losse "floating" chips rond het hoofd/schouders van een klant-NPC na
// een GESLAAGDE deal. Toont de echte winst-cijfers: +EUR (geld), +XP, +respect, +loyalty, +hooked
// (verslaving). Ankert zich in NativeTick aan de wereld-locatie van de klant (ProjectWorldLocation-
// ToScreen), laat de chips licht omhoog drijven, faded in/uit en verwijdert zichzelf.
// Client-side only; per-speler gespawnd via UPhoneClientComponent::ClientDealResultPopup.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DealResultPopupWidget.generated.h"

class UWidget;
class UBorder;
class UCanvasPanel;
class ACustomerBase;

UCLASS()
class WEEDSHOPCORE_API UDealResultPopupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Toon de popup voor een geslaagde deal. Customer = wereld-anker (kan null zijn -> dan is
	// AnchorWorld het vaste ankerpunt). Cents = uitbetaald geld (centen), XP = verdiende XP,
	// dR/dL/dA = respect/loyalty/verslaving-deltas. Regels met delta 0 worden weggelaten.
	void ShowResult(ACustomerBase* Customer, const FVector& AnchorWorld, int32 Cents, int32 XP, int32 dR, int32 dL, int32 dA);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void ClearChips();
	// Voeg een losse chip toe. Offset is canvas-space t.o.v. het hoofdanker.
	void AddChip(const FString& Text, const FLinearColor& Color, const FVector2D& Offset, bool bBig = false, const FString& IconName = FString(), bool bKitIcon = false);

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> Chips;

	TArray<FVector2D> ChipOffsets;
	TArray<float> ChipDelays;

	TWeakObjectPtr<ACustomerBase> AnchorCustomer; // wereld-anker (of null -> vaste locatie)
	FVector FallbackWorld = FVector::ZeroVector;  // ankerpunt als de actor-pointer ontbreekt/verdwijnt

	float Age = 0.f;      // seconden sinds ShowResult
	float LifeTime = 0.f; // totale levensduur (0 = inactief)
	bool bActive = false;
};
