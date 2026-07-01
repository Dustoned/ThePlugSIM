// UPackWidget — verpak-menu van de bench. Kies een GEDROOGDE strain + een bakje/jar dat je hebt, klik
// Pack: er gaat tot de container-capaciteit gram in een zakje en dat wordt verkoopbare Bag_<strain>-
// voorraad. Losse/natte wiet kun je niet aan klanten verkopen — alleen verpakt.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PackWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UVerticalBox;
class UWeedActionButton;

UCLASS()
class WEEDSHOPCORE_API UPackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	// PERSISTENT UI: het volledige pack- én unpack-scherm wordt ÉÉN keer opgebouwd (BuildPackPane /
	// BuildUnpackPane). Daarna updaten Refresh* alleen waardes/zichtbaarheid/rij-pools IN PLACE — nooit
	// ClearChildren op een klik (geen flash/scroll-sprong). FillBody is enkel nog de dispatcher.
	void BuildPackPane(UVerticalBox* Parent);
	void BuildUnpackPane(UVerticalBox* Parent);
	void FillBody();       // dispatcher: kiest de juiste pane + roept de juiste Refresh
	void RefreshPack();    // pack-flow in place bijwerken (rij-pools + labels + slider), geen teardown
	void RefreshUnpack();  // unpack-lijst in place bijwerken, geen teardown

	// Zet het aantal bags EN werkt slider + labels meteen bij (geen herbouw -> slider springt niet).
	void SetB(int32 N);   // pack-flow
	void SetUB(int32 N);  // unpack-flow

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;

	// Twee wederzijds-uitsluitende panes: alleen hun Visibility flipt op de tab-toggle (geen ClearChildren).
	UPROPERTY() TObjectPtr<UVerticalBox> PackPane;
	UPROPERTY() TObjectPtr<UVerticalBox> UnpackPane;

	// --- Pack-flow persistente widgets (één keer gebouwd) ---
	UPROPERTY() TObjectPtr<UVerticalBox> StrainList;   // container voor de strain-rijen
	UPROPERTY() TObjectPtr<class UTextBlock> NoBudLabel;
	UPROPERTY() TObjectPtr<UVerticalBox> ContSection;  // "2. Pick a bag/jar"-blok (titel + lijst + geen-melding)
	UPROPERTY() TObjectPtr<UVerticalBox> ContList;     // container voor de container-rijen
	UPROPERTY() TObjectPtr<class UTextBlock> NoContLabel;
	UPROPERTY() TObjectPtr<UVerticalBox> GpbSection;   // "2.b Grams per bag"-blok
	UPROPERTY() TObjectPtr<class UTextBlock> GpbLabel;
	UPROPERTY() TObjectPtr<UVerticalBox> BagsSection;  // "3. How many bags?"-blok
	UPROPERTY() TObjectPtr<class USlider> GramSlider;
	UPROPERTY() TObjectPtr<class UTextBlock> GramLabel;
	UPROPERTY() TObjectPtr<UWeedActionButton> PackButton;
	UPROPERTY() TObjectPtr<class UTextBlock> PackBtnLabel;

	// --- Unpack-flow persistente widgets (één keer gebouwd) ---
	UPROPERTY() TObjectPtr<UVerticalBox> UnpackList;   // container voor de bag-rijen
	UPROPERTY() TObjectPtr<class UTextBlock> UnpackEmptyLabel;
	UPROPERTY() TObjectPtr<UVerticalBox> UnpackControls; // slider/steppers/knop-blok (verborgen als geen bags)
	UPROPERTY() TObjectPtr<class USlider> UnpackSlider;
	UPROPERTY() TObjectPtr<class UTextBlock> UnpackLabel;
	UPROPERTY() TObjectPtr<UWeedActionButton> UnpackButton;
	UPROPERTY() TObjectPtr<class UTextBlock> UnpackBtnLabel;

	// --- Persistente rij-pools + per-rij signature (ItemId+Qty+Quality) -> diff-refresh, geen teardown ---
	UPROPERTY() TArray<TObjectPtr<UWeedActionButton>> StrainRowBtns;
	UPROPERTY() TArray<FName> StrainRowIds;
	TArray<FString> StrainRowSigs;

	UPROPERTY() TArray<TObjectPtr<UWeedActionButton>> ContRowBtns;
	UPROPERTY() TArray<FName> ContRowIds;
	TArray<FString> ContRowSigs;

	UPROPERTY() TArray<TObjectPtr<UWeedActionButton>> UnpackRowBtns;
	UPROPERTY() TArray<FName> UnpackRowIds;
	TArray<FString> UnpackRowSigs;

	UPROPERTY() TObjectPtr<class UTextBlock> TabBtnLabel; // header-knop naast Close: "Unpack bags" / "Back to packing"
	bool bUnpackTab = false; // true = aparte unpack-tab (alleen de uitpak-lijst); false = de normale pack-flow

	FName SelStrain;       // gekozen gedroogde Bud_<strain>
	FName SelContainer;    // gekozen container (Cont_*)
	int32 SelBags = 1;     // gekozen aantal bags (1..MaxBags)
	int32 SelGrams = 0;    // gram per zakje (0 = volle container-cap); kleiner = bv. 1g-zakjes
	int32 MaxBags = 1;     // max bags = containers die je hebt vs wiet die je hebt
	int32 PackCap = 1;     // capaciteit (gram) van de gekozen container
	int32 PackBudHave = 0; // beschikbare gram van de gekozen strain

	// Unpack-tab hergebruikt SelBags/MaxBags (de tabs zijn wederzijds uitsluitend).
	FName SelUnpackBag;     // gekozen verpakte Bag_<strain> om uit te pakken
	int32 UnpackPerBag = 1; // gram per zakje van de gekozen bag (voor de labels)

	FString LastSig;       // herbouw alleen bij wijziging
};
