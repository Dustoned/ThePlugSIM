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

	// Vraag om bij het tonen meteen de slot-picker te openen (1 = New, 2 = Load). Voor pauze -> Load.
	void RequestPicker(int32 Mode) { PendingPickerMode = Mode; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	void OnStart();      // (ongebruikt sinds slot-picker; behouden voor compat)
	void OnContinue();   // Continue: laatst gebruikte slot laden
	void OnSettings();
	void OnCredits();
	void OnQuit();

	// Slot-picker (3 saves): Mode 1 = New Game, 2 = Load.
	void OpenPicker(int32 Mode);
	void ClosePicker();
	void RefreshSlots();
	void OnSlotChosen(int32 SlotIdx);                 // New Game / Load handmatige save
	void OnLoadAutosave(int32 SlotIdx);               // Load de autosave van dit slot

	// Maakt een flikkerende neon-"lamp": gekleurde, afgeronde glow die zacht pulseert.
	UBorder* AddGlow(class UOverlay* Layers, const FLinearColor& Color, float W, float H,
		EHorizontalAlignment HA, EVerticalAlignment VA, const FMargin& Pad, float Freq);

	// Idem maar proportioneel geplaatst op (Fx,Fy) van het scherm (volgt de foto bij elke resolutie).
	// FlickAmount = hoe sterk deze lamp flikkert (1 = standaard rustig, hoger = nerveuzer).
	// bCandle = onregelmatige kaars-/vlam-flikker i.p.v. de rustige neon-puls.
	UBorder* AddGlowAt(class UCanvasPanel* C, float Fx, float Fy, float W, float H, const FLinearColor& Color, float Freq, float FlickAmount = 1.f, bool bCandle = false);

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Backdrop;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<class UWeedActionButton> ContinueBtn;

	// Slot-picker.
	int32 MenuMode = 0; // 0 = hoofdmenu, 1 = New Game-keuze, 2 = Load-keuze
	int32 PendingPickerMode = 0; // bij volgende open meteen deze picker tonen (0 = geen)
	UPROPERTY() TObjectPtr<UWidget> SlotPanel;          // de keuze-kaart (zichtbaar als MenuMode!=0)
	UPROPERTY() TObjectPtr<UWidget> MenuCanvas;          // de 6 hoofdmenu-knoppen (verbergen tijdens picker)
	UPROPERTY() TObjectPtr<UTextBlock> PickerTitle;
	UPROPERTY() TObjectPtr<class UVerticalBox> SlotsBox; // rijen worden per refresh dynamisch opgebouwd

	// Vaste balk boven de slots: autosave aan/uit + wanneer de laatste save was.
	UPROPERTY() TObjectPtr<class UWeedActionButton> AutosaveBtn;
	UPROPERTY() TObjectPtr<UTextBlock> AutosaveLabel;
	UPROPERTY() TObjectPtr<UTextBlock> LastSaveText;
	void OnToggleAutosave();

	// Vanaf schijf geladen achtergrond-foto + logo + knop-swatch (losse PNG's in Content/_Project/UI).
	UPROPERTY() TObjectPtr<class UTexture2D> BgTex;
	UPROPERTY() TObjectPtr<class UTexture2D> LogoTex;
	UPROPERTY() TObjectPtr<class UTexture2D> SwatchTex; // wit masker uit de verf-streep (kleurbaar)
	UPROPERTY() TObjectPtr<class UTexture2D> GlowTex;   // radiale soft-glow (helder centrum -> doorzichtig)

	// Menu-knoppen + hun (scherpe) label, dat alleen bij hover bovenop het paars verschijnt.
	UPROPERTY() TArray<TObjectPtr<class UWeedActionButton>> MenuButtons;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> MenuLabels;

	// Flikkerende neon-lampen.
	UPROPERTY() TArray<TObjectPtr<UBorder>> Glows;
	TArray<FLinearColor> GlowBase;
	TArray<float> GlowPhase;
	TArray<float> GlowFreq;
	TArray<float> GlowFlick; // per-lamp flikker-sterkte
	TArray<uint8> GlowCandle; // 1 = kaars/vlam-flikker (onregelmatig)

	// --- In-game lamp-editor: sleep de gloeden op hun plek; posities worden in GConfig bewaard ---
	UPROPERTY() TArray<TObjectPtr<class UCanvasPanelSlot>> GlowSlots; // canvas-slot per gloed (om te verplaatsen)
	TArray<FVector2D> GlowFrac;     // huidige (fx,fy) per gloed
	UPROPERTY() TObjectPtr<class UCanvasPanel> EditCanvas;            // markers/labels tijdens editen
	UPROPERTY() TArray<TObjectPtr<UBorder>> GlowHandles;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> GlowHandleLabels;
	UPROPERTY() TObjectPtr<class UTextBlock> EditHintText;
	bool bEditGlows = false;
	int32 DragGlow = -1;
	bool bLmbPrev = false;

	void ToggleGlowEdit();
	void RebuildGlowHandles();
	void SaveGlowPositions();
	void LoadGlowFrac(int32 Index, float& Fx, float& Fy) const; // override defaults uit GConfig

	bool bLastOpen = false;
};
