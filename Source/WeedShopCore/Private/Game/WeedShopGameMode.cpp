#include "Game/WeedShopGameMode.h"

#include "Game/WeedShopGameState.h"
#include "UI/WeedShopHUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AWeedShopGameMode::AWeedShopGameMode()
{
	// Gedeelde, replicerende game-state (kas + dag/nacht).
	GameStateClass = AWeedShopGameState::StaticClass();

	// C++ on-screen overlay (geld/dag/voorraad/prompt).
	HUDClass = AWeedShopHUD::StaticClass();

	// Hergebruik de First-Person-pawn en -controller van de template (inclusief jouw interactie-wiring).
	static ConstructorHelpers::FClassFinder<APawn> PawnBP(
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	if (PawnBP.Succeeded())
	{
		DefaultPawnClass = PawnBP.Class;
	}

	static ConstructorHelpers::FClassFinder<APlayerController> PCBP(
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonPlayerController"));
	if (PCBP.Succeeded())
	{
		PlayerControllerClass = PCBP.Class;
	}
}

APawn* AWeedShopGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayerController, const FTransform& SpawnTransform)
{
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayerController);
	if (!PawnClass || !GetWorld()) { return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayerController, SpawnTransform); }

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;
	// ALTIJD spawnen, positie bijstellen indien nodig -> nooit een null-pawn (geen "Couldn't spawn player").
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Result = GetWorld()->SpawnActor<APawn>(PawnClass, SpawnTransform, SpawnInfo);
	return Result;
}
