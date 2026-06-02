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

UCLASS()
class WEEDSHOPCORE_API UWeedActionButton : public UButton
{
	GENERATED_BODY()

public:
	int32 Action = 0;
	int32 Param = 0;
	FWeedButtonClicked OnAction;

	UFUNCTION()
	void Handle() { OnAction.ExecuteIfBound(Action, Param); }
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

	// Flat icoon (size x size) opgebouwd uit vormen, in de gegeven tint (meestal wit op een gekleurde knop).
	WEEDSHOPCORE_API UWidget* Icon(UWidgetTree* Tree, EIcon Type, float Size, const FLinearColor& Tint);

	// Leesbare naam voor een item-id (Bud_X -> X, Seed_X -> X seed, etc.).
	WEEDSHOPCORE_API FString PrettyItemName(FName ItemId);

	// --- Item-iconen ---------------------------------------------------------
	// Bestandsnaam-sleutel (zonder pad/extensie) voor het icoon van een item, bv. "weed", "cash",
	// "packaging". Drop een PNG met die naam in Content/_Project/UI/Icons/ en hij wordt gebruikt.
	WEEDSHOPCORE_API FString IconKeyFor(FName ItemId);

	// Laadt (en cachet) het PNG-icoon voor dit item van schijf, of nullptr als het er (nog) niet is.
	WEEDSHOPCORE_API class UTexture2D* ItemIconTexture(FName ItemId);

	// Klaar-voor-gebruik icoon-widget (Size x Size): het PNG als dat bestaat, anders een nette
	// procedurele tegel (gekleurde achtergrond per categorie + flat glyph). Altijd bruikbaar.
	WEEDSHOPCORE_API UWidget* ItemIcon(UWidgetTree* Tree, FName ItemId, float Size);

	// Accentkleur per item-categorie (voor randjes/labels in de inventory).
	WEEDSHOPCORE_API FLinearColor ItemAccent(FName ItemId);
}
