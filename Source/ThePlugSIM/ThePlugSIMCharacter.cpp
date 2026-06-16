// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThePlugSIMCharacter.h"
#include "UI/WeedToast.h"
#include "Animation/AnimInstance.h"
#include "UObject/UnrealType.h"
#include "Animation/AnimSequence.h"
#include "UObject/ConstructorHelpers.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Net/UnrealNetwork.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "NavigationInvokerComponent.h"
#include "NavigationSystem.h"
#include "World/DayNightController.h"
#include "World/CityGenerator.h"
#include "World/DoorRetrofitter.h"
#include "Misc/FileHelper.h"
#include "World/StoreCounter.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/NavigationConfig.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "Placement/PropMeshKit.h"
#include "World/WorldItemPickup.h"
#include "Components/StaticMeshComponent.h"
#include "UI/WeedShopHUD.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Placement/BuildComponent.h"
#include "Placement/PlaceableProp.h"
#include "Placement/PlaceableTypes.h"
#include "Placement/FurnitureTemplateLib.h"
#include "Customization/OutfitCatalog.h"
#include "Cultivation/WaterCanComponent.h"
#include "EngineUtils.h"
#include "Cultivation/GrowPlant.h"
#include "Interaction/InteractionComponent.h"
#include "Customer/CustomerBase.h"
#include "Customer/CustomerSpawner.h"
#include "World/ActivitySpotManager.h"
#include "Npc/NpcRegistryComponent.h"
#include "World/HeatComponent.h"
#include "Progression/LevelComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Save/SaveGameSubsystem.h"
#include "Engine/GameInstance.h"
#include "Input/ControlSettings.h"
#include "World/Atm.h"
#include "World/PackBench.h"
#include "World/StorageShelf.h"
#include "Cultivation/DryingRack.h"
#include "SmokePuff.h"
#include "ThePlugSIM.h"

AThePlugSIMCharacter::AThePlugSIMCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	// (Mesh-offset voor co-op staat in BeginPlay, niet hier - in de constructor zou het de spawn-collision
	//  beinvloeden en de player-spawn laten falen.)

	// Navigation-invoker: genereert runtime-navmesh rond de speler (de stad is procedureel, dus geen
	// vooraf gebakken navmesh). Ruime straal zodat NPC's over de stoepen kunnen lopen rond jou.
	NavInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavInvoker"));
	// Runtime-navmesh rond de speler. 90/110m was enorm duur (continue regeneratie over 220m diameter).
	// 45/60m is ruim genoeg: NPC's vlakbij lopen normaal, verre NPC's worden toch al gethrottled. Grote CPU-winst.
	NavInvoker->SetGenerationRadii(4500.f, 6000.f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component
	// Camera op VASTE ooghoogte aan de capsule (NIET aan de head-bone). Zo blijft de camera stabiel ongeacht
	// welke skin je FP-mesh is, en kan ik het hoofd van je FP-mesh veilig verbergen zonder de camera te bewegen.
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(8.f, 0.f, 70.f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// Third-person camera op een spring-arm achter de speler — uit by default (we starten first-person).
	// Toets B togglet 'm zodat je jezelf / je gekozen skin kunt bekijken zonder FP-camera-gedoe.
	ThirdPersonBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Third Person Boom"));
	ThirdPersonBoom->SetupAttachment(GetCapsuleComponent());
	ThirdPersonBoom->TargetArmLength = 260.f;
	ThirdPersonBoom->SocketOffset = FVector(0.f, 40.f, 70.f);
	ThirdPersonBoom->bUsePawnControlRotation = true;
	ThirdPersonBoom->bDoCollisionTest = false; // niet inklappen tegen muren -> camera blijft achter je
	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Third Person Camera"));
	ThirdPersonCamera->SetupAttachment(ThirdPersonBoom);
	ThirdPersonCamera->bUsePawnControlRotation = false;
	ThirdPersonCamera->SetActive(false);

	// Held item: 3D-model van wat je vasthoudt, in de FP-view (alleen voor jezelf). Positie wordt elke tick
	// vanuit de kijkrichting gezet (zie Tick), dus de relatieve offset hier maakt niet veel uit.
	HeldItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Held Item Mesh"));
	HeldItemMesh->SetupAttachment(FirstPersonCameraComponent);
	HeldItemMesh->SetOnlyOwnerSee(true);
	HeldItemMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	HeldItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeldItemMesh->SetCastShadow(false);
	HeldItemMesh->SetVisibility(false);

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	// Walk/idle/jump-sequences voor de client-kant fallback (zie UpdateProxyAnim).
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PIdle(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	if (PIdle.Succeeded()) { ProxyIdle = PIdle.Object; }
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PWalk(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd"));
	if (PWalk.Succeeded()) { ProxyWalk = PWalk.Object; }
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PJump(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop"));
	if (PJump.Succeeded()) { ProxyJump = PJump.Object; }
	// 'Texting'-pose: andere spelers zien je op je telefoon staan (cellphone-check anim uit de NPC-pack).
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PPhone(TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Check_Cellphone.Anim_Check_Cellphone"));
	if (PPhone.Succeeded()) { ProxyPhone = PPhone.Object; }

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetCharacterMovement()->GravityScale = 1.0f;     // zwaartekracht gegarandeerd aan
	GetCharacterMovement()->JumpZVelocity = 450.0f;  // normale sprong
	GetCharacterMovement()->SetWalkableFloorAngle(50.0f);

	// Voorraad-component (oogst in, verkoop uit).
	Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));

	// Persoonlijke portemonnee (cash + bank) — co-op: ieder z'n eigen geld.
	Economy = CreateDefaultSubobject<UEconomyComponent>(TEXT("Economy"));

	// Telefoon-logica (openen, tabs, kopen, afspraken).
	Phone = CreateDefaultSubobject<UPhoneClientComponent>(TEXT("Phone"));

	// Plaats-modus voor placeables (kweekpot).
	Build = CreateDefaultSubobject<UBuildComponent>(TEXT("Build"));

	// Waterfles-staat (vullen bij de gootsteen).
	WaterCan = CreateDefaultSubobject<UWaterCanComponent>(TEXT("WaterCan"));

	// Tick aan voor de stoned-buf-timer.
	PrimaryActorTick.bCanEverTick = true;
}

bool AThePlugSIMCharacter::FindFloorAt(const FVector& Near, FVector& OutSafe) const
{
	UWorld* W = GetWorld();
	if (!W) { return false; }
	const FVector Start = Near + FVector(0.f, 0.f, 400.f);
	const FVector End = Near - FVector(0.f, 0.f, 6000.f);
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(StuckFloor), false, this);
	if (W->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Q))
	{
		const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.f;
		OutSafe = Hit.ImpactPoint + FVector(0.f, 0.f, HalfH + 6.f);
		return true;
	}
	return false;
}

void AThePlugSIMCharacter::TickStuckRecovery(float DeltaSeconds)
{
	if (!IsLocallyControlled()) { return; }
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move) { return; }

	const FVector Loc = GetActorLocation();
	if (Move->IsMovingOnGround())
	{
		// Sta stevig op de grond -> onthoud deze plek als veilige terugval.
		LastGroundLoc = Loc; bHasGroundLoc = true; FallTime = 0.f; FloatTime = 0.f;
		return;
	}
	if (Move->IsFalling())
	{
		FallTime += DeltaSeconds;
		// "Zweven": in de lucht maar bijna geen verticale snelheid (vast op iets / gelanceerd en blijft hangen).
		if (FMath::Abs(GetVelocity().Z) < 35.f) { FloatTime += DeltaSeconds; } else { FloatTime = 0.f; }
	}

	const bool bTooLong = FallTime > 6.f;        // veel te lang in de lucht
	const bool bFloating = FloatTime > 1.5f;     // hangt stil in de lucht = vast
	const bool bBelowWorld = Loc.Z < -3000.f;    // door de vloer gezakt
	if (!(bTooLong || bFloating || bBelowWorld)) { return; }

	// Diagnose (key 7 = update in plaats): laat zien WAAROM 'ie vast zit.
	{
		const int32 MM = (int32)Move->MovementMode.GetValue();
		const bool bCol = GetCapsuleComponent() && GetCapsuleComponent()->IsCollisionEnabled();
		UWeedToast::NotifyPawn(this,7, 4.f, FColor::Cyan, FString::Printf(TEXT("stuck: mode=%d vZ=%.0f gScale=%.2f gZ=%.0f col=%d"),
			MM, GetVelocity().Z, Move->GravityScale, Move->GetGravityZ(), bCol ? 1 : 0));
	}

	RecoverToSafe(false); // auto: terug naar de laatste veilige grond-plek
	FallTime = 0.f; FloatTime = 0.f;
}

void AThePlugSIMCharacter::RecoverToSafe(bool bManual)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move) { return; }

	// Doel-plek. bManual (knop/H) = de DICHTSTBIJZIJNDE begaanbare plek (weg/stoep/vloer) via de navmesh
	// - NIET naar huis (thuis-TP mag niet altijd kunnen). Anders (auto-recovery) = laatste-grond-plek.
	FVector Safe; bool bGot = false;
	if (bManual)
	{
		const FVector Loc = GetActorLocation();
		if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld()))
		{
			FNavLocation Proj;
			// Eerst dichtbij zoeken (op de weg/stoep waar je staat), anders een ruimere straal.
			if (Nav->ProjectPointToNavigation(Loc, Proj, FVector(900.f, 900.f, 700.f))
				|| Nav->ProjectPointToNavigation(Loc, Proj, FVector(4000.f, 4000.f, 1500.f)))
			{
				Safe = Proj.Location + FVector(0.f, 0.f, 30.f);
				bGot = true;
			}
		}
	}
	if (!bGot)
	{
		if (!FindFloorAt(LastGroundLoc, Safe))
		{
			if (!FindFloorAt(InitialSpawnLoc, Safe)) { Safe = InitialSpawnLoc + FVector(0.f, 0.f, 100.f); }
		}
	}

	// Eerst teleporteren, dan op lopen zetten + vloer-check forceren. Forceer ook zwaartekracht +
	// capsule-collision terug aan (voor het geval iets ze had uitgezet -> 'eeuwig zweven'). Altijd RECHTOP
	// (alleen yaw): een scheve capsule detecteert geen vloer en blijft anders vallen.
	if (GetCapsuleComponent()) { GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); }
	Move->GravityScale = 1.0f;
	if (AWorldSettings* WS = GetWorldSettings())
	{
		if (FMath::Abs(WS->GetGravityZ()) < 1.f) { WS->bGlobalGravitySet = true; WS->GlobalGravityZ = -980.f; }
	}
	Move->StopMovementImmediately();
	Move->Velocity = FVector::ZeroVector;
	const FRotator UprightYaw(0.f, GetActorRotation().Yaw, 0.f);
	TeleportTo(Safe, UprightYaw, false, true);
	Move->SetMovementMode(MOVE_Walking);
	Move->bForceNextFloorCheck = true;
	UWeedToast::NotifyPawn(this, -1, 2.5f, FColor::Yellow,
		bManual ? TEXT("Unstuck - moved to the nearest road.") : TEXT("Recovered your position (you got stuck)."));
}

void AThePlugSIMCharacter::WeedUnstuck()
{
	if (!IsLocallyControlled()) { return; }
	RecoverToSafe(true); // handmatige reset -> dichtstbijzijnde begaanbare plek (weg), niet naar huis
}

void AThePlugSIMCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
	// De noclip-collision volgt de (gerepliceerde) vlieg-modus, zodat host EN client dezelfde staat tonen:
	// een vliegende co-op-speler gaat overal door muren (geen "zwevend mét collision/sliding" bij de ander).
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		const bool bFlying = GetCharacterMovement() && GetCharacterMovement()->MovementMode == MOVE_Flying;
		Cap->SetCollisionEnabled(bFlying ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
		Cap->SetCollisionResponseToChannel(ECC_WorldStatic, bFlying ? ECR_Ignore : ECR_Block);
		Cap->SetCollisionResponseToChannel(ECC_WorldDynamic, bFlying ? ECR_Ignore : ECR_Block);
	}
}

void AThePlugSIMCharacter::UpdateProxyAnim(float DeltaSeconds)
{
	// Mijn EIGEN pawn gebruikt de ABP/FP-view -> geen fallback. Elke ANDERE speler die ik zie (host ziet de
	// joiner als ROLE_Authority, een client ziet 'm als SimulatedProxy - beide zijn niet-lokaal) krijgt onze
	// single-node walk/idle/jump/telefoon. Lazy: zet de single-node-modus zodra we 'm voor het eerst tikken.
	if (IsLocallyControlled()) { return; }
	USkeletalMeshComponent* M = GetMesh();
	if (!M || (!ProxyIdle && !ProxyWalk)) { return; }
	if (!bProxyAnim)
	{
		M->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		bProxyAnim = true;
		ProxyAnimState = -1; // forceer de eerste pose-apply
	}
	// 'Beweegt' bepalen uit de positie (de capsule springt op net-updates) en kort vasthouden.
	const FVector Cur = GetActorLocation();
	if (bHasProxyPrev)
	{
		FVector D = Cur - ProxyPrevLoc; D.Z = 0.f;
		if (D.SizeSquared() > 4.f) { ProxyMoveHold = 0.2f; }
		else if (ProxyMoveHold > 0.f) { ProxyMoveHold -= DeltaSeconds; }
	}
	ProxyPrevLoc = Cur; bHasProxyPrev = true;

	// Telefoon open (gerepliceerd)? -> 'texting'-pose tonen wanneer je stilstaat.
	bool bPhone = false;
	if (const UPhoneClientComponent* Ph = FindComponentByClass<UPhoneClientComponent>()) { bPhone = Ph->IsPhoneOpenReplicated(); }

	int32 NewState = 0; // idle
	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling()) { NewState = 2; } // lucht (mode repliceert)
	else if (ProxyMoveHold > 0.f) { NewState = 1; } // lopen
	else if (bPhone && ProxyPhone) { NewState = 3; } // stilstaan + telefoon = texting
	if (NewState == ProxyAnimState) { return; }
	ProxyAnimState = NewState;
	UAnimSequence* Seq = (NewState == 2) ? ProxyJump : (NewState == 3) ? ProxyPhone : (NewState == 1) ? ProxyWalk : ProxyIdle;
	if (!Seq) { Seq = ProxyIdle; }
	if (Seq) { M->PlayAnimation(Seq, true); }
}

void AThePlugSIMCharacter::ServerDropActiveItem_Implementation()
{
	if (!Inventory) { return; }
	const int32 Sid = Inventory->GetActiveStackId();
	const int32 Idx = Inventory->FindStackById(Sid);
	if (!Inventory->GetStacks().IsValidIndex(Idx)) { return; }
	const FInventoryStack St = Inventory->GetStacks()[Idx];
	if (St.ItemId.IsNone() || St.Quantity <= 0 || St.ItemId == FName(TEXT("Cash"))) { return; }

	Inventory->RemoveFromStackById(Sid, St.Quantity); // de hele actieve stapel droppen

	FVector Fwd = GetActorForwardVector(); Fwd.Z = 0.f; Fwd = Fwd.GetSafeNormal();
	FVector Loc = GetActorLocation() + Fwd * 90.f;
	Loc.Z -= (GetSimpleCollisionHalfHeight() - 12.f); // bij de voeten neerleggen
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (GetWorld())
	{
		if (AWorldItemPickup* P = GetWorld()->SpawnActor<AWorldItemPickup>(AWorldItemPickup::StaticClass(), FTransform(FRotator::ZeroRotator, Loc), SP))
		{
			P->Setup(St.ItemId, St.Quantity, St.Quality, St.QualityPct);
		}
	}
}

void AThePlugSIMCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AThePlugSIMCharacter, PlayerSkin);
	DOREPLIFETIME(AThePlugSIMCharacter, OutfitTop);
	DOREPLIFETIME(AThePlugSIMCharacter, OutfitLegs);
	DOREPLIFETIME(AThePlugSIMCharacter, OutfitShoes);
	DOREPLIFETIME(AThePlugSIMCharacter, OutfitHair);
	DOREPLIFETIME(AThePlugSIMCharacter, OutfitAcc);
	DOREPLIFETIME(AThePlugSIMCharacter, OutfitNeck);
	DOREPLIFETIME(AThePlugSIMCharacter, OutfitSocks);
}

void AThePlugSIMCharacter::ApplySkinMesh()
{
	// 0 = Manny (man), 1 = Quinn (vrouw): zelfde mannequin-skelet, AnimBP werkt direct.
	// 2/3/4 = Casual-meisjes (Streetwear-pack, vervangt Lola): UE4-mannequin-skelet, compatible gemaakt.
	const TCHAR* Path;
	switch (PlayerSkin)
	{
		case 1:  Path = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"); break;
		case 2:  Path = WeedOutfit::FullBodyPaths[0]; break;
		case 3:  Path = WeedOutfit::FullBodyPaths[1]; break;
		case 4:  Path = WeedOutfit::FullBodyPaths[2]; break;
		case 5:  Path = WeedOutfit::PartAt(0, GetOutfitPart(0), true).Path; break; // male = gekozen complete Tony-look
		default: Path = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"); break;
	}
	USkeletalMesh* Skin = LoadObject<USkeletalMesh>(nullptr, Path);
	if (!Skin) { return; }

	// Third-person body (co-op + jij in 3rd-person) EN first-person mesh (jouw eigen view, hoofd verborgen).
	if (USkeletalMeshComponent* M = GetMesh()) { M->SetSkeletalMeshAsset(Skin); }
	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetSkeletalMeshAsset(Skin);
		// Hoofd verbergen kan veilig: de camera zit op de capsule, niet meer op deze head-bone.
		static const TCHAR* HeadBones[] = { TEXT("head"), TEXT("Head") };
		for (const TCHAR* B : HeadBones)
		{
			if (FirstPersonMesh->GetBoneIndex(FName(B)) != INDEX_NONE)
			{
				FirstPersonMesh->HideBoneByName(FName(B), EPhysBodyOp::PBO_None);
			}
		}
	}

	// Outfit-parts (Wardrobe): oude parts opruimen en de gekozen kleding/haar aanhangen (leader-pose volgt
	// de body). Geldt alleen voor de Casual-skins (2-4); Manny/Quinn hebben (nog) geen losse outfits.
	for (USkeletalMeshComponent* C : OutfitComps) { if (C) { C->DestroyComponent(); } }
	OutfitComps.Reset();
	if (PlayerSkin >= 2 && PlayerSkin <= 4)
	{
		AttachOutfitParts(GetMesh(), false, false);       // female (Casual girls)
		AttachOutfitParts(FirstPersonMesh, true, false);
		SyncOutfitViewFlags();
	}

	// Skins met eigen physics-asset (haar/cloth) -> die bones laten nawapperen. Deferred zodat de physics-state
	// klaar is na de mesh-swap. (Manny/Quinn: de mesh-swap reset de physics, dus niks te doen.)
	if (PlayerSkin >= 2 && PlayerSkin <= 4)
	{
		FTimerHandle Th;
		GetWorldTimerManager().SetTimer(Th, this, &AThePlugSIMCharacter::ApplySoftPhysics, 0.2f, false);
	}
}

void AThePlugSIMCharacter::ApplySoftPhysics()
{
	USkeletalMeshComponent* M = GetMesh();
	if (!M || PlayerSkin < 2) { return; }
	UE_LOG(LogTemp, Warning, TEXT("SOFTPHYS physasset=%s"), M->GetPhysicsAsset() ? TEXT("YES") : TEXT("NULL"));
	M->SetEnableGravity(true);
	int32 Enabled = 0, WithBody = 0;
	const int32 NumBones = M->GetNumBones();
	for (int32 i = 0; i < NumBones; ++i)
	{
		const FName Bn = M->GetBoneName(i);
		const FString S = Bn.ToString().ToLower();
		// Cloth/hair-extremiteiten die mogen nawapperen (geen torso/ledematen -> die blijven door de animatie aangestuurd).
		if (S.Contains(TEXT("hair")) || S.Contains(TEXT("skirt")) || S.Contains(TEXT("skrt")) || S.Contains(TEXT("dress"))
			|| S.Contains(TEXT("cloth")) || S.Contains(TEXT("ribbon")) || S.Contains(TEXT("tail")) || S.Contains(TEXT("strand"))
			|| S.Contains(TEXT("ponytail")) || S.Contains(TEXT("bang")) || S.Contains(TEXT("scarf")) || S.Contains(TEXT("strap"))
			|| S.Contains(TEXT("rok")) || S.Contains(TEXT("haar")))
		{
			if (M->GetBodyInstance(Bn)) { ++WithBody; }
			M->SetAllBodiesBelowSimulatePhysics(Bn, true, true);
			M->SetAllBodiesBelowPhysicsBlendWeight(Bn, 1.0f);
			++Enabled;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("SOFTPHYS done: %d soft-bones, %d met physics-body, van %d totaal"), Enabled, WithBody, NumBones);
}

void AThePlugSIMCharacter::ToggleThirdPerson()
{
	bThirdPerson = !bThirdPerson;
	if (FirstPersonCameraComponent) { FirstPersonCameraComponent->SetActive(!bThirdPerson); }
	if (ThirdPersonCamera)          { ThirdPersonCamera->SetActive(bThirdPerson); }
	if (FirstPersonMesh)            { FirstPersonMesh->SetVisibility(!bThirdPerson, true); } // armen weg in 3rd-person
	if (USkeletalMeshComponent* M = GetMesh())
	{
		M->SetOwnerNoSee(!bThirdPerson); // jouw body zichtbaar voor jezelf in 3rd-person
		// In 3rd-person de body als NORMALE primitive renderen; in FP weer als world-space-representation
		// (anders verbergt het FP-render-systeem 'm voor de owner -> je zou niemand zien).
		M->FirstPersonPrimitiveType = bThirdPerson ? EFirstPersonPrimitiveType::None
		                                            : EFirstPersonPrimitiveType::WorldSpaceRepresentation;
		M->MarkRenderStateDirty();
	}
	SyncOutfitViewFlags(); // outfit-parts dezelfde view-instellingen geven
}

void AThePlugSIMCharacter::OnRep_Skin() { ApplySkinMesh(); }

void AThePlugSIMCharacter::ServerSetSkin_Implementation(uint8 NewSkin)
{
	PlayerSkin = (NewSkin > 5) ? 5 : NewSkin;
	ApplySkinMesh(); // server lokaal toepassen; repliceert naar clients -> OnRep_Skin
}

void AThePlugSIMCharacter::RestoreSkin(uint8 S)
{
	PlayerSkin = (S > 5) ? 5 : S;
	ApplySkinMesh();
}

void AThePlugSIMCharacter::AttachOutfitParts(USkeletalMeshComponent* BodyComp, bool bFirstPerson, bool bMale)
{
	if (!BodyComp) { return; }
	auto Attach = [&](const TCHAR* MeshPath)
	{
		if (!MeshPath) { return; } // "None"-keuze
		USkeletalMesh* PartMesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
		if (!PartMesh) { return; }
		USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this);
		C->SetupAttachment(BodyComp);
		C->RegisterComponent();
		C->SetSkeletalMeshAsset(PartMesh);
		C->SetLeaderPoseComponent(BodyComp); // volgt de body-animatie bone-voor-bone
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OutfitComps.Add(C);
	};
	if (!bMale) { Attach(WeedOutfit::UnderwearPath); } // female-underwear niet op de male-body
	for (int32 SlotIdx = 0; SlotIdx < WeedOutfit::SlotCount(); ++SlotIdx)
	{
		Attach(WeedOutfit::PartAt(SlotIdx, GetOutfitPart(SlotIdx), bMale).Path);
	}
}

void AThePlugSIMCharacter::SyncOutfitViewFlags()
{
	// Parts nemen de zichtbaarheids-instellingen van hun body over (FP/TP, owner-see, schaduw).
	for (USkeletalMeshComponent* C : OutfitComps)
	{
		if (!C) { continue; }
		USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(C->GetAttachParent());
		if (!Parent) { continue; }
		C->SetOnlyOwnerSee(Parent->bOnlyOwnerSee);
		C->SetOwnerNoSee(Parent->bOwnerNoSee);
		C->SetCastShadow(Parent->CastShadow);
		C->FirstPersonPrimitiveType = Parent->FirstPersonPrimitiveType;
		C->SetVisibility(Parent->IsVisible(), false);
		C->MarkRenderStateDirty();
	}
}

void AThePlugSIMCharacter::ServerSetOutfit_Implementation(uint8 Slot, uint8 Index)
{
	const uint8 Clamped = (uint8)FMath::Clamp<int32>(Index, 0, WeedOutfit::PartCount(Slot, PlayerSkin == 5) - 1);
	switch (Slot)
	{
	case 1:  OutfitLegs = Clamped; break;
	case 2:  OutfitShoes = Clamped; break;
	case 3:  OutfitHair = Clamped; break;
	case 4:  OutfitAcc = Clamped; break;
	case 5:  OutfitNeck = Clamped; break;
	case 6:  OutfitSocks = Clamped; break;
	default: OutfitTop = Clamped; break;
	}
	ApplySkinMesh(); // server lokaal; repliceert naar clients -> OnRep_Skin
}

void AThePlugSIMCharacter::RestoreOutfit(uint8 Top, uint8 LegsIdx, uint8 ShoesIdx, uint8 HairIdx)
{
	OutfitTop = Top; OutfitLegs = LegsIdx; OutfitShoes = ShoesIdx; OutfitHair = HairIdx;
	ApplySkinMesh();
}

void AThePlugSIMCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// F9: dev-overlay met positie + mesh-onder-crosshair (en logt de plek naar MarkedSpots.txt).
	// F7: vlieg-modus (markers zetten op hoogte); Space = stijgen, Ctrl = dalen.
	if (IsLocallyControlled())
	{
		if (const APlayerController* PCk = Cast<APlayerController>(GetController()))
		{
			// ALLE F-toetsen (F7 fly/noclip, F9 spot-overlay) zijn dev-tools: alleen in Sandbox/Testing
			// (free-build). In een normale playthrough doen ze niets.
			const AWeedShopGameState* GSdev = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
			const bool bDevKeys = GSdev && GSdev->IsFreeBuild();

			const bool bF9 = bDevKeys && PCk->IsInputKeyDown(EKeys::F9);
			if (bF9 && !bSpotKeyWasDown) { if (Phone) { Phone->ToggleSpotInfo(); } }
			bSpotKeyWasDown = bF9;

			const bool bF7 = bDevKeys && PCk->IsInputKeyDown(EKeys::F7);
			if (bF7 && !bFlyKeyWasDown)
			{
				if (UCharacterMovementComponent* CM = GetCharacterMovement())
				{
					const bool bFlying = CM->MovementMode == MOVE_Flying;
					if (!bFlying)
					{
						CM->MaxFlySpeed = 1400.f;
						CM->BrakingDecelerationFlying = 4096.f;
					}
					// SetMovementMode triggert OnMovementModeChanged -> daar gaat de noclip-collision aan/uit
					// (gerepliceerde mode = zelfde noclip op host EN client).
					CM->SetMovementMode(bFlying ? MOVE_Walking : MOVE_Flying);
					UWeedToast::NotifyPawn(this, -1, 2.f, FColor::Cyan, bFlying ? TEXT("Fly mode OFF") : TEXT("Fly mode ON + noclip (Space = up, Ctrl = down, F7 = off)"));
				}
			}
			bFlyKeyWasDown = bF7;

			// --- Activity-spots (dev-tool) ---
			// F10: blader door de animatie-catalog. Shift+F10: plaats hier een activity-spot (NPC verschijnt op
			//      het gekozen tijdvak en doet die anim, kijkend in je huidige richting).
			// F12: blader door het tijdvak (hele dag/ochtend/middag/avond/nacht). Shift+F12: wis de dichtstbij spot.
			const bool bShift = PCk->IsInputKeyDown(EKeys::LeftShift) || PCk->IsInputKeyDown(EKeys::RightShift);

			// Tijdvak-presets (start,end) + label.
			auto TimeWindow = [](int32 Sel, float& OutStart, float& OutEnd) -> const TCHAR*
			{
				switch (((Sel % 5) + 5) % 5)
				{
					case 1:  OutStart = 6.f;  OutEnd = 12.f; return TEXT("Morning 6-12");
					case 2:  OutStart = 12.f; OutEnd = 18.f; return TEXT("Afternoon 12-18");
					case 3:  OutStart = 18.f; OutEnd = 24.f; return TEXT("Evening 18-24");
					case 4:  OutStart = 22.f; OutEnd = 6.f;  return TEXT("Night 22-6");
					default: OutStart = 0.f;  OutEnd = 24.f; return TEXT("All day");
				}
			};

			const bool bF10 = bDevKeys && PCk->IsInputKeyDown(EKeys::F10);
			if (bF10 && !bActAnimKeyWasDown)
			{
				if (bShift)
				{
					// Plaats een spot op de huidige plek/kijkrichting.
					AActivitySpotManager* Mgr = nullptr;
					for (TActorIterator<AActivitySpotManager> It(GetWorld()); It; ++It) { Mgr = *It; break; }
					if (Mgr)
					{
						float Ts = 0.f, Te = 24.f; const TCHAR* TLbl = TimeWindow(ActSelTime, Ts, Te);
						const int32 Total = Mgr->AddSpotLive(GetActorLocation(), GetControlRotation().Yaw, ActSelAnim, Ts, Te);
						UWeedToast::NotifyPawn(this, -1, 4.f, FColor::Green, FString::Printf(
							TEXT("Activity spot placed: %s @ %s (total %d)"), *ACustomerBase::ActivityAnimLabel(ActSelAnim), TLbl, Total));
					}
					else
					{
						UWeedToast::NotifyPawn(this, -1, 3.f, FColor::Orange, TEXT("No activity manager (host only)."));
					}
				}
				else
				{
					const int32 N = FMath::Max(1, ACustomerBase::ActivityAnimNum());
					ActSelAnim = (ActSelAnim + 1) % N;
					UWeedToast::NotifyPawn(this, -1, 2.5f, FColor::Cyan, FString::Printf(
						TEXT("Activity anim: %s (%d/%d) - Shift+F10 to place"), *ACustomerBase::ActivityAnimLabel(ActSelAnim), ActSelAnim + 1, N));
				}
			}
			bActAnimKeyWasDown = bF10;

			const bool bF12 = bDevKeys && PCk->IsInputKeyDown(EKeys::F12);
			if (bF12 && !bActTimeKeyWasDown)
			{
				if (bShift)
				{
					AActivitySpotManager* Mgr = nullptr;
					for (TActorIterator<AActivitySpotManager> It(GetWorld()); It; ++It) { Mgr = *It; break; }
					const FString Removed = Mgr ? Mgr->RemoveNearestSpot(GetActorLocation()) : FString();
					UWeedToast::NotifyPawn(this, -1, 3.f, Removed.IsEmpty() ? FColor::Orange : FColor::Yellow,
						Removed.IsEmpty() ? TEXT("No activity spot nearby to remove.") : FString::Printf(TEXT("Removed activity spot: %s"), *Removed));
				}
				else
				{
					ActSelTime = (ActSelTime + 1) % 5;
					float Ts = 0.f, Te = 24.f; const TCHAR* TLbl = TimeWindow(ActSelTime, Ts, Te);
					UWeedToast::NotifyPawn(this, -1, 2.5f, FColor::Cyan, FString::Printf(TEXT("Activity time: %s"), TLbl));
				}
			}
			bActTimeKeyWasDown = bF12;

			if (GetCharacterMovement() && GetCharacterMovement()->MovementMode == MOVE_Flying)
			{
				if (PCk->IsInputKeyDown(EKeys::SpaceBar))    { AddMovementInput(FVector::UpVector, 1.f); }
				if (PCk->IsInputKeyDown(EKeys::LeftControl)) { AddMovementInput(FVector::UpVector, -1.f); }
			}
		}
	}
	UpdateProxyAnim(DeltaSeconds); // client-kant: remote speler walk/idle/jump (host gebruikt de ABP)
	TickStuckRecovery(DeltaSeconds);

	// Lange klik: houd de linkermuisknop ~0.7s ingedrukt terwijl de telefoon-app open is en hij
	// sluit (naast Tab en de Home-knop). We lezen de ruwe toetsstatus via de PlayerController zodat
	// het ook werkt als UMG de klik opvangt (klikken op de telefoon zelf).
	if (Phone && Phone->IsOpen())
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (PC->IsInputKeyDown(EKeys::LeftMouseButton))
			{
				PhoneCloseHold += DeltaSeconds;
				if (PhoneCloseHold >= 0.7f) { Phone->Toggle(); PhoneCloseHold = 0.f; }
			}
			else { PhoneCloseHold = 0.f; }
		}
	}
	else { PhoneCloseHold = 0.f; }

	// (Held-item hold-pose staat voor nu UIT: geen AnimBP-push/debug. De AnimBP-bool blijft dus false ->
	//  geen arm-pose. Het item tonen we als viewmodel vóór de camera.)
	bHoldingItem = false;

	// --- Held item 3D-model (viewmodel vóór de camera) + drop (alleen lokale speler) ---
	if (IsLocallyControlled() && Inventory && HeldItemMesh)
	{
		const FName Active = Inventory->GetActiveItemId();
		// Viewmodel UIT: geen item-3D-model meer vóór de camera (zag er niet nice uit). Het echte item-model
		// zie je nog wel zodra je het DROPT (de oppakbare drop-pickup). Hou de held-mesh dus altijd verborgen.
		if (HeldItemMesh->IsVisible() || !LastHeldItemId.IsNone())
		{
			LastHeldItemId = NAME_None;
			PropKit::ClearItemModel(HeldItemMesh);
			HeldItemMesh->SetVisibility(false);
		}

		// Drop-toets (Q): HOUD ingedrukt (~0.5s) om het actieve item te droppen, zodat je niet per ongeluk
		// spullen op de grond legt (co-op: iedereen kan 't oppakken).
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			const bool bUiOpen = Phone && (Phone->IsOpen() || Phone->IsInventoryOpen() || Phone->IsRollOpen() || Phone->IsDealOpen());
			const bool bDown = PC->IsInputKeyDown(EKeys::Q) && !bUiOpen && !Active.IsNone();
			constexpr float DropHoldReq = 0.5f;
			if (bDown)
			{
				DropHoldAccum += DeltaSeconds;
				if (DropHoldAccum >= DropHoldReq && !bDroppedThisHold) { ServerDropActiveItem(); bDroppedThisHold = true; }
			}
			else { DropHoldAccum = 0.f; bDroppedThisHold = false; }
			// Voortgangsbalk in de HUD (zoals roken/rollen/oppakken).
			if (Phone) { Phone->SetDropHoldFrac((bDown && !bDroppedThisHold) ? FMath::Clamp(DropHoldAccum / DropHoldReq, 0.f, 1.f) : 0.f); }
		}
	}

	if (StonedSeconds > 0.f)
	{
		StonedSeconds = FMath::Max(0.f, StonedSeconds - DeltaSeconds);
		if (StonedSeconds <= 0.f) { StonedIntensity = 0.f; StonedXpFrac = 0.f; }
	}

	// Roken = rechtermuisknop inhouden met een joint in de hand. Duidelijke voortgangsbalk via de HUD,
	// zodat je niet per ongeluk je eigen joint oprookt.
	{
		const bool bUiOpen = Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen()
			|| Phone->IsInventoryOpen() || Phone->IsPotUpgradeOpen());
		const bool bJointInHand = Inventory && Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Joint_"));
		if (bRmbDown && !bUiOpen && bJointInHand)
		{
			SmokeHoldTime += DeltaSeconds;
			if (!bSmokeFired && SmokeHoldTime >= SmokeHoldRequired)
			{
				bSmokeFired = true;
				SmokeActiveJoint();
			}
		}
		else
		{
			SmokeHoldTime = 0.f;
		}
		if (Phone)
		{
			Phone->SetSmokeHoldFrac(bSmokeFired ? 0.f : FMath::Clamp(SmokeHoldTime / SmokeHoldRequired, 0.f, 1.f));
			Phone->SetStonedHud(GetStonedFraction(), StonedSeconds, GetStonedIntensity(), GetStonedXpFrac());
		}

		// Rollen: geladen vloei in de hand + rechtermuis inhouden -> rol de joint. (Laden gebeurt via
		// de "Load"-knop in het rol-menu; de lading blijft staan tot je 'm rolt.)
		const bool bPapersInHand = Inventory && Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Papers_"));
		if (bRmbDown && !bUiOpen && bPapersInHand && Phone && Phone->IsRollLoadedUI())
		{
			RollHoldTime += DeltaSeconds;
			if (!bRollFired && RollHoldTime >= RollHoldRequired)
			{
				bRollFired = true;
				Phone->SetRollGrams(Phone->GetRollLoadGramsUI());
				Phone->ConfirmRoll();
				Phone->SetRollLoadedUI(false, 0); // gerold -> open het menu weer (F) om opnieuw te laden
			}
		}
		else
		{
			RollHoldTime = 0.f;
		}
		if (Phone) { Phone->SetRollHoldFrac(bRollFired ? 0.f : FMath::Clamp(RollHoldTime / RollHoldRequired, 0.f, 1.f)); }
		// Stoned = XP-bonus op basis van de THC% van je wiet (niet hoe high je bent), zodat je niet te
		// snel levelt. Een 17%-joint -> +17% XP, max +50%. Server zet de multiplier.
		if (HasAuthority())
		{
			if (AWeedShopGameState* GSx = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
			{
				if (ULevelComponent* Lvx = GSx->GetLeveling())
				{
					Lvx->SetXpMultiplier(1.f + GetStonedXpFrac());
				}
			}
		}
	}

	// Joint overhandigen: korte LMB-hold terwijl je een joint vasthoudt en een klant aankijkt.
	{
		const bool bUiOpenG = Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen()
			|| Phone->IsInventoryOpen() || Phone->IsPotUpgradeOpen() || Phone->IsAtmOpen() || Phone->IsPackOpen());
		const bool bJointG = Inventory && Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Joint_"));
		ACustomerBase* FocusCust = nullptr;
		if (const UInteractionComponent* ICx = FindComponentByClass<UInteractionComponent>())
		{
			FocusCust = Cast<ACustomerBase>(ICx->GetFocusedActor());
		}
		if (bLmbDown && !bUiOpenG && bJointG && FocusCust)
		{
			GiveHoldTime += DeltaSeconds;
			if (!bGiveFired && GiveHoldTime >= GiveHoldRequired)
			{
				bGiveFired = true;
				GiveSample();
			}
		}
		else
		{
			GiveHoldTime = 0.f;
		}
		if (Phone) { Phone->SetGiveHoldFrac(bGiveFired ? 0.f : FMath::Clamp(GiveHoldTime / GiveHoldRequired, 0.f, 1.f)); }
	}

	// Stoned = wat extra motion blur + lichte vaagheid, zodat rondkijken "high" aanvoelt.
	if (FirstPersonCameraComponent)
	{
		FPostProcessSettings& PP = FirstPersonCameraComponent->PostProcessSettings;
		const float I = GetStonedIntensity();
		if (I > 0.f)
		{
			PP.bOverride_MotionBlurAmount = true;   PP.MotionBlurAmount = 0.35f + I * 0.65f;
			PP.bOverride_MotionBlurMax = true;      PP.MotionBlurMax = 8.f + I * 30.f;
			PP.bOverride_VignetteIntensity = true;  PP.VignetteIntensity = 0.35f + I * 0.55f;
			PP.bOverride_SceneFringeIntensity = true; PP.SceneFringeIntensity = I * 3.5f;
		}
		else
		{
			PP.bOverride_MotionBlurAmount = false;
			PP.bOverride_MotionBlurMax = false;
			PP.bOverride_VignetteIntensity = false;
			PP.bOverride_SceneFringeIntensity = false;
		}
	}
}

void AThePlugSIMCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AThePlugSIMCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AThePlugSIMCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AThePlugSIMCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AThePlugSIMCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AThePlugSIMCharacter::LookInput);
	}

	// Alle losse gameplaytoetsen (incl. de herbindbare) in één plek, zodat we ze kunnen herbinden.
	BindGameplayKeys(PlayerInputComponent);

	if (!Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		UE_LOG(LogThePlugSIM, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AThePlugSIMCharacter::BindGameplayKeys(UInputComponent* Input)
{
	if (!Input) { return; }
	UControlSettings* CS = UControlSettings::Get();

	// Herbindbare acties: bind zowel de MAIN- als de ALT-toets (als die geldig is).
	UPhoneClientComponent* Ph = Phone.Get();
	UBuildComponent* B = Build.Get();
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const bool bAlt = (Slot == 1);
		if (Ph)
		{
			const FKey KPhone = CS->GetKey(TEXT("Phone"), bAlt);
			if (KPhone.IsValid()) { Input->BindKey(KPhone, IE_Pressed, Ph, &UPhoneClientComponent::Toggle); }
			const FKey KTab = CS->GetKey(TEXT("PhoneTab"), bAlt);
			if (KTab.IsValid()) { Input->BindKey(KTab, IE_Pressed, Ph, &UPhoneClientComponent::CycleTab); }
			const FKey KInv = CS->GetKey(TEXT("Inventory"), bAlt);
			if (KInv.IsValid()) { Input->BindKey(KInv, IE_Pressed, Ph, &UPhoneClientComponent::ToggleInventory); }
		}
		// Interact-toets = zelfde gedrag als de linkermuisknop (interact/gebruik/plaatsen/joint-geven).
		const FKey KInteract = CS->GetKey(TEXT("Interact"), bAlt);
		if (KInteract.IsValid())
		{
			Input->BindKey(KInteract, IE_Pressed,  this, &AThePlugSIMCharacter::OnPrimaryClick);
			Input->BindKey(KInteract, IE_Released, this, &AThePlugSIMCharacter::OnPrimaryReleased);
		}
		const FKey KLoad = CS->GetKey(TEXT("RollLoad"), bAlt);
		if (KLoad.IsValid()) { Input->BindKey(KLoad, IE_Pressed, this, &AThePlugSIMCharacter::OnLoadKey); }
		if (B)
		{
			const FKey KRot = CS->GetKey(TEXT("Rotate"), bAlt);
			if (KRot.IsValid()) { Input->BindKey(KRot, IE_Pressed, B, &UBuildComponent::RotatePlacement); }
		}
		const FKey KUpg = CS->GetKey(TEXT("PotUpgrade"), bAlt);
		if (KUpg.IsValid()) { Input->BindKey(KUpg, IE_Pressed, this, &AThePlugSIMCharacter::OpenPotUpgradeUI); }
	}

	// Vaste toetsen (niet herbindbaar): cijfers, scroll, muisknoppen.
	Input->BindKey(EKeys::One,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Two,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Three, IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Four,  IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Five,  IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Six,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Seven, IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::Eight, IE_Pressed, this, &AThePlugSIMCharacter::HotbarOrPhoneKey);
	Input->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &AThePlugSIMCharacter::HotbarPrev);
	Input->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AThePlugSIMCharacter::HotbarNext);
	Input->BindKey(EKeys::RightMouseButton, IE_Pressed,  this, &AThePlugSIMCharacter::OnSecondaryPressed);
	Input->BindKey(EKeys::RightMouseButton, IE_Released, this, &AThePlugSIMCharacter::OnSecondaryReleased);
	Input->BindKey(EKeys::LeftMouseButton,  IE_Pressed,  this, &AThePlugSIMCharacter::OnPrimaryClick);
	Input->BindKey(EKeys::LeftMouseButton,  IE_Released, this, &AThePlugSIMCharacter::OnPrimaryReleased);

	// M: fullscreen stadskaart aan/uit.
	if (Ph) { Input->BindKey(EKeys::M, IE_Pressed, Ph, &UPhoneClientComponent::ToggleMapOverlay); }

	// B: wissel tussen first-person en third-person (om jezelf / je skin te bekijken).
	Input->BindKey(EKeys::B, IE_Pressed, this, &AThePlugSIMCharacter::ToggleThirdPerson);

	// Furniture-authoring hotkeys (sandbox): F8 = templates opslaan (F9 = nu de spot-info-overlay;
	// meubels wissen kan nog via het console-commando WeedClearFurniture),
	// F10 = woning-types-overzicht. Werkt zonder console.
	Input->BindKey(EKeys::F8,  IE_Pressed, this, &AThePlugSIMCharacter::WeedSaveFurniture);
	Input->BindKey(EKeys::F10, IE_Pressed, this, &AThePlugSIMCharacter::WeedFurnitureTypes);
	Input->BindKey(EKeys::F6,  IE_Pressed, this, &AThePlugSIMCharacter::WeedRegisterHome); // woning registreren (dev)

	// ESC: pauze-/menu-scherm. bExecuteWhenPaused zodat je er ook UIT kunt met ESC terwijl de
	// wereld gepauzeerd is (anders worden de pawn-bindings niet uitgevoerd tijdens pauze).
	{
		FInputKeyBinding& EscBind = Input->BindKey(EKeys::Escape, IE_Pressed, this, &AThePlugSIMCharacter::OnPauseKey);
		EscBind.bExecuteWhenPaused = true;
	}
}

void AThePlugSIMCharacter::RefreshKeyBindings()
{
	if (InputComponent)
	{
		InputComponent->KeyBindings.Empty(); // alleen de losse BindKey's; Enhanced-acties blijven staan
		BindGameplayKeys(InputComponent);
	}
}


void AThePlugSIMCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void AThePlugSIMCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void AThePlugSIMCharacter::DoAim(float Yaw, float Pitch)
{
	// Geen camera-kijken terwijl er een UI open is (anders draait de camera mee tijdens muis-gebruik).
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen()
		|| Phone->IsPotUpgradeOpen() || Phone->IsAtmOpen() || Phone->IsPackOpen() || Phone->IsShelfOpen()
		|| Phone->IsPauseOpen() || Phone->IsMainMenuOpen() || Phone->IsSettingsOpen() || Phone->IsMapOpen()))
	{
		return;
	}

	if (GetController())
	{
		// Muisgevoeligheid uit de settings toepassen.
		const float Sens = Phone ? Phone->GetLookSensitivity() : 1.f;
		AddControllerYawInput(Yaw * Sens);
		AddControllerPitchInput(Pitch * Sens);
	}
}

void AThePlugSIMCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AThePlugSIMCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void AThePlugSIMCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}

// Telefoon-logica zit nu in UPhoneClientComponent (zie Phone-component op deze pawn).

void AThePlugSIMCharacter::GiveSample()
{
	// Bepaal lokaal het aangekeken doel en stuur het naar de server.
	if (const UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>())
	{
		if (AActor* Focus = IC->GetFocusedActor())
		{
			ServerGiveSample(Focus);
		}
	}
}

void AThePlugSIMCharacter::GiveJointToCustomer(ACustomerBase* Customer)
{
	// Vanuit het praat-venster: geef precies deze klant een joint (zelfde server-flow als de hold).
	if (Customer) { ServerGiveSample(Customer); }
}

void AThePlugSIMCharacter::ServerGiveSample_Implementation(AActor* Target)
{
	ACustomerBase* Customer = Cast<ACustomerBase>(Target);
	if (!Customer)
	{
		return;
	}

	// Reikwijdte-check.
	if (FVector::DistSquared(GetActorLocation(), Customer->GetActorLocation()) > FMath::Square(400.f))
	{
		return;
	}

	// Een sample is een gedraaide joint. Je geeft de joint die je NU in je hand hebt (geselecteerd op
	// de hotbar) — niet automatisch de beste uit je voorraad.
	if (!Inventory)
	{
		return;
	}
	FName BestJoint = NAME_None;
	int32 BestGrams = 0;
	{
		const FString Hand = Inventory->GetActiveItemId().ToString();
		if (Hand.StartsWith(TEXT("Joint_")) && Hand.EndsWith(TEXT("g")))
		{
			BestJoint = Inventory->GetActiveItemId();
			BestGrams = FCString::Atoi(*Hand.Mid(6)); // "Joint_3g" -> 3
		}
	}
	// Wiet-kwaliteit van de joint (0..1) vóór we 'm weghalen — slechte wiet verslaaft/bindt minder.
	const float WeedQ = FMath::Clamp(Inventory->GetItemQualityPct(BestJoint) / 100.f, 0.f, 1.f);

	if (BestJoint.IsNone() || !Inventory->RemoveItem(BestJoint, 1))
	{
		if (GEngine)
		{
			UWeedToast::NotifyPawn(this,-1, 2.5f, FColor::Orange, TEXT("Hold a joint in your hand (select it on the hotbar) — roll one first (R)."));
		}
		return;
	}

	// Effectieve kwaliteit = wiet-kwaliteit geschaald met het aantal gram (zelfde formule als de
	// joint-sterkte): een dun jointje voelt zwakker en bindt/verslaaft daardoor minder.
	const float Quality = UPhoneClientComponent::JointIntensity(BestGrams, 0.f, WeedQ * 100.f);
	float LoyGain = 4.f + Quality * 12.f;   // top-joint ~16, brak ~4
	float AddGain = 3.f + Quality * 9.f;    // slechte wiet verslaaft nauwelijks
	float RespGain = 1.f + Quality * 4.f;

	// Kieskeurigheid: weinig-verslaafde mensen (locals/kenners) lusten geen slappe joint.
	const bool bPicky = Customer->Addiction < 20.f;
	if (bPicky && Quality < 0.5f)
	{
		LoyGain = -3.f; RespGain = -2.f; AddGain = 1.f;
		if (GEngine)
		{
			UWeedToast::NotifyPawn(this,-1, 3.f, FColor::Orange,
				FString::Printf(TEXT("%s found the joint too weak."), *Customer->NpcId.ToString()));
		}
	}

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS && GS->GetNpcRegistry() && !Customer->NpcId.IsNone())
	{
		float R = 0.f, L = 0.f, A = 0.f; FText N;
		if (GS->GetNpcRegistry()->GetStats(Customer->NpcId, R, L, A, N))
		{
			GS->GetNpcRegistry()->ApplyStats(Customer->NpcId, R + RespGain, L + LoyGain, A + AddGain);
		}
	}
	Customer->Respect = FMath::Clamp(Customer->Respect + RespGain, 0.f, 100.f);
	Customer->Loyalty = FMath::Clamp(Customer->Loyalty + LoyGain, 0.f, 100.f);
	Customer->Addiction = FMath::Clamp(Customer->Addiction + AddGain, 0.f, 100.f);

	// Straat-dealen is riskant -> heat omhoog.
	if (GS && GS->GetHeat())
	{
		GS->GetHeat()->AddHeat(5.f);
	}

	const bool bLikedIt = !(bPicky && Quality < 0.5f);

	// NPC praat terug in het venster.
	{
		static const TCHAR* Good[] = { TEXT("Ahh, that's the good stuff. Respect."), TEXT("Damn, that hits nice!"), TEXT("Yesss, exactly what I needed."), TEXT("That's fire, my man.") };
		static const TCHAR* Bad[] = { TEXT("Pfff, that's weak, bro."), TEXT("Meh... barely felt that."), TEXT("Come on, I've had better."), TEXT("That ain't it, man.") };
		Customer->Say(bLikedIt ? Good[FMath::RandRange(0, 3)] : Bad[FMath::RandRange(0, 3)]);
	}

	// Een GOEDE joint die ze lekker vinden maakt 'm meteen koop-bereid: hij vraagt dan om meer (grammen).
	if (bLikedIt && Quality >= 0.45f && Customer->Addiction < Customer->AddictionToBuy)
	{
		Customer->Addiction = Customer->AddictionToBuy;
		if (GS && GS->GetNpcRegistry() && !Customer->NpcId.IsNone())
		{
			float R2 = 0.f, L2 = 0.f, A2 = 0.f; FText N2;
			if (GS->GetNpcRegistry()->GetStats(Customer->NpcId, R2, L2, A2, N2))
			{
				GS->GetNpcRegistry()->ApplyStats(Customer->NpcId, R2, L2, FMath::Max(A2, Customer->AddictionToBuy));
			}
		}
	}

	// Of 'ie nú wil kopen wordt bepaald door z'n stats: genoeg verslaafd (prospect -> koper) en niet op
	// cooldown -> vraagt meteen grammen.
	const bool bConverted = Customer->RefreshProspect(); // prospect -> koper als verslaving hoog genoeg
	const bool bIsBuyer = (Customer->State == ECustomerState::WantsToOrder || Customer->State == ECustomerState::Negotiating);
	const bool bOnCooldown = (GS && GS->GetNpcRegistry() && !Customer->NpcId.IsNone()) ? GS->GetNpcRegistry()->IsOnCooldown(Customer->NpcId) : false;

	bool bWantsNow = false;
	if (bLikedIt && bIsBuyer && !bOnCooldown)
	{
		// Zoek verkoopbare wiet: verpakte Bag_ heeft voorrang (dat kopen klanten), anders gedroogde Bud_.
		FName WantId = NAME_None;
		for (const FInventoryStack& St : Inventory->GetStacks())
		{
			if (St.Quantity > 0 && St.ItemId.ToString().StartsWith(TEXT("Bag_"))) { WantId = St.ItemId; break; }
		}
		if (WantId.IsNone())
		{
			for (const FInventoryStack& St : Inventory->GetStacks())
			{
				if (St.Quantity > 0 && St.ItemId.ToString().StartsWith(TEXT("Bud_"))) { WantId = St.ItemId; break; }
			}
		}
		if (!WantId.IsNone())
		{
			Customer->DesiredProductId = WantId;
			Customer->DesiredQuantity = FMath::Clamp(Inventory->GetQuantity(WantId), 1, FMath::RandRange(2, 5));
			Customer->BecomeBuyerNow();
			bWantsNow = true;
		}
	}

	// XP voor het werven: klein per sample, bonus als je iemand net omzette naar koper.
	if (GS && GS->GetLeveling())
	{
		GS->GetLeveling()->AddXP(bConverted ? 25 : 3);
	}

	if (GEngine && bLikedIt)
	{
		if (bWantsNow)
		{
			UWeedToast::NotifyPawn(this,-1, 4.f, FColor::Green,
				FString::Printf(TEXT("\"That's good! Sell me %dg of that.\""), Customer->DesiredQuantity));
		}
		else if (bConverted)
		{
			UWeedToast::NotifyPawn(this,-1, 4.f, FColor::Green,
				TEXT("\"Damn, that's good - I'm hooked. Got any to sell?\""));
		}
		else
		{
			// Nog niet warm genoeg: laat de voortgang naar 'koper' + 'nummer' zien.
			float R = 0.f, L = 0.f, A = 0.f; FText N;
			float NeedRespect = 45.f;
			bool bUnlocked = false;
			if (GS && GS->GetNpcRegistry() && !Customer->NpcId.IsNone())
			{
				GS->GetNpcRegistry()->GetStats(Customer->NpcId, R, L, A, N);
				NeedRespect = GS->GetNpcRegistry()->UnlockRespect;
				bUnlocked = GS->GetNpcRegistry()->IsUnlocked(Customer->NpcId);
			}
			const FString NumHint = bUnlocked ? TEXT("") : FString::Printf(TEXT("  -  respect %.0f/%.0f for their number"), R, NeedRespect);
			UWeedToast::NotifyPawn(this,-1, 3.f, FColor::Green,
				FString::Printf(TEXT("Sample given (relationship+). Addiction %.0f/%.0f to start buying.%s"), A, Customer->AddictionToBuy, *NumHint));
		}
	}
}

void AThePlugSIMCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (UCapsuleComponent* Cap = GetCapsuleComponent()) { Cap->OnComponentHit.AddDynamic(this, &AThePlugSIMCharacter::OnCapsuleBump); }

	ApplySkinMesh(); // skin toepassen (default man; save-restore/keuze overschrijft via RestoreSkin/OnRep)

	// Spawn-plek onthouden als gegarandeerd-veilige terugval voor anti-stuck.
	InitialSpawnLoc = GetActorLocation();
	LastGroundLoc = InitialSpawnLoc;

	// Held item als viewmodel VÓÓR de camera (de in-de-hand-pose is voor nu uitgezet).
	bHeldOnHandBone = false;

	// Co-op-animatie: Update Rate Optimization op het third-person lichaam UIT, en altijd de pose tikken.
	// URO skipt anim-updates op remote spelers (simulated proxies) -> die "glijden" tot een sprong de
	// update forceert. Uitzetten houdt de standaard-template-AnimBP vloeiend voor andere spelers.
	if (USkeletalMeshComponent* M = GetMesh())
	{
		M->bEnableUpdateRateOptimizations = false;
		M->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

		// Third-person lichaam (wat co-op-spelers van JOU zien) op de capsule-BODEM zetten: zonder offset
		// staat de mesh op het capsule-midden -> voeten zweven ~96cm (co-op "zwevende speler"). In BeginPlay
		// (na spawn) zodat het de spawn-collision niet raakt.
		M->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -96.f), FRotator(0.f, -90.f, 0.f));
		// (De walk/idle/jump-fallback voor ANDERE spelers wordt lazy aangezet in UpdateProxyAnim zodra die
		//  pawn niet-lokaal blijkt - werkt zo op host EN client, ongeacht Authority/SimulatedProxy.)
	}

	// Forceer de movement-instellingen at RUNTIME (een Blueprint-default kan de constructor overschrijven).
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->GravityScale = 1.0f;
		Move->JumpZVelocity = 450.0f;
		Move->AirControl = 0.5f;
		if (Move->MovementMode == MOVE_None) { Move->SetMovementMode(MOVE_Walking); }
	}

	// BELANGRIJK: GravityScale vermenigvuldigt de WERELD-zwaartekracht. Als de map "Global Gravity Z"
	// op 0 heeft (of de level-default), kom je na een sprong nooit terug. Forceer normale zwaartekracht.
	if (AWorldSettings* WS = GetWorldSettings())
	{
		if (FMath::Abs(WS->GetGravityZ()) < 1.f)
		{
			WS->bGlobalGravitySet = true;
			WS->GlobalGravityZ = -980.f;
		}
	}

	// Herbind de gameplaytoetsen zodra de speler ze in de telefoon (Settings -> Controls) wijzigt.
	if (IsLocallyControlled())
	{
		UControlSettings::Get()->OnBindingsChanged.AddUObject(this, &AThePlugSIMCharacter::RefreshKeyBindings);
	}

	// Startvoorraad (concept): wat vloei, een paar gram wiet en een zaadje.
	if (HasAuthority() && Inventory)
	{
		// Lean begin: net genoeg om je eerste plant te kweken en je eerste joint te draaien.
		Inventory->AddItem(FName(TEXT("Papers_Small")), 3);
		Inventory->AddItem(FName(TEXT("Seed_SilverHaze")), 1);
		Inventory->AddItem(FName(TEXT("Soil_Basic")), 1);
		Inventory->AddItem(FName(TEXT("WaterBottle_Plastic")), 1);
		Inventory->AddItem(FName(TEXT("Pot_Broken")), 1);
		// (Geen Sink/ATM/meubels in de inventory: gootsteen + tafel/koelkast/matras staan al in elke
		// woning, en een ATM in elke winkel. NIET meer opvegen naar de inventory -> anders kreeg je ze
		// dubbel in je tas en stond je huis leeg.)

		// Om te kunnen beginnen krijg je wél het droogrek en de inpaktafel in je inventory, zodat je ze
		// zelf in je huis kunt neerzetten (drogen + verpakken hoort bij de basis-gameplay). LET OP: de
		// CHEAP rack is de level-1 starter; DryRack_Std unlockt pas op level 12 (dus die NIET als start).
		Inventory->AddItem(FName(TEXT("DryRack_Cheap")), 1);
		Inventory->AddItem(FName(TEXT("Bench_Pack")), 1);
		// 10 kleine zakjes (2g) om je eerste oogst te verpakken en te verkopen. Grotere zakjes ontgrendel je later.
		Inventory->AddItem(FName(TEXT("Cont_Bag2")), 10);
	}

	// (De ATM is nu een plaatsbaar item in je inventory: zet 'm zelf neer waar je wilt, binnen of buiten.)

	// Cash = fysiek briefgeld in de inventory: houd het gelijk aan MIJN cash-saldo (server).
	if (HasAuthority() && Inventory && Economy)
	{
		Economy->OnBalanceChanged.AddDynamic(this, &AThePlugSIMCharacter::OnCashChanged);
		Inventory->SetCashDisplayEuros((int64)Economy->GetBalanceEuros()); // begin-sync

		// Mijn inkomsten tellen mee voor de gedeelde milestones/fase.
		if (AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
		{
			if (UMilestoneComponent* Ms = GS->GetMilestones())
			{
				Economy->OnMoneyEarned.AddDynamic(Ms, &UMilestoneComponent::HandleMoneyEarned);
			}
		}

		// Als er al een save geladen is (host deed Continue/Load), herstel dan deze speler
		// (op username) — zo krijgt een co-op vriend die later joint z'n spullen terug.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (USaveGameSubsystem* Sv = GI->GetSubsystem<USaveGameSubsystem>())
			{
				Sv->RestorePlayerByPawn(this);
			}
		}
	}

	// Zorg dat de lokale speler z'n HUD-widgets (incl. de save-indicator) meteen heeft — ook
	// co-op clients, zodat ze "Saving/Saved" altijd zien, waar ze ook zijn.
	if (IsLocallyControlled() && Phone)
	{
		// Zet Slate's Tab-/pijl-focusnavigatie uit. Anders "eet" UMG de Tab-toets voor focus-
		// navigatie zodra je een knop in de telefoon hebt aangeklikt, en bereikt Tab de
		// toggle-binding niet meer (telefoon ging wel open maar niet meer dicht met Tab).
		if (FSlateApplication::IsInitialized())
		{
			TSharedRef<FNavigationConfig> Nav = MakeShared<FNavigationConfig>();
			Nav->bTabNavigation = false;
			Nav->bKeyNavigation = false;
			Nav->bAnalogNavigation = false;
			FSlateApplication::Get().SetNavigationConfig(Nav);
		}
		Phone->EnsureWidget();
		// Host/standalone: na een New Game/Load-herlaad voert de save-subsystem de actie uit
		// (verse start of save toepassen) en tonen we GEEN titelscherm; anders (kale boot) wel.
		bool bShownMenu = false;
		if (GetNetMode() != NM_Client)
		{
			bool bHandled = false;
			if (UGameInstance* GI = GetGameInstance())
			{
				if (USaveGameSubsystem* Sv = GI->GetSubsystem<USaveGameSubsystem>())
				{
					bHandled = Sv->RunPendingOnWorldReady();
				}
			}
			if (!bHandled) { Phone->ShowMainMenu(); bShownMenu = true; }
		}
		// Geen menu getoond (verse start / load / co-op client) -> meteen de gameplay-input-modus
		// zetten zodat de muis gelockt is (anders kon je alleen met LMB rondkijken).
		if (!bShownMenu) { Phone->RefreshInputMode(); }
	}

	// Enhanced Input: de template-actie IA_Interact zit in IMC_Default op E en botst met onze
	// Inventory=E. Interact gaat nu via F (= LMB), dus haal IA_Interact uit de mapping zodat E
	// alleen nog de inventory opent. Runtime, elke sessie (geen asset-save nodig).
	if (IsLocallyControlled())
	{
		if (UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default")))
		{
			if (UInputAction* IA = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Interact.IA_Interact")))
			{
				IMC->UnmapAllKeysFromAction(IA);
			}
		}
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsys = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
			{
				Subsys->RequestRebuildControlMappings();
			}
		}
	}

	// Dag/nacht-belichting + lantaarnpalen: één lokale controller per speler (cosmetisch, niet
	// gerepliceerd - de klok zelf is al gerepliceerd). Niet dubbel spawnen.
	if (IsLocallyControlled() && GetWorld())
	{
		const FString MapPath = GetWorld()->GetOutermost()->GetName();
		const bool bExternalMap = MapPath.StartsWith(TEXT("/Game/CityBeachStrip"));

		bool bHasController = false;
		for (TActorIterator<ADayNightController> It(GetWorld()); It; ++It) { bHasController = true; break; }
		if (!bHasController) // pack-maps: MINIMAL-modus (stock-look overdag, dimmen 's nachts)
		{
			GetWorld()->SpawnActor<ADayNightController>(ADayNightController::StaticClass(), FTransform::Identity);
		}

		// Procedurele stad rond het speelgebied (deterministisch, lokaal gebouwd -> elke speler dezelfde stad).
		// NIET in externe/asset-pack-maps (bv. /Game/CityBeachStrip/...) - daar willen we de map zelf zien,
		// niet onze blokken-stad er dwars doorheen.
		bool bHasCity = false;
		for (TActorIterator<ACityGenerator> It(GetWorld()); It; ++It) { bHasCity = true; break; }
		if (!bHasCity && !bExternalMap)
		{
			GetWorld()->SpawnActor<ACityGenerator>(ACityGenerator::StaticClass(), FTransform::Identity);
		}
		// Externe pack-map: maak de statische deur-bladen werkend (open/dicht + NPC-auto-open).
		// LET OP: hier GEEN free-build meer forceren! CityBeachStrip is nu de gewone speelmap, dus dat
		// zette in ELKE modus (ook Normal) alle dev-tools aan. Free-build komt nu puur uit de start-mode
		// (Sandbox/Testing via ApplyStartMode) of uit de geladen save.
		if (bExternalMap)
		{
			bool bHasRetro = false;
			for (TActorIterator<ADoorRetrofitter> It(GetWorld()); It; ++It) { bHasRetro = true; break; }
			if (!bHasRetro)
			{
				GetWorld()->SpawnActor<ADoorRetrofitter>(ADoorRetrofitter::StaticClass(), FTransform::Identity);
			}
		}

		// Stad staat er nu (BeginPlay bouwt 'm synchroon) -> ken meteen het starter-flatje toe en zet de
		// speler er DIRECT in, vóór het eerste frame. Zo zie je geen seconde het park meer (geen overzet).
		if (Phone) { Phone->PropertyTick(); }
	}
}

void AThePlugSIMCharacter::OnCashChanged(int64 NewCashCents)
{
	if (HasAuthority() && Inventory)
	{
		Inventory->SetCashDisplayEuros(NewCashCents / 100);
	}
}

void AThePlugSIMCharacter::WeedMarkSpot(const FString& Label)
{
	const FVector L = GetActorLocation();
	const FRotator R = GetControlRotation();
	const FString MapPath = GetWorld() ? GetWorld()->GetOutermost()->GetName() : TEXT("?");
	const FString Line = FString::Printf(TEXT("%s | map=%s | pos=(%.0f, %.0f, %.0f) | yaw=%.0f") TEXT("\n"),
		Label.IsEmpty() ? TEXT("spot") : *Label, *MapPath, L.X, L.Y, L.Z, R.Yaw);
	const FString File = FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt");
	FFileHelper::SaveStringToFile(Line, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
	UWeedToast::NotifyPawn(this, -1, 3.f, FColor::Cyan,
		FString::Printf(TEXT("Spot marked: %s (%.0f, %.0f, %.0f)"), Label.IsEmpty() ? TEXT("spot") : *Label, L.X, L.Y, L.Z));
}

void AThePlugSIMCharacter::WeedSaveFurniture()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Dev-tool (F8): alleen in Sandbox/Testing (free-build) - stil in een normale playthrough.
	const AWeedShopGameState* GSd = W->GetGameState<AWeedShopGameState>();
	if (!GSd || !GSd->IsFreeBuild()) { return; }
	if (!HasAuthority())
	{
		UWeedToast::Notify(-1, 3.f, FColor::Orange, TEXT("WeedSaveFurniture: run this on the host."));
		return;
	}
	ACityGenerator* City = nullptr;
	for (TActorIterator<ACityGenerator> It(W); It; ++It) { City = *It; break; }
	if (!City) { UWeedToast::Notify(-1, 3.f, FColor::Red, TEXT("No city found.")); return; }

	const int32 Types = FurnitureTemplates::SaveFromWorld(W, City);
	UWeedToast::Notify(-1, 5.f, Types > 0 ? FColor::Green : FColor::Orange,
		Types > 0 ? FString::Printf(TEXT("Furniture templates saved (%d type(s))."), Types)
				  : TEXT("Nothing to save: place furniture inside a home first."));
}

void AThePlugSIMCharacter::WeedRegisterHome()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Dev-tool (F6): alleen in Sandbox/Testing (free-build).
	const AWeedShopGameState* GSd = W->GetGameState<AWeedShopGameState>();
	if (!GSd || !GSd->IsFreeBuild()) { return; }
	for (TActorIterator<ADoorRetrofitter> It(W); It; ++It) { It->RegisterHomeAtPlayer(this); return; }
	UWeedToast::Notify(-1, 3.f, FColor::Orange, TEXT("WeedRegisterHome: geen DoorRetrofitter (alleen op de pack-map)."));
}

void AThePlugSIMCharacter::WeedClearFurniture()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	if (!HasAuthority())
	{
		UWeedToast::Notify(-1, 3.f, FColor::Orange, TEXT("WeedClearFurniture: run this on the host."));
		return;
	}
	const int32 N = FurnitureTemplates::ClearPlaced(W);

	// Authoring-reset: geef de volledige meubelset terug in de inventory (incl. de sink), zodat je
	// meteen schoon opnieuw kunt inrichten zonder iets te hoeven oppakken.
	if (Inventory)
	{
		auto Give = [this](const TCHAR* Id, int32 Num) { Inventory->AddItem(FName(Id), Num); };
		Give(TEXT("Table"), 5);   Give(TEXT("Fridge"), 5);    Give(TEXT("Mattress"), 5); Give(TEXT("Sink"), 5);
		Give(TEXT("DryRack_Std"), 3); Give(TEXT("Bench_Pack"), 3); Give(TEXT("Shelf"), 3); Give(TEXT("Chest"), 3);
		Give(TEXT("Lamp_Ceiling"), 3); Give(TEXT("Atm"), 2);
	}

	UWeedToast::Notify(-1, 3.f, FColor::Cyan, FString::Printf(TEXT("%d furniture cleared + furniture set (incl. sink) back in inventory."), N));
}

void AThePlugSIMCharacter::WeedFurnitureTypes()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Dev-tool (F10): alleen in Sandbox/Testing (free-build).
	const AWeedShopGameState* GSd = W->GetGameState<AWeedShopGameState>();
	if (!GSd || !GSd->IsFreeBuild()) { return; }
	ACityGenerator* City = nullptr;
	for (TActorIterator<ACityGenerator> It(W); It; ++It) { City = *It; break; }
	if (!City) { UWeedToast::Notify(-1, 3.f, FColor::Red, TEXT("No city found.")); return; }

	TMap<FString, int32> Counts;
	FurnitureTemplates::CountHomeTypes(City, Counts);
	TMap<FString, TArray<FFurnitureEntry>> Templates;
	FurnitureTemplates::LoadTemplates(Templates);

	TArray<FString> Keys; Counts.GetKeys(Keys); Keys.Sort();
	int32 Done = 0;
	for (const FString& K : Keys)
	{
		const bool bHas = Templates.Contains(K) && Templates[K].Num() > 0;
		if (bHas) { ++Done; }
		UE_LOG(LogTemp, Display, TEXT("WeedFurnitureType: %-12s %3d homes   %s"),
			*K, Counts[K], bHas ? TEXT("[OK sjabloon]") : TEXT("[TODO]"));
	}
	UWeedToast::Notify(-1, 7.f, FColor::Cyan,
		FString::Printf(TEXT("%d/%d home types furnished (list in log: WeedFurnitureType)."), Done, Keys.Num()));
}

void AThePlugSIMCharacter::ToggleRollUI()
{
	if (Phone)
	{
		Phone->ToggleRollUI();
	}
}

void AThePlugSIMCharacter::HotbarOrPhoneKey(FKey Key)
{
	// Kaart open -> cijfertoetsen doen niets (maar 1 UI tegelijk).
	if (Phone && Phone->IsMapOpen()) { return; }
	// Telefoon open -> catalogus-keuze; anders hotbar-slot selecteren.
	if (Phone && Phone->IsOpen())
	{
		Phone->HandleNumberKey(Key);
		return;
	}
	int32 Index = -1;
	if (Key == EKeys::One)        Index = 0;
	else if (Key == EKeys::Two)   Index = 1;
	else if (Key == EKeys::Three) Index = 2;
	else if (Key == EKeys::Four)  Index = 3;
	else if (Key == EKeys::Five)  Index = 4;
	else if (Key == EKeys::Six)   Index = 5;
	else if (Key == EKeys::Seven) Index = 6;
	else if (Key == EKeys::Eight) Index = 7;
	if (Index >= 0 && Inventory)
	{
		Inventory->SetActiveSlot(Index);
	}
}

void AThePlugSIMCharacter::HotbarPrev()
{
	if (Inventory && Phone && !Phone->IsOpen() && !Phone->IsMapOpen())
	{
		Inventory->CycleActiveSlot(-1);
	}
}

void AThePlugSIMCharacter::HotbarNext()
{
	if (Inventory && Phone && !Phone->IsOpen() && !Phone->IsMapOpen())
	{
		Inventory->CycleActiveSlot(+1);
	}
}

void AThePlugSIMCharacter::OnPrimaryClick()
{
	bLmbDown = true; GiveHoldTime = 0.f; bGiveFired = false;

	// Klik gaat naar de UI als die open is (HUD hit-boxes regelen dat zelf). Ook als een UI-knop
	// deze klik net heeft verwerkt (bv. een paneel sloot) negeren we 'm hier, zodat dezelfde klik
	// niet alsnog de wereld-interactie (en daarmee bv. de deal opnieuw) opent.
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen()
		|| Phone->IsPotUpgradeOpen() || Phone->IsAtmOpen() || Phone->IsPackOpen() || Phone->IsShelfOpen() || Phone->IsPauseOpen() || Phone->IsMainMenuOpen() || Phone->IsMapOpen() || Phone->DidUiConsumeClickRecently()))
	{
		return;
	}
	// In plaats-modus: bevestig de plaatsing.
	if (Build && Build->IsPlacing())
	{
		Build->ConfirmPlacement();
		return;
	}
	// Joint in de hand + je kijkt een klant aan -> dat is een "joint overhandigen" (korte LMB-hold,
	// afgehandeld in Tick). Niet meteen interacten (anders opent de deal).
	const bool bJointInHand = Inventory && Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Joint_"));
	if (UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>())
	{
		if (AActor* Focus = IC->GetFocusedActor())
		{
			if (bJointInHand && Cast<ACustomerBase>(Focus))
			{
				return; // hold-to-give regelt dit
			}
			// ATM -> open lokaal het ATM-scherm (bankieren) i.p.v. een server-interactie.
			if (Cast<AAtm>(Focus))
			{
				if (Phone) { Phone->OpenAtm(); }
				return;
			}
			// Winkelbalie in de stad -> open lokaal de juiste telefoon-shop.
			if (AStoreCounter* Counter = Cast<AStoreCounter>(Focus))
			{
				if (Phone && Counter->HasShop()) { Phone->OpenStore(Counter); } // fullscreen winkel-menu (geen telefoon)
				return;
			}
			// Kledingkast -> open lokaal het outfit-menu.
			if (APlaceableProp* WProp = Cast<APlaceableProp>(Focus))
			{
				FPlaceableDef WDef;
				if (GetPlaceableDef(WProp->ItemId, WDef) && WDef.bIsWardrobe)
				{
					if (Phone) { Phone->OpenWardrobe(); }
					return;
				}
			}
			// Winkelier (verkoper-NPC) -> zelfde als z'n balie: open de shop van DEZE winkel (dichtstbijzijnde balie).
			if (ACustomerBase* Keeper = Cast<ACustomerBase>(Focus))
			{
				if (Keeper->IsShopkeeper())
				{
					AStoreCounter* Best = nullptr; float BestD = 700.f;
					for (TActorIterator<AStoreCounter> It(GetWorld()); It; ++It)
					{
						if (!It->HasShop()) { continue; }
						const float D = FVector::Dist2D(It->GetActorLocation(), Keeper->GetActorLocation());
						if (D < BestD) { BestD = D; Best = *It; }
					}
					if (Phone && Best) { Phone->OpenStore(Best); }
					return;
				}
			}
			// Verpak-tafel -> open lokaal het verpak-menu (met de batch-grootte van deze tafel).
			if (APackBench* Bench = Cast<APackBench>(Focus))
			{
				if (Phone) { Phone->OpenPack(Bench->GetPackPerAction()); }
				return;
			}
			// Opslag-schap -> open lokaal het schap-menu.
			if (AStorageShelf* Shelf = Cast<AStorageShelf>(Focus))
			{
				if (Phone) { Phone->OpenShelf(Shelf); }
				return;
			}
			// Droogrek -> open lokaal het droogrek-menu (ook met lege hand, net als de verpak-tafel).
			if (ADryingRack* Rack = Cast<ADryingRack>(Focus))
			{
				if (Phone) { Phone->OpenDryRack(Rack); }
				return;
			}
			IC->TryInteract();
			return;
		}
	}
	UseActiveItem();
}

void AThePlugSIMCharacter::OnPrimaryReleased()
{
	bLmbDown = false;
	GiveHoldTime = 0.f;
	bGiveFired = false;
	if (Phone) { Phone->SetGiveHoldFrac(0.f); }
}

void AThePlugSIMCharacter::OnPauseKey()
{
	// ESC: staat er ÉÉN of meer UI open (pauze, settings, inventory, telefoon, panelen), dan gaat
	// ALLES in één keer dicht. Staat er niets open, dan opent ESC het pauze-menu.
	// Op het titelscherm doet ESC niets (je kiest daar Start/Continue/Quit).
	if (!Phone || Phone->IsMainMenuOpen()) { return; }
	if (Phone->IsMapOpen()) { Phone->CloseMapOverlay(); return; }
	if (Phone->IsAnyGameUIOpen()) { Phone->CloseAllUI(); }
	else { Phone->TogglePause(); }
}

void AThePlugSIMCharacter::OnInteractKey()
{
	// E doet hetzelfde als links-klikken op wat je aankijkt (pot/klant/ATM) + plaatsen bevestigen,
	// maar gebruikt nooit het actieve hand-item.
	// E sluit OOK een open wereld-paneel (zelfde toets als waarmee je 't opende: droogrek/winkel/schap/verpak/ATM).
	if (Phone)
	{
		if (Phone->IsDryRackOpen()) { Phone->CloseDryRack(); return; }
		if (Phone->IsStoreOpen())   { Phone->CloseStore();   return; }
		if (Phone->IsShelfOpen())   { Phone->CloseShelf();   return; }
		if (Phone->IsPackOpen())    { Phone->ClosePack();    return; }
		if (Phone->IsAtmOpen())     { Phone->CloseAtm();     return; }
	}
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen()
		|| Phone->IsPotUpgradeOpen() || Phone->IsPauseOpen() || Phone->IsMainMenuOpen()))
	{
		return;
	}
	if (Build && Build->IsPlacing())
	{
		Build->ConfirmPlacement();
		return;
	}
	if (UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>())
	{
		if (AActor* Focus = IC->GetFocusedActor())
		{
			if (Cast<AAtm>(Focus))
			{
				if (Phone) { Phone->OpenAtm(); }
				return;
			}
			if (AStoreCounter* Counter = Cast<AStoreCounter>(Focus))
			{
				if (Phone && Counter->HasShop()) { Phone->OpenStore(Counter); } // fullscreen winkel-menu (geen telefoon)
				return;
			}
			// Kledingkast -> open lokaal het outfit-menu.
			if (APlaceableProp* WProp = Cast<APlaceableProp>(Focus))
			{
				FPlaceableDef WDef;
				if (GetPlaceableDef(WProp->ItemId, WDef) && WDef.bIsWardrobe)
				{
					if (Phone) { Phone->OpenWardrobe(); }
					return;
				}
			}
			// Winkelier (verkoper-NPC) -> zelfde als z'n balie: open de shop van DEZE winkel.
			if (ACustomerBase* Keeper = Cast<ACustomerBase>(Focus))
			{
				if (Keeper->IsShopkeeper())
				{
					AStoreCounter* Best = nullptr; float BestD = 700.f;
					for (TActorIterator<AStoreCounter> It(GetWorld()); It; ++It)
					{
						if (!It->HasShop()) { continue; }
						const float D = FVector::Dist2D(It->GetActorLocation(), Keeper->GetActorLocation());
						if (D < BestD) { BestD = D; Best = *It; }
					}
					if (Phone && Best) { Phone->OpenStore(Best); }
					return;
				}
			}
			if (APackBench* Bench = Cast<APackBench>(Focus))
			{
				if (Phone) { Phone->OpenPack(Bench->GetPackPerAction()); }
				return;
			}
			if (AStorageShelf* Shelf = Cast<AStorageShelf>(Focus))
			{
				if (Phone) { Phone->OpenShelf(Shelf); }
				return;
			}
			if (ADryingRack* Rack = Cast<ADryingRack>(Focus))
			{
				if (Phone) { Phone->OpenDryRack(Rack); }
				return;
			}
			IC->TryInteract();
		}
	}
}

void AThePlugSIMCharacter::OnSecondaryPressed()
{
	if (!Phone || !Inventory)
	{
		return;
	}
	bRmbDown = true;
	SmokeHoldTime = 0.f;
	bSmokeFired = false;
	RollHoldTime = 0.f;
	bRollFired = false;
	// Rechtermuis = INHOUDEN: papers (geladen) -> rollen, joint -> roken. Beide in Tick afgehandeld.
}

void AThePlugSIMCharacter::OnSecondaryReleased()
{
	bRmbDown = false;
	SmokeHoldTime = 0.f;
	bSmokeFired = false;
	RollHoldTime = 0.f;
	bRollFired = false;
	if (Phone) { Phone->SetSmokeHoldFrac(0.f); Phone->SetRollHoldFrac(0.f); }
}

void AThePlugSIMCharacter::OnLoadKey()
{
	if (!Phone || !Inventory) { return; }
	if (Phone->IsOpen() || Phone->IsInventoryOpen() || Phone->IsDealOpen() || Phone->IsPotUpgradeOpen() || Phone->IsAtmOpen() || Phone->IsPackOpen()) { return; }

	// F opent (of sluit) het rol-menu zolang je vloei vasthoudt. In het menu kies je het aantal gram
	// en klik je op "Load"; daarna rechtermuis inhouden om te rollen.
	if (Inventory->GetActiveItemId().ToString().StartsWith(TEXT("Papers_")) || Phone->IsRollOpen())
	{
		Phone->ToggleRollUI();
	}
	else if (GEngine)
	{
		UWeedToast::NotifyPawn(this,-1, 2.f, FColor::Orange, TEXT("Hold rolling papers (hotbar) to open the roll menu."));
	}
}

void AThePlugSIMCharacter::UseActiveItem()
{
	if (!Inventory || !Build)
	{
		return;
	}
	// UI open? Niet gebruiken (klik is voor de UI).
	if (Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen() || Phone->IsInventoryOpen() || Phone->IsPotUpgradeOpen()))
	{
		return;
	}
	const FName Item = Inventory->GetActiveItemId();
	if (Item == FName(TEXT("Pot")))
	{
		Build->TogglePotPlacement();
	}
	// Joints rook je NIET met links-klik (dat zou per ongeluk je joint opbranden); dat gaat via
	// rechtermuisknop INHOUDEN (zie OnSecondaryPressed/Released + Tick), met een duidelijke balk.
}

void AThePlugSIMCharacter::SmokeActiveJoint()
{
	if (!Inventory) { return; }
	const FName Joint = Inventory->GetActiveItemId();
	if (!Joint.ToString().StartsWith(TEXT("Joint_"))) { return; }
	ServerSmokeJoint(Joint);
}

void AThePlugSIMCharacter::ServerSmokeJoint_Implementation(FName JointId)
{
	if (!Inventory || !JointId.ToString().StartsWith(TEXT("Joint_"))) { return; }

	// Hoe high: meer gram + hogere THC% + betere kwaliteit = sterker. Backwoods (10g) vol topwiet
	// (~36% THC, 100% kwaliteit) tikt tegen het maximum aan.
	const int32 Grams = FCString::Atoi(*JointId.ToString().Mid(6)); // "Joint_5g" -> 5
	const float Thc = Inventory->GetItemQuality(JointId);            // ~0..36
	const float Q = Inventory->GetItemQualityPct(JointId) / 100.f;   // 0..1

	if (!Inventory->RemoveItem(JointId, 1))
	{
		return;
	}

	const float Intensity = UPhoneClientComponent::JointIntensity(Grams, Thc, Q * 100.f);

	// Roken zelf geeft GEEN XP (alleen oogsten + klanten helpen leveren XP). Wel een XP-BOOST terwijl
	// je high bent, gebaseerd op de THC% van de wiet: 17%-joint -> +17% XP, gemaximeerd op +50%.
	const float XpFrac = FMath::Clamp(Thc / 100.f, 0.f, 0.5f);

	// Stoned-buf: duur schaalt met intensiteit (cap op het maximum).
	const float AddSeconds = Intensity * StonedMaxSeconds;
	const float NewSeconds = FMath::Min(StonedMaxSeconds, StonedSeconds + AddSeconds);
	const float NewIntensity = FMath::Max(StonedIntensity, Intensity);
	const float NewXpFrac = FMath::Max(StonedXpFrac, XpFrac);
	MulticastApplyStoned(NewSeconds, NewIntensity, 0, NewXpFrac);

	if (GEngine)
	{
		UWeedToast::NotifyPawn(this,-1, 3.f, FColor(120, 220, 160),
			FString::Printf(TEXT("You smoke the %dg joint... high %.0f%%  (XP boost active)"), Grams, Intensity * 100.f));
	}
}

void AThePlugSIMCharacter::MulticastApplyStoned_Implementation(float Seconds, float Intensity, int32 XpBonus, float XpFrac)
{
	StonedSeconds = Seconds;
	StonedIntensity = Intensity;
	StonedXpFrac = XpFrac;
	LastSmokeXp = XpBonus;

	// Klein rookwolkje uit het hoofd (cosmetisch, op elke client).
	if (UWorld* World = GetWorld())
	{
		const FVector Head = FirstPersonCameraComponent
			? FirstPersonCameraComponent->GetComponentLocation() + FirstPersonCameraComponent->GetForwardVector() * 22.f
			: GetActorLocation() + FVector(0.f, 0.f, 70.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const int32 Puffs = 1 + FMath::RoundToInt(Intensity * 2.f); // sterker = wat meer wolk
		for (int32 i = 0; i < Puffs; ++i)
		{
			const FVector Off(FMath::FRandRange(-6.f, 6.f), FMath::FRandRange(-6.f, 6.f), FMath::FRandRange(0.f, 8.f));
			World->SpawnActor<ASmokePuff>(ASmokePuff::StaticClass(), Head + Off, FRotator::ZeroRotator, Params);
		}
	}
}

void AThePlugSIMCharacter::OpenPotUpgradeUI()
{
	if (!Phone)
	{
		return;
	}
	if (const UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>())
	{
		if (AGrowPlant* Pot = Cast<AGrowPlant>(IC->GetFocusedActor()))
		{
			// Open de Grow shop op de "Pot Upgrades"-tab (alle potten + tiers op één plek).
			Phone->OpenToApp(1 /*Grow shop*/);
			Phone->SetSupplierCat(8 /*Pot Upgrades*/);
		}
	}
}

void AThePlugSIMCharacter::OnCapsuleBump(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!IsLocallyControlled() || !OtherComp) { return; }
	if (FMath::Abs(Hit.ImpactNormal.Z) > 0.5f) { return; } // vloer/helling: niet interessant
	if (!Phone || !Phone->IsSpotInfoVisible()) { return; }  // alleen met de F9-overlay aan
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now < BumpToastAt) { return; }
	BumpToastAt = Now + 2.f;

	const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(OtherComp);
	FString Name;
	if (SMC && SMC->GetStaticMesh()) { Name = SMC->GetStaticMesh()->GetName(); }
	else
	{
		Name = FString::Printf(TEXT("%s/%s"), *OtherComp->GetClass()->GetName(),
			OtherActor ? *OtherActor->GetName() : TEXT("?"));
	}
	UWeedToast::NotifyPawn(this, -1, 3.f, FColor::Yellow,
		FString::Printf(TEXT("BUMP: %s%s"), *Name, OtherComp->IsVisible() ? TEXT("") : TEXT(" [invisible]")));
}
