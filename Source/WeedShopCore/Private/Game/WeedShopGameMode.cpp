#include "Game/WeedShopGameMode.h"

#include "Game/WeedShopGameState.h"
#include "Game/WeedShopPlayerState.h"
#include "World/ActivitySpotManager.h"
#include "World/DoorRetrofitter.h" // joiner DIRECT thuiszetten na RestartPlayer (geen zichtbare beach-tussenstaat)
#include "WeedShopCore.h"
#include "UI/WeedShopHUD.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
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

	// PlayerState met de stabiele per-speler-id (PlugPid) — sleutel voor alle per-speler state
	// (competitive level/heat, save-records) zonder Online Subsystem.
	PlayerStateClass = AWeedShopPlayerState::StaticClass();

	// C++ on-screen overlay (geld/dag/voorraad/prompt).
	HUDClass = AWeedShopHUD::StaticClass();

	// MEET-MARKERS (boot-gap-diagnose, B.15): de FClassFinders hieronder SYNC-loaden de FirstPerson-BP's
	// (pawn + controller, inclusief alle hard-refs daarachter) - verdacht als bron van de ~27,6s stille
	// gap in LoadMap(Map_MainMenu). Eén boot met deze markers geeft de tijd-verdeling; daarna pas snijden.
	// (De statics laden maar 1x: bij elke latere constructor-run zijn beide ~0.00s.)
	const double _FinderT0 = FPlatformTime::Seconds();
	UE_LOG(LogWeedShop, Display, TEXT("[BOOTMARK] GameMode-ctor (%s): FClassFinder pawn-BP start (+%.2fs sinds start)"), *GetName(), _FinderT0 - GStartTime);

	// Hergebruik de First-Person-pawn en -controller van de template (inclusief jouw interactie-wiring).
	static ConstructorHelpers::FClassFinder<APawn> PawnBP(
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	if (PawnBP.Succeeded())
	{
		DefaultPawnClass = PawnBP.Class;
	}

	const double _FinderT1 = FPlatformTime::Seconds();
	UE_LOG(LogWeedShop, Display, TEXT("[BOOTMARK] GameMode-ctor: pawn-BP klaar na %.2fs; FClassFinder controller-BP start"), _FinderT1 - _FinderT0);

	static ConstructorHelpers::FClassFinder<APlayerController> PCBP(
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonPlayerController"));
	if (PCBP.Succeeded())
	{
		PlayerControllerClass = PCBP.Class;
	}

	UE_LOG(LogWeedShop, Display, TEXT("[BOOTMARK] GameMode-ctor: controller-BP klaar na %.2fs (FClassFinders totaal %.2fs)"), FPlatformTime::Seconds() - _FinderT1, FPlatformTime::Seconds() - _FinderT0);
}

void AWeedShopGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// 2-speler-cap: co-op EN competitive zijn voor precies 2 spelers ontworpen en getest. Een 3e
	// joiner wordt door de sessie geweigerd i.p.v. een ongeteste 3-speler-staat in te rollen.
	if (GameSession) { GameSession->MaxPlayers = 2; }
}

FString AWeedShopGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Err = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	// PlugPid: stabiele per-speler-key zonder Online Subsystem. Joiners sturen hun login-id mee op de
	// join-URL (?PlugPid=..., zie USaveGameSubsystem::JoinLan); de host (lokale controller) heeft geen
	// join-URL -> pak z'n eigen login-id. Zonder deze id viel StablePlayerId terug op de spelernaam
	// ("Player") -> stille key-botsing bij 2 gelijke namen (deelde 1 per-speler-entry).
	if (NewPlayerController)
	{
		if (AWeedShopPlayerState* PS = NewPlayerController->GetPlayerState<AWeedShopPlayerState>())
		{
			FString Pid = UGameplayStatics::ParseOption(Options, TEXT("PlugPid"));
			if (Pid.IsEmpty() && NewPlayerController->IsLocalController())
			{
				Pid = FPlatformMisc::GetLoginId();
			}
			if (!Pid.IsEmpty())
			{
				// DEDUPE: zelfde id al in gebruik bij een ANDERE PlayerState (2 instanties op 1 machine,
				// zoals de lokale 2-instance-test) -> "#2"/"#3"-suffix. Join-volgorde-deterministisch:
				// dezelfde test geeft elke sessie dezelfde keys (host = kale id, joiner = id#2).
				FString Unique = Pid;
				int32 Suffix = 2;
				bool bTaken = true;
				while (bTaken)
				{
					bTaken = false;
					if (GameState)
					{
						for (const APlayerState* Other : GameState->PlayerArray)
						{
							const AWeedShopPlayerState* WPS = Cast<AWeedShopPlayerState>(Other);
							if (WPS && WPS != PS && WPS->PlugPid == Unique) { bTaken = true; break; }
						}
					}
					if (bTaken) { Unique = FString::Printf(TEXT("%s#%d"), *Pid, Suffix++); }
				}
				PS->PlugPid = Unique;
			}
		}
	}
	return Err;
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
	// Direct thuiszetten meteen na de (base) RestartPlayer: geldt voor de host en voor joins die ná het laden
	// binnenkomen (de wereld is dan al klaar, dus geen wachtrij). De vroeg-vastgehouden joiner gaat via
	// FlushPendingJoiners hieronder.
	HomePlayerViaRetrofitter(NewPlayer);
}

void AWeedShopGameMode::FlushPendingJoiners()
{
	if (!IsHostWorldReady()) { return; } // nog niet klaar -> volgende tik opnieuw proberen
	for (const TWeakObjectPtr<APlayerController>& WP : PendingJoiners)
	{
		if (APlayerController* PC = WP.Get())
		{
			if (!PC->GetPawn())
			{
				RestartPlayer(PC);          // nu pas de pawn spawnen (wereld is stabiel)
				HomePlayerViaRetrofitter(PC); // ... en DIRECT thuiszetten -> gerepliceerde positie al "thuis" bij possess
			}
		}
	}
	PendingJoiners.Reset();
	if (UWorld* W = GetWorld()) { W->GetTimerManager().ClearTimer(JoinFlushTimer); }
}

void AWeedShopGameMode::HomePlayerViaRetrofitter(APlayerController* PC)
{
	// Vind de (per-proces) DoorRetrofitter in deze wereld en laat 'm deze speler DIRECT thuiszetten. GetWorld()-
	// filter is impliciet (TActorIterator loopt alleen door de eigen wereld). HomePawnNow is server-only en
	// idempotent: op een client of zonder bekende thuis-plek is dit een no-op en pakt de scan-pass het als vangnet.
	if (!PC) { return; }
	UWorld* W = GetWorld();
	if (!W) { return; }
	for (TActorIterator<ADoorRetrofitter> It(W); It; ++It)
	{
		if (*It) { It->HomePawnNow(PC); break; } // 1 retrofitter per map
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
