// IPlayerNpcActions — kleine brug zodat WeedShopCore-UI (telefoon/praat-venster) een NPC-actie op de
// speler-pawn kan aanroepen die in de game-module (ThePlugSIM) is geïmplementeerd. Omgekeerde
// module-afhankelijkheid is niet toegestaan, dus loopt het via deze interface.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PlayerNpcActions.generated.h"

class ACustomerBase;

UINTERFACE(MinimalAPI)
class UPlayerNpcActions : public UInterface
{
	GENERATED_BODY()
};

class IPlayerNpcActions
{
	GENERATED_BODY()

public:
	// Geef de aangewezen klant een joint (zelfde effect als de hold-LMB sample-flow).
	virtual void GiveJointToCustomer(ACustomerBase* Customer) {}

	// Speler-skin (0 = man, 1 = vrouw, uitbreidbaar). Voor de settings-UI + save (cross-module via deze interface).
	virtual uint8 GetPlayerSkinIndex() const { return 0; }
	virtual void SetPlayerSkinIndex(uint8 SkinIndex) {}

	// Outfit-customization (Wardrobe): per slot (0=Top, 1=Pants, 2=Shoes, 3=Hair) de gekozen part-index
	// uit WeedOutfit (OutfitCatalog.h). Geldt voor de Casual-skins (2-4); cross-module via deze interface.
	virtual uint8 GetOutfitPart(int32 Slot) const { return 0; }
	virtual void SetOutfitPart(int32 Slot, uint8 Index) {}
};
