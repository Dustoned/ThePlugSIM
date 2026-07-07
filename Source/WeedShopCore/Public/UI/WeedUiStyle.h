// WeedUiStyle — gedeelde UMG-bouwstenen voor één consistente, clean UI (afgeronde panelen, nette
// font, en flat "icoontjes" opgebouwd uit Slate-vormen, zonder losse texture-assets). Hergebruikt
// door de telefoon, status-HUD en (later) deal/inventory/plant-UI.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "WeedUiStyle.generated.h"

class UWidgetTree;
class UWidget;
class UTextBlock;

// Herbruikbare klik-knop die een actie-id + parameter doorgeeft via een gewone delegate
// (bind met BindLambda in C++). Voor knoppen in alle UMG-panelen.
DECLARE_DELEGATE_TwoParams(FWeedButtonClicked, int32, int32);

// Vooraf-declaratie zodat de knop hieronder een UI-geluid kan afspelen (definitie staat verderop).
// Category: 0 = UI, 1 = Game, 2 = Music (volume-instellingen per categorie).
namespace WeedUI { WEEDSHOPCORE_API void PlayUiSound(const UObject* WorldContext, const FString& Key, float Volume = 1.f, int32 Category = 0); }

UCLASS()
class WEEDSHOPCORE_API UWeedActionButton : public UButton
{
	GENERATED_BODY()

public:
	int32 Action = 0;
	int32 Param = 0;
	FWeedButtonClicked OnAction;

	UFUNCTION()
	void Handle() { WeedUI::PlayUiSound(this, TEXT("click"), 0.3f); OnAction.ExecuteIfBound(Action, Param); }
};

namespace WeedUI
{
	enum class EIcon : uint8
	{
		Coin, Clock, Flame, Level, Leaf, Upgrade, Shop, Person, Message, Gear, Map, Home, Box
	};

	// Afgeronde-box brush (geen texture nodig).
	WEEDSHOPCORE_API FSlateBrush Rounded(const FLinearColor& Color, float Radius);

	// Nette standaard-font (Roboto).
	WEEDSHOPCORE_API FSlateFontInfo Font(int32 Size, bool bBold = false);

	// Tekstblok-helper.
	WEEDSHOPCORE_API UTextBlock* Text(UWidgetTree* Tree, const FString& Txt, int32 Size, const FLinearColor& Col, bool bCenter = false, bool bBold = false);

	// Flat icoon (size x size): laadt eerst een PNG (ui_<naam>.png in Content/_Project/UI/Icons),
	// anders het uit vormen opgebouwde icoon. De tint kleurt het (witte) PNG of de vormen.
	WEEDSHOPCORE_API UWidget* Icon(UWidgetTree* Tree, EIcon Type, float Size, const FLinearColor& Tint);

	// Algemeen UI-icoon op naam: laadt Icons/<Key>.png (getint), of valt terug op het Fallback-vormpje.
	// Voor losse UI-iconen zoals zon/maan (dag-nacht) die geen EIcon hebben.
	WEEDSHOPCORE_API UWidget* UiGlyph(UWidgetTree* Tree, const FString& Key, float Size, const FLinearColor& Tint, EIcon Fallback);

	// Kit-icoon: laadt een texture uit de Dark GUI-kit op naam (bv. "t_drop_blue_128") en geeft
	// een UImage (getint). Kant-en-klare game-iconen i.p.v. de procedurele vormpjes.
	WEEDSHOPCORE_API UWidget* KitIcon(UWidgetTree* Tree, const FString& Name, float Size, const FLinearColor& Tint = FLinearColor::White);

	// Leesbare naam voor een item-id (Bud_X -> X, Seed_X -> X seed, etc.).
	WEEDSHOPCORE_API FString PrettyItemName(FName ItemId);

	// Korte TAG op het icoon (strain/variant) zodat items met hetzelfde icoon te onderscheiden zijn
	// (OG seeds vs Silver Haze seeds, Basic vs Premium soil). Leeg = uniek icoon, geen tag nodig.
	WEEDSHOPCORE_API FString ItemTag(FName ItemId);

	// KORTE code voor de tag-bubble op het icoon (UPPERCASE, ~2-4 tekens): strain-afkorting (OG, GSC,
	// SH), tier-rank (I/II/III), of materiaal/gram-code (PLA, 100g). Leeg = uniek icoon, geen bubble.
	WEEDSHOPCORE_API FString ItemTagShort(FName ItemId);

	// Aantal-badge voor een slot: zakjes -> "Nx Xg", wiet -> "Xg", overig stapelbaar -> "xN", anders "".
	WEEDSHOPCORE_API FString ItemQtyBadge(FName ItemId, int32 Qty);

	// Rijke hover-tooltip voor een item: naam + info-body (voor zwevende tooltips die LOS van het
	// details-paneel staan; het details-paneel zelf gebruikt ItemInfoBody, de naam staat daar al groot).
	WEEDSHOPCORE_API FString ItemTooltip(FName ItemId, int32 Qty, float Thc, float QualPct);

	// Info-BODY zonder naam-regel: per item-type de relevante stats. Type-regel -> kern-stats (THC/kwaliteit,
	// fles-vulling, oogsten-per-soil, joint-sterkte, paper-maat, strain-stats voor zaden, of de winkel-
	// omschrijving) -> hoeveelheid (alleen als die niet al glashelder op de cel-badge staat).
	// Thc = het Quality-veld van de stack (voor waterflessen zit daar de vulling in).
	WEEDSHOPCORE_API FString ItemInfoBody(FName ItemId, int32 Qty, float Thc, float QualPct);

	// Gedeelde item-detail-DATA voor de nette twee-koloms detail-weergave (hand-preview EN inventory-quick-view
	// delen deze bron, zodat ze NOOIT uit elkaar lopen). Bevat de gekleurde type-tag, de accent-kleur, de korte
	// hint (winkel-omschrijving voor niet-wiet) en de label/waarde-stat-rijen (label links, waarde rechts).
	struct FItemDetailInfo
	{
		FString Type;                              // type-tag (SEED / BAGGIE / ...), UPPERCASE
		FLinearColor TypeColor = FLinearColor::White; // accent/tag-kleur per categorie
		FString Hint;                              // korte beschrijvende hint onderaan
		TArray<TPair<FString, FString>> Stats;     // label -> waarde (twee-koloms rijen)
	};

	// Bouwt de detail-DATA voor een item (type-tag + kleur + hint + stat-rijen). WorldContext = een widget/actor
	// om de GameState/Store mee te vinden (winkel-omschrijving + strain-stats). Qty/Thc/QualPct komen uit de stack.
	WEEDSHOPCORE_API FItemDetailInfo BuildItemDetail(UObject* WorldContext, FName Id, int32 Qty, float Thc, float QualPct);

	// Kant-en-klare zwevende hover-tooltip als FText: naam bovenaan, daarna de gedeelde detail-regels uit
	// BuildItemDetail (type-tag, stat-rijen "Label: waarde", hint). Zelfde bron als hand-preview en
	// inventory-quick-view, dus tooltips lopen NOOIT uit de pas met de detail-panelen.
	// Gebruik: Widget->SetToolTipText(WeedUI::ItemTooltipText(this, Id, Qty, Thc, QualPct));
	WEEDSHOPCORE_API FText ItemTooltipText(UObject* WorldContext, FName Id, int32 Qty, float Thc, float QualPct);

	// --- Item-iconen ---------------------------------------------------------
	// Bestandsnaam-sleutel (zonder pad/extensie) voor het icoon van een item, bv. "weed", "cash",
	// "packaging". Drop een PNG met die naam in Content/_Project/UI/Icons/ en hij wordt gebruikt.
	WEEDSHOPCORE_API FString IconKeyFor(FName ItemId);

	// Laadt (en cachet) het PNG-icoon voor dit item van schijf, of nullptr als het er (nog) niet is.
	// WaterChargesOverride >= 0 = water van DEZE specifieke fles (per slot) voor vol/leeg; -1 = actieve fles.
	// RollLoadedOverride: -1 = actieve speler-state, 0/1 = expliciet voor dit slot.
	WEEDSHOPCORE_API class UTexture2D* ItemIconTexture(FName ItemId, int32 WaterChargesOverride = -1, int32 RollLoadedOverride = -1);

	// Klaar-voor-gebruik icoon-widget (Size x Size): het PNG als dat bestaat, anders een nette
	// procedurele tegel (gekleurde achtergrond per categorie + flat glyph). Altijd bruikbaar.
	WEEDSHOPCORE_API UWidget* ItemIcon(UWidgetTree* Tree, FName ItemId, float Size, int32 WaterChargesOverride = -1, int32 RollLoadedOverride = -1);

	// Prewarm ONDER de boot/laad-cover: forceert de eenmalige Exo-font-loads (regular + semibold) en
	// decodeert alvast de meest gebruikte item-icon-PNG's, zodat de eerste UI-open geen 30-80ms
	// sync-load/decode-hitch meer geeft. Interne guard tegen dubbel draaien; veilig om vaker aan te roepen.
	WEEDSHOPCORE_API void PrewarmCommonAssets(const UObject* WorldContext);

	// Accentkleur per item-categorie (voor randjes/labels in de inventory).
	WEEDSHOPCORE_API FLinearColor ItemAccent(FName ItemId);
	// Stabiele, per-tekst (per-strain) kleur uit een korte tag (OG/GSC/...). Value/Sat sturen helderheid (pill donker, frame fel).
	WEEDSHOPCORE_API FLinearColor TagColor(const FString& Tag, float Value, float Sat);
	// Tag-pill kleur per ITEM: alleen strains -> levendige per-strain hue; standaard-spul -> neutraal grijs.
	WEEDSHOPCORE_API FLinearColor TagColorForItem(FName ItemId, float Value = 0.42f, float Sat = 0.62f);
	// Gedeelde item-tag stijl (hotbar/inventory/pickers/store): hoog contrast, zelfde outline/pill overal.
	WEEDSHOPCORE_API FSlateFontInfo ItemTagFont(int32 Size = 11);
	WEEDSHOPCORE_API FSlateBrush ItemTagPillBrush(FName ItemId, float Radius = 6.f, float Alpha = 0.98f);
	WEEDSHOPCORE_API FSlateBrush ItemQtyPillBrush(float Radius = 7.f, float Alpha = 0.90f);
	// Gedeelde storage/inventory slotstijl: gevuld duidelijk, leeg rustig; actief krijgt alleen een dun accent.
	WEEDSHOPCORE_API FSlateBrush StorageSlotBrush(bool bFilled, bool bActive = false, FLinearColor Accent = FLinearColor(0.f, 0.f, 0.f, 0.f), float Radius = 8.f);
	WEEDSHOPCORE_API FSlateBrush StorageSlotBrushWithFill(FLinearColor Fill, bool bFilled, bool bActive = false, FLinearColor Accent = FLinearColor(0.f, 0.f, 0.f, 0.f), float Radius = 8.f);
	// Keuze-cellen (pickers/tabs): basis blijft rustig; geselecteerd = rand/accent, geen volledig paars vlak.
	WEEDSHOPCORE_API FSlateBrush SelectableSlotBrush(bool bFilled, bool bSelected = false, FLinearColor Accent = FLinearColor(0.f, 0.f, 0.f, 0.f), float Radius = 8.f);
	WEEDSHOPCORE_API FButtonStyle SelectableSlotButtonStyle(bool bFilled, bool bSelected = false, FLinearColor Accent = FLinearColor(0.f, 0.f, 0.f, 0.f), float Radius = 8.f, FMargin Padding = FMargin(0.f));
	// 9-slice brush uit een ontworpen kit-frame-texture (panel/slot met rand+soft-shadow), getint naar ons palet.
	// NineSlice = randfracties (0-1). Valt terug op Rounded(Tint) als de texture mist.
	WEEDSHOPCORE_API FSlateBrush KitBrush(const FString& TexturePath, const FMargin& NineSlice, const FLinearColor& Tint);

	// Premium-UI-palet (hex sRGB -> linear). Centrale theme: BG 151923 / Panel 252B3A / Inner 303747 /
	// Slot 3A4152 / Accent B98CFF / Highlight FF6BD6 / Text F1EAFE / Text2 B8B4C8 / Warn FF6B6B.
	WEEDSHOPCORE_API FLinearColor Hex(uint32 RGB, float Alpha = 1.f);

	// NAMED palet-kleuren -> gebruik OVERAL deze i.p.v. losse hex/FLinearColor's, zodat ALLE UI identiek oogt aan
	// de inventory. (Inline: roepen Hex aan; de A-parameter zet de opacity.) Semantische kleuren (Good/Warn) zijn
	// palet-afgestemd zodat ready/waarschuwing herkenbaar blijft zonder uit de toon te vallen.
	inline FLinearColor ColBg(float A = 1.f)        { return Hex(0x151923, A); } // scherm-achtergrond
	inline FLinearColor ColPanel(float A = 1.f)     { return Hex(0x252B3A, A); } // kaart/paneel
	inline FLinearColor ColInner(float A = 1.f)     { return Hex(0x303747, A); } // binnen-paneel
	inline FLinearColor ColWell(float A = 1.f)      { return Hex(0x1B202B, A); } // diepe well (achter een grid)
	inline FLinearColor ColStroke(float A = 1.f)    { return Hex(0x3A4152, A); } // dunne rand op panelen
	inline FLinearColor ColSlot(float A = 1.f)      { return Hex(0x3A4152, A); } // gevulde cel
	inline FLinearColor ColSlotEmpty(float A = 1.f) { return Hex(0x2A3140, A); } // lege cel
	inline FLinearColor ColAccent(float A = 1.f)    { return Hex(0xB98CFF, A); } // primair accent (paars)
	inline FLinearColor ColAccentDim(float A = 1.f) { return Hex(0x3A2B52, A); } // accent-vlak/actief slot
	inline FLinearColor ColHighlight(float A = 1.f) { return Hex(0xFF6BD6, A); } // secundair accent (roze)
	inline FLinearColor ColText(float A = 1.f)      { return Hex(0xF1EAFE, A); } // primaire tekst
	inline FLinearColor ColTextDim(float A = 1.f)   { return Hex(0xB8B4C8, A); } // secundaire/gedimde tekst
	inline FLinearColor ColWarn(float A = 1.f)      { return Hex(0xFF6B6B, A); } // waarschuwing/rood
	inline FLinearColor ColGood(float A = 1.f)      { return Hex(0x7FE0A8, A); } // positief/klaar (palet-afgestemd groen)

	// (PlayUiSound is hierboven al gedeclareerd zodat de knop 'm kan gebruiken.)

	// Volume per categorie (0 = UI, 1 = Game, 2 = Music, 3 = Weather), 0..1. Bewaard in GConfig; standaard 1.
	WEEDSHOPCORE_API float SoundCategoryVolume(int32 Category);
	WEEDSHOPCORE_API void SetSoundCategoryVolume(int32 Category, float Volume);
}
