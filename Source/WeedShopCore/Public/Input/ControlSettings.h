// UControlSettings — herbindbare toetsen voor de gameplay-hotkeys, met een MAIN- en een ALT-toets per
// actie. Config-backed (Game.ini), globaal via Get(). De character bindt beide toetsen hier vandaan en
// luistert op OnBindingsChanged om opnieuw te binden. Een toets die al ergens in gebruik is, wordt
// geweigerd (geen twee acties op dezelfde knop).

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "ControlSettings.generated.h"

UCLASS(Config = Game, DefaultConfig)
class WEEDSHOPCORE_API UControlSettings : public UObject
{
	GENERATED_BODY()

public:
	static UControlSettings* Get();

	// Vuurt na elke wijziging zodat de character opnieuw kan binden.
	FSimpleMulticastDelegate OnBindingsChanged;

	static const TArray<FName>& AllActions();
	static FText DisplayName(FName Action);
	static FKey DefaultKey(FName Action, bool bAlt);

	// Huidige toets (bAlt=false = main, true = alternatief). Kan Invalid zijn (alt is standaard leeg).
	FKey GetKey(FName Action, bool bAlt) const;

	// Zet een toets. Weigert (false) als een ANDERE slot/actie die toets al gebruikt; OutConflict = die
	// actie. Slaagt -> opslaan + OnBindingsChanged.
	bool SetKey(FName Action, bool bAlt, FKey NewKey, FName& OutConflict);

	// Maak een slot leeg (bv. de alt-toets verwijderen).
	void ClearKey(FName Action, bool bAlt);

	void ResetToDefaults();

protected:
	// ActionId -> FKey-naam (string). Twee aparte maps voor main en alternatief.
	UPROPERTY(Config)
	TMap<FName, FString> KeysMain;

	UPROPERTY(Config)
	TMap<FName, FString> KeysAlt;
};
