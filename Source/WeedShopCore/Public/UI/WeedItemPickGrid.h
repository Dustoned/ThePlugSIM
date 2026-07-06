// UWeedItemPickGrid — compact, herbruikbaar icoon-grid voor keuzelijsten (pack/deal/roll/chat/fridge).
// Zelfde cel-look als de inventory (UInvCell): 86x86-tegel in een WrapBox, icoon gecentreerd, badge-pill
// rechtsboven, tag-pill onder-center, geselecteerde cel = hotbar-actief-stijl. Persistent: SetItems groeit/
// krimpt een cel-POOL en vervangt cel-inhoud alleen bij een sig-verschil (nooit ClearChildren op een klik).
// De klik-lambda vangt de vaste pool-index en leest Id/Payload uit parallelle arrays, zodat een lijst-
// wijziging geen herbind vereist. Andere UI-panelen vullen 'm via SetItems + OnPick.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WeedItemPickGrid.generated.h"

class USizeBox;
class UScrollBox;
class UWrapBox;
class UWidget;
class UWeedActionButton;

// Eén keuze-item voor het grid. IconId None -> gebruik Id als icoon-id. Tag leeg -> WeedUI::ItemTagShort(IconId).
struct FWeedPickItem
{
	FName Id;                                              // logische keuze-id (doorgegeven aan OnPick)
	FName IconId = NAME_None;                              // icoon-id; None -> gebruik Id
	int32 Payload = 0;                                     // vrije parameter (gram/slot/index), meegegeven aan OnPick
	FString Badge;                                         // pill rechtsboven (bv. "12g" of "x5")
	FString Tag;                                           // tag-pill onder-center; leeg -> ItemTagShort(IconId)
	FString SubLine;                                       // klein regeltje boven de tag-pill (bv. prijs), leeg = geen
	FLinearColor SubCol = FLinearColor(0.72f, 0.75f, 0.82f); // kleur van SubLine
	FString Tooltip;                                       // zwevende hover-tooltip; leeg -> automatische WeedUI::ItemTooltipText op het icoon-id
	int32 Qty = 0;                                         // stack-aantal voor de automatische tooltip (0 = onbekend/n.v.t.)
	float Thc = 0.f;                                       // THC% van de stack voor de automatische tooltip
	float QualPct = 0.f;                                   // kwaliteit% van de stack voor de automatische tooltip
};

// Herbruikbaar icoon-keuze-grid. Vul via SetItems(); reageer op keuze via OnPick(Id, Payload).
UCLASS()
class WEEDSHOPCORE_API UWeedItemPickGrid : public UUserWidget
{
	GENERATED_BODY()

public:
	// --- Instelbaar VOOR de eerste RebuildWidget/SetItems ---
	float CellSize = 86.f;          // vierkante cel-afmeting (px)
	float IconSize = 0.f;           // 0 = auto = CellSize*0.79 (inventory-ratio)
	int32 MaxVisibleRows = 0;       // >0 = hoogte-cap op zoveel rijen (rest scrollt); 0 = geen cap
	bool bShowSelection = true;     // false = nooit een cel als geselecteerd markeren

	// Callback bij een klik: (Id, Payload) van de gekozen cel.
	TFunction<void(FName, int32)> OnPick;

	// Vul/ververs het grid. Groeit/krimpt de pool en vervangt alleen gewijzigde cel-inhoud (persistent).
	void SetItems(const TArray<FWeedPickItem>& Items, FName SelectedId = NAME_None);
	// Verplaats alleen de selectie (geen content-rebuild).
	void SetSelected(FName Id);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// Bouwt de root-schil (SizeBox -> ScrollBox -> WrapBox) als die er nog niet is.
	void BuildShell();
	// Bouwt de visuele inhoud van één cel (icoon + badge + subline + tag-pill).
	UWidget* MakeCellContent(const FWeedPickItem& Item) const;
	// Zet de cel-bg/rand op (on)geselecteerd zonder de inhoud aan te raken.
	void StyleCell(int32 i, bool bSel);

	// --- Schil ---
	UPROPERTY(Transient) TObjectPtr<USizeBox> RootSizeBox = nullptr;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroll = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWrapBox> Wrap = nullptr;

	// --- Cel-pool + parallelle arrays (index-gestuurd, geen herbind bij lijst-wijziging) ---
	UPROPERTY(Transient) TArray<TObjectPtr<UWeedActionButton>> Cells;
	TArray<FString> CellSigs;   // per cel: sig uit ALLE velden behalve selectie
	TArray<FName> CellIds;      // per cel: actuele keuze-id (door de klik-lambda gelezen)
	TArray<int32> CellPayloads; // per cel: actuele payload
	FName SelId = NAME_None;     // huidige geselecteerde id
};
