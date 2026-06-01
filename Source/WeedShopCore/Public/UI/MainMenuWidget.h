// UMainMenuWidget — het titelscherm ("THE PLUG SIMULATOR"). Volledig dekkend overlay met
// Start / Continue (load) / Settings / Quit. Wordt bij het opstarten getoond en is opnieuw te
// openen via het pauze-menu ("Main menu"). Zichtbaarheid volgt UPhoneClientComponent.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UTextBlock;
class UBorder;

UCLASS()
class WEEDSHOPCORE_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	void OnStart();      // Continue / New Game: gewoon het spel in
	void OnContinue();   // Load save
	void OnSettings();
	void OnCredits();
	void OnQuit();

	// Maakt een flikkerende neon-"lamp": gekleurde, afgeronde glow die zacht pulseert.
	UBorder* AddGlow(class UOverlay* Layers, const FLinearColor& Color, float W, float H,
		EHorizontalAlignment HA, EVerticalAlignment VA, const FMargin& Pad, float Freq);

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Backdrop;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<class UWeedActionButton> ContinueBtn;

	// Vanaf schijf geladen achtergrond-foto + logo + knop-swatch (losse PNG's in Content/_Project/UI).
	UPROPERTY() TObjectPtr<class UTexture2D> BgTex;
	UPROPERTY() TObjectPtr<class UTexture2D> LogoTex;
	UPROPERTY() TObjectPtr<class UTexture2D> SwatchTex; // wit masker uit de verf-streep (kleurbaar)

	// Flikkerende neon-lampen.
	UPROPERTY() TArray<TObjectPtr<UBorder>> Glows;
	TArray<FLinearColor> GlowBase;
	TArray<float> GlowPhase;
	TArray<float> GlowFreq;

	bool bLastOpen = false;
};
