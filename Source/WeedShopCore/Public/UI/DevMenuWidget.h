// UDevMenuWidget — één dev-menu (GTA-mod-menu-stijl): een volle-hoogte SIDEBAR links, getoggled met F10
// (alleen in free-build/Sandbox). Bundelt ALLE dev-tools die voorheen verspreid zaten over de telefoon-dev-tabs
// (Test/Rooms/Light/Spots) in nette secties met uitleg per categorie. De knoppen roepen dezelfde bestaande
// UPhoneClientComponent-methodes aan; de logica is dus niet verplaatst, alleen de UI.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DevMenuWidget.generated.h"

class UPhoneClientComponent;
class UVerticalBox;
class UHorizontalBox;
class UBorder;
class UCanvasPanel;
class UTextBlock;
class USlider;
class UWeedActionButton;

UCLASS()
class WEEDSHOPCORE_API UDevMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);
	void MarkDirty() { bContentDirty = true; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void BuildNav();   // bouwt de linker categorie-knoppen ÉÉN keer (niet elke klik)
	void RestyleNav(); // herkleurt alleen de nav-knoppen (highlight) zonder rebuild
	void ShowCategory(int32 Cat); // toont ALLEEN het gekozen panel, verbergt de rest (geen rebuild)

	UTextBlock* MakeText(const FString& Txt, int32 Size, const FLinearColor& Col, bool bCenter = false);
	UWeedActionButton* MakeActionBtn(const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick, int32 FontSize = 11);
	// Bouwt één statische categorie-panel-inhoud (labels/knoppen die nooit wijzigen) ÉÉN keer.
	void BuildCategoryPanel(int32 Cat, UVerticalBox* Panel);
	USlider* AddLightSlider(UVerticalBox* Panel, const FString& Label, float Norm, TObjectPtr<USlider>& OutS, TObjectPtr<UTextBlock>& OutV);
	void ApplyLightSliders();

	// Herbouwt ALLEEN een dynamische sub-lijst (per-rij diff), niet de hele categorie.
	void RefreshRoomTemplates(); // Rooms: templates-lijst
	void RefreshRoomPlaced();    // Rooms: geplaatste stamps-lijst
	void RefreshMarkedSpots();   // Marked spots: F9-marker-lijst

	TWeakObjectPtr<UPhoneClientComponent> Phone;

	UPROPERTY() TObjectPtr<UBorder> Frame;
	UPROPERTY() TObjectPtr<UVerticalBox> CatList; // linker categorie-navigatie
	UPROPERTY() TObjectPtr<UVerticalBox> Body;    // container: houdt alle 11 categorie-panels
	UPROPERTY() TArray<TObjectPtr<UWeedActionButton>> CatButtons; // gecachte nav-knoppen (voor highlight)
	UPROPERTY() TArray<TObjectPtr<UVerticalBox>> CatPanels;       // 11 persistente panels (één per categorie)

	int32 SelectedCat = 0; // gekozen categorie (index in de nav-lijst)

	// --- Shops: persistent type-label (type-cycle = alleen SetText, geen rebuild) ---
	UPROPERTY() TObjectPtr<UTextBlock> ShopTypeLabel;

	// --- Rooms: templates-lijst (persistent pool + per-rij signatuur) ---
	UPROPERTY() TObjectPtr<UTextBlock> RoomTplHead;                 // "Templates (click to place)" header, toggle-baar
	UPROPERTY() TObjectPtr<UVerticalBox> RoomTplBox;                // persistente container voor template-rijen
	UPROPERTY() TArray<TObjectPtr<UHorizontalBox>> RoomTplRows;     // rij-pool
	TArray<FString> RoomTplSigs;                                    // parallelle per-rij signaturen

	// --- Rooms: geplaatste stamps-lijst (persistent pool + per-rij signatuur) ---
	UPROPERTY() TObjectPtr<UVerticalBox> RoomPlacedHeadBox;         // header + "Undo last stamp" (toggle-baar)
	UPROPERTY() TObjectPtr<UVerticalBox> RoomPlacedBox;             // persistente container voor placed-rijen
	UPROPERTY() TArray<TObjectPtr<UHorizontalBox>> RoomPlacedRows;  // rij-pool
	TArray<FString> RoomPlacedSigs;                                 // parallelle per-rij signaturen

	// --- Marked spots: F9-marker-lijst (persistent pool + per-rij signatuur) ---
	UPROPERTY() TObjectPtr<UTextBlock> SpotsEmptyText;              // "No spots yet." (toggle-baar)
	UPROPERTY() TObjectPtr<UVerticalBox> SpotsBox;                  // persistente container voor spot-rijen
	UPROPERTY() TArray<TObjectPtr<UHorizontalBox>> SpotRows;        // rij-pool
	TArray<FString> SpotSigs;                                       // parallelle per-rij signaturen

	// Licht-/tijd-sliders (live toegepast in NativeTick), zelfde set als voorheen in de telefoon-Light/Test-tab.
	UPROPERTY() TObjectPtr<USlider> TimeSpeedSlider;
	UPROPERTY() TObjectPtr<USlider> LMoon;
	UPROPERTY() TObjectPtr<USlider> LSun;
	UPROPERTY() TObjectPtr<USlider> LSkyN;
	UPROPERTY() TObjectPtr<USlider> LSkyD;
	UPROPERTY() TObjectPtr<USlider> LPitch;
	UPROPERTY() TObjectPtr<USlider> LLamp;
	UPROPERTY() TObjectPtr<USlider> LExp;
	UPROPERTY() TObjectPtr<UTextBlock> TimeSpeedV;
	UPROPERTY() TObjectPtr<UTextBlock> LMoonV;
	UPROPERTY() TObjectPtr<UTextBlock> LSunV;
	UPROPERTY() TObjectPtr<UTextBlock> LSkyNV;
	UPROPERTY() TObjectPtr<UTextBlock> LSkyDV;
	UPROPERTY() TObjectPtr<UTextBlock> LPitchV;
	UPROPERTY() TObjectPtr<UTextBlock> LLampV;
	UPROPERTY() TObjectPtr<UTextBlock> LExpV;

	bool bContentDirty = true;
};
