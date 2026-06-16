#include "Game/WeedShopGameMode.h"

#include "Game/WeedShopGameState.h"
#include "World/ActivitySpotManager.h"
#include "WeedShopCore.h"
#include "UI/WeedShopHUD.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
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

void AWeedShopGameMode::BeginPlay()
{
	Super::BeginPlay();
	// Eén centrale, server-only activity-spot-manager (laadt ActivitySpots.txt, spawnt NPC's per tijdvak).
	if (UWorld* W = GetWorld())
	{
		bool bExists = false;
		for (TActorIterator<AActivitySpotManager> It(W); It; ++It) { bExists = true; break; }
		if (!bExists)
		{
			W->SpawnActor<AActivitySpotManager>(AActivitySpotManager::StaticClass(), FTransform::Identity);
		}
	}
}

bool AWeedShopGameMode::IsHostWorldReady() const
{
	// Wereld klaar = BeginPlay gestart EN de kamer is ingestreamd (laad-cover weg). Pas dan is de physics-/
	// collision-scene stabiel genoeg om een joiner-pawn in te spawnen zonder de host te crashen.
	const UWorld* W = GetWorld();
	if (!W || !W->HasBegunPlay()) { return false; }
	// Noodrem (zoals de cover z'n 24s-cap): als room-ready om wat voor reden ook niet binnenkomt, laat de
	// joiner na ~26s alsnog toe i.p.v. eeuwig vasthouden.
	return WeedShop_IsRoomReady() || WeedShop_LoadElapsedSeconds() > 26.0;
}

void AWeedShopGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Een REMOTE joiner die binnenkomt terwijl de host nog laadt -> niet meteen spawnen, maar vasthouden tot
	// de wereld klaar is. De host (local controller) en alle joins ná het laden spawnen gewoon direct.
	if (NewPlayer && !NewPlayer->IsLocalController() && !IsHostWorldReady())
	{
		PendingJoiners.AddUnique(NewPlayer);
		if (UWorld* W = GetWorld())
		{
			if (!W->GetTimerManager().IsTimerActive(JoinFlushTimer))
			{
				W->GetTimerManager().SetTimer(JoinFlushTimer, this, &AWeedShopGameMode::FlushPendingJoiners, 0.4f, true);
			}
		}
		return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AWeedShopGameMode::FlushPendingJoiners()
{
	if (!IsHostWorldReady()) { return; } // nog niet klaar -> volgende tik opnieuw proberen
	for (const TWeakObjectPtr<APlayerController>& WP : PendingJoiners)
	{
		if (APlayerController* PC = WP.Get())
		{
			if (!PC->GetPawn()) { RestartPlayer(PC); } // nu pas de pawn spawnen (wereld is stabiel)
		}
	}
	PendingJoiners.Reset();
	if (UWorld* W = GetWorld()) { W->GetTimerManager().ClearTimer(JoinFlushTimer); }
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

	// Op de VLOER zetten: de zijdelingse spreiding/AlwaysSpawn-adjust kan de speler iets in de lucht
	// neerzetten -> down-trace en de voeten op de grond plaatsen (geen zwevende 2e speler).
	// ALLEEN nadat de wereld is gestart (physics-scene klaar): bij de host-init laadt de map nog en zou een
	// trace de scene in een halve staat raken -> dat corrumpeerde de boel en liet de lift-build crashen.
	if (ACharacter* Ch = (GetWorld()->HasBegunPlay() ? Cast<ACharacter>(Result) : nullptr))
	{
		const float HalfH = Ch->GetCapsuleComponent() ? Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.f;
		const FVector P = Ch->GetActorLocation();
		FHitResult Hit;
		FCollisionQueryParams Q(FName(TEXT("SpawnGroundSnap")), false, Ch);
		if (GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0.f, 0.f, 120.f), P - FVector(0.f, 0.f, 600.f), ECC_WorldStatic, Q))
		{
			Ch->SetActorLocation(Hit.Location + FVector(0.f, 0.f, HalfH + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	return Result;
}
