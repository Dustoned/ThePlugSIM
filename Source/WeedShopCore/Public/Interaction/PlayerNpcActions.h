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

	// Geef de klant een SPECIFIEKE joint (gekozen in de deal-kiezer) i.p.v. het hand-item.
	virtual void GiveJointToCustomerId(ACustomerBase* Customer, FName JointId) {}

	// Speler-skin (0 = man, 1 = vrouw, uitbreidbaar). Voor de settings-UI + save (cross-module via deze interface).
	virtual uint8 GetPlayerSkinIndex() const { return 0; }
	virtual void SetPlayerSkinIndex(uint8 SkinIndex) {}

	// Outfit-customization (Wardrobe): per slot (0=Top, 1=Pants, 2=Shoes, 3=Hair) de gekozen part-index
	// uit WeedOutfit (OutfitCatalog.h). Geldt voor de Casual-skins (2-4); cross-module via deze interface.
	virtual uint8 GetOutfitPart(int32 Slot) const { return 0; }
	virtual void SetOutfitPart(int32 Slot, uint8 Index) {}

	// Dev-tools (vanuit het F10-dev-menu): aim-/positie-gebaseerde acties die in de game-module leven.
	// De bijbehorende losse hotkeys (F6/F8/F11/Shift+F7/Ctrl+F7/Shift+F9/oude F10) zijn vervallen.
	virtual void DevRegisterHome() {}
	virtual void DevMarkDeliveryPoint() {}
	virtual void DevAddMeetSpot() {}
	virtual void DevSaveFurnitureTemplate() {}
	virtual void DevMarkBuildAreaCorner() {}
	virtual void DevSaveMenuCam() {}
	virtual void DevActivityNpcAtAim() {}
};
