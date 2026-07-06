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

	// Dev-launch-flags voor AUTONOME perf/test-sessies (log/CSV/screenshot-gedreven verificatie zonder speler):
	//   -WeedAutoContinue   -> in het hoofdmenu automatisch op Continue drukken (laatste save laden)
	//   -WeedGameHour=8.5   -> klok op dat uur zetten (bv. vlak voor dageraad)
	//   -WeedTPSpot=0       -> teleporteer naar marked spot #N (Saved/MarkedSpots.txt, fallback BakedData)
	//   -WeedExec="a;b"     -> console-commando's (;-gescheiden) uitvoeren zodra de wereld klaar is
	//   -WeedShotEvery=2    -> elke N sec een screenshot (Saved/Screenshots)
	// Op de GAMEINSTANCE (niet de character): pawns/widgets kunnen mid-flow vernietigd worden waardoor
	// weak-gebonden timers stil sterven; de GameInstance overleeft elke map-wissel. Vuurt per map-load.
	void OnDevFlagsMapLoaded(UWorld* World);
	void RunDevFlags(UWorld* World);
	int32 DevFlagRetries = 0;
};
