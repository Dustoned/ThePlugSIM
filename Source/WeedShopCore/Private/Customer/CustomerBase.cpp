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
#include "CollisionQueryParams.h"
#include "NavigationPath.h"
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

namespace
{
	bool ResidentSideHasStreetNeighbor(ACityGenerator* City, const FCityMapBlock& Block, int32 SideX, int32 SideY)
	{
		if (!City || (SideX == 0 && SideY == 0))
		{
			return false;
		}

		const FVector Center = City->GetCityCenter();
		const float Pitch = FMath::Max(1.f, City->GetPitch());
		const int32 R = City->GetGridRadiusClamped();
		const int32 GX = FMath::RoundToInt((Block.Center.X - Center.X) / Pitch);
		const int32 GY = FMath::RoundToInt((Block.Center.Y - Center.Y) / Pitch);
		const int32 NX = GX + FMath::Clamp(SideX, -1, 1);
		const int32 NY = GY + FMath::Clamp(SideY, -1, 1);
		return NX >= -R && NX <= R && NY >= -R && NY <= R;
	}

	const TArray<FCityMapBlock>& GetResidentMapBlocksCached(ACityGenerator* City)
	{
		static TWeakObjectPtr<ACityGenerator> CachedCity;
		static TArray<FCityMapBlock> CachedBlocks;
		static const TArray<FCityMapBlock> EmptyBlocks;

		if (!City)
		{
			return EmptyBlocks;
		}
		if (CachedCity.Get() != City || CachedBlocks.Num() == 0)
		{
			CachedCity = City;
			City->GetMapBlocks(CachedBlocks);
		}
		return CachedBlocks;
	}
}

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
		Move->AvoidanceConsiderationRadius = 135.f;
		Move->AvoidanceWeight = 0.62f;
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

bool ACustomerBase::WalkTo(const FVector& Dest, float AcceptanceRadius, bool bAllowPartialPath, bool bForceRepath)
{
	if (!GetController())
	{
		SpawnDefaultController();
	}
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		const bool bSameGoal = bHasLastMoveRequestGoal
			&& FVector::DistSquared2D(LastMoveRequestGoal, Dest) < FMath::Square(85.f)
			&& FMath::Abs(LastMoveRequestGoal.Z - Dest.Z) < 180.f;
		if (!bForceRepath && bSameGoal && (Now - LastMoveRequestTime) < 1.1f
			&& AI->GetMoveStatus() == EPathFollowingStatus::Moving)
		{
			return true;
		}

		const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(
			Dest,
			FMath::Max(35.f, AcceptanceRadius),
			true,
			true,
			true,
			false,
			nullptr,
			bAllowPartialPath);
		if (Result != EPathFollowingRequestResult::Failed)
		{
			LastMoveRequestGoal = Dest;
			bHasLastMoveRequestGoal = true;
			LastMoveRequestTime = Now;
		}
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
	HomeInteriorPos = InteriorPos;
	HomeHallPos = HallPos;
	bHasHomeHall = !HallPos.IsNearlyZero();
	HomeNumber = HouseNumber;
	HomeFrontSpot = ResolveResidentHomeFrontSpot(FrontSpot);
	HomeExitSidewalkSpot = ResolveResidentHomeExitSidewalkSpot(GetResidentCity(GetWorld()), HomeFrontSpot);
	bDespawnAfterServed = false;
	RoamRouteSeed = static_cast<int32>(GetTypeHash(HomeNumber));
	ParkLegCountdown = 2 + FMath::Abs(RoamRouteSeed % 3);
	HallLegCountdown = 3 + FMath::Abs((RoamRouteSeed / 7) % 4);
	RoamLegIndex = FMath::Abs(RoamRouteSeed % 97);
	ResidentRouteDay = -1;
	ResidentStreetLegsToday = 0;
	LastParkVisitDay = -1;
	LastMorningParkVisitDay = -1;
	LastLaterParkVisitDay = -1;
	PendingParkVisitSlot = 0;
	ActiveParkVisitSlot = 0;
	ResidentWakeDelay = ComputeResidentGoalThinkDelay(0.3f, 16.f);
	RoamTimer = ComputeResidentGoalThinkDelay(0.6f, 4.8f);
	bAtHomeInside = true;
	bEmergingFromHome = false;
	bEnteringHome = false;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorLocation(MakeResidentStandingLocation(HomeInteriorPos));
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}

	// Rustige wandeltred: bewoners slenteren over straat i.p.v. te sprinten.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = 135.f;
	}
}

bool ACustomerBase::ShouldShowOnCityMap() const
{
	if (IsHidden())
	{
		return false;
	}
	if (!bResident)
	{
		return true;
	}
	if (bApptActive && !bApptComeToPlayer && bApptArrived)
	{
		return true;
	}
	if (bAtHomeInside || bEmergingFromHome)
	{
		return false;
	}
	if (bEnteringHome && HomeEntryStage > 0)
	{
		return false;
	}
	return true;
}

bool ACustomerBase::GetResidentMovementSnapshot(FResidentMovementSnapshot& OutSnapshot)
{
	OutSnapshot = FResidentMovementSnapshot();
	if (!bResident)
	{
		return false;
	}

	OutSnapshot.bValid = true;
	OutSnapshot.ResidentLabel = !HomeNumber.IsEmpty() ? HomeNumber : NpcId.ToString();
	OutSnapshot.bVisibleOnMap = ShouldShowOnCityMap();
	OutSnapshot.bAtHomeInside = bAtHomeInside;
	OutSnapshot.bEmergingFromHome = bEmergingFromHome;
	OutSnapshot.bEnteringHome = bEnteringHome;
	OutSnapshot.bHasGoal = bHasRoamGoal;
	OutSnapshot.bGoalIsPark = bRoamGoalIsPark;
	OutSnapshot.bParkPause = ParkPauseTimer > 0.f;
	OutSnapshot.Speed2D = GetVelocity().Size2D();
	OutSnapshot.NoGoalSeconds = ResidentNoGoalTimer;
	OutSnapshot.StuckSeconds = ResidentStuckTimer;
	OutSnapshot.Location = GetActorLocation();
	OutSnapshot.Goal = RoamGoal;
	OutSnapshot.DistanceToGoal = bHasRoamGoal ? FVector::Dist2D(OutSnapshot.Location, RoamGoal) : 0.f;

	ACityGenerator* City = GetResidentCity(GetWorld());
	if (City)
	{
		OutSnapshot.DistanceFromCenter = FVector::Dist2D(OutSnapshot.Location, City->GetCityCenter());
		const float EdgeDistance = City->GetPitch() * (static_cast<float>(City->GetGridRadiusClamped()) - 0.35f);
		OutSnapshot.bNearMapEdge = OutSnapshot.DistanceFromCenter >= EdgeDistance;
		OutSnapshot.bOnSidewalkOrPark = bAtHomeInside || bEmergingFromHome || bEnteringHome
			|| IsResidentOutdoorSidewalkPoint(City, OutSnapshot.Location, true)
			|| IsResidentParkPoint(City, OutSnapshot.Location);
		OutSnapshot.bLikelyStreetCrossing = !OutSnapshot.bOnSidewalkOrPark
			&& bHasRoamGoal
			&& OutSnapshot.Speed2D > 55.f
			&& OutSnapshot.DistanceToGoal > 260.f;
	}
	else
	{
		OutSnapshot.bOnSidewalkOrPark = true;
	}

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr;
	const int32 Today = DC ? DC->GetDayNumber() : ResidentRouteDay;
	OutSnapshot.bNeedsParkVisitToday = Today >= 0 && GetResidentParkVisitsToday(Today) < 2;
	if (City && DC && OutSnapshot.bNeedsParkVisitToday && !bAtHomeInside && !bEmergingFromHome && !bEnteringHome)
	{
		const int32 NextSlot = PickResidentParkVisitSlot(City, DC, Today, DC->GetClockHour());
		OutSnapshot.bParkUrgentToday = NextSlot > 0 && DC->GetClockHour() >= ComputeResidentParkUrgencyHour(City, DC, Today, NextSlot);
	}
	const bool bOutdoorGoalBlocked = !bAtHomeInside && !bEmergingFromHome && !bEnteringHome && bHasRoamGoal;
	OutSnapshot.bStuckSuspect = bOutdoorGoalBlocked && (ResidentStuckTimer > 1.4f || ResidentNoGoalTimer > 10.f);
	return true;
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

FVector ACustomerBase::ResolveResidentHomeFrontSpot(const FVector& FrontSpot)
{
	ACityGenerator* City = GetResidentCity(GetWorld());
	const FVector SidewalkSpot = City ? SnapResidentPointToSidewalk(City, FrontSpot, false) : FrontSpot;

	if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		FNavLocation Projected;
		const FVector TightExtent(360.f, 360.f, 520.f);
		if (Nav->ProjectPointToNavigation(SidewalkSpot, Projected, TightExtent)
			&& (!City || IsResidentOutdoorSidewalkPoint(City, Projected.Location, false)))
		{
			return Projected.Location + FVector(0.f, 0.f, 3.f);
		}
	}

	return SidewalkSpot + FVector(0.f, 0.f, 3.f);
}

FVector ACustomerBase::ResolveResidentHomeExitSidewalkSpot(ACityGenerator* City, const FVector& SafeFrontSpot) const
{
	if (!City)
	{
		return SafeFrontSpot;
	}

	const int32 Seed = static_cast<int32>(GetTypeHash(HomeNumber));
	const float BaseOffset = 780.f + static_cast<float>(FMath::Abs(Seed % 3)) * 160.f;
	const float FirstSign = (Seed & 1) == 0 ? 1.f : -1.f;
	const float Offsets[] = {
		FirstSign * BaseOffset,
		-FirstSign * BaseOffset,
		FirstSign * BaseOffset * 1.35f,
		-FirstSign * BaseOffset * 1.35f
	};

	const TArray<FCityMapBlock>& Blocks = GetResidentMapBlocksCached(City);
	const float BlockSize = FMath::Max(500.f, City->GetMapBlockSize());
	const float Half = BlockSize * 0.5f;
	const float Tolerance = 220.f;
	const float MinLaunchDistance = 560.f;

	for (const FCityMapBlock& B : Blocks)
	{
		if (B.Label.Equals(TEXT("Park"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		FVector Local = SafeFrontSpot - FVector(B.Center.X, B.Center.Y, SafeFrontSpot.Z);
		Local.Z = 0.f;
		if (FMath::Abs(Local.X) > Half + Tolerance || FMath::Abs(Local.Y) > Half + Tolerance)
		{
			continue;
		}

		TArray<FVector> Tangents;
		auto AddTangent = [&](int32 SideX, int32 SideY)
		{
			if (ResidentSideHasStreetNeighbor(City, B, SideX, SideY))
			{
				Tangents.Add(SideX != 0 ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f));
			}
		};

		const int32 SignX = Local.X >= 0.f ? 1 : -1;
		const int32 SignY = Local.Y >= 0.f ? 1 : -1;
		if (FMath::Abs(Local.X) >= FMath::Abs(Local.Y))
		{
			AddTangent(SignX, 0);
			AddTangent(0, SignY);
			AddTangent(-SignX, 0);
			AddTangent(0, -SignY);
		}
		else
		{
			AddTangent(0, SignY);
			AddTangent(SignX, 0);
			AddTangent(0, -SignY);
			AddTangent(-SignX, 0);
		}

		for (const FVector& Tangent : Tangents)
		{
			for (float Offset : Offsets)
			{
				FVector Candidate = SnapResidentPointToSidewalk(City, SafeFrontSpot + Tangent * Offset, false);
				if (FVector::Dist2D(SafeFrontSpot, Candidate) < MinLaunchDistance
					|| !IsResidentOutdoorSidewalkPoint(City, Candidate, false))
				{
					continue;
				}

				if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld()))
				{
					FNavLocation Projected;
					if (Nav->ProjectPointToNavigation(Candidate, Projected, FVector(420.f, 420.f, 520.f))
						&& IsResidentOutdoorSidewalkPoint(City, Projected.Location, false))
					{
						return Projected.Location + FVector(0.f, 0.f, 3.f);
					}
				}
				else
				{
					return Candidate + FVector(0.f, 0.f, 3.f);
				}
			}
		}

		break;
	}

	return SafeFrontSpot;
}

FVector ACustomerBase::MakeResidentStandingLocation(const FVector& FloorLocation) const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
	return FVector(FloorLocation.X, FloorLocation.Y, FloorLocation.Z + HalfHeight + 2.f);
}

float ACustomerBase::ComputeResidentGoalThinkDelay(float MinDelay, float MaxDelay) const
{
	const int32 Noise = FMath::Abs(static_cast<int32>(
		(static_cast<int64>(RoamRouteSeed) * 37
			+ static_cast<int64>(RoamLegIndex) * 113
			+ static_cast<int64>(ResidentStreetLegsToday) * 53) % 1000));
	const float Alpha = static_cast<float>(Noise) / 999.f;
	return FMath::Lerp(MinDelay, MaxDelay, Alpha);
}

FVector ACustomerBase::GetResidentHomeEntrySpot() const
{
	FVector ToHome = HomeInteriorPos - HomeFrontSpot;
	ToHome.Z = 0.f;
	if (!ToHome.Normalize())
	{
		return HomeInteriorPos + FVector(0.f, 0.f, 4.f);
	}

	const float EntryDepth = FMath::Min(520.f, FMath::Max(260.f, FVector::Dist2D(HomeInteriorPos, HomeFrontSpot) * 0.65f));
	return HomeFrontSpot + ToHome * EntryDepth;
}

void ACustomerBase::StartResidentHomeExit(bool bFromInterior)
{
	bAtHomeInside = false;
	bEmergingFromHome = true;
	bEnteringHome = false;
	HomeExitStage = (bFromInterior && bHasHomeHall) ? 0 : 1;
	HomeExitStuckTimer = 0.f;
	bHasRoamGoal = false;
	bRoamGoalIsPark = false;
	bPendingRoamGoalIsPark = false;
	PendingParkVisitSlot = 0;
	ActiveParkVisitSlot = 0;
	bLeavingHomeRoute = false;
	ResidentWakeDelay = -1.f;
	ParkPauseTimer = 0.f;
	ResidentStuckTimer = 0.f;
	ResidentRecoveryCooldown = 0.f;
	ResidentOffSidewalkTimer = 0.f;
	bHasResidentPrevMoveLoc = false;
	bHasResidentBestDistToGoal = false;
	ResidentRecoveryAttempts = 0;
	ResidentNoGoalTimer = 0.f;
	ResidentGoalFailCount = 0;
	bHasLastMoveRequestGoal = false;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	if (!GetController())
	{
		SpawnDefaultController();
	}

	const FVector Spawn = bFromInterior ? HomeInteriorPos : (bHasHomeHall ? HomeHallPos : HomeInteriorPos);
	SetActorLocation(MakeResidentStandingLocation(Spawn));
}

bool ACustomerBase::TickResidentHomeExit(float DeltaSeconds)
{
	if (!bEmergingFromHome)
	{
		return false;
	}

	if (bHasHomeHall && HomeExitStage == 1)
	{
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		SetActorLocation(MakeResidentStandingLocation(GetResidentHomeEntrySpot()));
		SetActorEnableCollision(true);
		SetActorHiddenInGame(false);
		HomeExitStage = 2;
		HomeExitStuckTimer = 0.f;
		bHasResidentPrevMoveLoc = false;
		bHasLastMoveRequestGoal = false;
		return true;
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
		bLeavingHomeRoute = FVector::Dist2D(HomeFrontSpot, HomeExitSidewalkSpot) >= 520.f;
		bHasRoamGoal = false;
		bRoamGoalIsPark = false;
		bPendingRoamGoalIsPark = false;
		PendingParkVisitSlot = 0;
		ActiveParkVisitSlot = 0;
		RoamTimer = ComputeResidentGoalThinkDelay(0.4f, 4.2f);
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

	if (HomeExitStuckTimer >= (bHallStage ? 0.9f : 1.2f))
	{
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}

		if (bHallStage)
		{
			SetActorLocation(MakeResidentStandingLocation(HomeHallPos));
			HomeExitStage = 1;
		}
		else
		{
			SetActorLocation(MakeResidentStandingLocation(HomeFrontSpot));
			bEmergingFromHome = false;
			bLeavingHomeRoute = FVector::Dist2D(HomeFrontSpot, HomeExitSidewalkSpot) >= 520.f;
			bHasRoamGoal = false;
			bRoamGoalIsPark = false;
			bPendingRoamGoalIsPark = false;
			PendingParkVisitSlot = 0;
			ActiveParkVisitSlot = 0;
			RoamTimer = ComputeResidentGoalThinkDelay(0.4f, 4.2f);
		}
		HomeExitStuckTimer = 0.f;
		bHasResidentPrevMoveLoc = false;
	}

	return true;
}

void ACustomerBase::StartResidentHomeEntry()
{
	bEnteringHome = true;
	bEmergingFromHome = false;
	HomeEntryStage = 0;
	HomeEntryStuckTimer = 0.f;
	bHasRoamGoal = false;
	bRoamGoalIsPark = false;
	bPendingRoamGoalIsPark = false;
	PendingParkVisitSlot = 0;
	ActiveParkVisitSlot = 0;
	ParkPauseTimer = 0.f;
	ResidentStuckTimer = 0.f;
	ResidentRecoveryCooldown = 0.f;
	ResidentOffSidewalkTimer = 0.f;
	bHasResidentPrevMoveLoc = false;
	bHasResidentBestDistToGoal = false;
	ResidentRecoveryAttempts = 0;
	ResidentNoGoalTimer = 0.f;
	ResidentGoalFailCount = 0;
	bHasLastMoveRequestGoal = false;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	if (!GetController())
	{
		SpawnDefaultController();
	}
}

bool ACustomerBase::TickResidentHomeEntry(float DeltaSeconds)
{
	if (!bEnteringHome)
	{
		return false;
	}

	const bool bFrontStage = (HomeEntryStage == 0);
	const bool bApartmentUnitStage = bHasHomeHall && HomeEntryStage >= 2;
	const FVector Target = bFrontStage ? HomeFrontSpot : (bApartmentUnitStage ? (HomeInteriorPos + FVector(0.f, 0.f, 4.f)) : GetResidentHomeEntrySpot());
	const FVector Cur = GetActorLocation();
	const bool bArrived = FVector::Dist2D(Cur, Target) < (bFrontStage ? 160.f : 125.f)
		&& FMath::Abs(Cur.Z - Target.Z) < (bFrontStage ? 230.f : (bApartmentUnitStage ? 220.f : 190.f));

	if (bArrived)
	{
		if (bFrontStage)
		{
			HomeEntryStage = 1;
			HomeEntryStuckTimer = 0.f;
			bHasResidentPrevMoveLoc = false;
			return true;
		}
		if (bHasHomeHall && HomeEntryStage == 1)
		{
			if (AAIController* AI = Cast<AAIController>(GetController()))
			{
				AI->StopMovement();
			}
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			SetActorLocation(MakeResidentStandingLocation(HomeHallPos));
			SetActorEnableCollision(true);
			SetActorHiddenInGame(false);
			HomeEntryStage = 2;
			HomeEntryStuckTimer = 0.f;
			bHasResidentPrevMoveLoc = false;
			bHasLastMoveRequestGoal = false;
			return true;
		}

		bEnteringHome = false;
		bAtHomeInside = true;
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		SetActorLocation(HomeInteriorPos + FVector(0.f, 0.f, 4.f));
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}
		HomeEntryStuckTimer = 0.f;
		bHasResidentPrevMoveLoc = false;
		return true;
	}

	if (!bFrontStage)
	{
		const FVector DesiredActorLocation = MakeResidentStandingLocation(Target);
		FVector ToTarget = DesiredActorLocation - Cur;
		const float Dist = ToTarget.Size();
		if (Dist > 2.f)
		{
			const UCharacterMovementComponent* Move = GetCharacterMovement();
			const float Step = (Move ? FMath::Max(90.f, Move->MaxWalkSpeed) : 135.f) * DeltaSeconds;
			const FVector Next = Cur + ToTarget.GetSafeNormal() * FMath::Min(Dist, Step);
			SetActorLocation(Next, false);
			ToTarget.Z = 0.f;
			if (!ToTarget.IsNearlyZero())
			{
				SetActorRotation(ToTarget.Rotation());
			}
		}
		HomeEntryStuckTimer = 0.f;
		bHasResidentPrevMoveLoc = false;
		return true;
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
		HomeEntryStuckTimer += DeltaSeconds;
	}
	else
	{
		HomeEntryStuckTimer = 0.f;
	}

	if (HomeEntryStuckTimer >= (bFrontStage ? 1.2f : (bApartmentUnitStage ? 1.0f : 1.2f)))
	{
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}

		if (bFrontStage)
		{
			SetActorLocation(MakeResidentStandingLocation(HomeFrontSpot));
			HomeEntryStage = 1;
		}
		else if (bHasHomeHall && HomeEntryStage == 1)
		{
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			SetActorLocation(MakeResidentStandingLocation(HomeHallPos));
			SetActorEnableCollision(true);
			SetActorHiddenInGame(false);
			HomeEntryStage = 2;
		}
		else
		{
			SetActorLocation(MakeResidentStandingLocation(Target));
			bEnteringHome = false;
			bAtHomeInside = true;
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			SetActorLocation(HomeInteriorPos + FVector(0.f, 0.f, 4.f));
		}
		HomeEntryStuckTimer = 0.f;
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

	if (bComeToPlayer)
	{
		if (bEnteringHome)
		{
			if (AAIController* AI = Cast<AAIController>(GetController()))
			{
				AI->StopMovement();
			}
			bEnteringHome = false;
			bAtHomeInside = true;
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			SetActorLocation(HomeInteriorPos + FVector(0.f, 0.f, 4.f));
		}
		return;
	}

	bEnteringHome = false;
	bEmergingFromHome = false;
}

void ACustomerBase::EndAppointment()
{
	if (!HasAuthority()) { return; }
	bApptActive = false;
	bApptComeToPlayer = false;
	bApptArrived = false;
	SetNeedsPlayer(false);
	RoamTimer = 0.f;           // pak meteen een nieuw roam-doel
	bHasRoamGoal = false;
	bRoamGoalIsPark = false;
	bPendingRoamGoalIsPark = false;
	ResidentStuckTimer = 0.f;
	ResidentRecoveryCooldown = 0.f;
	bHasResidentPrevMoveLoc = false;
	bHasResidentBestDistToGoal = false;
	ResidentRecoveryAttempts = 0;
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

int32 ACustomerBase::CountResidentCrowdNear(const FVector& Point, float Radius) const
{
	UWorld* W = GetWorld();
	if (!W)
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
		if (FVector::DistSquared2D(C->GetActorLocation(), Point) <= RadiusSq)
		{
			++Count;
		}
	}

	if (const APlayerController* PC = W->GetFirstPlayerController())
	{
		if (const APawn* P = PC->GetPawn())
		{
			if (FVector::DistSquared2D(P->GetActorLocation(), Point) <= RadiusSq)
			{
				Count += 2;
			}
		}
	}
	return Count;
}

bool ACustomerBase::HasResidentPath(const FVector& From, const FVector& To, float MinDistance2D) const
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return false;
	}
	if (MinDistance2D > 0.f && FVector::Dist2D(From, To) < MinDistance2D)
	{
		return false;
	}
	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(W, From, To, const_cast<ACustomerBase*>(this));
	return Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() > 1;
}

bool ACustomerBase::HasResidentObstacleAhead(const FVector& Goal) const
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return false;
	}

	FVector Dir = Goal - GetActorLocation();
	Dir.Z = 0.f;
	if (!Dir.Normalize())
	{
		return false;
	}

	const FVector Start = GetActorLocation();
	const FVector End = Start + Dir * 260.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CustomerResidentAvoidance), false, this);
	Params.AddIgnoredActor(this);
	for (TActorIterator<ACustomerBase> It(W); It; ++It)
	{
		if (ACustomerBase* Other = *It)
		{
			Params.AddIgnoredActor(Other);
		}
	}
	if (const APlayerController* PC = W->GetFirstPlayerController())
	{
		if (APawn* P = PC->GetPawn())
		{
			Params.AddIgnoredActor(P);
		}
	}
	const FCollisionShape Shape = FCollisionShape::MakeSphere(46.f);
	FHitResult Hit;
	if (W->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_WorldStatic, Shape, Params))
	{
		return true;
	}
	return W->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_WorldDynamic, Shape, Params);
}

bool ACustomerBase::TrySetResidentDetourGoal(const FVector& FinalGoal)
{
	UWorld* W = GetWorld();
	UNavigationSystemV1* Nav = W ? UNavigationSystemV1::GetCurrent(W) : nullptr;
	if (!W || !Nav)
	{
		return false;
	}
	ACityGenerator* City = GetResidentCity(W);
	const bool bFinalIsPark = bRoamGoalIsPark;

	FVector Start = GetActorLocation();
	FNavLocation ProjectedStart;
	if (Nav->ProjectPointToNavigation(Start, ProjectedStart, FVector(420.f, 420.f, 700.f)))
	{
		Start = ProjectedStart.Location;
	}

	FVector ToGoal = FinalGoal - Start;
	ToGoal.Z = 0.f;
	if (!ToGoal.Normalize())
	{
		ToGoal = GetActorForwardVector();
		ToGoal.Z = 0.f;
		if (!ToGoal.Normalize())
		{
			ToGoal = FVector::ForwardVector;
		}
	}

	static const float Angles[] = { 85.f, -85.f, 125.f, -125.f, 45.f, -45.f, 170.f };
	static const float Radii[] = { 520.f, 820.f, 1180.f, 1550.f };
	const FVector Extent(420.f, 420.f, 520.f);

	bool bFound = false;
	FVector BestGoal = FVector::ZeroVector;
	bool bBestGoalIsPark = false;
	float BestScore = TNumericLimits<float>::Max();
	for (float Radius : Radii)
	{
		for (float Angle : Angles)
		{
			const FVector Raw = Start + ToGoal.RotateAngleAxis(Angle, FVector::UpVector) * Radius;
			FNavLocation Candidate;
			if (!Nav->ProjectPointToNavigation(Raw, Candidate, Extent))
			{
				continue;
			}
			FVector CandidateGoal = Candidate.Location;
			bool bCandidateIsPark = false;
			if (City)
			{
				bCandidateIsPark = bFinalIsPark && IsResidentParkPoint(City, CandidateGoal);
				if (!bCandidateIsPark)
				{
					CandidateGoal = SnapResidentPointToSidewalk(City, CandidateGoal, false);
					FNavLocation SidewalkProjection;
					if (!Nav->ProjectPointToNavigation(CandidateGoal, SidewalkProjection, Extent)
						|| !IsResidentOutdoorSidewalkPoint(City, SidewalkProjection.Location, false))
					{
						continue;
					}
					CandidateGoal = SidewalkProjection.Location;
					bCandidateIsPark = bFinalIsPark && IsResidentParkPoint(City, CandidateGoal);
				}
			}
			if (!HasResidentPath(Start, CandidateGoal, 260.f))
			{
				continue;
			}
			if (!HasResidentPath(CandidateGoal, FinalGoal, 320.f))
			{
				continue;
			}

			const int32 Crowd = CountResidentCrowdNear(CandidateGoal, 320.f);
			const float Score = FVector::Dist2D(CandidateGoal, FinalGoal) + Crowd * 1200.f + FMath::Abs(Angle) * 3.f;
			if (Score < BestScore)
			{
				BestScore = Score;
				BestGoal = CandidateGoal;
				bBestGoalIsPark = bCandidateIsPark;
				bFound = true;
			}
		}
	}

	if (!bFound || !WalkTo(BestGoal, 90.f, false, true))
	{
		return false;
	}

	RoamGoal = BestGoal;
	bHasRoamGoal = true;
	bRoamGoalIsPark = bBestGoalIsPark;
	bPendingRoamGoalIsPark = false;
	RoamTimer = FMath::Clamp(ComputeResidentRoamTimeout(RoamGoal), 12.f, 70.f);
	ResidentPrevMoveLoc = GetActorLocation();
	bHasResidentPrevMoveLoc = true;
	ResidentStuckTimer = 0.f;
	ResidentRecoveryCooldown = 0.8f;
	ResidentRecoveryAttempts = 0;
	ResidentNoGoalTimer = 0.f;
	ResidentGoalFailCount = 0;
	ResidentBestDistToGoal = FVector::Dist2D(GetActorLocation(), RoamGoal);
	bHasResidentBestDistToGoal = true;
	return true;
}

FVector ACustomerBase::SnapResidentPointToSidewalk(ACityGenerator* City, const FVector& Desired, bool bAllowPark) const
{
	if (!City)
	{
		return Desired;
	}

	const TArray<FCityMapBlock>& Blocks = GetResidentMapBlocksCached(City);
	const float BlockSize = FMath::Max(500.f, City->GetMapBlockSize());
	const float Half = BlockSize * 0.5f;
	const float SidewalkWidth = FMath::Clamp(City->GetSidewalkWidth(), 120.f, Half * 0.45f);
	const float Lane = Half - SidewalkWidth * 0.5f;
	const float EdgeClamp = Half - FMath::Max(45.f, SidewalkWidth * 0.22f);
	const FVector CityCenter = City->GetCityCenter();

	bool bFound = false;
	FVector Best = Desired;
	float BestScore = TNumericLimits<float>::Max();
	for (const FCityMapBlock& B : Blocks)
	{
		const bool bParkBlock = B.Label.Equals(TEXT("Park"), ESearchCase::IgnoreCase);
		if (bParkBlock && !bAllowPark)
		{
			continue;
		}

		const FVector BlockCenter(B.Center.X, B.Center.Y, Desired.Z);
		FVector Local = Desired - BlockCenter;
		Local.Z = 0.f;
		if (bParkBlock)
		{
			const float Ax = FMath::Abs(Local.X);
			const float Ay = FMath::Abs(Local.Y);
			if (Ax <= Half && Ay <= Half)
			{
				return Desired;
			}
			const FVector ClampedPark(
				BlockCenter.X + FMath::Clamp(Local.X, -Half + 120.f, Half - 120.f),
				BlockCenter.Y + FMath::Clamp(Local.Y, -Half + 120.f, Half - 120.f),
				Desired.Z);
			const float ParkScore = FVector::DistSquared2D(Desired, ClampedPark);
			if (ParkScore < BestScore)
			{
				BestScore = ParkScore;
				Best = ClampedPark;
				bFound = true;
			}
			continue;
		}

		if (Local.IsNearlyZero())
		{
			Local = BlockCenter - CityCenter;
			Local.Z = 0.f;
			if (Local.IsNearlyZero())
			{
				Local = HomeFrontSpot - BlockCenter;
				Local.Z = 0.f;
			}
		}

		auto TrySide = [&](int32 SideX, int32 SideY)
		{
			if (!ResidentSideHasStreetNeighbor(City, B, SideX, SideY))
			{
				return;
			}

			FVector Candidate = BlockCenter;
			if (SideX != 0)
			{
				Candidate.X += static_cast<float>(SideX) * Lane;
				Candidate.Y += FMath::Clamp(Local.Y, -EdgeClamp, EdgeClamp);
			}
			else
			{
				Candidate.X += FMath::Clamp(Local.X, -EdgeClamp, EdgeClamp);
				Candidate.Y += static_cast<float>(SideY) * Lane;
			}

			const float Score = FVector::DistSquared2D(Desired, Candidate);
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Candidate;
				bFound = true;
			}
		};

		TrySide(1, 0);
		TrySide(-1, 0);
		TrySide(0, 1);
		TrySide(0, -1);
	}

	return bFound ? Best : Desired;
}

bool ACustomerBase::IsResidentOutdoorSidewalkPoint(ACityGenerator* City, const FVector& Point, bool bAllowPark) const
{
	if (!City)
	{
		return true;
	}

	const TArray<FCityMapBlock>& Blocks = GetResidentMapBlocksCached(City);
	const float BlockSize = FMath::Max(500.f, City->GetMapBlockSize());
	const float Half = BlockSize * 0.5f;
	const float SidewalkWidth = FMath::Clamp(City->GetSidewalkWidth(), 120.f, Half * 0.45f);
	const float Tolerance = 80.f;
	for (const FCityMapBlock& B : Blocks)
	{
		const bool bParkBlock = B.Label.Equals(TEXT("Park"), ESearchCase::IgnoreCase);
		if (bParkBlock && !bAllowPark)
		{
			continue;
		}

		const float LocalX = Point.X - B.Center.X;
		const float LocalY = Point.Y - B.Center.Y;
		const float Ax = FMath::Abs(LocalX);
		const float Ay = FMath::Abs(LocalY);
		if (Ax > Half + Tolerance || Ay > Half + Tolerance)
		{
			continue;
		}
		if (bParkBlock)
		{
			return true;
		}
		const float BandStart = Half - SidewalkWidth - Tolerance;
		if (Ax >= BandStart)
		{
			const int32 SideX = LocalX >= 0.f ? 1 : -1;
			if (ResidentSideHasStreetNeighbor(City, B, SideX, 0))
			{
				return true;
			}
		}
		if (Ay >= BandStart)
		{
			const int32 SideY = LocalY >= 0.f ? 1 : -1;
			if (ResidentSideHasStreetNeighbor(City, B, 0, SideY))
			{
				return true;
			}
		}
	}

	return false;
}

bool ACustomerBase::IsResidentParkPoint(ACityGenerator* City, const FVector& Point) const
{
	if (!City)
	{
		return false;
	}

	const TArray<FCityMapBlock>& Blocks = GetResidentMapBlocksCached(City);
	const float Half = FMath::Max(500.f, City->GetMapBlockSize()) * 0.5f;
	const float Tolerance = 90.f;
	for (const FCityMapBlock& B : Blocks)
	{
		if (!B.Label.Equals(TEXT("Park"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const float LocalX = Point.X - B.Center.X;
		const float LocalY = Point.Y - B.Center.Y;
		return FMath::Abs(LocalX) <= Half + Tolerance && FMath::Abs(LocalY) <= Half + Tolerance;
	}
	return false;
}

void ACustomerBase::BuildResidentStreetStops(ACityGenerator* City, TArray<FVector>& OutStops) const
{
	OutStops.Reset();
	if (!City)
	{
		return;
	}

	TArray<FCityMapBlock> Blocks;
	City->GetMapBlocks(Blocks);
	const float BlockSize = FMath::Max(500.f, City->GetMapBlockSize());
	const float SidewalkWidth = FMath::Clamp(City->GetSidewalkWidth(), 120.f, BlockSize * 0.225f);
	const float SideOffset = BlockSize * 0.5f - SidewalkWidth * 0.5f;
	const float AlongWide = FMath::Clamp(BlockSize * 0.24f, 260.f, 950.f);
	const float AlongSmall = FMath::Clamp(BlockSize * 0.10f, 120.f, 420.f);
	OutStops.Reserve(Blocks.Num() * 12);

	int32 BlockIndex = 0;
	for (const FCityMapBlock& B : Blocks)
	{
		++BlockIndex;
		if (B.Label.Equals(TEXT("Park"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FVector Base(B.Center.X, B.Center.Y, HomeFrontSpot.Z);
		const int32 ZigStep = FMath::Abs(static_cast<int32>((static_cast<int64>(RoamRouteSeed) + static_cast<int64>(BlockIndex) * 41) % 5)) - 2;
		const float Zig = static_cast<float>(ZigStep) * AlongSmall;

		auto AddXSide = [&](int32 SideX, float SideZig)
		{
			if (!ResidentSideHasStreetNeighbor(City, B, SideX, 0))
			{
				return;
			}
			const float SX = static_cast<float>(SideX);
			OutStops.Add(Base + FVector(SX * SideOffset, SideZig, 0.f));
			OutStops.Add(Base + FVector(SX * SideOffset, AlongWide, 0.f));
			OutStops.Add(Base + FVector(SX * SideOffset, -AlongWide, 0.f));
		};

		auto AddYSide = [&](int32 SideY, float SideZig)
		{
			if (!ResidentSideHasStreetNeighbor(City, B, 0, SideY))
			{
				return;
			}
			const float SY = static_cast<float>(SideY);
			OutStops.Add(Base + FVector(SideZig, SY * SideOffset, 0.f));
			OutStops.Add(Base + FVector(AlongWide, SY * SideOffset, 0.f));
			OutStops.Add(Base + FVector(-AlongWide, SY * SideOffset, 0.f));
		};

		AddXSide(1, Zig);
		AddXSide(-1, -Zig);
		AddYSide(1, Zig);
		AddYSide(-1, -Zig);
	}
}

bool ACustomerBase::PickResidentStreetRoamGoal(ACityGenerator* City, int32 RouteLeg, FVector& OutGoal, float& OutSearchXY, float& OutSearchZ) const
{
	OutGoal = FVector::ZeroVector;
	OutSearchXY = 520.f;
	OutSearchZ = 650.f;

	TArray<FVector> StreetStops;
	BuildResidentStreetStops(City, StreetStops);
	if (StreetStops.Num() == 0 || !City)
	{
		return false;
	}

	const FVector Current = GetActorLocation();
	const FVector Center = City->GetCityCenter();
	const float Pitch = FMath::Max(100.f, City->GetPitch());
	const float MinTrip = FMath::Clamp(Pitch * 0.85f, 1100.f, 3200.f);
	const int32 GridR = FMath::Max(1, City->GetGridRadiusClamped());
	const int32 StartIndex = FMath::Abs(static_cast<int32>((static_cast<int64>(RoamRouteSeed) + static_cast<int64>(RouteLeg) * 31) % StreetStops.Num()));
	const int32 AngleSeed = FMath::Abs(static_cast<int32>((static_cast<int64>(RoamRouteSeed) * 13 + static_cast<int64>(RouteLeg) * 71 + static_cast<int64>(ResidentStreetLegsToday) * 29) % 360));
	const float TargetAngle = FMath::DegreesToRadians(static_cast<float>(AngleSeed));
	const FVector TargetDir(FMath::Cos(TargetAngle), FMath::Sin(TargetAngle), 0.f);
	auto BuildDistrictTarget = [&]() -> FVector
	{
		const bool bPreferOuterRing = (RouteLeg % 3) != 1;
		const int32 Salt = FMath::Abs(static_cast<int32>(
			(static_cast<int64>(RoamRouteSeed) * 19
				+ static_cast<int64>(RouteLeg) * 43
				+ static_cast<int64>(ResidentStreetLegsToday) * 17) % 4096));
		int32 GX = 0;
		int32 GY = 0;
		if (bPreferOuterRing)
		{
			const int32 PerSide = GridR * 2 + 1;
			const int32 Perimeter = FMath::Max(1, PerSide * 4 - 4);
			int32 EdgeIndex = Salt % Perimeter;
			if (EdgeIndex < PerSide)
			{
				GX = -GridR + EdgeIndex;
				GY = -GridR;
			}
			else if (EdgeIndex < PerSide * 2 - 1)
			{
				GX = GridR;
				GY = -GridR + (EdgeIndex - PerSide + 1);
			}
			else if (EdgeIndex < PerSide * 3 - 2)
			{
				GX = GridR - (EdgeIndex - (PerSide * 2 - 1) + 1);
				GY = GridR;
			}
			else
			{
				GX = -GridR;
				GY = GridR - (EdgeIndex - (PerSide * 3 - 2) + 1);
			}
		}
		else
		{
			const int32 Width = GridR * 2 + 1;
			int32 Index = Salt % FMath::Max(1, Width * Width - 1);
			GX = -GridR + (Index % Width);
			GY = -GridR + (Index / Width);
			if (GX == 0 && GY == 0)
			{
				GX = (RoamRouteSeed & 1) ? GridR : -GridR;
			}
		}

		return FVector(Center.X + static_cast<float>(GX) * Pitch, Center.Y + static_cast<float>(GY) * Pitch, Current.Z);
	};
	const FVector DistrictTarget = BuildDistrictTarget();
	const float MapRadius = FMath::Max(Pitch, static_cast<float>(GridR) * Pitch * 1.45f);
	const float CenterSoftRadius = FMath::Max(900.f, City->GetMapBlockSize() * 0.9f);
	const int32 CenterCrowd = CountResidentCrowdNear(Center, CenterSoftRadius);

	bool bFound = false;
	FVector BestGoal = FVector::ZeroVector;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bAllowShortTrip = Pass > 0;
		const int32 CandidateCount = FMath::Min(StreetStops.Num(), 72);
		const int32 Step = (CandidateCount < StreetStops.Num()) ? FMath::Max(1, StreetStops.Num() / CandidateCount + 1) : 1;
		const int32 MaxPathChecksPerPass = 10;
		TArray<FVector> TopGoals;
		TArray<float> TopScores;
		TopGoals.Reserve(MaxPathChecksPerPass);
		TopScores.Reserve(MaxPathChecksPerPass);
		for (int32 Offset = 0; Offset < CandidateCount; ++Offset)
		{
			const int32 Index = (StartIndex + Offset * Step) % StreetStops.Num();
			const FVector Candidate = StreetStops[Index];
			const float TravelDist = FVector::Dist2D(Current, Candidate);
			if (!bAllowShortTrip && TravelDist < MinTrip)
			{
				continue;
			}

			const int32 Crowd = CountResidentCrowdNear(Candidate, 420.f);
			if (Crowd >= (bAllowShortTrip ? 7 : 5))
			{
				continue;
			}

			const float CenterDist = FVector::Dist2D(Candidate, Center);
			const bool bCenterCandidate = CenterDist < CenterSoftRadius;
			if (bCenterCandidate && CenterCrowd >= (bAllowShortTrip ? 8 : 6))
			{
				continue;
			}

			const float DistrictDist = FVector::Dist2D(Candidate, DistrictTarget);
			FVector FromCenter = Candidate - Center;
			FromCenter.Z = 0.f;
			float Alignment = 0.f;
			if (FromCenter.Normalize())
			{
				Alignment = FVector::DotProduct(FromCenter, TargetDir);
			}

			const int32 NoiseSeed = FMath::Abs(static_cast<int32>((static_cast<int64>(RoamRouteSeed) + static_cast<int64>(RouteLeg) * 131 + static_cast<int64>(Index) * 977) % 1000));
			const float CenterPenalty = bCenterCandidate ? Pitch * (3.0f + static_cast<float>(CenterCrowd) * 0.55f) : 0.f;
			const float Score = TravelDist * 0.55f
				+ CenterDist * 0.85f
				+ FMath::Max(0.f, MapRadius - DistrictDist) * 1.45f
				+ Alignment * Pitch * 0.85f
				- static_cast<float>(Crowd) * 2100.f
				- CenterPenalty
				+ static_cast<float>(NoiseSeed) * 0.25f;

			int32 InsertAt = 0;
			while (InsertAt < TopScores.Num() && Score <= TopScores[InsertAt])
			{
				++InsertAt;
			}
			if (InsertAt < MaxPathChecksPerPass)
			{
				TopScores.Insert(Score, InsertAt);
				TopGoals.Insert(Candidate, InsertAt);
				if (TopGoals.Num() > MaxPathChecksPerPass)
				{
					TopGoals.RemoveAt(MaxPathChecksPerPass);
					TopScores.RemoveAt(MaxPathChecksPerPass);
				}
			}
		}

		const float MinPathDistance = bAllowShortTrip ? 520.f : MinTrip * 0.65f;
		for (const FVector& CandidateGoal : TopGoals)
		{
			if (HasResidentPath(Current, CandidateGoal, MinPathDistance))
			{
				BestGoal = CandidateGoal;
				bFound = true;
				break;
			}
		}

		if (bFound)
		{
			break;
		}
	}

	if (!bFound)
	{
		return false;
	}

	OutGoal = BestGoal;
	return true;
}

bool ACustomerBase::ForceResidentOutdoorRoamGoal(bool bAllowSnapToStreet)
{
	(void)bAllowSnapToStreet;
	ACityGenerator* City = GetResidentCity(GetWorld());
	UWorld* W = GetWorld();
	UNavigationSystemV1* Nav = W ? UNavigationSystemV1::GetCurrent(W) : nullptr;
	if (!City || !W || !Nav)
	{
		return false;
	}

	const FVector Cur = GetActorLocation();
	if (FVector::Dist2D(Cur, HomeFrontSpot) < 430.f
		&& FVector::Dist2D(Cur, HomeExitSidewalkSpot) >= 520.f
		&& SetResidentRoamGoal(HomeExitSidewalkSpot, 520.f, 500.f))
	{
		return true;
	}

	for (int32 Try = 0; Try < 6; ++Try)
	{
		FVector StreetGoal;
		float SearchXY = 520.f;
		float SearchZ = 650.f;
		if (PickResidentStreetRoamGoal(City, RoamLegIndex + Try, StreetGoal, SearchXY, SearchZ)
			&& SetResidentRoamGoal(StreetGoal, SearchXY, SearchZ))
		{
			RoamLegIndex += Try + 1;
			++ResidentStreetLegsToday;
			return true;
		}
	}

	for (int32 Try = 0; Try < 16; ++Try)
	{
		FNavLocation Candidate;
		if (!Nav->GetRandomReachablePointInRadius(Cur, 3600.f, Candidate))
		{
			continue;
		}
		FVector CandidateGoal = SnapResidentPointToSidewalk(City, Candidate.Location, false);
		FNavLocation SidewalkProjection;
		if (!Nav->ProjectPointToNavigation(CandidateGoal, SidewalkProjection, FVector(650.f, 650.f, 650.f)))
		{
			continue;
		}
		CandidateGoal = SidewalkProjection.Location;
		if (IsResidentOutdoorSidewalkPoint(City, CandidateGoal, false)
			&& HasResidentPath(Cur, CandidateGoal, 700.f)
			&& CountResidentCrowdNear(CandidateGoal, 360.f) < 4
			&& WalkTo(CandidateGoal, 90.f, false, true))
		{
			RoamGoal = CandidateGoal;
			bHasRoamGoal = true;
			bRoamGoalIsPark = false;
			bPendingRoamGoalIsPark = false;
			PendingParkVisitSlot = 0;
			ActiveParkVisitSlot = 0;
			RoamTimer = FMath::Clamp(ComputeResidentRoamTimeout(RoamGoal), 16.f, 95.f);
			ResidentPrevMoveLoc = GetActorLocation();
			bHasResidentPrevMoveLoc = true;
			ResidentStuckTimer = 0.f;
			ResidentRecoveryCooldown = 0.5f;
			ResidentNoGoalTimer = 0.f;
			ResidentGoalFailCount = 0;
			ResidentRecoveryAttempts = 0;
			ResidentBestDistToGoal = FVector::Dist2D(GetActorLocation(), RoamGoal);
			bHasResidentBestDistToGoal = true;
			++RoamLegIndex;
			++ResidentStreetLegsToday;
			return true;
		}
	}

	return false;
}

bool ACustomerBase::RecoverResidentSidewalkDrift(float DeltaSeconds)
{
	if (ParkPauseTimer > 0.f || bAtHomeInside || bEmergingFromHome || bEnteringHome || bApptActive)
	{
		ResidentOffSidewalkTimer = 0.f;
		return false;
	}

	UWorld* W = GetWorld();
	ACityGenerator* City = GetResidentCity(W);
	if (!W || !City)
	{
		ResidentOffSidewalkTimer = 0.f;
		return false;
	}

	const FVector Cur = GetActorLocation();
	if (IsResidentOutdoorSidewalkPoint(City, Cur, true) || IsResidentParkPoint(City, Cur))
	{
		ResidentOffSidewalkTimer = 0.f;
		return false;
	}

	const float Speed2D = GetVelocity().Size2D();
	const float DistToGoal = bHasRoamGoal ? FVector::Dist2D(Cur, RoamGoal) : 0.f;
	const bool bLikelyCrossingStreet = bHasRoamGoal && Speed2D > 55.f && DistToGoal > 260.f;
	ResidentOffSidewalkTimer += DeltaSeconds;
	const float DriftGraceSeconds = bLikelyCrossingStreet ? 7.5f : 1.0f;
	if (ResidentOffSidewalkTimer < DriftGraceSeconds)
	{
		return false;
	}

	UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(W);
	if (!Nav)
	{
		ResidentOffSidewalkTimer = 0.f;
		return false;
	}

	FVector SidewalkGoal = SnapResidentPointToSidewalk(City, Cur, false);
	FNavLocation Projected;
	if (!Nav->ProjectPointToNavigation(SidewalkGoal, Projected, FVector(650.f, 650.f, 650.f))
		|| !IsResidentOutdoorSidewalkPoint(City, Projected.Location, false))
	{
		ResidentOffSidewalkTimer = 0.f;
		return false;
	}

	if (!WalkTo(Projected.Location, 80.f, false, true))
	{
		ResidentOffSidewalkTimer = 0.f;
		return false;
	}

	RoamGoal = Projected.Location;
	bHasRoamGoal = true;
	bRoamGoalIsPark = false;
	bPendingRoamGoalIsPark = false;
	PendingParkVisitSlot = 0;
	ActiveParkVisitSlot = 0;
	RoamTimer = FMath::Clamp(ComputeResidentRoamTimeout(RoamGoal), 8.f, 28.f);
	ResidentStuckTimer = 0.f;
	ResidentRecoveryCooldown = 0.5f;
	ResidentRecoveryAttempts = 0;
	ResidentNoGoalTimer = 0.f;
	ResidentBestDistToGoal = FVector::Dist2D(GetActorLocation(), RoamGoal);
	bHasResidentBestDistToGoal = true;
	bHasResidentPrevMoveLoc = false;
	ResidentOffSidewalkTimer = 0.f;
	return true;
}

void ACustomerBase::RecoverResidentIfStuck(float DeltaSeconds)
{
	if (ParkPauseTimer > 0.f || bAtHomeInside || bApptActive)
	{
		ResidentStuckTimer = 0.f;
		ResidentRecoveryCooldown = 0.f;
		bHasResidentPrevMoveLoc = false;
		bHasResidentBestDistToGoal = false;
		ResidentRecoveryAttempts = 0;
		ResidentNoGoalTimer = 0.f;
		ResidentGoalFailCount = 0;
		return;
	}

	const FVector Cur = GetActorLocation();
	const bool bCloseToGoal = bHasRoamGoal
		&& FVector::Dist2D(Cur, RoamGoal) < 180.f
		&& FMath::Abs(Cur.Z - RoamGoal.Z) < 220.f;
	if (!bHasRoamGoal || bCloseToGoal)
	{
		ResidentStuckTimer = 0.f;
		ResidentRecoveryCooldown = 0.f;
		ResidentPrevMoveLoc = Cur;
		bHasResidentPrevMoveLoc = true;
		bHasResidentBestDistToGoal = false;
		ResidentRecoveryAttempts = 0;
		ResidentNoGoalTimer = 0.f;
		return;
	}

	if (ResidentRecoveryCooldown > 0.f)
	{
		ResidentRecoveryCooldown = FMath::Max(0.f, ResidentRecoveryCooldown - DeltaSeconds);
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

	const float DistToGoal = FVector::Dist2D(Cur, RoamGoal);
	if (!bHasResidentBestDistToGoal || DistToGoal < ResidentBestDistToGoal - 35.f)
	{
		ResidentBestDistToGoal = DistToGoal;
		bHasResidentBestDistToGoal = true;
		ResidentStuckTimer = 0.f;
		ResidentRecoveryAttempts = 0;
		return;
	}

	const float Speed2D = GetVelocity().Size2D();
	const bool bBarelyMoved = MoveDelta < 7.f && Speed2D < 18.f;
	const bool bNoGoalProgress = DistToGoal > ResidentBestDistToGoal - 12.f && MoveDelta < 15.f;
	const bool bMayNeedObstacleSweep = !bPathActuallyMoving || bBarelyMoved || bNoGoalProgress || MoveDelta < 24.f;
	const bool bObstacleAhead = bMayNeedObstacleSweep && HasResidentObstacleAhead(RoamGoal);
	if (!bPathActuallyMoving || bBarelyMoved || bNoGoalProgress || (bObstacleAhead && MoveDelta < 24.f))
	{
		ResidentStuckTimer += DeltaSeconds;
	}
	else
	{
		ResidentStuckTimer = 0.f;
	}

	const float StuckThreshold = bObstacleAhead ? 1.0f : 2.2f;
	if (ResidentStuckTimer < StuckThreshold)
	{
		return;
	}

	++ResidentRecoveryAttempts;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}

	if (ResidentRecoveryAttempts == 1 && WalkTo(RoamGoal, 90.f, false, true))
	{
		ResidentStuckTimer = 0.f;
		ResidentRecoveryCooldown = 0.7f;
		bHasResidentPrevMoveLoc = false;
		return;
	}

	if (TrySetResidentDetourGoal(RoamGoal))
	{
		return;
	}

	if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		ACityGenerator* City = GetResidentCity(GetWorld());
		const bool bRecoveringParkGoal = bRoamGoalIsPark;
		for (int32 Try = 0; Try < 10; ++Try)
		{
			FNavLocation Candidate;
			if (!Nav->GetRandomReachablePointInRadius(Cur, 1350.f, Candidate))
			{
				continue;
			}
			FVector CandidateGoal = Candidate.Location;
			bool bCandidateIsPark = false;
			if (City)
			{
				bCandidateIsPark = bRecoveringParkGoal && IsResidentParkPoint(City, CandidateGoal);
				if (!bCandidateIsPark)
				{
					CandidateGoal = SnapResidentPointToSidewalk(City, CandidateGoal, false);
					FNavLocation SidewalkProjection;
					if (!Nav->ProjectPointToNavigation(CandidateGoal, SidewalkProjection, FVector(650.f, 650.f, 650.f))
						|| !IsResidentOutdoorSidewalkPoint(City, SidewalkProjection.Location, false))
					{
						continue;
					}
					CandidateGoal = SidewalkProjection.Location;
					bCandidateIsPark = bRecoveringParkGoal && IsResidentParkPoint(City, CandidateGoal);
				}
			}
			if (HasResidentPath(Cur, CandidateGoal, 320.f)
				&& (bCandidateIsPark || IsResidentOutdoorSidewalkPoint(City, CandidateGoal, false))
				&& CountResidentCrowdNear(CandidateGoal, 300.f) < 3)
			{
				if (WalkTo(CandidateGoal, 90.f, false, true))
				{
					RoamGoal = CandidateGoal;
					bHasRoamGoal = true;
					bRoamGoalIsPark = bCandidateIsPark;
					bPendingRoamGoalIsPark = false;
					PendingParkVisitSlot = 0;
					ActiveParkVisitSlot = bCandidateIsPark ? ActiveParkVisitSlot : 0;
					RoamTimer = FMath::Clamp(ComputeResidentRoamTimeout(RoamGoal), 12.f, 70.f);
					ResidentPrevMoveLoc = GetActorLocation();
					bHasResidentPrevMoveLoc = true;
					ResidentStuckTimer = 0.f;
					ResidentRecoveryCooldown = 0.8f;
					ResidentBestDistToGoal = FVector::Dist2D(GetActorLocation(), RoamGoal);
					bHasResidentBestDistToGoal = true;
					return;
				}
			}
		}

	}

	bHasRoamGoal = false;
	bRoamGoalIsPark = false;
	bPendingRoamGoalIsPark = false;
	PendingParkVisitSlot = 0;
	ActiveParkVisitSlot = 0;
	ParkPauseTimer = 0.f;
	RoamTimer = 0.f;
	ResidentStuckTimer = 0.f;
	ResidentRecoveryCooldown = 0.f;
	bHasResidentPrevMoveLoc = false;
	bHasResidentBestDistToGoal = false;
	ResidentRecoveryAttempts = 0;
	ResidentNoGoalTimer = 0.f;
}

bool ACustomerBase::SetResidentRoamGoal(const FVector& DesiredGoal, float SearchXY, float SearchZ)
{
	UWorld* W = GetWorld();
	UNavigationSystemV1* Nav = W ? UNavigationSystemV1::GetCurrent(W) : nullptr;
	const bool bGoalIsPark = bPendingRoamGoalIsPark;
	const int32 ParkVisitSlot = PendingParkVisitSlot;
	ACityGenerator* City = GetResidentCity(W);
	const FVector TargetGoal = (!bGoalIsPark && City) ? SnapResidentPointToSidewalk(City, DesiredGoal, false) : DesiredGoal;
	bPendingRoamGoalIsPark = false;
	PendingParkVisitSlot = 0;
	bRoamGoalIsPark = false;
	if (!Nav)
	{
		return false;
	}

	FVector Start = GetActorLocation();
	FNavLocation ProjectedStart;
	if (Nav->ProjectPointToNavigation(Start, ProjectedStart, FVector(420.f, 420.f, 700.f)))
	{
		Start = ProjectedStart.Location + FVector(0.f, 0.f, 3.f);
	}

	FNavLocation Projected;
	const FVector Extent(FMath::Max(80.f, SearchXY), FMath::Max(80.f, SearchXY), FMath::Max(80.f, SearchZ));
	auto HasFullPathTo = [&](const FVector& Goal) -> bool
	{
		if (FVector::Dist2D(Start, Goal) < 520.f)
		{
			return false;
		}
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(W, Start, Goal, this);
		return Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() > 1;
	};

	const bool bProjectedUsable = Nav->ProjectPointToNavigation(TargetGoal, Projected, Extent)
		&& HasFullPathTo(Projected.Location)
		&& (bGoalIsPark ? IsResidentParkPoint(City, Projected.Location) : IsResidentOutdoorSidewalkPoint(City, Projected.Location, false))
		&& (bGoalIsPark || CountResidentCrowdNear(Projected.Location, 280.f) < 4);
	if (!bProjectedUsable)
	{
		if (bGoalIsPark)
		{
			const FVector ParkBase = bHasPark ? ParkCenter : (City ? City->GetCityCenter() : TargetGoal);
			const float ParkWide = City ? City->GetMapBlockSize() * 0.32f : 700.f;
			const float ParkInner = City ? City->GetMapBlockSize() * 0.15f : 320.f;
			const FVector ParkCandidates[] = {
				TargetGoal,
				FVector(ParkBase.X, ParkBase.Y, TargetGoal.Z),
				FVector(ParkBase.X + ParkWide, ParkBase.Y, TargetGoal.Z),
				FVector(ParkBase.X - ParkWide, ParkBase.Y, TargetGoal.Z),
				FVector(ParkBase.X, ParkBase.Y + ParkWide, TargetGoal.Z),
				FVector(ParkBase.X, ParkBase.Y - ParkWide, TargetGoal.Z),
				FVector(ParkBase.X + ParkInner, ParkBase.Y + ParkInner, TargetGoal.Z),
				FVector(ParkBase.X - ParkInner, ParkBase.Y + ParkInner, TargetGoal.Z),
				FVector(ParkBase.X + ParkInner, ParkBase.Y - ParkInner, TargetGoal.Z),
				FVector(ParkBase.X - ParkInner, ParkBase.Y - ParkInner, TargetGoal.Z)
			};

			bool bFoundPark = false;
			FNavLocation BestPark;
			float BestParkScore = TNumericLimits<float>::Max();
			for (const FVector& RawParkGoal : ParkCandidates)
			{
				FNavLocation Candidate;
				if (!Nav->ProjectPointToNavigation(RawParkGoal, Candidate, Extent)
					|| !IsResidentParkPoint(City, Candidate.Location)
					|| !HasFullPathTo(Candidate.Location))
				{
					continue;
				}

				const float Score = FVector::Dist2D(Candidate.Location, TargetGoal)
					+ static_cast<float>(CountResidentCrowdNear(Candidate.Location, 360.f)) * 850.f;
				if (Score < BestParkScore)
				{
					BestParkScore = Score;
					BestPark = Candidate;
					bFoundPark = true;
				}
			}

			if (!bFoundPark)
			{
				return false;
			}
			Projected = BestPark;
		}
		else
		{
			const float DesiredDistance = FVector::Dist2D(Start, TargetGoal);
			const float FallbackRadius = FMath::Clamp(DesiredDistance + 900.f, FMath::Max(3200.f, SearchXY * 3.f), 12000.f);
			bool bFoundFallback = false;
			FNavLocation BestCandidate;
			float BestScore = TNumericLimits<float>::Max();
			for (int32 Try = 0; Try < 18; ++Try)
			{
				FNavLocation Candidate;
				if (!Nav->GetRandomReachablePointInRadius(Start, FallbackRadius, Candidate))
				{
					continue;
				}

				FVector CandidateGoal = Candidate.Location;
				if (!bGoalIsPark && City)
				{
					CandidateGoal = SnapResidentPointToSidewalk(City, CandidateGoal, false);
					FNavLocation SidewalkProjection;
					if (!Nav->ProjectPointToNavigation(CandidateGoal, SidewalkProjection, Extent))
					{
						continue;
					}
					CandidateGoal = SidewalkProjection.Location;
				}

				if (HasFullPathTo(CandidateGoal)
					&& (bGoalIsPark || IsResidentOutdoorSidewalkPoint(City, CandidateGoal, false))
					&& (bGoalIsPark || CountResidentCrowdNear(CandidateGoal, 340.f) < 6))
				{
					const int32 Crowd = CountResidentCrowdNear(CandidateGoal, 320.f);
					const float ShortHopPenalty = FVector::Dist2D(Start, CandidateGoal) < 1200.f ? 16000.f : 0.f;
					const float Score = FVector::Dist2D(CandidateGoal, TargetGoal) + Crowd * 1400.f + ShortHopPenalty;
					if (Score < BestScore)
					{
						BestScore = Score;
						BestCandidate.Location = CandidateGoal;
					}
					bFoundFallback = true;
				}
			}
			if (!bFoundFallback)
			{
				return false;
			}
			Projected = BestCandidate;
		}
	}

	if (!WalkTo(Projected.Location))
	{
		return false;
	}

	RoamGoal = Projected.Location;
	bHasRoamGoal = true;
	bRoamGoalIsPark = bGoalIsPark;
	ActiveParkVisitSlot = bGoalIsPark ? FMath::Clamp(ParkVisitSlot, 1, 2) : 0;
	RoamTimer = ComputeResidentRoamTimeout(RoamGoal);
	ResidentPrevMoveLoc = GetActorLocation();
	bHasResidentPrevMoveLoc = true;
	ResidentStuckTimer = 0.f;
	ResidentRecoveryCooldown = 0.f;
	ResidentOffSidewalkTimer = 0.f;
	ResidentBestDistToGoal = FVector::Dist2D(GetActorLocation(), RoamGoal);
	bHasResidentBestDistToGoal = true;
	ResidentRecoveryAttempts = 0;
	ResidentNoGoalTimer = 0.f;
	ResidentGoalFailCount = 0;
	return true;
}

int32 ACustomerBase::GetResidentParkVisitsToday(int32 Today) const
{
	int32 Visits = 0;
	if (LastMorningParkVisitDay == Today) { ++Visits; }
	if (LastLaterParkVisitDay == Today) { ++Visits; }
	return Visits;
}

float ACustomerBase::ComputeResidentParkVisitHour(int32 Today, int32 VisitSlot) const
{
	const int32 ParkMinuteSeed = FMath::Abs(static_cast<int32>(
		(static_cast<int64>(RoamRouteSeed) * 7
			+ static_cast<int64>(Today) * 41
			+ static_cast<int64>(VisitSlot) * 173) % 100));
	if (VisitSlot <= 1)
	{
		return 8.0f + static_cast<float>(ParkMinuteSeed) * 0.026f; // 08:00 - 10:35
	}
	return 14.0f + static_cast<float>(ParkMinuteSeed) * 0.030f; // 14:00 - 16:58
}

float ACustomerBase::ComputeResidentParkUrgencyHour(ACityGenerator* City, const UDayCycleComponent* DayCycle, int32 Today, int32 VisitSlot) const
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Speed = Move ? FMath::Max(80.f, Move->MaxWalkSpeed) : 135.f;
	const FVector Park = City ? City->GetCityCenter() : ParkCenter;
	const float DistanceToPark = FVector::Dist2D(GetActorLocation(), Park);
	const float DayHours = DayCycle ? FMath::Max(1.f, DayCycle->SunsetHour - DayCycle->SunriseHour) : 14.f;
	const float DaySeconds = DayCycle ? FMath::Max(1.f, DayCycle->DayLengthSeconds) : 1200.f;
	const float TravelClockHours = (DistanceToPark / Speed) * (DayHours / DaySeconds);
	const int32 SpreadSeed = FMath::Abs(static_cast<int32>(
		(static_cast<int64>(RoamRouteSeed) * 11
			+ static_cast<int64>(Today) * 31
			+ static_cast<int64>(VisitSlot) * 97) % 12));
	const float SpreadHours = static_cast<float>(SpreadSeed) * 0.055f;
	if (VisitSlot <= 1)
	{
		return FMath::Clamp(12.45f - TravelClockHours - SpreadHours, 9.75f, 13.15f);
	}
	return FMath::Clamp(18.45f - TravelClockHours - SpreadHours, 15.25f, 18.6f);
}

int32 ACustomerBase::PickResidentParkVisitSlot(ACityGenerator* City, const UDayCycleComponent* DayCycle, int32 Today, float Hour) const
{
	if (Today < 0)
	{
		return 0;
	}

	const bool bNeedsMorning = LastMorningParkVisitDay != Today;
	const bool bNeedsLater = LastLaterParkVisitDay != Today;
	if (!bNeedsMorning && !bNeedsLater)
	{
		return 0;
	}

	if (bNeedsMorning)
	{
		const float MorningHour = ComputeResidentParkVisitHour(Today, 1);
		const float MorningUrgency = ComputeResidentParkUrgencyHour(City, DayCycle, Today, 1);
		if (Hour >= MorningHour || Hour >= MorningUrgency)
		{
			return 1;
		}
	}

	if (bNeedsLater)
	{
		const float LaterHour = ComputeResidentParkVisitHour(Today, 2);
		const float LaterUrgency = ComputeResidentParkUrgencyHour(City, DayCycle, Today, 2);
		if (Hour >= LaterHour || Hour >= LaterUrgency)
		{
			return 2;
		}
	}

	return 0;
}

bool ACustomerBase::PickResidentRoamGoal(FVector& OutGoal, float& OutSearchXY, float& OutSearchZ)
{
	OutGoal = FVector::ZeroVector;
	OutSearchXY = 700.f;
	OutSearchZ = 500.f;
	bPendingRoamGoalIsPark = false;
	PendingParkVisitSlot = 0;

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
		if (FVector::Dist2D(GetActorLocation(), HomeExitSidewalkSpot) >= 520.f)
		{
			OutGoal = HomeExitSidewalkSpot;
			OutSearchXY = 520.f;
			OutSearchZ = 500.f;
			return true;
		}
	}

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr;
	const int32 Today = DC ? DC->GetDayNumber() : 1;
	const float Hour = DC ? DC->GetClockHour() : 12.f;
	if (ResidentRouteDay != Today)
	{
		ResidentRouteDay = Today;
		ResidentStreetLegsToday = 0;
		ParkLegCountdown = 1 + FMath::Abs(static_cast<int32>((static_cast<int64>(RoamRouteSeed) + static_cast<int64>(Today) * 17) % 4));
	}

	const int32 ParkVisitSlot = PickResidentParkVisitSlot(City, DC, Today, Hour);
	const bool bParkWindowOpen = ParkVisitSlot > 0 && ResidentStreetLegsToday >= ParkLegCountdown;
	const bool bParkOverdue = ParkVisitSlot > 0 && Hour >= ComputeResidentParkUrgencyHour(City, DC, Today, ParkVisitSlot);
	if (ParkVisitSlot > 0 && (bParkWindowOpen || bParkOverdue))
	{
		const float ParkCrowdRadius = FMath::Max(800.f, City->GetMapBlockSize() * 0.82f);
		const int32 ParkCrowd = CountResidentParkVisitors(ParkCrowdRadius);
		const int32 CenterCrowd = CountResidentCrowdNear(ParkCenter, ParkCrowdRadius);
		const int32 ParkSoftLimit = 3;   // strakker: hou het park/centrum dun, NPC's blijven verspreid
		const int32 CenterSoftLimit = 4;
		const bool bParkCrowded = !bParkOverdue && (ParkCrowd >= ParkSoftLimit || CenterCrowd >= CenterSoftLimit);
		if (!bParkCrowded)
		{
			const float D = City->GetMapBlockSize() * 0.32f;
			const float Inner = City->GetMapBlockSize() * 0.15f;
			const FVector ParkStops[] = {
				FVector(0.f, 0.f, 0.f),
				FVector( D, 0.f, 0.f),
				FVector(-D, 0.f, 0.f),
				FVector(0.f,  D, 0.f),
				FVector(0.f, -D, 0.f),
				FVector( Inner,  Inner, 0.f),
				FVector(-Inner,  Inner, 0.f),
				FVector( Inner, -Inner, 0.f),
				FVector(-Inner, -Inner, 0.f)
			};
			const int32 ParkStopCount = UE_ARRAY_COUNT(ParkStops);
			const int32 Pick = FMath::Abs(static_cast<int32>((static_cast<int64>(RoamRouteSeed) + static_cast<int64>(Today) * 53 + static_cast<int64>(RoamLegIndex) * 3) % ParkStopCount));
			OutGoal = FVector(ParkCenter.X, ParkCenter.Y, HomeFrontSpot.Z) + ParkStops[Pick];
			OutSearchXY = 760.f;
			OutSearchZ = 500.f;
			bPendingRoamGoalIsPark = true;
			PendingParkVisitSlot = ParkVisitSlot;
			++RoamLegIndex;
			return true;
		}
	}

	if (PickResidentStreetRoamGoal(City, RoamLegIndex, OutGoal, OutSearchXY, OutSearchZ))
	{
		++RoamLegIndex;
		++ResidentStreetLegsToday;
		return true;
	}

	return false;
}

void ACustomerBase::TickResident(float DeltaSeconds)
{
	UWorld* W = GetWorld();
	const AWeedShopGameState* GS = W ? W->GetGameState<AWeedShopGameState>() : nullptr;
	const UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr;
	const float Hour = DC ? DC->GetClockHour() : 12.f;
	const int32 Today = DC ? DC->GetDayNumber() : ResidentRouteDay;
	const bool bNight = DC ? DC->IsNight() : (Hour >= 20.f || Hour < 6.f);

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
				StartResidentHomeExit(true);
			}
			if (TickResidentHomeExit(DeltaSeconds))
			{
				return;
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
				SetActorLocation(MakeResidentStandingLocation(HomeInteriorPos));
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
		if (bAtHomeInside)
		{
			ResidentWakeDelay = -1.f;
			return;
		}
		if (!bEnteringHome)
		{
			StartResidentHomeEntry();
		}
		TickResidentHomeEntry(DeltaSeconds);
		return;
	}
	if (bEnteringHome)
	{
		bEnteringHome = false;
		HomeEntryStuckTimer = 0.f;
		bHasResidentPrevMoveLoc = false;
	}

	// Dag: verschijn weer bij de voordeur als 'ie binnen was.
	if (bAtHomeInside)
	{
		if (ResidentWakeDelay < 0.f)
		{
			ResidentWakeDelay = ComputeResidentGoalThinkDelay(0.3f, 18.f);
		}
		ResidentWakeDelay = FMath::Max(0.f, ResidentWakeDelay - DeltaSeconds);
		if (ResidentWakeDelay > 0.f)
		{
			return;
		}
		StartResidentHomeExit(true);
	}
	if (TickResidentHomeExit(DeltaSeconds))
	{
		return;
	}

	if (Today >= 0 && bHasRoamGoal && !bRoamGoalIsPark && GetResidentParkVisitsToday(Today) < 2)
	{
		if (ACityGenerator* City = GetResidentCity(W))
		{
			const int32 ParkVisitSlot = PickResidentParkVisitSlot(City, DC, Today, Hour);
			if (ParkVisitSlot > 0 && Hour >= ComputeResidentParkUrgencyHour(City, DC, Today, ParkVisitSlot))
			{
				if (AAIController* AI = Cast<AAIController>(GetController()))
				{
					AI->StopMovement();
				}
				bHasRoamGoal = false;
				bPendingRoamGoalIsPark = false;
				PendingParkVisitSlot = 0;
				ActiveParkVisitSlot = 0;
				RoamTimer = 0.f;
				ResidentStuckTimer = 0.f;
				ResidentRecoveryCooldown = 0.f;
				bHasResidentBestDistToGoal = false;
				ResidentRecoveryAttempts = 0;
			}
		}
	}

	// Roam: vaste grote stadsronde buiten. Binnenroutes zijn alleen voor echt naar huis/naar buiten gaan.
	if (RecoverResidentSidewalkDrift(DeltaSeconds))
	{
		return;
	}
	RecoverResidentIfStuck(DeltaSeconds);
	if (ParkPauseTimer > 0.f)
	{
		ParkPauseTimer -= DeltaSeconds;
		if (ParkPauseTimer <= 0.f)
		{
			ParkPauseTimer = 0.f;
			bHasRoamGoal = false;
			RoamTimer = 0.f;
			ResidentStuckTimer = 0.f;
			ResidentRecoveryCooldown = 0.f;
			bHasResidentBestDistToGoal = false;
			ResidentRecoveryAttempts = 0;
			ResidentNoGoalTimer = 0.f;
		}
		return;
	}

	if (!bHasRoamGoal)
	{
		ResidentNoGoalTimer += DeltaSeconds;
	}

	RoamTimer -= DeltaSeconds;
	const bool bArrived = bHasRoamGoal
		&& FVector::Dist2D(GetActorLocation(), RoamGoal) < 145.f
		&& FMath::Abs(GetActorLocation().Z - RoamGoal.Z) < 180.f;
	if (bArrived)
	{
		if (bRoamGoalIsPark)
		{
			if (IsResidentParkPoint(GetResidentCity(W), GetActorLocation())
				|| IsResidentParkPoint(GetResidentCity(W), RoamGoal))
			{
				const int32 VisitDay = DC ? DC->GetDayNumber() : ResidentRouteDay;
				LastParkVisitDay = VisitDay;
				if (ActiveParkVisitSlot <= 1)
				{
					LastMorningParkVisitDay = VisitDay;
				}
				else
				{
					LastLaterParkVisitDay = VisitDay;
				}
			}
			bHasRoamGoal = false;
			bRoamGoalIsPark = false;
			ActiveParkVisitSlot = 0;
			ParkPauseTimer = ComputeResidentGoalThinkDelay(6.f, 16.f);
			RoamTimer = 0.f;
			ResidentStuckTimer = 0.f;
			ResidentRecoveryCooldown = 0.f;
			bHasResidentBestDistToGoal = false;
			ResidentRecoveryAttempts = 0;
		}
		else
		{
			bHasRoamGoal = false;
			bRoamGoalIsPark = false;
			ActiveParkVisitSlot = 0;
			RoamTimer = ComputeResidentGoalThinkDelay(0.5f, 2.8f);
			ResidentStuckTimer = 0.f;
			ResidentRecoveryCooldown = 0.f;
			bHasResidentBestDistToGoal = false;
			ResidentRecoveryAttempts = 0;
		}
	}

	if (!bHasRoamGoal && RoamTimer > 0.f)
	{
		return;
	}

	if (!bHasRoamGoal || RoamTimer <= 0.f)
	{
		FVector DesiredGoal;
		float SearchXY = 700.f;
		float SearchZ = 500.f;
		const bool bPickedGoal = PickResidentRoamGoal(DesiredGoal, SearchXY, SearchZ);
		const bool bPickedParkGoal = bPendingRoamGoalIsPark;
		if (bPickedGoal && SetResidentRoamGoal(DesiredGoal, SearchXY, SearchZ))
		{
			return;
		}
		if (bPickedParkGoal)
		{
			++ResidentGoalFailCount;
			if (ResidentNoGoalTimer >= 14.f || ResidentGoalFailCount >= 5)
			{
				if (AAIController* AI = Cast<AAIController>(GetController()))
				{
					AI->StopMovement();
				}
				bAtHomeInside = true;
				SetActorHiddenInGame(true);
				SetActorEnableCollision(false);
				SetActorLocation(HomeInteriorPos + FVector(0.f, 0.f, 4.f));
				StartResidentHomeExit(true);
				return;
			}
			bHasRoamGoal = false;
			bRoamGoalIsPark = false;
			RoamTimer = ComputeResidentGoalThinkDelay(1.2f, 3.2f);
			ResidentStuckTimer = 0.f;
			ResidentRecoveryCooldown = 0.f;
			bHasResidentBestDistToGoal = false;
			ResidentRecoveryAttempts = 0;
			return;
		}

		// Fallback: kies een verre, echt bereikbare roam-bestemming. Nooit "doel" vlak naast de NPC.
		const FVector Self = GetActorLocation();
		const float Angle = FMath::DegreesToRadians(static_cast<float>(FMath::Abs(RoamRouteSeed + RoamLegIndex * 37) % 360));
		const FVector SeedGoal = Self + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 2600.f;
		if (SetResidentRoamGoal(SeedGoal, 2200.f, 700.f))
		{
			++RoamLegIndex;
			++ResidentStreetLegsToday;
			return;
		}
		++ResidentGoalFailCount;
		if (ForceResidentOutdoorRoamGoal(ResidentGoalFailCount >= 2))
		{
			return;
		}
		if (ResidentNoGoalTimer >= 14.f || ResidentGoalFailCount >= 5)
		{
			if (AAIController* AI = Cast<AAIController>(GetController()))
			{
				AI->StopMovement();
			}
			bAtHomeInside = true;
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			SetActorLocation(HomeInteriorPos + FVector(0.f, 0.f, 4.f));
			StartResidentHomeExit(true);
			return;
		}
		bHasRoamGoal = false;
		bRoamGoalIsPark = false;
		RoamTimer = ComputeResidentGoalThinkDelay(2.4f, 6.0f);
		ResidentStuckTimer = 0.f;
		ResidentRecoveryCooldown = 0.f;
		bHasResidentBestDistToGoal = false;
		ResidentRecoveryAttempts = 0;
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
