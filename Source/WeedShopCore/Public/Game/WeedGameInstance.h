// UWeedGameInstance — eigen GameInstance-subclass. Enige taak: de engine-network-/travel-failure-events
// afvangen zodat een co-op-disconnect NIET tot een lange bevriezing + stille terugval leidt (D27).
//   - HOST (listen-server): een joiner die wegvalt mag de host NIET uit z'n wereld gooien -> alleen een
//     melding + log, doorspelen.
//   - JOINER (client): de sessie is weg -> korte melding + nette teardown terug naar het hoofdmenu.
// LET OP: USaveGameSubsystem is een GameInstance-SUBSYSTEM en blijft onder deze subclass gewoon werken
// (subsystems hangen aan elke UGameInstance) -> daar verandert niets aan.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineTypes.h"
#include "WeedGameInstance.generated.h"

UCLASS()
class WEEDSHOPCORE_API UWeedGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

private:
	// Verbinding verbroken / net-driver-fout (time-out, host weg, kick, ...).
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	// ClientTravel/ServerTravel mislukt (bv. map laadt niet).
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
};
