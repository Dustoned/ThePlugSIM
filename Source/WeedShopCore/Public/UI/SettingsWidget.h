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
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void RefreshTabs();
	void RefreshContent();
	void ShowRestartPopup(); // toont de "herstart nodig"-popup (schaduw-grens-wissel Potato <-> hoger)

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
	UPROPERTY() TObjectPtr<UWidget> RestartPopup;  // modale "herstart nodig"-popup, standaard verborgen
	UPROPERTY() TObjectPtr<class UWeedActionButton> TabGraphics;
	UPROPERTY() TObjectPtr<class UWeedActionButton> TabGame;
	UPROPERTY() TObjectPtr<class UWeedActionButton> TabControls;
	UPROPERTY() TObjectPtr<class UWeedActionButton> TabAudio;

	// Game-sliders (gepolld in NativeTick zodat slepen live toepast, met afronding tegen config-spam).
	UPROPERTY() TObjectPtr<class USlider> FovSlider;
	UPROPERTY() TObjectPtr<class USlider> SensSlider;
	UPROPERTY() TObjectPtr<UTextBlock> FovVal;
	UPROPERTY() TObjectPtr<UTextBlock> SensVal;
	UPROPERTY() TObjectPtr<class UComboBoxString> ResCombo;
	int32 LastFovApplied = -1;
	int32 LastSensApplied = -1; // sensitivity * 10

	// Audio-sliders (0..1) per categorie; gepolld in NativeTick en opgeslagen in GConfig.
	UPROPERTY() TObjectPtr<class USlider> VolUiSlider;
	UPROPERTY() TObjectPtr<class USlider> VolGameSlider;
	UPROPERTY() TObjectPtr<class USlider> VolMusicSlider;
	UPROPERTY() TObjectPtr<UTextBlock> VolUiVal;
	UPROPERTY() TObjectPtr<UTextBlock> VolGameVal;
	UPROPERTY() TObjectPtr<UTextBlock> VolMusicVal;
	int32 LastVolUi = -1, LastVolGame = -1, LastVolMusic = -1; // procenten

	int32 Category = 0; // 0 = Graphics, 1 = Game, 2 = Controls, 3 = Audio
	bool bLastOpen = false;

	// Key-rebinding (Controls-tab): klik een toets, druk de nieuwe in.
	bool bRebinding = false;
	bool bRebindAlt = false;
	FName RebindAction;
	FString RebindMsg;
};
