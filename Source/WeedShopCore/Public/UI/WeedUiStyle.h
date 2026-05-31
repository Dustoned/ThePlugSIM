// WeedUiStyle — gedeelde UMG-bouwstenen voor één consistente, clean UI (afgeronde panelen, nette
// font, en flat "icoontjes" opgebouwd uit Slate-vormen, zonder losse texture-assets). Hergebruikt
// door de telefoon, status-HUD en (later) deal/inventory/plant-UI.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"

class UWidgetTree;
class UWidget;
class UTextBlock;

namespace WeedUI
{
	enum class EIcon : uint8
	{
		Coin, Clock, Flame, Level, Leaf, Upgrade, Shop, Person, Message, Gear, Map
	};

	// Afgeronde-box brush (geen texture nodig).
	WEEDSHOPCORE_API FSlateBrush Rounded(const FLinearColor& Color, float Radius);

	// Nette standaard-font (Roboto).
	WEEDSHOPCORE_API FSlateFontInfo Font(int32 Size, bool bBold = false);

	// Tekstblok-helper.
	WEEDSHOPCORE_API UTextBlock* Text(UWidgetTree* Tree, const FString& Txt, int32 Size, const FLinearColor& Col, bool bCenter = false, bool bBold = false);

	// Flat icoon (size x size) opgebouwd uit vormen, in de gegeven tint (meestal wit op een gekleurde knop).
	WEEDSHOPCORE_API UWidget* Icon(UWidgetTree* Tree, EIcon Type, float Size, const FLinearColor& Tint);
}
