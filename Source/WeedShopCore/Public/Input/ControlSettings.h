// UControlSettings — herbindbare toetsen voor de gameplay-hotkeys. Config-backed (opgeslagen in
// Game.ini), globaal benaderbaar via Get(). De character bindt zijn toetsen hier vandaan en luistert
// op OnBindingsChanged om opnieuw te binden als de speler in de telefoon (Settings -> Controls) iets
// wijzigt. Twee acties op dezelfde toets kan niet (SetKey weigert een conflict).

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "ControlSettings.generated.h"

UCLASS(Config = Game, DefaultConfig)
class WEEDSHOPCORE_API UControlSettings : public UObject
{
	GENERATED_BODY()

public:
	// CDO als globale singleton (config-backed).
	static UControlSettings* Get();

	// Vuurt na elke wijziging zodat luisteraars (de character) opnieuw kunnen binden.
	FSimpleMulticastDelegate OnBindingsChanged;

	// Volgorde van herbindbare acties (voor de UI).
	static const TArray<FName>& AllActions();
	static FText DisplayName(FName Action);
	static FKey DefaultKey(FName Action);

	// Huidige toets voor een actie (geconfigureerd, anders de default).
	FKey GetKey(FName Action) const;

	// Zet een nieuwe toets. Weigert (false) als een andere actie die toets al gebruikt; OutConflict
	// bevat dan de botsende actie. Slaagt -> opslaan + OnBindingsChanged.
	bool SetKey(FName Action, FKey NewKey, FName& OutConflict);

	// Zet alles terug naar de standaardtoetsen.
	void ResetToDefaults();

protected:
	// ActionId -> FKey-naam (als string zodat config netjes serialiseert).
	UPROPERTY(Config)
	TMap<FName, FString> Bindings;
};
