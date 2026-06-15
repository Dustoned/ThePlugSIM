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
		Coin, Clock, Flame, Level, Leaf, Upgrade, Shop, Person, Message, Gear, Map, Home
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

	// Rijke hover-tooltip voor een item: naam + soort + (voor wiet) THC%/kwaliteit + hoeveelheid in gram.
	WEEDSHOPCORE_API FString ItemTooltip(FName ItemId, int32 Qty, float Thc, float QualPct);

	// --- Item-iconen ---------------------------------------------------------
	// Bestandsnaam-sleutel (zonder pad/extensie) voor het icoon van een item, bv. "weed", "cash",
	// "packaging". Drop een PNG met die naam in Content/_Project/UI/Icons/ en hij wordt gebruikt.
	WEEDSHOPCORE_API FString IconKeyFor(FName ItemId);

	// Laadt (en cachet) het PNG-icoon voor dit item van schijf, of nullptr als het er (nog) niet is.
	// WaterChargesOverride >= 0 = water van DEZE specifieke fles (per slot) voor vol/leeg; -1 = actieve fles.
	WEEDSHOPCORE_API class UTexture2D* ItemIconTexture(FName ItemId, int32 WaterChargesOverride = -1);

	// Klaar-voor-gebruik icoon-widget (Size x Size): het PNG als dat bestaat, anders een nette
	// procedurele tegel (gekleurde achtergrond per categorie + flat glyph). Altijd bruikbaar.
	WEEDSHOPCORE_API UWidget* ItemIcon(UWidgetTree* Tree, FName ItemId, float Size, int32 WaterChargesOverride = -1);

	// Accentkleur per item-categorie (voor randjes/labels in de inventory).
	WEEDSHOPCORE_API FLinearColor ItemAccent(FName ItemId);

	// (PlayUiSound is hierboven al gedeclareerd zodat de knop 'm kan gebruiken.)

	// Volume per categorie (0 = UI, 1 = Game, 2 = Music), 0..1. Bewaard in GConfig; standaard 1.
	WEEDSHOPCORE_API float SoundCategoryVolume(int32 Category);
	WEEDSHOPCORE_API void SetSoundCategoryVolume(int32 Category, float Volume);
}
