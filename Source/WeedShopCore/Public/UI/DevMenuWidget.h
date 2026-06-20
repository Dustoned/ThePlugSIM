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

	UTextBlock* MakeText(const FString& Txt, int32 Size, const FLinearColor& Col, bool bCenter = false);
	UWeedActionButton* MakeActionBtn(const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick, int32 FontSize = 11);
	USlider* AddLightSlider(const FString& Label, float Norm, TObjectPtr<USlider>& OutS, TObjectPtr<UTextBlock>& OutV);
	void ApplyLightSliders();

	TWeakObjectPtr<UPhoneClientComponent> Phone;

	UPROPERTY() TObjectPtr<UBorder> Frame;
	UPROPERTY() TObjectPtr<UVerticalBox> CatList; // linker categorie-navigatie
	UPROPERTY() TObjectPtr<UVerticalBox> Body;    // rechter inhoud van de gekozen categorie
	UPROPERTY() TArray<TObjectPtr<UWeedActionButton>> CatButtons; // gecachte nav-knoppen (voor highlight)

	int32 SelectedCat = 0; // gekozen categorie (index in de nav-lijst)
	void FillCategory(int32 Cat); // bouwt ALLEEN de rechter-inhoud voor één categorie

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
