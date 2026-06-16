#include "Customer/CustomerBase.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "CollisionQueryParams.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "World/DayCycleComponent.h"
#include "World/CityGenerator.h"
#include "World/StoreCounter.h"
#include "Customer/CustomerSpawner.h"
#include "EngineUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/DataTable.h"
#include "Data/WeedShopProduct.h"
#include "Economy/EconomyComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Game/WeedShopGameState.h"
#include "Phone/ContactsComponent.h"
#include "Npc/NpcRegistryComponent.h"
#include "Save/SaveGameSubsystem.h"
#include "Progression/LevelComponent.h"
#include "Progression/GoalsComponent.h"
#include "Progression/StoreComponent.h"
#include "World/HeatComponent.h"
#include "World/CityDoor.h" // FriendlyNpcName fallback
#include "Phone/ContactsComponent.h" // afspraak-statusberichten
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
		// NPC-OPTIMALISATIE (alle NPC's standaard goedkoop, niet alleen spawner-walkers):
		//  - OnlyTickPoseWhenRendered: buiten beeld animeert de NPC niet (beweegt nog wel). Scheelt
		//    enorm bij een grote menigte - de meeste NPC's zijn op een willekeurig moment niet in beeld.
		//  - URO aan: NPC's die WEL in beeld zijn maar klein/ver updaten hun animatie minder vaak
		//    (frame-skip op scherm-grootte). Dit is precies wat een "anim-budget"-tool onder de motorkap doet.
		MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		MeshComp->bEnableUpdateRateOptimizations = true;
		MeshComp->SetCastShadow(false);
		//  - Max render-afstand: heel verre NPC's worden niet getekend (skinned mesh = duur), maar ruim genoeg
		//    dat wat je op de kaart ziet ook in de wereld zichtbaar is (de stad is ~150m). Schaalt mee met de
		//    graphics-tier via r.ViewDistanceScale (lagere tier cullt dichterbij = meer FPS).
		MeshComp->SetCullDistance(16000.f);
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
		// Zachtere rotatie: draai niet razendsnel mee met de (oscillerende) RVO-velocity -> geen tollen op de plek.
		Move->RotationRate = FRotator(0.f, 280.f, 0.f);
		Move->MaxWalkSpeed = 200.f;
		Move->bUseRVOAvoidance = true;             // ontwijk elkaar/de speler -> niet vastlopen
		// Kleinere consideration-radius: ze ontwijken elkaar pas als ze ECHT dichtbij zijn i.p.v. al van ver om
		// elkaar heen te dansen (dat veroorzaakte het van-plek-wisselen + samen rondjes draaien).
		Move->AvoidanceConsiderationRadius = 90.f;
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
	if (bNpcUseAbp || !bNpcAnimStarted) { return; } // AnimBP stuurt de locomotie zelf aan
	if (ActivityAnimIndex >= 0) { return; }          // activity-NPC speelt z'n vaste pose -> niet overschrijven
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

void ACustomerBase::ForceIdleAnimNow()
{
	NpcMoveHold = 0.f;
	if (NpcAnimState != 0)
	{
		NpcAnimState = 0;
		if (USkeletalMeshComponent* M = GetMesh())
		{
			if (NpcIdle) { M->PlayAnimation(NpcIdle, true); }
		}
	}
}

// --- ACTIVITY-NPC: catalog + gedrag (dev-tool: vaste plek + anim op een tijdvak) ---
namespace
{
	struct FActivityAnim { const TCHAR* Path; const TCHAR* Label; };
	// Append-only: nieuwe entries onderaan toevoegen zodat opgeslagen spot-indices stabiel blijven.
	static const FActivityAnim GActivityAnims[] = {
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Idle_Wall_1.Anim_Idle_Wall_1"),               TEXT("Lean against wall") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Idle_Hands_Crossed.Anim_Idle_Hands_Crossed"), TEXT("Arms crossed") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Idle_Hands_On_Waist.Anim_Idle_Hands_On_Waist"),TEXT("Hands on waist") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Idle_Lean_Forward_1.Anim_Idle_Lean_Forward_1"),TEXT("Lean forward") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Phone_Talking_Big.Anim_Phone_Talking_Big"),    TEXT("Phone call") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Check_Cellphone.Anim_Check_Cellphone"),        TEXT("Check phone") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Music_From_Phone.Anim_Music_From_Phone"),      TEXT("Music on phone") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Dance_1.Anim_Dance_1"),                        TEXT("Dance 1") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Dance_2.Anim_Dance_2"),                        TEXT("Dance 2") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Call_Taxi_1.Anim_Call_Taxi_1"),               TEXT("Hail taxi") },
		{ TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Sit_Floor_1.Anim_Sit_Floor_1"),               TEXT("Sit on floor") },
	};
}

int32 ACustomerBase::ActivityAnimNum() { return (int32)UE_ARRAY_COUNT(GActivityAnims); }
const TCHAR* ACustomerBase::ActivityAnimPath(int32 Idx)
{
	return (Idx >= 0 && Idx < ActivityAnimNum()) ? GActivityAnims[Idx].Path : nullptr;
}
FString ACustomerBase::ActivityAnimLabel(int32 Idx)
{
	return (Idx >= 0 && Idx < ActivityAnimNum()) ? FString(GActivityAnims[Idx].Label) : TEXT("?");
}
UAnimSequence* ACustomerBase::ResolveActivityAnim(int32 Idx)
{
	const TCHAR* P = ActivityAnimPath(Idx);
	return P ? LoadObject<UAnimSequence>(nullptr, P) : nullptr;
}

void ACustomerBase::BeginActivity(const FVector& Spot, float Yaw, int32 AnimIdx)
{
	bActivityNpc = true;
	ActivitySpot = Spot;
	ActivityYaw = Yaw;
	ActivityPendingAnim = FMath::Clamp(AnimIdx, 0, FMath::Max(0, ActivityAnimNum() - 1));
	bActivityArrived = false;
	ActivityAnimIndex = -1; // tijdens de aanloop nog gewoon walk/idle
}

void ACustomerBase::SetActivityAnimNow(int32 AnimIdx)
{
	ActivityPendingAnim = FMath::Clamp(AnimIdx, 0, FMath::Max(0, ActivityAnimNum() - 1));
	if (bActivityArrived)
	{
		ActivityAnimIndex = ActivityPendingAnim; // gerepliceerd -> OnRep speelt 'm op clients
		ApplyActivityAnim();                      // host: direct
	}
}

void ACustomerBase::ApplyActivityAnim()
{
	if (ActivityAnimIndex < 0) { return; }
	if (USkeletalMeshComponent* M = GetMesh())
	{
		if (UAnimSequence* Seq = ResolveActivityAnim(ActivityAnimIndex))
		{
			M->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			M->PlayAnimation(Seq, true);
			NpcAnimState = 2; // niet 0/1 -> UpdateNpcAnim laat 'm met rust
		}
	}
}

void ACustomerBase::OnRep_ActivityAnim()
{
	if (ActivityAnimIndex >= 0) { ApplyActivityAnim(); }
}

void ACustomerBase::TickActivity(float DeltaSeconds)
{
	// Loop naar de plek; eenmaal aangekomen: stoppen, naar de kijkrichting draaien en de anim spelen.
	const float Dist = FVector::Dist2D(GetActorLocation(), ActivitySpot);
	if (!bActivityArrived && Dist > 110.f)
	{
		WalkTo(ActivitySpot, 80.f);
		return;
	}
	bActivityArrived = true;
	if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
	// Netjes naar de kijkrichting draaien (vloeiend, geen snap).
	const FRotator Cur = GetActorRotation();
	const FRotator Want(0.f, ActivityYaw, 0.f);
	SetActorRotation(FMath::RInterpConstantTo(Cur, Want, DeltaSeconds, 220.f));
	// Pose aanzetten (1x): zet de gerepliceerde index -> OnRep speelt 'm op clients, host speelt direct.
	if (ActivityAnimIndex != ActivityPendingAnim)
	{
		ActivityAnimIndex = ActivityPendingAnim;
		ApplyActivityAnim();
	}
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

// Globale NPC-skin-pool (NIET de standaard Manny/Quinn - die zijn voor de speler). De registry kiest de
// index 1x en BEWAART 'm in de persistente NPC-state -> dezelfde persoon ziet er altijd hetzelfde uit
// (ook na tier-stijging, respawn of save/load).
//  0-2   Karl A/B/C       (Citizens, casual man)
//  3-5   SK_Casual 1/2/3  (Casual-pack vrouw, GEKLEED - was de naakte FullBody, nu vervangen)
//  6-9   Tony A/B/C/D     (Citizens, net gekleed - high/whale-flavor)
//  10-12 SK_Casual 1/2/3  (idem als 3-5, voor oude opgeslagen indices)
// Alle skins zijn GEKLEED en SK_Mannequin-compatibel, dus de single-node walk/idle-anims blijven werken.
static USkeletalMesh* WeedNpc_SkinByIndex(int32 Idx)
{
	static const TCHAR* Pool[] = {
		TEXT("/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Karl_A.SK_Citizens_Pack_Karl_A"),
		TEXT("/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Karl_B.SK_Citizens_Pack_Karl_B"),
		TEXT("/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Karl_C.SK_Citizens_Pack_Karl_C"),
		// 3-5: WAS de FullBody-Casual, maar die zijn NAAKT (alleen body+head, geen kleding) -> vervangen door
		// de geklede Casual-skins zodat bestaande NPC's met index 3-5 ook gekleed worden (geen re-roll nodig).
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Casual_1/SK_Casual_1.SK_Casual_1"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Casual_2/SK_Casual_2.SK_Casual_2"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Casual_3/SK_Casual_3.SK_Casual_3"),
		TEXT("/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Tony_A.SK_Citizens_Pack_Tony_A"),
		TEXT("/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Tony_B.SK_Citizens_Pack_Tony_B"),
		TEXT("/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Tony_C.SK_Citizens_Pack_Tony_C"),
		TEXT("/Game/Citizens_Pack/Meshes/SK_Citizens_Pack_Tony_D.SK_Citizens_Pack_Tony_D"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Casual_1/SK_Casual_1.SK_Casual_1"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Casual_2/SK_Casual_2.SK_Casual_2"),
		TEXT("/Game/Casual_Wear_Pack1/Mesh/Casual_3/SK_Casual_3.SK_Casual_3"),
	};
	const int32 N = UE_ARRAY_COUNT(Pool);
	return LoadObject<USkeletalMesh>(nullptr, Pool[FMath::Clamp(Idx, 0, N - 1)]);
}

// Gedempte, deterministische kleding-kleur uit een hash (realistische tinten: niet te fel/donker).
static FLinearColor WeedNpc_ClothColor(uint32 H)
{
	H = H * 2654435761u + 40503u;
	const uint8 Hue = (uint8)(H % 256u);
	const uint8 Sat = (uint8)(60 + ((H >> 8) % 95u));   // ~0.23..0.60 verzadiging (gedempt)
	const uint8 Val = (uint8)(80 + ((H >> 16) % 110u)); // ~0.31..0.75 helderheid
	return FLinearColor::MakeFromHSV8(Hue, Sat, Val);
}

// Forward-decl: tint de kleding-slots / het haar van een (deel-)mesh-component.
static void WeedNpc_TintClothing(USkeletalMeshComponent* SkM, uint32 Seed);
static void WeedNpc_TintHair(USkeletalMeshComponent* SkM, uint32 Seed);

// STABIELE seed uit de NpcId-string (FNV-1a). GetTypeHash(FName) is NIET stabiel tussen sessies (hangt af
// van de naam-tabel-volgorde) -> daarmee zou een NPC bij elke load andere kleren/kleur krijgen. Deze hash
// hangt alleen van de tekst af, dus dezelfde persoon ziet er ALTIJD hetzelfde uit (herkenbaar).
static uint32 WeedNpc_StableSeed(FName NpcId)
{
	const FString S = NpcId.ToString();
	uint32 H = 2166136261u;
	for (const TCHAR Ch : S) { H = (H ^ (uint32)Ch) * 16777619u; }
	return H ? H : 1u;
}

// MODULAIRE Casual-NPC: bouw een persoon uit losse kledingstukken (Casual_Wear_Pack1) -> elke NPC krijgt
// een eigen combinatie top/broek/schoenen/kapsel/hoofd. De basis-body draait de anim; de kledingstukken
// volgen via SetLeaderPoseComponent (zelfde UE4_Mannequin_Skeleton_Main). Plus per-part random kleur.
// Geeft enorme variatie (3^5 combo's x kleur) zonder nieuwe assets. Iets duurder (meer componenten) dus
// alleen voor de Casual-fractie; Karl/Tony blijven 1 losse mesh.
static void WeedNpc_BuildModular(AActor* Owner, USkeletalMeshComponent* Body, uint32 Seed)
{
	if (!Owner || !Body) { return; }
	static const TCHAR* C = TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/");
	// Naakte basis-body op de hoofd-mesh (wordt bedekt door de kledingstukken).
	if (USkeletalMesh* B = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Casual_Wear_Pack1/Mesh/Parts/Bodys/SK_Body.SK_Body")))
	{
		Body->SetSkeletalMesh(B);
	}
	auto Pick = [&](const TArray<FString>& Opts, uint32 Salt) -> FString
	{
		if (Opts.Num() == 0) { return FString(); }
		return Opts[(Seed * 131u + Salt) % (uint32)Opts.Num()];
	};
	// Slots: pad-lijsten (relatief aan C). Salt per slot zodat keuzes onafhankelijk variëren.
	struct FSlot { TArray<FString> Opts; uint32 Salt; bool bCloth; };
	const TArray<FSlot> Slots = {
		{ { TEXT("Heads/SK_Head_Casual_1"), TEXT("Heads/SK_Head_Casual_2"), TEXT("Heads/SK_Head_Casual_3") }, 11u, false },
		{ { TEXT("Cloth/Torso/SK_Top_1"), TEXT("Cloth/Torso/SK_Top_2"), TEXT("Cloth/Torso/SK_Hoodies_Mini"),
		    TEXT("Cloth/Torso/SK_Top_1_Optimized_Outerwear"), TEXT("Cloth/Torso/SK_Top_2_Optimized_Outerwear"),
		    TEXT("Cloth/Torso/SK_Top_1_Optimized_Shirt") }, 23u, true },
		{ { TEXT("Cloth/Legs/SK_Baggy_Jeans"), TEXT("Cloth/Legs/SK_Wide_Leg_Jeans"), TEXT("Cloth/Legs/SK_Shorts_1") }, 41u, true },
		{ { TEXT("Cloth/Shoes/SK_Sneakers_2"), TEXT("Cloth/Shoes/SK_Sneakers_4"), TEXT("Cloth/Shoes/SK_Sneakers_5") }, 67u, true },
		// Haar + hoofddeksels (kaal/pet/hoed/panama) -> veel kapsel- en hoed-variatie.
		{ { TEXT("Hairs/SK_HairShort"), TEXT("Hairs/SK_Hairshort_Cap"), TEXT("Hairs/SK_Hairshort_Hat"), TEXT("Hairs/SK_Hairshort_Panama"),
		    TEXT("Hairs/SK_Hair_Braid"),
		    TEXT("Hairs/SK_Hair_Medium_1"), TEXT("Hairs/SK_Hair_Medium_1_Cap"), TEXT("Hairs/SK_Hair_Medium_1_Hat"), TEXT("Hairs/SK_Hair_Medium_1_Panama"),
		    TEXT("Hairs/SK_Hair_Medium_2_Cap"), TEXT("Hairs/SK_Hair_Medium_2_Hat"), TEXT("Hairs/SK_Hair_Medium_2_Panama") }, 89u, false },
		// Optionele accessoire: meestal niks, soms koptelefoon (lege string = sla over).
		{ { TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("Cloth/Accessories/Head_Accessories/SK_Headphones") }, 113u, false },
	};
	for (const FSlot& Sl : Slots)
	{
		const FString Rel = Pick(Sl.Opts, Sl.Salt);
		if (Rel.IsEmpty()) { continue; }
		const FString Path = FString(C) + Rel + TEXT(".") + FPaths::GetCleanFilename(Rel);
		USkeletalMesh* PM = LoadObject<USkeletalMesh>(nullptr, *Path);
		if (!PM) { continue; }
		USkeletalMeshComponent* Part = NewObject<USkeletalMeshComponent>(Owner);
		Part->SetupAttachment(Body);
		Part->RegisterComponent();
		Part->SetSkeletalMesh(PM);
		Part->SetLeaderPoseComponent(Body); // volgt de pose van de basis-body -> animeert mee, geen eigen eval
		Part->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		Part->bEnableUpdateRateOptimizations = true;
		Part->SetCastShadow(false);
		Part->SetCullDistance(16000.f);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Sl.bCloth) { WeedNpc_TintClothing(Part, Seed + Sl.Salt); }      // top/broek/schoenen: random kleur
		else           { WeedNpc_TintHair(Part, Seed + Sl.Salt); }          // haar: random natuurlijke haarkleur
	}
}

// MODULAIRE Citizens-NPC (Tony-parts): zelfde idee als WeedNpc_BuildModular maar met de Citizens-pack
// (ander skelet SKEL_Citizens_Pack_Skeleton). Basis = Tony_Body (naakte body met hoofd/ogen/mond); daar
// overheen losse garments (Tshirt/Shirt top, Pants/Shorts broek, Shoes/Sneakers, pet/hoed/panama, bril,
// horloge) als follower-pose componenten. Kleur via Tone_A..F. ~192 combo's x kleur i.p.v. 4 vaste Tony's.
// Karl heeft maar 1 part-set (geen winst) dus die blijft een vaste mesh; dit is alleen voor de Tony-band.
static void WeedNpc_BuildModularCitizens(AActor* Owner, USkeletalMeshComponent* Body, uint32 Seed)
{
	if (!Owner || !Body) { return; }
	static const TCHAR* C = TEXT("/Game/Citizens_Pack/Meshes/Citizens_Pack_Parts_Tony/SK_Citizens_Pack_Tony_");

	auto Load = [&](const TCHAR* Rel) -> USkeletalMesh*
	{
		const FString Path = FString(C) + Rel + TEXT(".SK_Citizens_Pack_Tony_") + Rel;
		return LoadObject<USkeletalMesh>(nullptr, *Path);
	};
	auto AddPart = [&](USkeletalMesh* PM, uint32 Salt)
	{
		if (!PM) { return; }
		USkeletalMeshComponent* Part = NewObject<USkeletalMeshComponent>(Owner);
		Part->SetupAttachment(Body);
		Part->RegisterComponent();
		Part->SetSkeletalMesh(PM);
		Part->SetLeaderPoseComponent(Body); // volgt de pose van de basis-body (zelfde Citizens-skelet)
		Part->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		Part->bEnableUpdateRateOptimizations = true;
		Part->SetCastShadow(false);
		Part->SetCullDistance(16000.f);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeedNpc_TintClothing(Part, Seed + Salt); // Set1/Set2-slots krijgen random kleur (Tone_A..F)
	};
	auto Pick = [&](uint32 N, uint32 Salt) -> uint32 { return (Seed * 137u + Salt) % N; };

	// Torso-stijl: 0=naakte body + Tshirt, 1=naakte body + Shirt, 2=Body_Cloth (shirt in de body gebakken).
	const uint32 Torso = Pick(3u, 5u);
	USkeletalMesh* BaseBody = (Torso == 2u) ? Load(TEXT("Body_Cloth")) : Load(TEXT("Body"));
	if (!BaseBody) { BaseBody = Load(TEXT("Body")); }
	if (BaseBody) { Body->SetSkeletalMesh(BaseBody); }
	WeedNpc_TintClothing(Body, Seed); // tint een eventueel ingebakken shirt (Body_Cloth) mee
	if (Torso == 0u) { AddPart(Load(TEXT("Tshirt")), 11u); }
	else if (Torso == 1u) { AddPart(Load(TEXT("Shirt")), 11u); }

	// Broek: Pants of Shorts.
	AddPart((Pick(2u, 23u) == 0u) ? Load(TEXT("Pants")) : Load(TEXT("Shorts")), 23u);
	// Schoenen: Shoes of Sneakers.
	AddPart((Pick(2u, 41u) == 0u) ? Load(TEXT("Shoes")) : Load(TEXT("Sneakers")), 41u);
	// Hoofddeksel: vaak niks (Tony is kaal), soms pet/hoed/panama.
	switch (Pick(5u, 67u))
	{
		case 1u: AddPart(Load(TEXT("Cap")), 67u); break;
		case 2u: AddPart(Load(TEXT("Hat")), 67u); break;
		case 3u: AddPart(Load(TEXT("Panama")), 67u); break;
		default: break; // 0,4 -> kaal
	}
	// Bril: ~1/3.
	if (Pick(3u, 89u) == 0u) { AddPart(Load(TEXT("Glasses")), 89u); }
	// Horloge: ~1/2.
	if (Pick(2u, 113u) == 0u) { AddPart(Load(TEXT("Watch")), 113u); }
}

// Natuurlijke haarkleur uit een hash (zwart/bruin/blond/auburn/grijs/wit).
static FLinearColor WeedNpc_HairColor(uint32 H)
{
	H = H * 2654435761u + 7u;
	static const FLinearColor Pal[] = {
		FLinearColor(0.02f, 0.02f, 0.02f), FLinearColor(0.05f, 0.035f, 0.025f),  // zwart, donkerbruin
		FLinearColor(0.13f, 0.08f, 0.045f), FLinearColor(0.27f, 0.17f, 0.085f),  // bruin, lichtbruin
		FLinearColor(0.55f, 0.42f, 0.20f), FLinearColor(0.34f, 0.11f, 0.05f),    // blond, auburn
		FLinearColor(0.45f, 0.45f, 0.47f), FLinearColor(0.80f, 0.80f, 0.82f),    // grijs, wit
	};
	return Pal[H % (uint32)UE_ARRAY_COUNT(Pal)];
}

// Random natuurlijke haarkleur op de haar-materiaalslots (M_Hair: RootColor/TipColor/DyeColor).
static void WeedNpc_TintHair(USkeletalMeshComponent* SkM, uint32 Seed)
{
	if (!SkM) { return; }
	const TArray<UMaterialInterface*> Mats = SkM->GetMaterials();
	for (int32 i = 0; i < Mats.Num(); ++i)
	{
		UMaterialInterface* MI = Mats[i];
		if (!MI) { continue; }
		const FString Nm = MI->GetName();
		if (!Nm.Contains(TEXT("Hair")) || Nm.Contains(TEXT("Pin"))) { continue; } // alleen echt haar (geen hairpin)
		UMaterialInstanceDynamic* MID = SkM->CreateDynamicMaterialInstance(i);
		if (!MID) { continue; }
		const FLinearColor C = WeedNpc_HairColor(Seed + (uint32)i);
		MID->SetVectorParameterValue(TEXT("RootColor"), C * 0.7f);
		MID->SetVectorParameterValue(TEXT("TipColor"),  C);
		MID->SetVectorParameterValue(TEXT("DyeColor"),  C);
	}
}

// Per-NPC RANDOM kleding-kleur: tint alleen de kleding-materiaalslots (Citizens 'Tone_A..F' / Casual
// 'ColorCloth*') met kleuren uit de NPC-seed -> elke Karl/Tony/Casual heeft een ander shirt/broek/sneakers
// zonder extra meshes. Huid/gezicht/haar/ogen blijven ongemoeid. Niet-bestaande params worden genegeerd.
static void WeedNpc_TintClothing(USkeletalMeshComponent* SkM, uint32 Seed)
{
	if (!SkM) { return; }
	const TArray<UMaterialInterface*> Mats = SkM->GetMaterials();
	for (int32 i = 0; i < Mats.Num(); ++i)
	{
		UMaterialInterface* MI = Mats[i];
		if (!MI) { continue; }
		const FString Nm = MI->GetName();
		if (Nm.Contains(TEXT("Hair"))) { continue; } // 'Hair_cap' is geen kleding
		const bool bCloth = Nm.Contains(TEXT("Cloth")) || Nm.Contains(TEXT("Set")) || Nm.Contains(TEXT("Top"))
			|| Nm.Contains(TEXT("Jean")) || Nm.Contains(TEXT("Pant")) || Nm.Contains(TEXT("Short"))
			|| Nm.Contains(TEXT("Hoodie")) || Nm.Contains(TEXT("Shirt")) || Nm.Contains(TEXT("Sweater"))
			|| Nm.Contains(TEXT("Jacket")) || Nm.Contains(TEXT("Sneak")) || Nm.Contains(TEXT("Shoe"))
			|| Nm.Contains(TEXT("Panama"));
		if (!bCloth) { continue; }
		UMaterialInstanceDynamic* MID = SkM->CreateDynamicMaterialInstance(i);
		if (!MID) { continue; }
		const FLinearColor C1 = WeedNpc_ClothColor(Seed * 131u + (uint32)i * 7u + 1u);
		const FLinearColor C2 = WeedNpc_ClothColor(Seed * 131u + (uint32)i * 7u + 2u);
		// Casual-kleding (M_Cloth / M_Cloth_Masks):
		MID->SetVectorParameterValue(TEXT("ColorCloth"),  C1);
		MID->SetVectorParameterValue(TEXT("ColorCloth1"), C1);
		MID->SetVectorParameterValue(TEXT("ColorCloth2"), C2);
		MID->SetVectorParameterValue(TEXT("ColorCloth3"), C1);
		// Citizens-kleding (Tone_A..F): zachte variatie rond 1 basiskleur zodat het samenhangend blijft.
		MID->SetVectorParameterValue(TEXT("Tone_A"), C1);
		MID->SetVectorParameterValue(TEXT("Tone_B"), C1 * 0.8f);
		MID->SetVectorParameterValue(TEXT("Tone_C"), C2);
		MID->SetVectorParameterValue(TEXT("Tone_D"), C1 * 1.1f);
		MID->SetVectorParameterValue(TEXT("Tone_E"), C2 * 0.85f);
		MID->SetVectorParameterValue(TEXT("Tone_F"), C1 * 0.7f);
	}
}

// Bouwt het uiterlijk (mesh + modulaire parts + kleur-tint) deterministisch op uit NpcId (seed) + RepSkinIndex.
// Draait op host EN client: meshes/parts/MIDs repliceren niet, maar zijn 100% afleidbaar uit deze twee
// gerepliceerde waarden, dus host en client bouwen exact dezelfde persoon lokaal. 1x per pawn (idempotent).
void ACustomerBase::BuildAppearance()
{
	if (bAppearanceBuilt) { return; }
	if (NpcId.IsNone() || RepSkinIndex < 0) { return; } // wacht tot beide gerepliceerd zijn (client)
	USkeletalMeshComponent* SkM = GetMesh();
	if (!SkM) { return; }

	const uint32 LookSeed = WeedNpc_StableSeed(NpcId); // stabiel -> altijd dezelfde look
	const int32 SkinIdx = RepSkinIndex;
	if (SkinIdx >= 3 && SkinIdx <= 5)
	{
		// Casual-band -> MODULAIRE persoon: random combinatie van top/broek/schoenen/kapsel/hoofd (+ kleur).
		WeedNpc_BuildModular(this, SkM, LookSeed);
	}
	else if (SkinIdx >= 6 && SkinIdx <= 9)
	{
		// Tony-band -> MODULAIRE Citizens-persoon: random top/broek/schoenen/pet/bril/horloge (+ kleur).
		WeedNpc_BuildModularCitizens(this, SkM, LookSeed);
	}
	else if (USkeletalMesh* Sk = WeedNpc_SkinByIndex(SkinIdx))
	{
		SkM->SetSkeletalMesh(Sk);
		WeedNpc_TintClothing(SkM, LookSeed); // Karl/Tony (losse mesh): per-NPC random kleren-kleur bovenop
	}
	bAppearanceBuilt = true;

	// Activity-NPC: na de mesh-swap z'n vaste pose opnieuw aanzetten (SetSkeletalMesh kan de single-node reset).
	if (ActivityAnimIndex >= 0) { ApplyActivityAnim(); }
}

void ACustomerBase::OnRep_Appearance()
{
	BuildAppearance(); // client: zodra NpcId + RepSkinIndex binnen zijn, bouw exact dezelfde persoon lokaal op
}

void ACustomerBase::BeginPlay()
{
	Super::BeginPlay();

	// Loop/idle zelf aansturen (single-node) -> NPC's animeren echt i.p.v. glijden. (Een volle locomotie-
	// AnimBP draait NIET betrouwbaar op de gemengde NPC-skeletten via compatibele-skeletons -> ref-pose/
	// zweven; single-node PlayAnimation is veel toleranter en werkt op alle skins.)
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
		if (!bActivityNpc) { State = ECustomerState::WantsToOrder; }
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

		// Skin-index 1x toegewezen (tier-gewogen) + PERSISTENT bewaard per NPC. We bewaren 'm OOK gerepliceerd
		// op de pawn (RepSkinIndex) zodat een co-op-CLIENT exact hetzelfde uiterlijk lokaal opbouwt; de mesh +
		// modulaire parts + tint repliceren namelijk niet. De host bouwt het uiterlijk hier direct op.
		if (UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr)
		{
			const int32 Tier = Reg->GetCustomerTier(NpcId);
			RepSkinIndex = Reg->GetOrAssignSkin(NpcId, Tier, (int32)WeedNpc_StableSeed(NpcId));
		}
		BuildAppearance();

		// Activity-NPC (dev-tool): heeft nu een varieerde skin, maar is verder inert -> geen koop-/afspraak-/
		// product-logica. Z'n loop-naar-de-plek + anim wordt in TickActivity afgehandeld.
		if (bActivityNpc)
		{
			return;
		}

		// Nog te weinig verslaving? Dan is dit (nog) geen koper maar een prospect: eerst opwarmen
		// met gratis samples. Wie al verslaafd genoeg is (bv. een vaste klant) wil meteen kopen.
		if (Addiction < AddictionToBuy)
		{
			State = ECustomerState::Prospect;
		}

		// Geen gewenst product ingesteld? Kies een strain die binnen je SPELERLEVEL valt: vroeg in het
		// spel vragen klanten lage-level (lage-%) wiet, niet meteen level-38 top-strains. Kwaliteit blijft
		// belangrijk (zie de deal-acceptatie); dit gaat puur over WELKE strain ze willen.
		if (DesiredProductId.IsNone() && ProductTable)
		{
			if (bApptActive)
			{
				// Afspraak: exact wat in het telefoonbericht stond (volledig product + aantal).
				if (!ApptWantProduct.IsNone())     { DesiredProductId = ApptWantProduct; }
				else if (!ApptWantStrain.IsNone()) { DesiredProductId = FName(*FString::Printf(TEXT("Bag_%s"), *ApptWantStrain.ToString())); }
				if (ApptWantQty > 0) { DesiredQuantity = ApptWantQty; }
			}
			else
			{
				// Walk-in: gedeelde keuze-logica (tier-weging + premium hasj/edibles + soms net boven je bereik).
				DesiredProductId = ACustomerBase::PickDesiredProduct(GS, ProductTable, NpcId, DesiredQuantity);
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
	DOREPLIFETIME(ACustomerBase, ActivityAnimIndex);
	DOREPLIFETIME(ACustomerBase, NpcId);        // co-op: client heeft de seed nodig voor het uiterlijk
	DOREPLIFETIME(ACustomerBase, RepSkinIndex); // co-op: welke skin/band -> client herbouwt 'm lokaal
	DOREPLIFETIME(ACustomerBase, DealingPawn);  // co-op: wie deal er nu (exclusiviteit)
	DOREPLIFETIME(ACustomerBase, bNeedsPlayer);
	DOREPLIFETIME(ACustomerBase, bTalkingToPlayer);
	DOREPLIFETIME(ACustomerBase, bShopkeeper);
	DOREPLIFETIME(ACustomerBase, bApptActive);
	DOREPLIFETIME(ACustomerBase, ApptTimeout);
	DOREPLIFETIME(ACustomerBase, NpcId);
}

void ACustomerBase::PushApptMessage(const FString& InBody)
{
	if (!HasAuthority() || NpcId.IsNone()) { return; }
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return; }
	Con->PushInfoMessage(NpcId, FText::FromString(ACityDoor::FriendlyNpcName(NpcId)), FText::FromString(InBody));
}

void ACustomerBase::SetTalkingToPlayer(bool b, APawn* Pawn)
{
	if (!HasAuthority()) { return; }
	bTalkingToPlayer = b;
	DealingPawn = b ? Pawn : nullptr; // gerepliceerd -> andere client weet wie bezig is
	if (b)
	{
		if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
	}
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
	// In competitive schrijven we naar de per-speler-sleutel (ActiveRelKey); in co-op naar de gedeelde NpcId.
	const FName Key = ActiveRelKey.IsNone() ? NpcId : ActiveRelKey;
	if (Key.IsNone())
	{
		return;
	}
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS && GS->GetNpcRegistry())
	{
		GS->GetNpcRegistry()->ApplyStats(Key, Respect, Loyalty, Addiction);
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

	// Activity-NPC (dev-tool): loopt naar z'n vaste plek en doet daar z'n anim - geen klant-/bewoner-logica.
	if (bActivityNpc)
	{
		TickActivity(DeltaSeconds);
		return;
	}

	// Verkoper achter de balie: altijd stil blijven staan.
	if (bShopkeeper)
	{
		if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
		return;
	}

	// In gesprek met een speler -> stilstaan (niet doorlopen) tot het gesprek sluit.
	if (bTalkingToPlayer)
	{
		// Veiligheid tegen vastvriezen: staat er geen speler meer in de buurt, dan is het "gesprek"
		// voorbij (UI raar gesloten / speler weggelopen) -> laat 'm weer gewoon doorlopen.
		bool bPlayerNear = false;
		if (UWorld* Wd = GetWorld())
		{
			for (FConstPlayerControllerIterator It = Wd->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PC = It->Get();
				const APawn* P = PC ? PC->GetPawn() : nullptr;
				if (P && FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) < 500.f) { bPlayerNear = true; break; }
			}
		}
		if (!bPlayerNear)
		{
			bTalkingToPlayer = false; // anti-vastvriezen: gesprek is duidelijk voorbij
		}
		else
		{
			if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
			return;
		}
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
	// Activity-NPC's (dev-tool) zijn vaste ambiance, geen klanten -> niet als stip op de map.
	if (bActivityNpc)
	{
		return false;
	}
	// Winkeliers staan vast achter hun balie - geen map-stip (oogt anders als een vastzittende NPC).
	if (bShopkeeper)
	{
		return false;
	}
	if (!bResident)
	{
		return !IsHidden();
	}
	// Binnen: niet op de map (ze staan niet 'op straat').
	if (bAtHomeInside)
	{
		return false;
	}
	if (bApptActive && !bApptComeToPlayer && bApptArrived)
	{
		return true;
	}
	// 's Morgens uit huis lopen: tonen, zodat de marker ze al vanaf hun huis volgt (niet pas op de stoep).
	if (bEmergingFromHome)
	{
		return true;
	}
	// Naar huis lopen: tonen zolang 'ie in de buurt van z'n eigen huis is (de nette aanloop); de verre
	// transit door het centrum verbergen we, anders lijkt iedereen-naar-huis op een cluster.
	if (bEnteringHome)
	{
		return FVector::Dist2D(GetActorLocation(), HomeFrontSpot) < 1800.f;
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
	OutSnapshot.ResidentLabel = !HomeNumber.IsEmpty() ? HomeNumber : ACityDoor::FriendlyNpcName(NpcId);
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

	// Nav-walk exit (rijtjeshuis + apartment): loop via de navmesh van de EIGEN unit naar de voordeur/straat
	// (de trap af voor apartments). Geen staged-teleport meer die upper-floor bewoners een verdieping lager uit
	// een verkeerde kamer liet ploppen. Alleen bij echte pathing-stuck: teleporteer naar de voordeur als fallback.
	if (bResident)
	{
		const FVector Dest = HomeFrontSpot;
		const FVector Cur = GetActorLocation();
		auto FinishOutside = [&]()
		{
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
		};
		if (FVector::Dist2D(Cur, Dest) < 155.f && FMath::Abs(Cur.Z - Dest.Z) < 230.f)
		{
			FinishOutside();
			return false;
		}
		const bool bMoveStarted = WalkTo(Dest);
		float MoveDelta = 9999.f;
		if (bHasResidentPrevMoveLoc) { MoveDelta = FVector::Dist2D(Cur, ResidentPrevMoveLoc); }
		ResidentPrevMoveLoc = Cur;
		bHasResidentPrevMoveLoc = true;
		bool bPathMoving = bMoveStarted;
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			bPathMoving = bMoveStarted && (AI->GetMoveStatus() == EPathFollowingStatus::Moving);
		}
		if (!bPathMoving || MoveDelta < 4.f) { HomeExitStuckTimer += DeltaSeconds; }
		else { HomeExitStuckTimer = 0.f; }
		if (HomeExitStuckTimer >= 5.f)
		{
			if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
			SetActorLocation(MakeResidentStandingLocation(HomeFrontSpot));
			FinishOutside();
			return false;
		}
		return true;
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

void ACustomerBase::SendHomeAndDespawn()
{
	if (!bResident || bAtHomeInside) { Destroy(); return; } // niet-bewoner of al binnen -> meteen weg
	bDespawnWhenInside = true;
	DespawnSafetyTimer = 0.f;
	bApptActive = false; // afspraak is upstream al uitgesloten; voor de zekerheid
	if (!bEnteringHome) { StartResidentHomeEntry(); }
}

bool ACustomerBase::TickResidentHomeEntry(float DeltaSeconds)
{
	if (!bEnteringHome)
	{
		return false;
	}

	// Loop ECHT via de navmesh naar binnen (zowel rijtjeshuis als apartment) - de deuren bridge'n de navmesh
	// nu en de trappen verbinden de verdiepingen, dus geen stages/slide meer. Alleen bij echte pathing-stuck
	// teleporteren we naar binnen (verborgen), nooit zichtbaar in de voortuin/op straat/op de trap.
	if (bResident)
	{
		const FVector Dest = HomeInteriorPos + FVector(0.f, 0.f, 4.f);
		const FVector Cur = GetActorLocation();
		auto FinishInside = [&]()
		{
			if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
			bEnteringHome = false;
			bAtHomeInside = true;
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			SetActorLocation(Dest);
			HomeEntryStuckTimer = 0.f;
			bHasResidentPrevMoveLoc = false;
		};
		if (FVector::Dist2D(Cur, Dest) < 130.f && FMath::Abs(Cur.Z - Dest.Z) < 220.f)
		{
			FinishInside();
			return true;
		}
		const bool bMoveStarted = WalkTo(Dest);
		float MoveDelta = 9999.f;
		if (bHasResidentPrevMoveLoc) { MoveDelta = FVector::Dist2D(Cur, ResidentPrevMoveLoc); }
		ResidentPrevMoveLoc = Cur;
		bHasResidentPrevMoveLoc = true;
		bool bPathMoving = bMoveStarted;
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			bPathMoving = bMoveStarted && (AI->GetMoveStatus() == EPathFollowingStatus::Moving);
		}
		if (!bPathMoving || MoveDelta < 4.f) { HomeEntryStuckTimer += DeltaSeconds; }
		else { HomeEntryStuckTimer = 0.f; }
		if (HomeEntryStuckTimer >= 4.f) { FinishInside(); }
		return true;
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
	ApptTimeout = ApptTimeoutMax; // daarna geeft de NPC de afspraak op
	bApptSaidOnWay = bApptSaidHere = bApptSaidWaiting = false;
	SetNeedsPlayer(true);      // poppetje op de kompas zodat de speler weet waar te zijn
	BecomeBuyerNow();          // afspraak = wil kopen (geen prospect-sampling meer)

	// "Ik ben onderweg"-appje.
	PushApptMessage(bComeToPlayer ? TEXT("On my way - I'll wait at your main entrance.") : TEXT("Come by mine whenever, I'm home."));
	bApptSaidOnWay = true;

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
		// Pack-map (geen CityGenerator): geen stoep-data, maar wel een harde HOOGTE-regel.
		// Referentie = het HAL/STRAAT-punt als dat is meegegeven (toren-bewoners krijgen daar
		// het straatniveau door, zodat ze de trap af naar beneden lopen i.p.v. op hun eigen
		// verdieping andermans appartementen in te dwalen), anders het punt voor de voordeur.
		const FVector Ref = bHasHomeHall ? HomeHallPos : (HomeExitSidewalkSpot.IsNearlyZero() ? HomeFrontSpot : HomeExitSidewalkSpot);
		return Ref.IsNearlyZero() || FMath::Abs(Point.Z - Ref.Z) < 250.f;
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
	const float MinTrip = FMath::Clamp(Pitch * 0.95f, 1400.f, 3200.f); // langere trips voor ze stoppen/omdraaien
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
	const float CenterSoftRadius = FMath::Max(1300.f, City->GetMapBlockSize() * 1.6f); // dekt de centrale blokken
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
			if (bCenterCandidate && CenterCrowd >= (bAllowShortTrip ? 4 : 3))
			{
				continue; // centrum vol -> mijd centrale doelen, blijf in de wijken
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
			const float CenterPenalty = bCenterCandidate ? Pitch * (5.0f + static_cast<float>(CenterCrowd) * 0.9f) : 0.f;
			// MIDDELLANGE trips (~2 blokken) i.p.v. zo-ver-mogelijk. TravelDist*0.55 beloonde maximale afstand:
			// iedereen koos doelen aan de overkant (CENTERDIAG: goalDist 8-15k) -> alle routes door de centrum-
			// ring (de bottleneck van de grid-stad) -> de permanente ophoping daar. Nu blijven ze in hun regio.
			const float PreferredTrip = Pitch * 2.1f;
			const float TripScore = -FMath::Abs(TravelDist - PreferredTrip) * 0.7f;
			// District-bonus alleen LOKAAL (binnen ~2,5 blok van het district-doel). De oude MapRadius-variant
			// trok ook naar districten aan de overkant -> alsnog dwars-door-de-stad-trips. Ligt het district-doel
			// ver weg, dan telt 'ie gewoon niet mee en blijven ze in hun regio (en migreren ze over dagen vanzelf).
			// Centrum-mijding BEGRENSD: alleen straf BINNEN ~1,6 blok van het centrum, geen beloning verder weg.
			// (De oude onbegrensde +CenterDist werd na de trip-band de dominante term -> iedereen koos doelen op
			// de buitenrand en kluitte daar samen rond de buiten-blokken/winkels.)
			// Naar-buiten-druk AFGETOPT op ~2,8 blok: mild naar buiten (anders zakt de random-walk naar het
			// midden-3x3), maar daarbuiten vlak zodat de rand geen magneet wordt (dat was de vorige kluit).
			const float Score = TripScore
				- FMath::Max(0.f, Pitch * 1.6f - CenterDist) * 1.2f
				+ FMath::Min(CenterDist, Pitch * 2.8f) * 0.5f
				+ FMath::Max(0.f, Pitch * 2.5f - DistrictDist) * 1.0f
				+ Alignment * Pitch * 0.85f
				- static_cast<float>(Crowd) * 2400.f
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
	// Oversteken RUIMHARTIG herkennen: een doel hebben + (een beetje) bewegen. De oude eis "doel > 260 ver"
	// sloot juist het LAATSTE stuk van een oversteek uit (doel net aan de overkant), en de snelheids-eis
	// (>55) viel weg zodra RVO even afremde -> bewoners werden halverwege de rijweg teruggetrokken naar de
	// stoep en leken niet te 'durven' oversteken.
	const bool bLikelyCrossingStreet = bHasRoamGoal && Speed2D > 25.f;
	ResidentOffSidewalkTimer += DeltaSeconds;
	const float DriftGraceSeconds = bLikelyCrossingStreet ? 10.f : 2.0f;
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

	// Iedereen komt 1-2x per dag bij het park - de PARK-WACHTRIJ op de spawner regelt dat dat netjes om de
	// beurt gebeurt (max een paar tegelijk op trip), dus hier geen gate/crowd-cap meer nodig.
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
		// PARK-WACHTRIJ: vraag je beurt aan bij de spawner. Geen beurt? Dan sta je in de FIFO-rij - roam
		// gewoon door en probeer 't bij de volgende leg opnieuw. Zo gaat iedereen netjes om de beurt (max
		// een paar tegelijk op trip) en hoopt het centrum nooit op.
		ACustomerSpawner* ParkQueueOwner = nullptr;
		for (TActorIterator<ACustomerSpawner> SpIt(GetWorld()); SpIt; ++SpIt) { ParkQueueOwner = *SpIt; break; }
		const bool bMyTurn = ParkQueueOwner && ParkQueueOwner->RequestParkVisit(this);
		if (bMyTurn)
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

	// "Ga naar huis en verdwijn" (nacht-populatie/rotatie): loop eerst rustig naar je EIGEN huis (de
	// normale entry-routing door je deur) en despawn pas zodra je ECHT binnen bent - geen plop op straat.
	if (bDespawnWhenInside)
	{
		if (bAtHomeInside) { Destroy(); return; }
		DespawnSafetyTimer += DeltaSeconds;
		if (DespawnSafetyTimer >= 90.f)
		{
			// Pathing faalt? Verdwijn dan verborgen BINNEN, nooit zichtbaar op straat/in de voortuin.
			if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
			SetActorHiddenInGame(true);
			SetActorLocation(HomeInteriorPos + FVector(0.f, 0.f, 4.f));
			Destroy();
			return;
		}
		if (!bEnteringHome) { StartResidentHomeEntry(); }
		TickResidentHomeEntry(DeltaSeconds);
		return;
	}

	// --- Afspraak heeft voorrang op roamen/nacht ---
	if (bApptActive)
	{
		ParkPauseTimer = 0.f;
		bRoamGoalIsPark = false;
		// Afgehandeld (deal gesloten) of veiligheids-timeout -> afspraak loslaten, normaal leven hervatten.
		ApptTimeout -= DeltaSeconds;
		// "Taking too long"-appje als de tijd bijna op is (1x).
		if (ApptTimeout > 0.f && ApptTimeout < 75.f && !bApptSaidWaiting && State != ECustomerState::Served)
		{
			PushApptMessage(TEXT("Yo, where you at? I can't wait much longer..."));
			bApptSaidWaiting = true;
		}
		if (State == ECustomerState::Served || State == ECustomerState::Leaving)
		{
			EndAppointment();
		}
		else if (ApptTimeout <= 0.f)
		{
			// Te lang laten wachten -> de NPC geeft de afspraak op: respect + loyaliteit omlaag en vertrekt.
			Respect = ClampAttr(Respect - 5.f);
			Loyalty = ClampAttr(Loyalty - 8.f);
			WriteStatsToRegistry();
			Say(TEXT("You took too long. I'm out."));
			EndAppointment();
			LeaveAngry();
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
				if (!bApptSaidHere) { PushApptMessage(TEXT("I'm here at the entrance, come outside.")); bApptSaidHere = true; }
			}
			return;
		}
		else
		{
			// "Kom bij mij": wacht ECHT bij de eigen deur (bereikbaar), niet midden in de kamer:
			//  - appartement -> in de gang vóór de unitdeur (HomeHallPos),
			//  - rijtjeshuis -> vóór de voordeur (HomeFrontSpot).
			if (!bApptArrived)
			{
				bApptArrived = true;
				bAtHomeInside = false;
				SetActorHiddenInGame(false);
				SetActorEnableCollision(true);
				const FVector DoorSpot = bHasHomeHall ? HomeHallPos : HomeFrontSpot;
				SetActorLocation(MakeResidentStandingLocation(DoorSpot));
				if (AAIController* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
				if (!bApptSaidHere) { PushApptMessage(TEXT("I'm home, come through whenever.")); bApptSaidHere = true; }
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

	// 's Nachts gaan alleen de NIET-verslaafde bewoners slapen (naar binnen). Verslaafden (>= ~30) blijven
	// 's nachts buiten rondhangen -> er lopen ook 's nachts NPC's op straat (de nacht-crowd).
	if (bNight && Addiction < 30.f)
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
				// Urgent naar het park - maar ALLEEN als het ook echt onze beurt is (park-wachtrij). Anders
				// cancelden we hier elke leg de roam-goal terwijl de goal-keuze 'm weigerde (geen beurt) ->
				// tientallen bewoners die rond het urgency-uur stokstijf stonden te wachten (livelock).
				ACustomerSpawner* QueueOwner = nullptr;
				for (TActorIterator<ACustomerSpawner> SpIt(W); SpIt; ++SpIt) { QueueOwner = *SpIt; break; }
				if (QueueOwner && QueueOwner->RequestParkVisit(this))
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
			// Park-trip klaar -> beurt vrijgeven zodat de volgende in de rij kan gaan.
			for (TActorIterator<ACustomerSpawner> SpIt(GetWorld()); SpIt; ++SpIt) { SpIt->FinishParkVisit(this); break; }
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
	// Via de unified prijs-functie: zo krijgt een gewenst zakje/concentraat ook de juiste prijs
	// (bag -> bud-rij, concentraat × premium-factor) i.p.v. €0 bij een ontbrekende directe rij.
	return GetMarketPriceForProduct(DesiredProductId);
}

int32 ACustomerBase::GetMarketPriceForProduct(FName ProductId) const
{
	if (!ProductTable || ProductId.IsNone()) { return 0; }
	// Alleen VERPAKTE wiet is verkoopbaar aan klanten. Een Bag_<strain> wordt geprijsd via de
	// product-rij van de strain (Bud_<strain>). Losse/natte buds -> 0 (niet verkoopbaar).
	const FString S = ProductId.ToString();
	FName LookupId = ProductId;
	float Mult = 1.f;
	// Verkoopbaar aan klanten: verpakte wiet (Bag_) + de verwerkte drugs (Crystal_/Hash_/Edible_), geprijsd
	// via de strain-rij (Bud_<strain>) maal een premium-factor. Tussenstappen (Baked_/ButterMix_) niet.
	auto StrainOf = [&](int32 PreLen) { return FName(*FString::Printf(TEXT("Bud_%s"), *S.RightChop(PreLen))); };
	// Premium-factor per gram CONCENTRAAT t.o.v. de bud-prijs. De lage-conversie ketens (rosin/bubble/
	// hash/oil leveren maar ~10-24% van het ingaande gewicht) moeten per gram veel duurder zijn, anders
	// is de hele keten minder waard dan de ruwe bud gewoon verkopen. Mults afgestemd op:
	// (totaal-conv × mult) > 1.5 op de Std/Pro-machines = concentreren loont duidelijk.
	if      (S.StartsWith(TEXT("Bag_")))     { LookupId = FName(*FString::Printf(TEXT("Bud_%s"), *UInventoryComponent::BagStrain(ProductId).ToString())); }
	else if (S.StartsWith(TEXT("Crystal_")))  { LookupId = StrainOf(8); Mult = 4.0f; }   // kief (tussenstap -> pers tot hash)
	else if (S.StartsWith(TEXT("Hash_")))     { LookupId = StrainOf(5); Mult = 10.0f; }  // 2-staps keten (mesh+press)
	else if (S.StartsWith(TEXT("Edible_")))   { LookupId = StrainOf(7); Mult = 4.0f; }
	else if (S.StartsWith(TEXT("Cookie_")))   { LookupId = StrainOf(7); Mult = 4.2f; }  // baked edible (ingredients)
	else if (S.StartsWith(TEXT("Gummy_")))    { LookupId = StrainOf(6); Mult = 4.2f; }  // set edible (ingredients)
	else if (S.StartsWith(TEXT("Rosin_")))    { LookupId = StrainOf(6); Mult = 9.0f; }   // solventless premium (lage conv)
	else if (S.StartsWith(TEXT("Bubble_")))   { LookupId = StrainOf(7); Mult = 11.0f; }  // ice/bubble hash (laagste conv)
	else if (S.StartsWith(TEXT("Oil_")))      { LookupId = StrainOf(4); Mult = 9.0f; }   // cannabis-olie (premium concentraat)
	else if (S.StartsWith(TEXT("Moonrock_"))) { LookupId = StrainOf(9); Mult = 3.4f; }  // moonrocks (hoge conv)
	const FWeedShopProductRow* Row =
		ProductTable->FindRow<FWeedShopProductRow>(LookupId, TEXT("ACustomerBase::GetMarketPriceForProduct"), false);
	// Losse Bud_ (niet verpakt) + de tussenstappen zijn NIET verkoopbaar aan klanten. (Oil IS nu wel
	// verkoopbaar: het was een eindproduct zonder afzet -> dead weight.)
	if (S.StartsWith(TEXT("Bud_")) || S.StartsWith(TEXT("WetBud_")) || S.StartsWith(TEXT("Baked_")) || S.StartsWith(TEXT("ButterMix_"))) { return 0; }
	const int32 Raw = Row ? FMath::RoundToInt(Row->MarketPriceCents * Mult) : 0;
	const int64 Rounded = WeedRoundEuros((int64)Raw);
	return Raw > 0 ? (int32)FMath::Max<int64>(100, Rounded) : (int32)Rounded;
}

FName ACustomerBase::PickDesiredProduct(AWeedShopGameState* GS, UDataTable* InProductTable, FName InNpcId, int32& OutQty)
{
	OutQty = FMath::RandRange(1, 3);
	if (!InProductTable) { return NAME_None; }
	const int32 PlayerLvl = (GS && GS->GetLeveling()) ? GS->GetLeveling()->GetLevel() : 1;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UNpcRegistryComponent* Reg = GS ? GS->GetNpcRegistry() : nullptr;
	const int32 CTier = FMath::Clamp((Reg && !InNpcId.IsNone()) ? Reg->GetCustomerTier(InNpcId) : 1, 1, 5);
	if (Reg && !InNpcId.IsNone()) { int32 Mn = 1, Mx = 3; Reg->GetTierOrderGrams(InNpcId, Mn, Mx); OutQty = FMath::RandRange(Mn, Mx); }

	// Strains: ontgrendeld (Eligible) + net buiten bereik (JustAbove, voor veeleisende whales), met unlock-level.
	TArray<TPair<int32, FName>> Eligible, JustAbove;
	FName LowestStrain; int32 LowestLvl = MAX_int32;
	for (const FName& Row : InProductTable->GetRowNames())
	{
		const FString RS = Row.ToString();
		if (!RS.StartsWith(TEXT("Bud_"))) { continue; }
		const FName Strain(*RS.RightChop(4));
		const int32 Lvl = Store ? Store->RequiredLevelFor(Strain) : 1;
		if (Lvl < LowestLvl) { LowestLvl = Lvl; LowestStrain = Strain; }
		if (Lvl <= PlayerLvl + 2) { Eligible.Add(TPair<int32, FName>(Lvl, Strain)); }
		else if (Lvl <= PlayerLvl + 12) { JustAbove.Add(TPair<int32, FName>(Lvl, Strain)); }
	}
	Eligible.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B) { return A.Key < B.Key; });
	FName PickStrain = LowestStrain;
	if (Eligible.Num() > 0)
	{
		const float TierFrac = (CTier - 1) / 4.f;                                   // 0 Casual .. 1 Whale
		const float Pos = FMath::Clamp(TierFrac + FMath::FRandRange(-0.30f, 0.30f), 0.f, 1.f);
		PickStrain = Eligible[FMath::Clamp(FMath::RoundToInt(Pos * (Eligible.Num() - 1)), 0, Eligible.Num() - 1)].Value;
	}
	// High/Whale vragen soms om wiet net boven je bereik -> substitueren + flink compenseren.
	if (CTier >= 4 && JustAbove.Num() > 0)
	{
		const float AspChance = (CTier >= 5) ? 0.18f : 0.10f;
		if (FMath::FRand() < AspChance)
		{
			JustAbove.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B) { return A.Key < B.Key; });
			PickStrain = JustAbove[FMath::RandRange(0, FMath::Min(2, JustAbove.Num() - 1))].Value;
		}
	}
	if (PickStrain.IsNone()) { return NAME_None; }

	// Producttype per tier (gegate op unlock-level): tier 1-2 vooral wiet, hoger steeds vaker premium
	// (hasj -> edibles -> moonrocks -> rosin -> isolator). Gewogen keuze; het weed-gewicht houdt lage tiers
	// vooral op wiet. Level-gates zorgen dat klanten pas iets vragen zodra jij het kunt maken.
	float WWeed, WHash, WEdible, WMoon, WRosin, WBubble;
	switch (CTier)
	{
		case 1:  WWeed = 1.00f; WHash = 0.00f; WEdible = 0.00f; WMoon = 0.00f; WRosin = 0.00f; WBubble = 0.00f; break;
		case 2:  WWeed = 1.00f; WHash = 0.12f; WEdible = 0.04f; WMoon = 0.00f; WRosin = 0.00f; WBubble = 0.00f; break;
		case 3:  WWeed = 0.65f; WHash = 0.28f; WEdible = 0.14f; WMoon = 0.12f; WRosin = 0.00f; WBubble = 0.00f; break;
		case 4:  WWeed = 0.45f; WHash = 0.32f; WEdible = 0.22f; WMoon = 0.18f; WRosin = 0.16f; WBubble = 0.12f; break;
		default: WWeed = 0.35f; WHash = 0.38f; WEdible = 0.28f; WMoon = 0.22f; WRosin = 0.20f; WBubble = 0.16f; break;
	}
	// Level-gates (ongeveer wanneer de bijbehorende machine ontgrendelt).
	if (PlayerLvl < 14) { WHash   = 0.f; }
	if (PlayerLvl < 9)  { WEdible = 0.f; }
	if (PlayerLvl < 34) { WMoon   = 0.f; } // Moonrock-machine ~lvl 34
	if (PlayerLvl < 40) { WRosin  = 0.f; } // Rosin ~lvl 40
	if (PlayerLvl < 46) { WBubble = 0.f; } // Isolator (bubble hash) ~lvl 46

	const float WCookie = WEdible * 0.5f; // cookies/gummies delen de edible-vraag (zelfde level-gate)
	const float WGummy  = WEdible * 0.5f;
	struct FPick { const TCHAR* Px; float W; };
	const FPick Picks[] = {
		{ TEXT("Bag_"), WWeed }, { TEXT("Hash_"), WHash }, { TEXT("Edible_"), WEdible },
		{ TEXT("Moonrock_"), WMoon }, { TEXT("Rosin_"), WRosin }, { TEXT("Bubble_"), WBubble },
		{ TEXT("Cookie_"), WCookie }, { TEXT("Gummy_"), WGummy } };
	float Total = 0.f; for (const FPick& P : Picks) { Total += P.W; }
	FString ProdType = TEXT("Bag_");
	if (Total > 0.f)
	{
		float R = FMath::FRand() * Total;
		for (const FPick& P : Picks) { if (P.W <= 0.f) { continue; } if (R < P.W) { ProdType = P.Px; break; } R -= P.W; }
	}
	return FName(*(ProdType + PickStrain.ToString()));
}

float ACustomerBase::ThcWillingnessBonus(float OfferedThc, float ExpectedThc)
{
	if (OfferedThc < 0.f) { return 0.f; }                       // onbekend -> geen bonus
	const float Baseline = (ExpectedThc > 0.f) ? ExpectedThc : 15.f; // wat de klant verwacht van z'n strain
	return FMath::Clamp((OfferedThc - Baseline) * 2.5f, 0.f, 45.f);  // sterker dan verwacht -> bereidwilliger
}

float ACustomerBase::GetExpectedThc() const
{
	// Strain uit DesiredProductId halen (werkt voor alle producttypes: Bag_/Bud_ + verwerkte drugs/concentraten).
	FString S = DesiredProductId.ToString();
	S.RemoveFromStart(TEXT("Bag_"));      S.RemoveFromStart(TEXT("Bud_"));
	S.RemoveFromStart(TEXT("Hash_"));     S.RemoveFromStart(TEXT("Edible_"));   S.RemoveFromStart(TEXT("Crystal_"));
	S.RemoveFromStart(TEXT("Moonrock_")); S.RemoveFromStart(TEXT("Rosin_"));    S.RemoveFromStart(TEXT("Bubble_"));
	S.RemoveFromStart(TEXT("Cookie_"));   S.RemoveFromStart(TEXT("Gummy_"));
	if (S.IsEmpty()) { return 15.f; }
	if (const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (UStoreComponent* Store = GS->GetStore())
		{
			float Thc = 0.f, Y = 0.f, G = 0.f;
			if (Store->GetStrainStats(FName(*S), Thc, Y, G) && Thc > 0.f) { return Thc; }
		}
	}
	return 15.f;
}

float ACustomerBase::TierPriceTolerance(int32 Tier)
{
	switch (Tier)
	{
	case 5: return 0.30f; // Whale
	case 4: return 0.20f; // VIP
	case 3: return 0.12f; // Heavy User
	case 2: return 0.06f; // Regular
	default: return 0.f;  // Casual
	}
}

int32 ACustomerBase::GetMyCustomerTier() const
{
	if (NpcId.IsNone()) { return 1; }
	if (const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry()) { return Reg->GetCustomerTier(NpcId); }
	}
	return 1;
}

float ACustomerBase::GetAcceptanceChance(int32 AskPriceCentsPerUnit, float Quality01, float ThcPercent) const
{
	// Prijs-tolerantie: hogere tier ervaart de prijs als lager (alleen prijs; kwaliteit/THC blijven tellen).
	const float EffAsk = static_cast<float>(AskPriceCentsPerUnit) * (1.f - TierPriceTolerance(GetMyCustomerTier()));
	const float Base = UWeedDealLibrary::CalculateAcceptanceChance(
		static_cast<float>(GetMarketPriceCents()), EffAsk,
		Respect, Loyalty, Addiction, Quality01);
	return FMath::Min(100.f, Base + ThcWillingnessBonus(ThcPercent, GetExpectedThc()));
}

float ACustomerBase::GetSubstituteAcceptance(FName AltProductId, int32 AskPriceCentsPerUnit, float Quality01, float ThcPercent) const
{
	const int32 Market = GetMarketPriceForProduct(AltProductId);
	if (Market <= 0) { return 0.f; }
	const float EffAsk = static_cast<float>(AskPriceCentsPerUnit) * (1.f - TierPriceTolerance(GetMyCustomerTier()));
	const float Base = UWeedDealLibrary::CalculateAcceptanceChance(
		static_cast<float>(Market), EffAsk,
		Respect, Loyalty, Addiction, Quality01) + ThcWillingnessBonus(ThcPercent, GetExpectedThc());
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

	// COMPETITIVE: respect/loyaliteit/verslaving staan PER SPELER los. Laad de relatie van de DEALENDE speler
	// (sleutel "NpcId#spelerId") zodat elke speler z'n eigen band met deze klant opbouwt. Co-op = gedeeld.
	if (PayTo && !NpcId.IsNone())
	{
		AWeedShopGameState* GScomp = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
		if (GScomp && GScomp->IsCompetitive())
		{
			FString Pid;
			if (APawn* Buyer = Cast<APawn>(PayTo->GetOwner())) { Pid = USaveGameSubsystem::StablePlayerId(Buyer); }
			if (!Pid.IsEmpty())
			{
				const FName Key(*FString::Printf(TEXT("%s#%s"), *NpcId.ToString(), *Pid));
				if (UNpcRegistryComponent* Reg = GScomp->GetNpcRegistry())
				{
					Reg->EnsurePlayerNpc(Key, NpcId, FText());
					float R = Respect, L = Loyalty, A = Addiction; FText Nm;
					Reg->GetStats(Key, R, L, A, Nm);
					Respect = R; Loyalty = L; Addiction = A;
					ActiveRelKey = Key;
				}
			}
		}
	}

	const int32 Market = GetMarketPriceForProduct(ProductId);
	if (Market <= 0)
	{
		return EDealResult::Refused;
	}

	// Producttype: verpakte wiet (Bag_) gaat per HELE zakjes; verwerkte drugs (Hash_/Edible_/Crystal_) per
	// losse gram. Bepaal voorraad + kwaliteit/THC van het juiste product.
	const FString PS = ProductId.ToString();
	const bool bBag = PS.StartsWith(TEXT("Bag_"));
	const FName Strain = bBag ? UInventoryComponent::BagStrain(ProductId) : NAME_None;
	int32 Available = 0;
	float Quality01 = 0.6f, ThcStock = 15.f;
	if (bBag)
	{
		Available = StockFrom ? StockFrom->BagGramsAvailable(Strain) : 0;
		for (const FInventoryStack& BS : StockFrom->GetStacks())
		{
			if (UInventoryComponent::IsBag(BS.ItemId) && UInventoryComponent::BagStrain(BS.ItemId) == Strain)
			{ Quality01 = FMath::Clamp(BS.QualityPct / 100.f, 0.f, 1.f); ThcStock = BS.Quality; break; }
		}
	}
	else
	{
		// Hasj/edibles/crystals: losse gram-voorraad van precies dit product.
		Available = StockFrom ? StockFrom->GetQuantity(ProductId) : 0;
		for (const FInventoryStack& BS : StockFrom->GetStacks())
		{
			if (BS.ItemId == ProductId) { Quality01 = FMath::Clamp(BS.QualityPct / 100.f, 0.f, 1.f); ThcStock = BS.Quality; break; }
		}
	}
	if (Available < DesiredQuantity)
	{
		UE_LOG(LogWeedShop, Log, TEXT("Customer: no stock of %s (%dg)."), *ProductId.ToString(), DesiredQuantity);
		return EDealResult::NoStock;
	}

	// Boven budget -> dingt af.
	if (AskPriceCentsPerUnit > BudgetCentsPerUnit)
	{
		State = ECustomerState::Negotiating;
		return EDealResult::Haggle;
	}

	// Substituut = ~50% basis (stats-afhankelijk); anders de normale kans. Sterkere wiet (THC) maakt ze
	// veel bereidwilliger (zit nu in beide functies via ThcWillingnessBonus).
	const float Chance = bSubstitute
		? GetSubstituteAcceptance(ProductId, AskPriceCentsPerUnit, Quality01, ThcStock)
		: GetAcceptanceChance(AskPriceCentsPerUnit, Quality01, ThcStock);
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

	// Deal rond: verpakte wiet -> hele zakjes; hasj/edibles -> losse gram. Reken de ECHTE grammen af.
	float SoldThc = 0.f, SoldQual = 0.f;
	int32 SoldGrams = 0;
	if (bBag)
	{
		SoldGrams = StockFrom->RemoveBagsForGrams(Strain, DesiredQuantity, SoldThc, SoldQual);
	}
	else
	{
		SoldGrams = FMath::Min(DesiredQuantity, Available);
		SoldThc = ThcStock; SoldQual = Quality01 * 100.f;
		StockFrom->RemoveItem(ProductId, SoldGrams);
	}
	if (SoldGrams <= 0) { return EDealResult::NoStock; }
	int32 Total = AskPriceCentsPerUnit * SoldGrams;

	// DAG-ORDER vervuld: de juiste strain (geen substituut), THC >= eis en de volle bestelde hoeveelheid
	// -> bonus-uitbetaling bovenop de prijs. Eenmalig (de afspraak-klant vertrekt hierna).
	bool bOrderFulfilled = false;
	int32 OrderBonusCents = 0;
	if (IsOrder() && !bSubstitute && SoldThc >= GetOrderMinThc() && SoldGrams >= DesiredQuantity)
	{
		OrderBonusCents = (int32)WeedRoundEuros((int64)FMath::RoundToInt(Total * (GetOrderBonusMult() - 1.f)));
		Total += OrderBonusCents;
		bOrderFulfilled = true;
	}

	if (PayTo)
	{
		PayTo->AddMoney(Total);
		PayTo->NoteLegitIncome(Total); // verkoop-omzet -> "schone ruimte" om wit te wassen (anti-witwas-heat)
		if (bOrderFulfilled)
		{
			Say(TEXT("Exactly what I wanted. Pleasure doing business."));
			UWeedToast::NotifyPawn(PayTo->GetOwner(), -1, 5.f, FColor(255, 215, 90),
				FString::Printf(TEXT("VIP order filled! +%d%% bonus (+EUR %d)"),
					FMath::RoundToInt((GetOrderBonusMult() - 1.f) * 100.f), (int32)(WeedRoundEuros((int64)OrderBonusCents) / 100)));
		}
	}

	float dR = 0.f, dL = 0.f, dA = 0.f;
	ComputeAcceptedDeltas(AskPriceCentsPerUnit, Market, Quality01, ThcStock, bSubstitute, dR, dL, dA);
	Respect = ClampAttr(Respect + dR);
	Loyalty = ClampAttr(Loyalty + dL);
	Addiction = ClampAttr(Addiction + dA);

	// Over-levering: kreeg de klant MEER grammen dan gevraagd (door hele zakjes)? Kleine bonus respect +
	// loyaliteit en een nette reactie zodat de speler 't merkt. HARD gecapt zodat je niet door extra te
	// dumpen iemand instant maxt.
	const int32 Surplus = SoldGrams - DesiredQuantity;
	if (Surplus > 0)
	{
		const float Eff = static_cast<float>(FMath::Min(Surplus, 4)); // alleen de eerste paar extra grammen tellen
		Respect = ClampAttr(Respect + FMath::Min(Eff * 0.4f, 1.6f));
		Loyalty = ClampAttr(Loyalty + FMath::Min(Eff * 0.6f, 2.5f));
		static const TCHAR* Nice[] = {
			TEXT("Yo, extra? You're a real one!"),
			TEXT("Damn, you hooked me up - respect!"),
			TEXT("More than I asked, good lookin' out!"),
			TEXT("Aight, that's love right there.") };
		Say(Nice[FMath::RandRange(0, 3)]);
	}

	State = ECustomerState::Served;
	WriteStatsToRegistry();
	// COMPETITIVE: ben je nu de favoriet van deze klant en pakte je 'm van een rivaal af? -> "afgepakt"-melding.
	if (!ActiveRelKey.IsNone() && PayTo)
	{
		FString MyId, Lhs; ActiveRelKey.ToString().Split(TEXT("#"), &Lhs, &MyId);
		AWeedShopGameState* GSp = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
		if (GSp && GSp->GetNpcRegistry() && !MyId.IsEmpty())
		{
			FString PrevOwner;
			if (GSp->GetNpcRegistry()->NotePlayerLoyalty(NpcId, MyId, Loyalty, PrevOwner))
			{
				float r = 0.f, l = 0.f, a = 0.f; FText Nm;
				GSp->GetNpcRegistry()->GetStats(NpcId, r, l, a, Nm);
				const FString Who = Nm.IsEmpty() ? TEXT("a regular") : Nm.ToString();
				UWeedToast::NotifyPawn(PayTo->GetOwner(), -1, 5.f, FColor(120, 255, 140),
					FString::Printf(TEXT("You poached %s from your rival!"), *Who));
				// De rivaal voelt het ook: zoek z'n pawn op (per stabiele speler-id) en meld het verlies.
				if (UWorld* W = GetWorld())
				{
					for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
					{
						APawn* RP = It->Get() ? It->Get()->GetPawn() : nullptr;
						if (RP && USaveGameSubsystem::StablePlayerId(RP) == PrevOwner)
						{
							UWeedToast::NotifyPawn(RP, -1, 5.f, FColor(255, 140, 120),
								FString::Printf(TEXT("%s started buying from your rival!"), *Who));
							break;
						}
					}
				}
			}
		}
	}
	// Cooldown starten: deze NPC (in persoon of via telefoon-afspraak) komt niet meteen terug.
	if (AWeedShopGameState* GSc = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (GSc->GetNpcRegistry() && !NpcId.IsNone())
		{
			GSc->GetNpcRegistry()->MarkDealt(NpcId);
			GSc->GetNpcRegistry()->AddCustomerValue(NpcId, SoldGrams); // klant-tier groeit met verkochte grammen
		}
		if (UGoalsComponent* Gl = GSc->GetGoals()) { Gl->NoteDeal(); Gl->NoteGramsSold(SoldGrams); } // goal-tellers: deal + gram verkocht
	}

	// XP op MOEITE (verkochte grammen × THC-bonus), NIET op verdiende euro's. Anders balloont de XP mee
	// met de geld-inflatie van de late strains en vlieg je door lvl 30-50; nu blijft 0-50 een gelijkmatige
	// grind. THC weegt mee (premium = meer XP/gram): 14% -> x1.35, 40% -> x2.0.
	if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (ULevelComponent* Lv = GS->GetLeveling())
		{
			// Basis-XP op moeite (gram x THC-bonus) + extra XP voor een vervulde VIP-order (premium opdracht).
			int32 DealXP = 5 + FMath::RoundToInt(SoldGrams * (1.f + SoldThc / 40.f));
			if (bOrderFulfilled) { DealXP += 15 + FMath::RoundToInt(SoldGrams * 0.5f); }
			Lv->AddXP(DealXP);
		}
		// Heat: op straat dealen trekt aandacht. 's Nachts fors riskanter dan overdag (BustThreshold = 80).
		if (UHeatComponent* Heat = GS->GetHeat())
		{
			const UDayCycleComponent* DCh = GS->GetDayCycle();
			Heat->AddHeat((DCh && DCh->IsNight()) ? 3.0f : 0.5f);
		}
	}

	UE_LOG(LogWeedShop, Log, TEXT("Deal: %dx %s%s for %d cents (resp %.0f loy %.0f rep %.0f)."),
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
	UE_LOG(LogWeedShop, Log, TEXT("Customer interaction result: %d"), static_cast<int32>(Result));

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
	// Winkelier: prompt hoort bij ZIJN winkel (geen deal). Winkelsoort via de dichtstbijzijnde balie.
	if (bShopkeeper)
	{
		const TCHAR* Shop = TEXT("Shop");
		float BestD = 700.f;
		for (TActorIterator<AStoreCounter> It(GetWorld()); It; ++It)
		{
			const float D = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
			if (D >= BestD) { continue; }
			BestD = D;
			switch (It->Kind)
			{
			case EShopKind::Grow:       Shop = TEXT("Grow shop"); break;
			case EShopKind::Supplies:   Shop = TEXT("Supplies store"); break;
			case EShopKind::Furniture:  Shop = TEXT("Furniture store"); break;
			case EShopKind::GasStation: Shop = TEXT("Gas station"); break;
			default:                    Shop = TEXT("Shop"); break;
			}
		}
		return FText::FromString(FString::Printf(TEXT("%s - talk to the clerk"), Shop));
	}

	switch (State)
	{
	case ECustomerState::WantsToOrder:
	case ECustomerState::Negotiating:
	{
		// Toon de klant-tier + WAT/HOEVEEL ze willen, en (op afspraak) hoelang ze nog wachten.
		FString Tier;
		if (const AWeedShopGameState* GSt = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
		{
			if (UNpcRegistryComponent* Reg = GSt->GetNpcRegistry())
			{
				if (!NpcId.IsNone()) { Tier = UNpcRegistryComponent::TierName(Reg->GetCustomerTier(NpcId)); }
			}
		}
		FString S = Tier.IsEmpty() ? TEXT("Deal") : FString::Printf(TEXT("[%s] Deal"), *Tier);
		if (!DesiredProductId.IsNone())
		{
			FString Item = DesiredProductId.ToString();
			Item.RemoveFromStart(TEXT("Bag_"));
			Item.RemoveFromStart(TEXT("Bud_"));
			S += FString::Printf(TEXT(" - wants %dg %s"), FMath::Max(1, DesiredQuantity), *Item);
		}
		if (bApptActive)
		{
			const int32 Left = FMath::CeilToInt(GetApptTimeLeft());
			S += FString::Printf(TEXT("  (leaves in %d:%02d)"), Left / 60, Left % 60);
		}
		return FText::FromString(S);
	}
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
	UE_LOG(LogWeedShop, Log, TEXT("Customer leaves angry (out of patience). Respect now %.0f."), Respect);
}
