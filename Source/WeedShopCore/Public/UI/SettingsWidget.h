// USettingsWidget — instellingen-scherm met Graphics (window mode, kwaliteit, VSync, FPS-limiet)
// en Game (FOV, muisgevoeligheid). Graphics via UGameUserSettings; game-instellingen via de
// PhoneClientComponent op de pawn. Zichtbaarheid volgt UPhoneClientComponent::IsSettingsOpen().

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SettingsWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UVerticalBox;
class UTextBlock;

UCLASS()
class WEEDSHOPCORE_API USettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void RefreshTabs();
	void RefreshContent();

	UPhoneClientComponent* GetPhone() const;

	// Rij-helper: label links + een knop rechts met de huidige waarde (klik = OnClick).
	void AddValueRow(const FString& Label, const FString& Value, TFunction<void()> OnClick);
	// Rij-helper: label + slider + waarde-tekst. Geeft de slider terug (waarde 0..1).
	class USlider* AddSliderRow(const FString& Label, float Normalized, TObjectPtr<class UTextBlock>& OutValue);
	// Rij-helper: label + resolutie-dropdown.
	void AddResolutionRow();

	UFUNCTION()
	void OnResolutionChanged(FString Item, ESelectInfo::Type SelectType);

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;     // rechter inhoud (per categorie)
	UPROPERTY() TObjectPtr<class UWeedActionButton> TabGraphics;
	UPROPERTY() TObjectPtr<class UWeedActionButton> TabGame;
	UPROPERTY() TObjectPtr<class UWeedActionButton> TabControls;

	// Game-sliders (gepolld in NativeTick zodat slepen live toepast, met afronding tegen config-spam).
	UPROPERTY() TObjectPtr<class USlider> FovSlider;
	UPROPERTY() TObjectPtr<class USlider> SensSlider;
	UPROPERTY() TObjectPtr<UTextBlock> FovVal;
	UPROPERTY() TObjectPtr<UTextBlock> SensVal;
	UPROPERTY() TObjectPtr<class UComboBoxString> ResCombo;
	int32 LastFovApplied = -1;
	int32 LastSensApplied = -1; // sensitivity * 10

	int32 Category = 0; // 0 = Graphics, 1 = Game, 2 = Controls
	bool bLastOpen = false;
};
