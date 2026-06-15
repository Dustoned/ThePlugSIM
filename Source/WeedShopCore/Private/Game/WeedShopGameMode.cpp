#include "Game/WeedShopGameMode.h"

#include "Game/WeedShopGameState.h"
#include "UI/WeedShopHUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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

	// Elke EXTRA speler opzij van de PlayerStart zetten i.p.v. er bovenop: als de 2e speler op de host
	// spawnt, schuift de engine 'm OMHOOG om de overlap op te lossen -> zwevende 2e speler. Een vaste zijdelingse
	// spreiding (op vloer-hoogte) per al-aanwezige speler voorkomt de overlap én het omhoog schuiven.
	FTransform T = SpawnTransform;
	int32 Existing = 0;
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		if (*It && It->GetController() && It->GetController()->IsPlayerController()) { ++Existing; }
	}
	if (Existing > 0)
	{
		const FVector Right = T.GetRotation().GetRightVector();
		T.AddToTranslation(Right * (95.f * Existing));
	}

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;
	// ALTIJD spawnen, positie bijstellen indien nodig -> nooit een null-pawn (geen "Couldn't spawn player").
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Result = GetWorld()->SpawnActor<APawn>(PawnClass, T, SpawnInfo);
	return Result;
}
