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
	// Bouwt de 4 categorie-panelen ÉÉN keer (persistent). Daarna wisselt een tab alleen Visibility -> geen teardown/flash.
	void BuildAllPanels();
	void BuildGraphicsPanel(class UVerticalBox* Into);
	void BuildGamePanel(class UVerticalBox* Into);
	void BuildAudioPanel(class UVerticalBox* Into);
	void BuildControlsPanel(class UVerticalBox* Into);
	// Toont het actieve categorie-paneel (Visibility) i.p.v. de body opnieuw op te bouwen.
	void ShowActiveCategory();
	// Ververst ALLEEN de tekst van de Graphics cycle-labels naar de live GUS-status (bv. na alt-enter/OS-wissel).
	// Geen widget-constructie -> geen flash; enkel SetText op bestaande labels bij (her)openen.
	void RefreshGraphicsLabels();
	void ShowRestartPopup(); // toont de "herstart nodig"-popup (schaduw-grens-wissel Potato <-> hoger)

	UPhoneClientComponent* GetPhone() const;

	// Rij-helper: label links + een knop rechts met de huidige waarde (klik = OnClick). OutValueLabel = het
	// tekstblok IN de knop, zodat de klik-lambda later alleen dié tekst kan bijwerken (geen rebuild).
	void AddValueRow(class UVerticalBox* Into, const FString& Label, const FString& Value, TFunction<void()> OnClick, TObjectPtr<class UTextBlock>* OutValueLabel = nullptr);
	// Rij-helper: label + resolutie-dropdown.
	void AddResolutionRow(class UVerticalBox* Into);
	// Rij-helper: label + kit-W_Toggle (bool, gepolld in NativeTick via IsToggled-reflectie). OutW = het kit-widget
	// (voor het in-place duwen van een waarde vanuit de Preset-rij via reflectie, zonder het te herbouwen).
	void AddKitToggle(class UVerticalBox* Into, const FString& Label, bool Initial, TFunction<void(bool)> Apply, TWeakObjectPtr<UUserWidget>* OutW = nullptr);
	// Rij-helper: label + kit-W_Slider + waarde-tekst (genormaliseerde Value 0-1 via reflectie, gepolld).
	// Apply(norm, key&) mapt de waarde, past 'm toe als key veranderde, en geeft de display-string (leeg = onveranderd).
	void AddKitSlider(class UVerticalBox* Into, const FString& Label, double InitialNorm, TFunction<FString(double, int32&)> Apply);
	// Duwt een waarde in een bestaand kit-W_Toggle (IsToggled via reflectie) + houdt de poll-tracker in sync
	// (geen dubbele Apply). Voor afgeleide waarden die de Preset-rij in de losse toggles zet, zonder rebuild.
	void SetKitToggleValueInPlace(TWeakObjectPtr<UUserWidget> W, bool bValue);

	UFUNCTION()
	void OnResolutionChanged(FString Item, ESelectInfo::Type SelectType);

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;     // rechter inhoud (bevat de 4 categorie-panelen)
	UPROPERTY() TObjectPtr<UWidget> RestartPopup;  // modale "herstart nodig"-popup, standaard verborgen
	UPROPERTY() TObjectPtr<class UTextBlock> SavedMsg; // "Saved"-cue in de footer (opacity-fade), i.p.v. een rebuild bij opslaan
	float SavedMsgOpacity = 0.f;                    // fade-teller voor de Saved-cue

	// De 4 categorie-panelen (één keer gebouwd). Tab-wissel = alleen Visibility togglen.
	UPROPERTY() TObjectPtr<class UVerticalBox> PanelGraphics;
	UPROPERTY() TObjectPtr<class UVerticalBox> PanelGame;
	UPROPERTY() TObjectPtr<class UVerticalBox> PanelControls;
	UPROPERTY() TObjectPtr<class UVerticalBox> PanelAudio;
	bool bPanelsBuilt = false;

	// --- Persistente waarde-labels van de Graphics cycle-rijen (klik = SetText alleen dit label) ---
	UPROPERTY() TObjectPtr<class UTextBlock> WindowModeVal;
	UPROPERTY() TObjectPtr<class UTextBlock> PresetVal;
	UPROPERTY() TObjectPtr<class UTextBlock> ResScaleVal;
	UPROPERTY() TObjectPtr<class UTextBlock> FrameLimitVal;
	UPROPERTY() TObjectPtr<class UTextBlock> TexturesVal;
	UPROPERTY() TObjectPtr<class UTextBlock> RendererVal; // "DirectX 12" / "DirectX 11" (RHI-voorkeur, geldt na herstart)
	UPROPERTY() TObjectPtr<class UTextBlock> CharacterVal;

	// Kit-widgets waar de Preset-rij een afgeleide waarde in duwt (via reflectie, geen rebuild).
	TWeakObjectPtr<UUserWidget> ShadowsToggleW;
	TWeakObjectPtr<UUserWidget> LumenToggleW;
	TWeakObjectPtr<UUserWidget> RayTracingToggleW; // "Ray tracing (experimental)" - eigen RTOff-vlag, presets raken 'm niet
	TWeakObjectPtr<UUserWidget> MotionBlurToggleW;

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

	// Kit-toggles (W_Toggle uit de Minimalist-kit): per-setting bool, gepolld in NativeTick (IsToggled via reflectie).
	struct FKitToggle { TWeakObjectPtr<UUserWidget> W; bool Last = false; TFunction<void(bool)> Apply; };
	TArray<FKitToggle> KitToggles;

	// Kit-sliders (W_Slider): genormaliseerde Value 0-1 via reflectie, gepolld; Apply mapt+past toe+geeft display.
	struct FKitSlider { TWeakObjectPtr<UUserWidget> W; int32 LastKey = MIN_int32; TFunction<FString(double, int32&)> Apply; TWeakObjectPtr<UTextBlock> ValText; };
	TArray<FKitSlider> KitSliders;

	int32 Category = 0; // 0 = Graphics, 1 = Game, 2 = Controls, 3 = Audio
	bool bLastOpen = false;

	// Key-rebinding (Controls-tab): klik een toets, druk de nieuwe in.
	bool bRebinding = false;
	bool bRebindAlt = false;
	FName RebindAction;
	FString RebindMsg;

	// --- Persistente Controls-lijst: per-actie key-knoppen ÉÉN keer gebouwd; rebind/capture SetText alleen
	// de 1-2 getroffen knoppen + de RebindMsg-tekst -> geen ClearChildren, scroll blijft staan. ---
	struct FKeyBtn { TWeakObjectPtr<class UWeedActionButton> Btn; TWeakObjectPtr<UTextBlock> Label; FName Action; bool bAlt = false; };
	TArray<FKeyBtn> KeyButtons;
	UPROPERTY() TObjectPtr<UTextBlock> RebindMsgText; // "cleared/rebound"-cue onder de lijst (in-place SetText)
	// Werk de key-knoppen bij die deze actie tonen (main+alt) + de RebindMsg-tekst. Geen lijst-teardown.
	void RefreshKeyButtonsFor(FName Action);
	void RefreshAllKeyButtons(); // na Reset-to-defaults: alle labels opnieuw zetten (geen rebuild)
	void UpdateKeyButtonLabel(const FKeyBtn& KB);
	void UpdateRebindMsg();
};
