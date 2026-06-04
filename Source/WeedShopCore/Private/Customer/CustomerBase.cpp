#include "Customer/CustomerBase.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "World/DayCycleComponent.h"
#include "World/CityGenerator.h"
#include "EngineUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/DataTable.h"
#include "Data/WeedShopProduct.h"
#include "Economy/EconomyComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Game/WeedShopGameState.h"
#include "Phone/ContactsComponent.h"
#include "Npc/NpcRegistryComponent.h"
#include "Progression/LevelComponent.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

ACustomerBase::ACustomerBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f; // 5 Hz i.p.v. elke frame -> veel goedkoper bij veel NPC's
	// (Beweging zelf loopt via de AIController/path-following, dus 5 Hz schaadt het lopen niet.)

	// Zorg dat de interactie-trace (ECC_Visibility) de klant raakt.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Verborgen fallback-cilinder (voor het geval de mesh niet laadt).
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetVisibility(false);

	// Echt geanimeerd model: mannequin + unarmed locomotion-AnimBP (idle/lopen op snelheid).
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyFinder(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
		if (MannyFinder.Succeeded()) { MeshComp->SetSkeletalMesh(MannyFinder.Object); }
		MeshComp->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
		MeshComp->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		// Walk/idle zelf afspelen (single-node); ABP_Unarmed animeert deze NPC's niet. Afspelen in Tick.
		static ConstructorHelpers::FObjectFinder<UAnimSequence> NIdle(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
		if (NIdle.Succeeded()) { NpcIdle = NIdle.Object; }
		static ConstructorHelpers::FObjectFinder<UAnimSequence> NWalk(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd"));
		if (NWalk.Succeeded()) { NpcWalk = NWalk.Object; }
		MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
		MeshComp->bEnableUpdateRateOptimizations = false;
		MeshComp->SetCastShadow(false);
	}

	// AI: laat een AIController de klant besturen zodat hij kan pathfinden.
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// Beweging: draai naar de looprichting, rustige loopsnelheid + RVO-avoidance (niet in elkaar lopen).
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bUseControllerDesiredRotation = false;
		Move->bOrientRotationToMovement = true;
		Move->RotationRate = FRotator(0.f, 540.f, 0.f);
		Move->MaxWalkSpeed = 200.f;
		Move->bUseRVOAvoidance = true;             // ontwijk elkaar/de speler -> niet vastlopen
		Move->AvoidanceConsiderationRadius = 120.f;
		Move->AvoidanceWeight = 0.5f;
	}
	// (Geen per-NPC navmesh-invoker meer: één centrale invoker (CityGenerator) dekt de hele stad,
	//  dat schaalt veel beter naar 40+ NPC's dan 40 losse invokers.)

	// Productenlijst (voor marktprijs + willekeurig gewenst product).
	static ConstructorHelpers::FObjectFinder<UDataTable> ProdFinder(TEXT("/Game/_Project/Data/DT_Products.DT_Products"));
	if (ProdFinder.Succeeded()) { ProductTable = ProdFinder.Object; }
}

void ACustomerBase::UpdateNpcAnim(float DeltaSeconds)
{
	if (!bNpcAnimStarted) { return; }
	USkeletalMeshComponent* M = GetMesh();
	if (!M) { return; }
	// 'Beweegt' uit de positie (werkt op host én client-proxy), kort vasthouden tussen net-updates.
	const FVector Cur = GetActorLocation();
	if (bHasNpcPrev)
	{
		FVector D = Cur - NpcPrevLoc; D.Z = 0.f;
		if (D.SizeSquared() > 0.25f) { NpcMoveHold = 0.5f; } // lage drempel: walk-anim ook bij rustige tred/hoge FPS
		else if (NpcMoveHold > 0.f) { NpcMoveHold -= DeltaSeconds; }
	}
	NpcPrevLoc = Cur; bHasNpcPrev = true;

	const int32 NewState = (NpcMoveHold > 0.f) ? 1 : 0;
	if (NewState == NpcAnimState) { return; }
	NpcAnimState = NewState;
	if (UAnimSequence* Seq = (NewState == 1) ? NpcWalk : NpcIdle) { M->PlayAnimation(Seq, true); }
}

bool ACustomerBase::WalkTo(const FVector& Dest)
{
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(Dest, 70.f, true, true, true, false, nullptr, true);
		return Result != EPathFollowingRequestResult::Failed;
	}
	return false;
}

void ACustomerBase::BeginPlay()
{
	Super::BeginPlay();

	// Loop/idle zelf aansturen (single-node) -> NPC's animeren echt i.p.v. glijden.
	if (USkeletalMeshComponent* M = GetMesh())
	{
		if (NpcIdle || NpcWalk)
		{
			M->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			if (NpcIdle) { M->PlayAnimation(NpcIdle, true); }
			NpcAnimState = 0;
			bNpcAnimStarted = true;
		}
	}

	if (HasAuthority())
	{
		State = ECustomerState::WantsToOrder;
		BasePatienceSeconds = PatienceSeconds;

		// Koppel aan een persoon in het register en laad zijn persistente stats.
		AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
		if (GS && GS->GetNpcRegistry())
		{
			if (NpcId.IsNone())
			{
				NpcId = GS->GetNpcRegistry()->AssignNpc();
			}
			float R = Respect, L = Loyalty, A = Addiction;
			FText Name;
			if (GS->GetNpcRegistry()->GetStats(NpcId, R, L, A, Name))
			{
				Respect = R; Loyalty = L; Addiction = A;
			}
		}

		// Nog te weinig verslaving? Dan is dit (nog) geen koper maar een prospect: eerst opwarmen
		// met gratis samples. Wie al verslaafd genoeg is (bv. een vaste klant) wil meteen kopen.
		if (Addiction < AddictionToBuy)
		{
			State = ECustomerState::Prospect;
		}

		// Geen gewenst product ingesteld (bv. gespawnd)? Kies er willekeurig één uit de productenlijst.
		if (DesiredProductId.IsNone() && ProductTable)
		{
			const TArray<FName> Rows = ProductTable->GetRowNames();
			if (Rows.Num() > 0)
			{
				// Klanten willen VERPAKTE wiet (Bag_<strain>); losse/natte buds kopen ze niet.
				const FName Row = Rows[FMath::RandRange(0, Rows.Num() - 1)];
				const FString RS = Row.ToString();
				DesiredProductId = RS.StartsWith(TEXT("Bud_")) ? FName(*FString::Printf(TEXT("Bag_%s"), *RS.RightChop(4))) : Row;
				DesiredQuantity = FMath::RandRange(1, 3);
			}
		}

		// Loop naar de toegewezen plek (door de spawner gezet).
		if (bHasSpot)
		{
			WalkTo(SpotLocation);
		}
	}
}

void ACustomerBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACustomerBase, DesiredProductId);
	DOREPLIFETIME(ACustomerBase, DesiredQuantity);
	DOREPLIFETIME(ACustomerBase, Respect);
	DOREPLIFETIME(ACustomerBase, Loyalty);
	DOREPLIFETIME(ACustomerBase, Addiction);
	DOREPLIFETIME(ACustomerBase, State);
	DOREPLIFETIME(ACustomerBase, SpeechLine);
	DOREPLIFETIME(ACustomerBase, bNeedsPlayer);
	DOREPLIFETIME(ACustomerBase, NpcId);
}

void ACustomerBase::BecomeBuyerNow()
{
	if (!HasAuthority()) { return; }
	// Een goede gratis joint zet 'm meteen aan het kopen (ook als 'ie nog geen prospect-drempel haalde).
	if (State == ECustomerState::Prospect || State == ECustomerState::Served || State == ECustomerState::Leaving)
	{
		State = ECustomerState::WantsToOrder;
		PatienceSeconds = BasePatienceSeconds;
		LeaveTimer = 0.f;
	}
}

bool ACustomerBase::RefreshProspect()
{
	if (!HasAuthority() || State != ECustomerState::Prospect)
	{
		return false;
	}
	if (Addiction >= AddictionToBuy)
	{
		// Genoeg opgewarmd: wordt een kopende klant.
		State = ECustomerState::WantsToOrder;
		PatienceSeconds = BasePatienceSeconds;
		LeaveTimer = 0.f;
		return true;
	}
	return false;
}

void ACustomerBase::WriteStatsToRegistry()
{
	if (NpcId.IsNone())
	{
		return;
	}
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS && GS->GetNpcRegistry())
	{
		GS->GetNpcRegistry()->ApplyStats(NpcId, Respect, Loyalty, Addiction);
	}
}

void ACustomerBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateNpcAnim(DeltaSeconds); // op alle machines (host + client-proxy) -> iedereen ziet de NPC lopen

	if (!HasAuthority())
	{
		return;
	}

	// Bewoners gebruiken hun eigen dag/nacht-schema (roamen / naar huis) i.p.v. de geduld-/vertrek-logica.
	if (bResident)
	{
		TickResident(DeltaSeconds);
		return;
	}

	// Geduld loopt af zolang hij wacht (wil bestellen of onderhandelt).
	if (State == ECustomerState::WantsToOrder || State == ECustomerState::Negotiating)
	{
		PatienceSeconds -= DeltaSeconds;
		if (PatienceSeconds <= 0.f)
		{
			LeaveAngry();
		}
	}
	// Klaar (geholpen of vertrokken).
	else if (State == ECustomerState::Served || State == ECustomerState::Leaving)
	{
		LeaveTimer += DeltaSeconds;

		const bool bShouldLeave = (State == ECustomerState::Leaving) || bDespawnAfterServed;
		if (bShouldLeave)
		{
			// Loop naar huis/uitgang en verdwijn bij aankomst (of na een veiligheids-timeout).
			if (bHasHome && !bWalkingHome)
			{
				bWalkingHome = true;
				WalkTo(HomeLocation);
			}
			const bool bArrived = bHasHome && FVector::DistSquared2D(GetActorLocation(), HomeLocation) < FMath::Square(150.f);
			if (bArrived || LeaveTimer >= 20.f)
			{
				Destroy();
			}
		}
		else if (LeaveTimer >= OrderCooldownSeconds)
		{
			// Vaste klant heeft z'n spul opgerookt -> wil weer iets; loopt terug naar z'n plek.
			State = ECustomerState::WantsToOrder;
			PatienceSeconds = BasePatienceSeconds;
			LeaveTimer = 0.f;
			if (bHasSpot) { WalkTo(SpotLocation); }
		}
	}
}

void ACustomerBase::SetupResident(const FVector& FrontSpot, const FVector& InteriorPos, const FString& HouseNumber, const FVector& HallPos)
{
	bResident = true;
	HomeFrontSpot = ProjectResidentPointToNav(FrontSpot, FVector(1400.f, 1400.f, 700.f));
	HomeInteriorPos = InteriorPos;
	HomeHallPos = HallPos;
	bHasHomeHall = !HallPos.IsNearlyZero();
	HomeNumber = HouseNumber;
	bDespawnAfterServed = false;
	StartResidentHomeExit(true);
	RoamRouteSeed = static_cast<int32>(GetTypeHash(HomeNumber));
	ParkLegCountdown = 2 + FMath::Abs(RoamRouteSeed % 3);
	HallLegCountdown = 3 + FMath::Abs((RoamRouteSeed / 7) % 4);
	RoamLegIndex = FMath::Abs(RoamRouteSeed % 97);
	RoamTimer = FMath::FRandRange(0.5f, 4.f); // spreiding: niet allemaal tegelijk vertrekken na het naar buiten komen

	// Rustige wandeltred: bewoners slenteren over straat i.p.v. te sprinten.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = 135.f;
	}
}

FVector ACustomerBase::ProjectResidentPointToNav(const FVector& Desired, const FVector& Extent) const
{
	if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		FNavLocation Out;
		if (Nav->ProjectPointToNavigation(Desired, Out, Extent))
		{
			return Out.Location + FVector(0.f, 0.f, 3.f);
		}
	}
	return Desired + FVector(0.f, 0.f, 3.f);
}

void ACustomerBase::StartResidentHomeExit(bool bFromInterior)
{
	bAtHomeInside = false;
	bEmergingFromHome = true;
	HomeExitStage = (bFromInterior && bHasHomeHall) ? 0 : 1;
	HomeExitStuckTimer = 0.f;
	bHasRoamGoal = false;
	bRoamGoalIsPark = false;
	bPendingRoamGoalIsPark = false;
	ParkPauseTimer = 0.f;
	ResidentStuckTimer = 0.f;
	bHasResidentPrevMoveLoc = false;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	if (!GetController())
	{
		SpawnDefaultController();
	}

	const FVector Spawn = bFromInterior ? HomeInteriorPos : (bHasHomeHall ? HomeHallPos : HomeInteriorPos);
	SetActorLocation(Spawn + FVector(0.f, 0.f, 4.f));
}

bool ACustomerBase::TickResidentHomeExit(float DeltaSeconds)
{
	if (!bEmergingFromHome)
	{
		return false;
	}

	const bool bHallStage = (HomeExitStage == 0 && bHasHomeHall);
	const FVector Target = bHallStage ? (HomeHallPos + FVector(0.f, 0.f, 4.f)) : HomeFrontSpot;
	const FVector Cur = GetActorLocation();
	const bool bArrived = FVector::Dist2D(Cur, Target) < (bHallStage ? 120.f : 155.f)
		&& FMath::Abs(Cur.Z - Target.Z) < (bHallStage ? 170.f : 230.f);

	if (bArrived)
	{
		if (bHallStage)
		{
			HomeExitStage = 1;
			HomeExitStuckTimer = 0.f;
			bHasResidentPrevMoveLoc = false;
			return true;
		}

		bEmergingFromHome = false;
		bLeavingHomeRoute = false;
		bHasRoamGoal = false;
		bRoamGoalIsPark = false;
		bPendingRoamGoalIsPark = false;
		RoamTimer = 0.f;
		HomeExitStuckTimer = 0.f;
		bHasResidentPrevMoveLoc = false;
		return false;
	}

	const bool bMoveStarted = WalkTo(Target);
	float MoveDelta = 9999.f;
	if (bHasResidentPrevMoveLoc)
	{
		MoveDelta = FVector::Dist2D(Cur, ResidentPrevMoveLoc);
	}
	ResidentPrevMoveLoc = Cur;
	bHasResidentPrevMoveLoc = true;

	bool bPathMoving = bMoveStarted;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		bPathMoving = bMoveStarted && (AI->GetMoveStatus() == EPathFollowingStatus::Moving);
	}

	if (!bPathMoving || MoveDelta < 4.f)
	{
		HomeExitStuckTimer += DeltaSeconds;
	}
	else
	{
		HomeExitStuckTimer = 0.f;
	}

	if (HomeExitStuckTimer >= (bHallStage ? 2.2f : 3.0f))
	{
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}

		if (bHallStage)
		{
			SetActorLocation(HomeHallPos + FVector(0.f, 0.f, 4.f));
			HomeExitStage = 1;
		}
		else
		{
			SetActorLocation(ProjectResidentPointToNav(HomeFrontSpot, FVector(1400.f, 1400.f, 700.f)));
			bEmergingFromHome = false;
			bLeavingHomeRoute = false;
			bHasRoamGoal = false;
			bRoamGoalIsPark = false;
			bPendingRoamGoalIsPark = false;
			RoamTimer = 0.f;
		}
		HomeExitStuckTimer = 0.f;
		bHasResidentPrevMoveLoc = false;
	}

	return true;
}

void ACustomerBase::BeginAppointment(bool bComeToPlayer)
{
	if (!HasAuthority()) { return; }
	bApptActive = true;
	bApptComeToPlayer = bComeToPlayer;
	bApptArrived = false;
	ApptTimeout = 360.f;       // 6 min: daarna geeft de NPC de afspraak op
	SetNeedsPlayer(true);      // poppetje op de kompas zodat de speler weet waar te zijn
	BecomeBuyerNow();          // afspraak = wil kopen (geen prospect-sampling meer)
}

void ACustomerBase::EndAppointment()
{
	if (!HasAuthority()) { return; }
	bApptActive = false;
	bApptComeToPlayer = false;
	bApptArrived = false;
	SetNeedsPlayer(false);
	RoamTimer = 0.f;           // pak meteen een nieuw roam-doel
}

ACityGenerator* ACustomerBase::GetResidentCity(UWorld* W)
{
	if (CachedCity.IsValid())
	{
		return CachedCity.Get();
	}
	if (!W)
	{
		return nullptr;
	}
	for (TActorIterator<ACityGenerator> It(W); It; ++It)
	{
		CachedCity = *It;
		return *It;
	}
	return nullptr;
}

float ACustomerBase::ComputeResidentRoamTimeout(const FVector& Goal) const
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Speed = Move ? FMath::Max(80.f, Move->MaxWalkSpeed) : 135.f;
	const float TravelSeconds = FVector::Dist(GetActorLocation(), Goal) / Speed;
	return FMath::Clamp(TravelSeconds * 1.55f + FMath::FRandRange(8.f, 18.f), 18.f, 140.f);
}

int32 ACustomerBase::CountResidentParkVisitors(float Radius) const
{
	UWorld* W = GetWorld();
	if (!W || !bHasPark)
	{
		return 0;
	}

	const float RadiusSq = FMath::Square(Radius);
	int32 Count = 0;
	for (TActorIterator<ACustomerBase> It(W); It; ++It)
	{
		const ACustomerBase* C = *It;
		if (!IsValid(C) || C == this || !C->bResident || C->bAtHomeInside)
		{
			continue;
		}

		const bool bNearPark = FVector::DistSquared2D(C->GetActorLocation(), ParkCenter) <= RadiusSq;
		if (bNearPark || C->bRoamGoalIsPark || C->ParkPauseTimer > 0.f)
		{
			++Count;
		}
	}
	return Count;
}

void ACustomerBase::RecoverResidentIfStuck(float DeltaSeconds)
{
	if (ParkPauseTimer > 0.f || bAtHomeInside || bApptActive)
	{
		ResidentStuckTimer = 0.f;
		bHasResidentPrevMoveLoc = false;
		return;
	}

	const FVector Cur = GetActorLocation();
	const bool bCloseToGoal = bHasRoamGoal
		&& FVector::Dist2D(Cur, RoamGoal) < 180.f
		&& FMath::Abs(Cur.Z - RoamGoal.Z) < 220.f;
	if (!bHasRoamGoal || bCloseToGoal)
	{
		ResidentStuckTimer = 0.f;
		ResidentPrevMoveLoc = Cur;
		bHasResidentPrevMoveLoc = true;
		return;
	}

	float MoveDelta = 9999.f;
	if (bHasResidentPrevMoveLoc)
	{
		MoveDelta = FVector::Dist2D(Cur, ResidentPrevMoveLoc);
	}
	ResidentPrevMoveLoc = Cur;
	bHasResidentPrevMoveLoc = true;

	bool bPathActuallyMoving = true;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		bPathActuallyMoving = (AI->GetMoveStatus() == EPathFollowingStatus::Moving);
	}

	if (!bPathActuallyMoving || MoveDelta < 6.f)
	{
		ResidentStuckTimer += DeltaSeconds;
	}
	else
	{
		ResidentStuckTimer = 0.f;
	}

	if (ResidentStuckTimer < 1.8f)
	{
		return;
	}

	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}

	if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		FNavLocation Proj;
		if (Nav->ProjectPointToNavigation(Cur, Proj, FVector(900.f, 900.f, 600.f)))
		{
			SetActorLocation(Proj.Location + FVector(0.f, 0.f, 3.f));
		}
	}

	bHasRoamGoal = false;
	bRoamGoalIsPark = false;
	bPendingRoamGoalIsPark = false;
	ParkPauseTimer = 0.f;
	RoamTimer = 0.f;
	ResidentStuckTimer = 0.f;
	bHasResidentPrevMoveLoc = false;
}

bool ACustomerBase::SetResidentRoamGoal(const FVector& DesiredGoal, float SearchXY, float SearchZ)
{
	UWorld* W = GetWorld();
	UNavigationSystemV1* Nav = W ? UNavigationSystemV1::GetCurrent(W) : nullptr;
	const bool bGoalIsPark = bPendingRoamGoalIsPark;
	bPendingRoamGoalIsPark = false;
	bRoamGoalIsPark = false;
	if (!Nav)
	{
		return false;
	}

	FNavLocation Projected;
	const FVector Extent(FMath::Max(80.f, SearchXY), FMath::Max(80.f, SearchXY), FMath::Max(80.f, SearchZ));
	if (!Nav->ProjectPointToNavigation(DesiredGoal, Projected, Extent))
	{
		if (!Nav->GetRandomReachablePointInRadius(DesiredGoal, FMath::Max(250.f, SearchXY), Projected))
		{
			return false;
		}
	}

	if (!WalkTo(Projected.Location))
	{
		return false;
	}

	RoamGoal = Projected.Location;
	bHasRoamGoal = true;
	bRoamGoalIsPark = bGoalIsPark;
	RoamTimer = ComputeResidentRoamTimeout(RoamGoal);
	ResidentPrevMoveLoc = GetActorLocation();
	bHasResidentPrevMoveLoc = true;
	ResidentStuckTimer = 0.f;
	return true;
}

bool ACustomerBase::PickResidentRoamGoal(FVector& OutGoal, float& OutSearchXY, float& OutSearchZ)
{
	OutGoal = FVector::ZeroVector;
	OutSearchXY = 700.f;
	OutSearchZ = 500.f;
	bPendingRoamGoalIsPark = false;

	ACityGenerator* City = GetResidentCity(GetWorld());
	if (!City)
	{
		return false;
	}

	ParkCenter = City->GetCityCenter();
	bHasPark = true;

	if (bLeavingHomeRoute)
	{
		bLeavingHomeRoute = false;
		OutGoal = HomeFrontSpot;
		OutSearchXY = 500.f;
		OutSearchZ = 300.f;
		return true;
	}

	--ParkLegCountdown;
	--HallLegCountdown;

	if (ParkLegCountdown <= 0)
	{
		ParkLegCountdown = 4 + FMath::Abs((RoamRouteSeed + RoamLegIndex) % 4);
		if (CountResidentParkVisitors(City->GetMapBlockSize() * 0.58f) < 10)
		{
			const float D = City->GetMapBlockSize() * 0.30f;
			const FVector ParkStops[] = {
				FVector(0.f, 0.f, 0.f),
				FVector(D, 0.f, 0.f),
				FVector(-D, 0.f, 0.f),
				FVector(0.f, D, 0.f),
				FVector(0.f, -D, 0.f)
			};
			const int32 Pick = FMath::Abs(RoamRouteSeed + RoamLegIndex * 3) % UE_ARRAY_COUNT(ParkStops);
			OutGoal = FVector(ParkCenter.X, ParkCenter.Y, HomeFrontSpot.Z) + ParkStops[Pick];
			OutSearchXY = 650.f;
			OutSearchZ = 400.f;
			bPendingRoamGoalIsPark = true;
			++RoamLegIndex;
			return true;
		}
	}

	const TArray<FApartmentHome>& Homes = City->GetApartmentHomes();
	if (HallLegCountdown <= 0 && Homes.Num() > 0)
	{
		TArray<FVector> HallStops;
		HallStops.Reserve(Homes.Num());
		for (const FApartmentHome& H : Homes)
		{
			if (H.bApartment && !H.HallPos.IsNearlyZero())
			{
				HallStops.Add(H.HallPos);
			}
		}
		if (HallStops.Num() > 0)
		{
			HallLegCountdown = 5 + FMath::Abs((RoamRouteSeed + RoamLegIndex) % 5);
			const int32 Pick = FMath::Abs(RoamRouteSeed * 5 + RoamLegIndex * 7) % HallStops.Num();
			OutGoal = HallStops[Pick];
			OutSearchXY = 360.f;
			OutSearchZ = 190.f;
			++RoamLegIndex;
			return true;
		}
		HallLegCountdown = 4;
	}

	TArray<FCityMapBlock> Blocks;
	City->GetMapBlocks(Blocks);
	TArray<FVector> StreetStops;
	StreetStops.Reserve(Blocks.Num());

	const FVector Center = City->GetCityCenter();
	const float Pitch = City->GetPitch();
	const float SideOffset = City->GetMapBlockSize() * 0.5f - 130.f;
	for (const FCityMapBlock& B : Blocks)
	{
		if (B.Label.Equals(TEXT("Park"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		const int32 GX = FMath::RoundToInt((B.Center.X - Center.X) / Pitch);
		const int32 GY = FMath::RoundToInt((B.Center.Y - Center.Y) / Pitch);
		FVector Dir((GX > 0) ? -1.f : (GX < 0) ? 1.f : 0.f, (GY > 0) ? -1.f : (GY < 0) ? 1.f : 0.f, 0.f);
		if (!FMath::IsNearlyZero(Dir.X))
		{
			Dir.Y = 0.f;
		}
		if (Dir.IsNearlyZero())
		{
			continue;
		}
		StreetStops.Add(FVector(B.Center.X, B.Center.Y, HomeFrontSpot.Z) + Dir * SideOffset);
	}

	if (StreetStops.Num() == 0)
	{
		return false;
	}

	const int32 Pick = FMath::Abs(RoamRouteSeed + RoamLegIndex * 11) % StreetStops.Num();
	OutGoal = StreetStops[Pick];
	OutSearchXY = 260.f;
	OutSearchZ = 500.f;
	++RoamLegIndex;
	return true;
}

void ACustomerBase::TickResident(float DeltaSeconds)
{
	UWorld* W = GetWorld();
	const AWeedShopGameState* GS = W ? W->GetGameState<AWeedShopGameState>() : nullptr;
	const UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr;
	const float Hour = DC ? DC->GetClockHour() : 12.f;
	const bool bNight = (Hour >= 19.f || Hour < 7.f);

	// --- Afspraak heeft voorrang op roamen/nacht ---
	if (bApptActive)
	{
		ParkPauseTimer = 0.f;
		bRoamGoalIsPark = false;
		// Afgehandeld (deal gesloten) of veiligheids-timeout -> afspraak loslaten, normaal leven hervatten.
		ApptTimeout -= DeltaSeconds;
		if (State == ECustomerState::Served || State == ECustomerState::Leaving || ApptTimeout <= 0.f)
		{
			EndAppointment();
		}
		else if (bApptComeToPlayer)
		{
			// "Ik kom langs": de bewoner komt naar beneden en wacht bij de hoofddeur van z'n gebouw
			// (begane grond, op de kompas), zodat je 'm daar treft i.p.v. helemaal naar boven te moeten.
			if (bAtHomeInside)
			{
				bAtHomeInside = false;
				SetActorHiddenInGame(false);
				SetActorEnableCollision(true);
				SetActorLocation(HomeFrontSpot);
			}
			WalkTo(HomeFrontSpot);
			if (FVector::Dist2D(GetActorLocation(), HomeFrontSpot) < 160.f)
			{
				if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
			}
			return;
		}
		else
		{
			// "Kom bij mij": verschijn in de eigen unit (navmesh kan niet naar boven, dus verplaats erheen)
			// en wacht daar tot de speler komt.
			if (!bApptArrived)
			{
				bApptArrived = true;
				bAtHomeInside = false;
				SetActorHiddenInGame(false);
				SetActorEnableCollision(true);
				SetActorLocation(HomeInteriorPos + FVector(0.f, 0.f, 4.f));
				if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
			}
			return;
		}
	}

	// Blijf alleen staan voor de speler als 'ie JOU nodig heeft (afspraak / staat te wachten). Gewone
	// roamers blijven gewoon doorlopen, ook als je vlak langs ze loopt (ze stopten anders te vaak).
	if (!bAtHomeInside && bNeedsPlayer && W)
	{
		if (const APlayerController* PC = W->GetFirstPlayerController())
		{
			if (const APawn* P = PC->GetPawn())
			{
				if (FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) < 280.f)
				{
					if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
					return;
				}
			}
		}
	}

	if (bNight)
	{
		if (bEmergingFromHome)
		{
			bEmergingFromHome = false;
			bAtHomeInside = true;
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			SetActorLocation(HomeInteriorPos + FVector(0.f, 0.f, 4.f));
			if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
			return;
		}
		// 's Nachts naar huis (plek vóór de voordeur) en daar 'naar binnen' verdwijnen.
		if (bAtHomeInside) { return; }
		bLeavingHomeRoute = false;
		bHasRoamGoal = false;
		bRoamGoalIsPark = false;
		ParkPauseTimer = 0.f;
		WalkTo(HomeFrontSpot);
		if (FVector::Dist2D(GetActorLocation(), HomeFrontSpot) < 170.f && FMath::Abs(GetActorLocation().Z - HomeFrontSpot.Z) < 220.f)
		{
			bAtHomeInside = true;
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
		}
		return;
	}

	// Dag: verschijn weer bij de voordeur als 'ie binnen was.
	if (bAtHomeInside)
	{
		StartResidentHomeExit(true);
	}
	if (TickResidentHomeExit(DeltaSeconds))
	{
		return;
	}

	// Roam: vaste grote stadsronde, met periodieke park- en flat-hal-stops. Alleen een nieuw doel
	// kiezen bij aankomst of timeout, zodat pathfinding tijd krijgt om een lange route af te maken.
	RecoverResidentIfStuck(DeltaSeconds);
	if (ParkPauseTimer > 0.f)
	{
		ParkPauseTimer -= DeltaSeconds;
		if (ParkPauseTimer <= 0.f)
		{
			ParkPauseTimer = 0.f;
			bHasRoamGoal = false;
			RoamTimer = 0.f;
		}
		return;
	}

	RoamTimer -= DeltaSeconds;
	const bool bArrived = bHasRoamGoal
		&& FVector::Dist2D(GetActorLocation(), RoamGoal) < 145.f
		&& FMath::Abs(GetActorLocation().Z - RoamGoal.Z) < 180.f;
	if (bArrived)
	{
		if (bRoamGoalIsPark)
		{
			if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
			bHasRoamGoal = false;
			bRoamGoalIsPark = false;
			ParkPauseTimer = FMath::FRandRange(4.f, 9.f);
			return;
		}

		bHasRoamGoal = false;
		bRoamGoalIsPark = false;
		RoamTimer = 0.f;
	}

	if (!bHasRoamGoal || RoamTimer <= 0.f)
	{
		FVector DesiredGoal;
		float SearchXY = 700.f;
		float SearchZ = 500.f;
		if (PickResidentRoamGoal(DesiredGoal, SearchXY, SearchZ) && SetResidentRoamGoal(DesiredGoal, SearchXY, SearchZ))
		{
			return;
		}

		// Fallback: als een hall/ver blok nog geen navmesh heeft, blijf niet hangen maar pak een lokaal pad.
		if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(W))
		{
			FNavLocation Out;
			const FVector Self = GetActorLocation();
			if (Nav->GetRandomReachablePointInRadius(Self, 1800.f, Out) && WalkTo(Out.Location))
			{
				RoamGoal = Out.Location;
				bHasRoamGoal = true;
				bRoamGoalIsPark = false;
				RoamTimer = ComputeResidentRoamTimeout(RoamGoal);
			}
			else if (Nav->ProjectPointToNavigation(HomeFrontSpot, Out, FVector(1400.f, 1400.f, 600.f)) && WalkTo(Out.Location))
			{
				RoamGoal = Out.Location;
				bHasRoamGoal = true;
				bRoamGoalIsPark = false;
				RoamTimer = ComputeResidentRoamTimeout(RoamGoal);
			}
			else
			{
				bHasRoamGoal = false;
				bRoamGoalIsPark = false;
				RoamTimer = 3.f;
			}
		}
	}
}

int32 ACustomerBase::GetMarketPriceCents() const
{
	if (!ProductTable || DesiredProductId.IsNone())
	{
		return 0;
	}
	const FWeedShopProductRow* Row =
		ProductTable->FindRow<FWeedShopProductRow>(DesiredProductId, TEXT("ACustomerBase::GetMarketPriceCents"), false);
	return Row ? Row->MarketPriceCents : 0;
}

int32 ACustomerBase::GetMarketPriceForProduct(FName ProductId) const
{
	if (!ProductTable || ProductId.IsNone()) { return 0; }
	// Alleen VERPAKTE wiet is verkoopbaar aan klanten. Een Bag_<strain> wordt geprijsd via de
	// product-rij van de strain (Bud_<strain>). Losse/natte buds -> 0 (niet verkoopbaar).
	const FString S = ProductId.ToString();
	FName LookupId = ProductId;
	if (S.StartsWith(TEXT("Bag_")))
	{
		LookupId = FName(*FString::Printf(TEXT("Bud_%s"), *S.RightChop(4)));
	}
	else if (!S.StartsWith(TEXT("Bud_")))
	{
		// Geen verpakt product en geen tabel-rij -> niet verkoopbaar.
	}
	const FWeedShopProductRow* Row =
		ProductTable->FindRow<FWeedShopProductRow>(LookupId, TEXT("ACustomerBase::GetMarketPriceForProduct"), false);
	// Losse Bud_ (niet verpakt) is bewust NIET verkoopbaar aan klanten.
	if (S.StartsWith(TEXT("Bud_"))) { return 0; }
	return Row ? Row->MarketPriceCents : 0;
}

float ACustomerBase::GetAcceptanceChance(int32 AskPriceCentsPerUnit, float Quality01) const
{
	return UWeedDealLibrary::CalculateAcceptanceChance(
		static_cast<float>(GetMarketPriceCents()), static_cast<float>(AskPriceCentsPerUnit),
		Respect, Loyalty, Addiction, Quality01);
}

float ACustomerBase::GetSubstituteAcceptance(FName AltProductId, int32 AskPriceCentsPerUnit, float Quality01) const
{
	const int32 Market = GetMarketPriceForProduct(AltProductId);
	if (Market <= 0) { return 0.f; }
	const float Base = UWeedDealLibrary::CalculateAcceptanceChance(
		static_cast<float>(Market), static_cast<float>(AskPriceCentsPerUnit),
		Respect, Loyalty, Addiction, Quality01);
	// Bereidheid om iets anders te nemen: ~50% basis, hoger bij loyaliteit/verslaving, EN een scherpe
	// prijs compenseert het substituut (goedkoper -> hij neemt het toch). Kan oplopen tot ~100%.
	const float Ratio = FMath::Clamp(static_cast<float>(AskPriceCentsPerUnit) / static_cast<float>(Market), 0.30f, 2.20f);
	const float PriceComp = FMath::Max(0.f, 1.f - Ratio) * 0.6f; // 40% prijs -> +0.36
	const float Willing = FMath::Clamp(0.50f + (Loyalty - 30.f) * 0.004f + (Addiction - 30.f) * 0.005f + PriceComp, 0.30f, 1.0f);
	return Base * Willing;
}

namespace
{
	// Relatie-winst van een GESLAAGDE deal (gedeeld door SubmitOffer en de UI-preview, zodat ze
	// gegarandeerd gelijk lopen). Vloeiend verloop:
	//   * Respect/Loyalty volgen de prijs: scherp (goedkoop) bouwt op, woeker (duur) breekt af.
	//     + een kleine bonus/straf op basis van kwaliteit.
	//   * Verslaving (A) hangt aan de POTENTIE (THC%), niet de prijs, en is bescheiden: zwakke
	//     17%-startwiet verslaaft maar licht, sterke wiet meer.
	// Quality01 < 0 = neutraal (0.6); ThcPercent < 0 = neutraal (15%).
	void ComputeAcceptedDeltas(int32 Ask, int32 Market, float Quality01, float ThcPercent,
		bool bSubstitute, float& dR, float& dL, float& dA)
	{
		const float Q = (Quality01 >= 0.f) ? FMath::Clamp(Quality01, 0.f, 1.f) : 0.6f;
		const float Thc = FMath::Clamp((ThcPercent >= 0.f) ? ThcPercent : 15.f, 0.f, 40.f);
		const float Ratio = (Market > 0) ? FMath::Clamp(float(Ask) / float(Market), 0.30f, 2.20f) : 1.f;

		// Respect: eerlijke/scherpe prijs verdient respect, woeker kost het. + kwaliteit-nuance.
		dR = (1.00f - Ratio) * 6.0f + (Q - 0.50f) * 3.0f;
		// Loyalty: bouwt op goede (goedkope + kwaliteit) deals, nauwelijks op dure.
		dL = (1.15f - Ratio) * 7.0f + (Q - 0.50f) * 4.0f;
		// Verslaving: gedreven door potentie, bescheiden en prijs-onafhankelijk.
		dA = 0.5f + (Thc / 100.f) * 11.0f;

		// Een andere strain dan gevraagd bindt iets minder (maar een scherpe prijs compenseert mee
		// via de prijs-termen hierboven).
		if (bSubstitute) { dL *= 0.6f; }
	}
}

void ACustomerBase::PreviewDealOutcome(int32 AskPriceCentsPerUnit, float Quality01, float ThcPercent,
	float& OutRespect, float& OutLoyalty, float& OutAddiction, bool bSubstitute) const
{
	float dR = 0.f, dL = 0.f, dA = 0.f;
	ComputeAcceptedDeltas(AskPriceCentsPerUnit, GetMarketPriceCents(), Quality01, ThcPercent, bSubstitute, dR, dL, dA);
	OutRespect = ClampAttr(Respect + dR);
	OutLoyalty = ClampAttr(Loyalty + dL);
	OutAddiction = ClampAttr(Addiction + dA);
}

EDealResult ACustomerBase::SubmitOffer(int32 AskPriceCentsPerUnit, UEconomyComponent* PayTo, UInventoryComponent* StockFrom)
{
	return SubmitOfferProduct(DesiredProductId, AskPriceCentsPerUnit, PayTo, StockFrom);
}

EDealResult ACustomerBase::SubmitOfferProduct(FName ProductId, int32 AskPriceCentsPerUnit, UEconomyComponent* PayTo, UInventoryComponent* StockFrom)
{
	if (!HasAuthority())
	{
		return EDealResult::Refused;
	}
	if (State != ECustomerState::WantsToOrder && State != ECustomerState::Negotiating)
	{
		return EDealResult::Refused;
	}
	if (ProductId.IsNone()) { ProductId = DesiredProductId; }
	const bool bSubstitute = (ProductId != DesiredProductId);

	const int32 Market = GetMarketPriceForProduct(ProductId);
	if (Market <= 0)
	{
		return EDealResult::Refused;
	}

	// Voorraad-check (op het aangeboden product).
	if (!StockFrom || !StockFrom->HasItem(ProductId, DesiredQuantity))
	{
		UE_LOG(LogWeedShop, Log, TEXT("Klant: geen voorraad van %s (x%d)."), *ProductId.ToString(), DesiredQuantity);
		return EDealResult::NoStock;
	}

	// Kwaliteit (0..1) + potentie (THC%) van de wiet die je verkoopt.
	const float Quality01 = FMath::Clamp(StockFrom->GetItemQualityPct(ProductId) / 100.f, 0.f, 1.f);
	const float ThcStock = StockFrom->GetItemQuality(ProductId);

	// Boven budget -> dingt af.
	if (AskPriceCentsPerUnit > BudgetCentsPerUnit)
	{
		State = ECustomerState::Negotiating;
		return EDealResult::Haggle;
	}

	// Substituut = ~50% basis (stats-afhankelijk); anders de normale kans.
	const float Chance = bSubstitute
		? GetSubstituteAcceptance(ProductId, AskPriceCentsPerUnit, Quality01)
		: GetAcceptanceChance(AskPriceCentsPerUnit, Quality01);
	const bool bAccepts = FMath::FRandRange(0.f, 100.f) <= Chance;

	if (!bAccepts)
	{
		// Te duur -> onderhandelen; anders simpelweg geweigerd (kleine respect-knauw).
		if (!bSubstitute && AskPriceCentsPerUnit > Market)
		{
			State = ECustomerState::Negotiating;
			return EDealResult::Haggle;
		}
		Respect = ClampAttr(Respect - (bSubstitute ? 2.f : 4.f));
		WriteStatsToRegistry();
		return EDealResult::Refused;
	}

	// Deal rond: betalen, voorraad af, attributen bijwerken.
	const int32 Total = AskPriceCentsPerUnit * DesiredQuantity;
	StockFrom->RemoveItem(ProductId, DesiredQuantity);
	if (PayTo)
	{
		PayTo->AddMoney(Total);
	}

	float dR = 0.f, dL = 0.f, dA = 0.f;
	ComputeAcceptedDeltas(AskPriceCentsPerUnit, Market, Quality01, ThcStock, bSubstitute, dR, dL, dA);
	Respect = ClampAttr(Respect + dR);
	Loyalty = ClampAttr(Loyalty + dL);
	Addiction = ClampAttr(Addiction + dA);

	State = ECustomerState::Served;
	WriteStatsToRegistry();
	// Cooldown starten: deze NPC (in persoon of via telefoon-afspraak) komt niet meteen terug.
	if (AWeedShopGameState* GSc = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (GSc->GetNpcRegistry() && !NpcId.IsNone()) { GSc->GetNpcRegistry()->MarkDealt(NpcId); }
	}

	// XP: per verdiende euro + een vaste bonus per geslaagde deal.
	if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (ULevelComponent* Lv = GS->GetLeveling())
		{
			Lv->AddXP(5 + Total / 100);
		}
	}

	UE_LOG(LogWeedShop, Log, TEXT("Deal: %dx %s%s voor %d cents (resp %.0f loy %.0f ver %.0f)."),
		DesiredQuantity, *ProductId.ToString(), bSubstitute ? TEXT(" [substitute]") : TEXT(""), Total, Respect, Loyalty, Addiction);
	return EDealResult::Accepted;
}

void ACustomerBase::Interact_Implementation(APawn* InstigatorPawn)
{
	// Server-authoritative (via de interactie-component). Snel-test: verkoop tegen marktprijs uit
	// de voorraad van de speler naar de gedeelde kas op de GameState.
	if (!HasAuthority())
	{
		return;
	}

	// Klanten betalen de speler die ze bedient (z'n eigen portemonnee).
	UEconomyComponent* Econ = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UEconomyComponent>() : nullptr;
	UInventoryComponent* Stock = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;

	// (Contact/'nummer' krijg je via het NPC-register zodra de loyaliteit hoog genoeg is.)

	const EDealResult Result = SubmitOffer(GetMarketPriceCents(), Econ, Stock);
	UE_LOG(LogWeedShop, Log, TEXT("Klant-interactie resultaat: %d"), static_cast<int32>(Result));

	if (GEngine)
	{
		FColor C = FColor::White;
		FString Msg;
		switch (Result)
		{
		case EDealResult::Accepted: C = FColor::Green;  Msg = TEXT("Sold!"); break;
		case EDealResult::NoStock:  C = FColor::Orange; Msg = FString::Printf(TEXT("No stock: %s"), *DesiredProductId.ToString()); break;
		case EDealResult::Haggle:   C = FColor::Yellow; Msg = TEXT("Customer thinks it's too expensive"); break;
		default:                    C = FColor::Red;    Msg = TEXT("Customer refuses"); break;
		}
		UWeedToast::Notify(-1, 3.f, C, Msg);
	}
}

FText ACustomerBase::GetInteractionPrompt_Implementation() const
{
	switch (State)
	{
	case ECustomerState::WantsToOrder:
	case ECustomerState::Negotiating:
		return FText::FromString(TEXT("Deal"));
	case ECustomerState::Prospect:
		return FText::FromString(TEXT("Give a hit"));
	case ECustomerState::Served:
		return FText::FromString(TEXT("Satisfied customer"));
	default:
		return FText::GetEmpty();
	}
}

void ACustomerBase::OnRep_Order()
{
	// Hook voor UI (bv. order-bubbel boven de klant updaten).
}

void ACustomerBase::LeaveAngry()
{
	Respect = ClampAttr(Respect - 10.f);
	State = ECustomerState::Leaving;
	UE_LOG(LogWeedShop, Log, TEXT("Klant vertrekt boos (geduld op). Respect nu %.0f."), Respect);
}
