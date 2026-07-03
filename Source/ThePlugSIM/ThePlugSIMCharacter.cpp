// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThePlugSIMCharacter.h"
#include "UI/WeedToast.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "UObject/UnrealType.h"
#include "Animation/AnimSequence.h"
#include "UObject/ConstructorHelpers.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Net/UnrealNetwork.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "NavigationInvokerComponent.h"
#include "NavigationSystem.h"
#include "World/DayNightController.h"
#include "World/DoorRetrofitter.h"
#include "World/PackElevator.h" // stuck-recovery overslaan als je op een bewegende lift-cabine staat (H.4)
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
#include "ActivitySpotEditorWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Npc/NpcRegistryComponent.h"
#include "World/HeatComponent.h"
#include "Progression/LevelComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Save/SaveGameSubsystem.h"
#include "Save/AssetKeepAliveSubsystem.h" // runtime-geladen assets rooten over map-loads heen (laadtijd-fix)
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
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = false; // TEST: aparte FP-FOV (70) tekent je zichtbare FP-voeten op een ander schermpunt dan je wereld-schaduw -> schaduw lijkt los van de voeten. Uit = FP-mesh op de gewone camera-FOV = voet valt samen met de schaduw. Kost: FP-armen/held-item ander uiterlijk. FP-FOV UIT: armen renderen op je WERELD-FOV (bv 120) zodat ze bij het lichaam passen (70 gaf mismatch). Alleen FP-scale (0.6, regel hieronder) aan zodat de armen niet enorm/dichtbij zijn.
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;       // idem: de 0.6-schaal verschoof de FP-voeten ook; uit = wereld-schaal
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
	// Alleen nog voor de single-node-FALLBACK (skins zonder loco-ABP); loco-ABP-skins gebruiken TextingAnim.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PPhone(TEXT("/Game/GenericNPCAnimPack2/Animations/Anim_Check_Cellphone.Anim_Check_Cellphone"));
	if (PPhone.Succeeded()) { ProxyPhone = PPhone.Object; }
	// Dezelfde cellphone-clip maar native geretarget op SK_Mannequin: die kan als dynamische montage op
	// de 'UpperBody'-slot van ABP_Unarmed draaien -> bovenlijf sms't, benen blijven uit de loco-ABP
	// (geen slide meer bij lopen met de telefoon open). Zie UpdateTextingMontage.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PTextNative(TEXT("/Game/Characters/Texting_Anim_Manny.Texting_Anim_Manny"));
	if (PTextNative.Succeeded()) { TextingAnim = PTextNative.Object; }
	// Symmetrische idle voor de lokale speler (rechte houding, voeten gelijk) i.p.v. de scheve template-idle.
	// Uit de NPC-pack (ander skelet, maar speelt prima single-node op de speler - net als de telefoon-anim).
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PLocalIdle(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop.M_Neutral_Stand_Idle_Loop"));
	if (PLocalIdle.Succeeded()) { LocalIdleAnim = PLocalIdle.Object; }

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetCharacterMovement()->GravityScale = 1.0f;     // zwaartekracht gegarandeerd aan
	GetCharacterMovement()->JumpZVelocity = 350.0f;  // normale sprong
	GetCharacterMovement()->SetWalkableFloorAngle(50.0f);
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed; // standaard LOPEN (was 600 = jogtempo); Shift = rennen

	// CO-OP SYNC: vloeiende beweging van de ANDERE speler (simulated proxy). Exponential network-smoothing
	// interpoleert de proxy-positie i.p.v. te happen op elke net-update; hogere update-frequentie = strakkere
	// sync. (Default liet 'm soms houterig ogen.) De smooth-afstanden ruim genoeg voor de loopsnelheid.
	GetCharacterMovement()->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
	GetCharacterMovement()->NetworkMaxSmoothUpdateDistance = 256.f;
	GetCharacterMovement()->NetworkNoSmoothUpdateDistance = 384.f;
	SetNetUpdateFrequency(100.f);   // 100Hz positie-updates naar de andere speler (LAN/lokaal ruim haalbaar)
	SetMinNetUpdateFrequency(33.f);
	NetPriority = 3.0f;             // spelers belangrijker dan props/NPC's voor bandbreedte

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

	// LIFT-UITZONDERING (co-op H.4): tijdens een lift-rit staat de speler op de bewegende cabine-vloer met
	// ~0 verticale snelheid; mist de grond-check een frame, dan tikt FloatTime op en teleporteert de recovery
	// de speler valselijk terug. Staat de movement-base op een APackElevator-cabine, dan is dit GEEN echte
	// vastloper: reset de timer en sla over. bBelowWorld (echt door de vloer) blijft WEL herstellen - dat is
	// nooit een normale lift-toestand. Conservatief: liever 1 valse recovery overslaan dan in de lift teleporteren.
	if (!bBelowWorld)
	{
		const UPrimitiveComponent* Base = GetMovementBase(); // ACharacter::GetMovementBase (BasedMovement.MovementBase)
		const AActor* BaseActor = Base ? Base->GetOwner() : nullptr;
		if (BaseActor && BaseActor->IsA<APackElevator>())
		{
			FloatTime = 0.f; FallTime = 0.f;
			return;
		}
	}

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
		// 1) De ECHTE weg in het MIDDEN: dichtstbijzijnde punt op de NPC-route-midlijn (val terug op de
		//    straat-zoeker) via de DoorRetrofitter. Zo land je altijd op de boulevard buiten, niet op een
		//    navmesh-eilandje binnen een pand of op een stoeprand.
		for (TActorIterator<ADoorRetrofitter> It(GetWorld()); It; ++It)
		{
			FVector Road;
			if (It->FindNearestRoadPoint(Loc, Road)) { Safe = Road; bGot = true; }
			break; // er is er maar één
		}
		// 2) Geen DoorRetrofitter/route (bv. andere map): navmesh-projectie (oud gedrag).
		if (!bGot)
		{
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

void AThePlugSIMCharacter::ApplyNoclipCollision(bool bNoClip)
{
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetCollisionEnabled(bNoClip ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
		Cap->SetCollisionResponseToChannel(ECC_WorldStatic, bNoClip ? ECR_Ignore : ECR_Block);
		Cap->SetCollisionResponseToChannel(ECC_WorldDynamic, bNoClip ? ECR_Ignore : ECR_Block);
	}
}

void AThePlugSIMCharacter::SetDevNoClip(bool bEnable)
{
	bDevNoClip = bEnable;
	ApplyNoclipCollision(bEnable);                       // lokaal direct
	if (!HasAuthority()) { ServerSetDevNoClip(bEnable); } // server zet 'm + repliceert naar andere spelers
}

void AThePlugSIMCharacter::ServerSetDevNoClip_Implementation(bool bEnable)
{
	bDevNoClip = bEnable;          // gerepliceerd -> OnRep_DevNoClip op de andere clients
	ApplyNoclipCollision(bEnable); // host-kant
}

void AThePlugSIMCharacter::StartSprint()
{
	if (UCharacterMovementComponent* M = GetCharacterMovement()) { M->MaxWalkSpeed = SprintSpeed; } // lokaal direct (responsief)
	if (!HasAuthority()) { ServerSetSprint(true); } // co-op: server ook, anders corrigeert client-prediction de snelheid
}

void AThePlugSIMCharacter::StopSprint()
{
	if (UCharacterMovementComponent* M = GetCharacterMovement()) { M->MaxWalkSpeed = WalkSpeed; }
	if (!HasAuthority()) { ServerSetSprint(false); }
}

void AThePlugSIMCharacter::ServerSetSprint_Implementation(bool bSprint)
{
	if (UCharacterMovementComponent* M = GetCharacterMovement()) { M->MaxWalkSpeed = bSprint ? SprintSpeed : WalkSpeed; }
}

void AThePlugSIMCharacter::OnRep_DevNoClip()
{
	ApplyNoclipCollision(bDevNoClip); // andere spelers zien je door muren gaan
}

void AThePlugSIMCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
	// BEWUST geen collision-koppeling aan MOVE_Flying meer: het thuis-spawn/settle-systeem zet spelers
	// tijdelijk in MOVE_Flying (zweven tot de vloer er is) en dat mag de collision niet uitzetten.
	// Noclip wordt enkel door SetDevNoClip (F7) gestuurd.
}

void AThePlugSIMCharacter::UpdateProxyAnim(float DeltaSeconds)
{
	// Texting (telefoon open) draait bij loco-ABP-skins als UPPER-BODY-montage, los van de single-node-
	// logica hieronder - en voor ALLE pawns, ook je eigen pawn in first person (de body-mesh is daar
	// owner-no-see maar bestaat wel: schaduw + third-person-toggle B kloppen zo altijd).
	UpdateTextingMontage();

	// Mijn EIGEN pawn gebruikt de ABP/FP-view -> geen fallback. Elke ANDERE speler die ik zie (host ziet de
	// joiner als ROLE_Authority, een client ziet 'm als SimulatedProxy - beide zijn niet-lokaal) krijgt onze
	// single-node walk/idle/jump/telefoon. Lazy: zet de single-node-modus zodra we 'm voor het eerst tikken.
	// In THIRD-PERSON (toets B) tonen we óók op je EIGEN body de single-node-proxy-poses van FALLBACK-skins,
	// want de normale ABP kent die niet -> anders zie je jezelf niks doen. (Texting op loco-ABP-skins komt
	// hier niet meer langs: dat is de upper-body-montage hierboven, die in 1p én 3p draait.) In first-person
	// blijft de ABP/FP-view leidend, dus daar returnen we nog steeds meteen.
	if (IsLocallyControlled() && !bThirdPerson)
	{
		// Voorkom dat een single-node-pose (bv. texting) uit een 3p-sessie blijft hangen na terug naar 1p.
		if (USkeletalMeshComponent* LM = GetMesh())
		{
			if (LM->GetAnimationMode() == EAnimationMode::AnimationSingleNode)
			{
				LM->SetAnimationMode(EAnimationMode::AnimationBlueprint);
				ProxyAnimState = 0;
			}
		}
		return;
	}
	USkeletalMeshComponent* M = GetMesh();
	if (!M) { return; }

	// 'Beweegt' bepalen uit de positie (de capsule springt op net-updates) en kort vasthouden.
	const FVector Cur = GetActorLocation();
	if (bHasProxyPrev)
	{
		FVector D = Cur - ProxyPrevLoc; D.Z = 0.f;
		if (D.SizeSquared() > 4.f) { ProxyMoveHold = 0.2f; }
		else if (ProxyMoveHold > 0.f) { ProxyMoveHold -= DeltaSeconds; }
	}
	ProxyPrevLoc = Cur; bHasProxyPrev = true;

	const bool bMoving = ProxyMoveHold > 0.f;
	const bool bFalling = GetCharacterMovement() && GetCharacterMovement()->IsFalling();
	bool bPhone = false;
	if (Phone) { bPhone = Phone->IsPhoneOpenReplicated(); } // bestaand member; geen FindComponentByClass per tick

	// De texting-clip (Anim_Check_Cellphone) is een VOLLEDIGE cyclus: pakken -> kijken -> wegstoppen. In de
	// single-node-FALLBACK (skins zonder loco-ABP, verderop) spelen we 'm vooruit tot het 'kijk'-punt
	// (PhoneHoldTime) en PAUZEREN daar zolang je telefoon open is; pas bij sluiten spelen we het laatste stuk
	// (kijk-punt -> eind = wegstoppen). PhoneHoldFrac = waar in de clip de kijk-pose zit (visueel getuned).
	// De cliplengte kennen we pas at runtime, dus het kijk-punt is een fractie daarvan.
	const float PhoneHoldFrac    = 0.45f; // waar in de clip de 'kijk'-pose zit (vasthouden)
	const float PhonePutAwayRate = 2.4f;  // sneller afspelen bij sluiten -> telefoon vrijwel 'gelijk' terug in de zak
	const float PhoneClipLen  = ProxyPhone ? ProxyPhone->GetPlayLength() : 0.f;
	const float PhoneHoldTime = PhoneClipLen * PhoneHoldFrac;

	if (bBodyHasLocoAbp)
	{
		// Manny/Quinn: de locomotie-ABP doet walk/run/idle/jump. TEXTING draait hier NIET meer single-node
		// (dat bevroor het HELE lichaam -> sliden zodra je met de telefoon open ging lopen) maar als
		// upper-body-montage in UpdateTextingMontage(). De ABP blijft dus altijd de baas over de benen;
		// hier alleen borgen dat de mesh in Blueprint-modus staat (bv. na een skin-wissel of een oude
		// single-node-pose uit een 3p-sessie).
		if (ProxyAnimState != 0)
		{
			ProxyAnimState = 0;
			M->SetPlayRate(1.f);
			M->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		}
		return;
	}

	// Andere skins (ander skelet, ABP draait daar niet betrouwbaar): single-node fallback.
	if (!ProxyIdle && !ProxyWalk) { return; }
	if (M->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
	{
		M->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		ProxyAnimState = -1;
	}
	int32 NewState = 0; // idle
	if (bFalling) { NewState = 2; }
	else if (bMoving) { NewState = 1; }
	else if (bPhone && ProxyPhone) { NewState = 3; }

	// Telefoon-pose (3): zelfde volledige-cyclus-clip als hierboven. Vooruit tot het kijk-punt + daar bevriezen;
	// bij verlaten het laatste stuk vooruit afspelen (state 4 = wegstoppen) voor we naar de nieuwe pose gaan.
	if (ProxyAnimState == 3)
	{
		if (NewState == 3)
		{
			// Vasthouden op het kijk-punt zolang de telefoon open is.
			if (PhoneHoldTime > 0.f && M->GetPosition() >= PhoneHoldTime)
			{
				M->SetPosition(PhoneHoldTime, false);
				M->SetPlayRate(0.f);
			}
			return;
		}
		// Verlaten -> wegstoppen: vanaf het kijk-punt vooruit naar het eind. Positie expliciet NA Play() zetten,
		// want Play() kan vanaf frame 0 herstarten (= telefoon opnieuw pakken i.p.v. wegstoppen).
		ProxyAnimState = 4;
		M->Play(false);
		M->SetPlayRate(PhonePutAwayRate);
		M->SetPosition(PhoneHoldTime, false);
		return;
	}
	if (ProxyAnimState == 4)
	{
		if (NewState == 3) { ProxyAnimState = 3; M->SetPlayRate(0.f); M->SetPosition(PhoneHoldTime, false); return; } // weer geopend -> terug naar kijk-punt
		if (M->IsPlaying() && (PhoneClipLen <= 0.f || M->GetPosition() < PhoneClipLen - 0.02f)) { return; } // nog bezig met wegstoppen
		ProxyAnimState = -1; // klaar -> hieronder de nieuwe pose forceren
	}

	if (NewState == ProxyAnimState) { return; }
	ProxyAnimState = NewState;
	M->SetPlayRate(1.f);
	if (NewState == 3)
	{
		M->PlayAnimation(ProxyPhone, false); // vooruit; wordt hierboven op het kijk-punt bevroren (vasthouden)
		return;
	}
	UAnimSequence* Seq = (NewState == 2) ? ProxyJump : (NewState == 1) ? ProxyWalk : ProxyIdle;
	if (!Seq) { Seq = ProxyIdle; }
	if (Seq) { M->PlayAnimation(Seq, true); }
}

// === Texting-afstelling ==========================================================================
// Hold-trick-schakelaar: true = de oude aanpak (montage bevriezen op het kijk-punt), false = de clip
// gewoon laten LOOPEN. De Manny-texting-clip is een doorlopende texting-loop van 18,5s; loopen oogt
// levend (AAA-idle-gedrag: texten is bewegen, geen standbeeld) en de freeze pakte op 45% net een
// tussenmoment van een gebaar - de rare halfuitgestoken linkerarm uit de playtest. Eén schakelaar
// terug als de hold ooit toch nodig blijkt.
static constexpr bool bTextingHoldAtFrac = false;
static constexpr float TextingHoldFrac = 0.45f;     // kijk-punt-fractie (alleen gebruikt bij de hold-trick)
// Telefoon-prop: texting-clips houden het toestel vrijwel altijd in de LINKERhand (de rechterhand
// tikt) - andere hand = alleen dit woord wisselen. Offset/rotatie = eerste schatting "plat in de
// handpalm, scherm naar het gezicht"; tunen op speler-feedback.
static const FName TextingPhoneBone(TEXT("hand_l"));
static const FVector TextingPhoneSizeCm(14.5f, 7.0f, 0.8f);        // lang x breed x dun (~gsm met hoesje)
static const FVector TextingPhoneOffsetCm(10.0f, 2.0f, -1.0f);     // vanaf de hand-bone de handpalm in
static const FRotator TextingPhoneRot(0.f, 0.f, 90.f);             // plat op de palm (dunne kant tegen de hand)
static const FLinearColor TextingPhoneColor(0.02f, 0.02f, 0.025f); // donkergrijs/zwart hoesje

void AThePlugSIMCharacter::UpdateTextingMontage()
{
	// Texting als DYNAMISCHE MONTAGE op de 'UpperBody'-slot van ABP_Unarmed (layered blend per bone vanaf
	// spine_01, via de UnrealClaude-authoring-helper in het ABP gezet): het bovenlijf sms't, de benen
	// blijven uit de loco-ABP komen -> lopen met de telefoon open slidet niet meer. De slot zit in een
	// EIGEN slot-groep, dus de DefaultSlot-montages (symmetrische idle / fall-loop) draaien er gewoon
	// naast zonder elkaar te interrupten. bPhoneOpenRep repliceert al; montages zijn client-side -> elke
	// machine speelt 'm zelf af, geen extra replicatie nodig. De telefoon-prop in de hand volgt de
	// montage (aan bij start, weg bij stop) en is dus ook per-client.
	if (!bBodyHasLocoAbp || !TextingAnim) { DestroyTextingPhoneProp(); return; } // fallback-skins: single-node-pad in UpdateProxyAnim
	UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInst) { TextingMontage = nullptr; DestroyTextingPhoneProp(); return; }

	bool bPhone = false;
	if (Phone) { bPhone = Phone->IsPhoneOpenReplicated(); } // bestaand member; geen FindComponentByClass per tick

	const bool bActive = (TextingMontage != nullptr) && AnimInst->Montage_IsPlaying(TextingMontage);
	if (bPhone)
	{
		if (!bActive)
		{
			// Zelfde patroon als de symmetrische idle-montage: dynamisch + root motion UIT (GASP-gotcha:
			// clips met root motion zouden je anders op je plek vastzetten). LoopCount 99999 speelt de
			// hele clip eindeloos door; de clip is als loop geauthored (begin- en eindpose matchen), dus
			// de sectie-wrap knakt niet. Mocht een toekomstige clip dat wel doen: sub-range kiezen of de
			// hold-trick (bTextingHoldAtFrac) terugzetten.
			TextingMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(TextingAnim, FName("UpperBody"), 0.25f, 0.25f, 1.0f, 99999);
			if (TextingMontage)
			{
				TextingMontage->bEnableRootMotionTranslation = false;
				TextingMontage->bEnableRootMotionRotation = false;
				ShowTextingPhoneProp(); // toestel in de hand zolang je sms't (vers op de HUIDIGE mesh)
			}
		}
		else
		{
			// Hold-trick (staat UIT, zie bTextingHoldAtFrac hierboven): bevries de montage op het
			// kijk-punt zolang de telefoon open blijft - zelfde fractie als de single-node-fallback.
			if constexpr (bTextingHoldAtFrac)
			{
				const float HoldTime = TextingAnim->GetPlayLength() * TextingHoldFrac;
				if (HoldTime > 0.f
					&& AnimInst->Montage_GetPlayRate(TextingMontage) > 0.f
					&& AnimInst->Montage_GetPosition(TextingMontage) >= HoldTime)
				{
					AnimInst->Montage_SetPosition(TextingMontage, HoldTime);
					AnimInst->Montage_SetPlayRate(TextingMontage, 0.f);
				}
			}
			// Defensief: prop kwijt terwijl de montage nog draait (hoort niet, maar goedkoop te vangen).
			if (!TextingPhoneProp) { ShowTextingPhoneProp(); }
		}
	}
	else if (bActive)
	{
		// Telefoon dicht -> DIRECT stoppen (korte uitblend); geen wegstop-animatie op het bovenlijf.
		AnimInst->Montage_Stop(0.15f, TextingMontage);
		TextingMontage = nullptr;
		DestroyTextingPhoneProp();
	}
	else
	{
		TextingMontage = nullptr; // opruimen als de montage elders gestopt/verlopen is
		DestroyTextingPhoneProp();
	}
}

void AThePlugSIMCharacter::ShowTextingPhoneProp()
{
	// Altijd VERS aanmaken op de huidige mesh: ApplySkinMesh kan de skeletal mesh vervangen hebben
	// (de montage sterft daarbij en start hier opnieuw), dus nooit een oude prop hergebruiken.
	DestroyTextingPhoneProp();
	USkeletalMeshComponent* M = GetMesh();
	if (!M || M->GetBoneIndex(TextingPhoneBone) == INDEX_NONE) { return; } // skin zonder deze hand-bone: dan maar geen toestel
	UStaticMesh* Cu = PropKit::Cube();
	if (!Cu) { return; }
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	if (!C) { return; }
	C->SetStaticMesh(Cu);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false); // mini-prop, geen schaduw-kosten (licht-budget)
	// Zelfde view-instellingen als de body-mesh: eigen pawn in first person ziet 'm niet (owner-no-see),
	// remote spelers en je eigen 3p-view (B) wel. Wisselt B midden in het texten: zie ToggleThirdPerson.
	C->SetOnlyOwnerSee(M->bOnlyOwnerSee);
	C->SetOwnerNoSee(M->bOwnerNoSee);
	C->FirstPersonPrimitiveType = M->FirstPersonPrimitiveType;
	C->RegisterComponent();
	// SetupAttachment kan niet runtime; AttachToComponent met de bone-naam als socket wel.
	C->AttachToComponent(M, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TextingPhoneBone);
	C->SetRelativeLocationAndRotation(TextingPhoneOffsetCm, TextingPhoneRot);
	C->SetRelativeScale3D(TextingPhoneSizeCm / 100.f); // BasicShapes-kubus = 100 cm
	// Donker hoesje via het PropKit-idioom (BaseMat + kleur-MID).
	if (UMaterialInterface* BM = PropKit::BaseMat())
	{
		if (UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, BM)) { MID->SetVectorParameterValue(TEXT("Color"), TextingPhoneColor); }
	}
	TextingPhoneProp = C;
}

void AThePlugSIMCharacter::DestroyTextingPhoneProp()
{
	if (TextingPhoneProp)
	{
		TextingPhoneProp->DestroyComponent();
		TextingPhoneProp = nullptr;
	}
}

void AThePlugSIMCharacter::ServerDropActiveItem_Implementation()
{
	if (!Inventory) { return; }
	// Eén gedeeld drop-pad voor alles (ook briefgeld): ServerDropStack haalt de stapel eruit, spawnt de
	// pickup bij de voeten, en boekt bij cash het bedrag ook echt van het saldo af. We draaien hier al op
	// de server, dus de RPC voert direct z'n implementatie uit.
	Inventory->ServerDropStack(Inventory->GetActiveStackId());
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
	DOREPLIFETIME(AThePlugSIMCharacter, bDevNoClip); // F7-noclip zichtbaar voor andere spelers
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
		case 3:
		case 4:  Path = WeedOutfit::GirlVariantPath(PlayerSkin, OutfitTop); break; // Gamer(3)/School(4): gekozen outfit-variant (OutfitTop = variant-index)
		case 5:  Path = WeedOutfit::PartAt(0, GetOutfitPart(0), true).Path; break; // male = gekozen complete Tony-look
		case 6:  Path = WeedOutfit::CitizenManBodyPath; break;                      // Citizen_man: headless modulaire basis-body
		default: Path = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"); break;
	}
	// [BOOTMARK]-attributie: de skin/ABP-keten was het stille ~26s-blok in BeginPlay (load + compile op
	// ELKE map-load, want LoadMap-GC purgde de assets). Per-stap timen; het keep-alive-subsystem hieronder
	// root de assets zodat de tweede map-load ze memory-resident vindt.
	const double _SkinT0 = FPlatformTime::Seconds();
	USkeletalMesh* Skin = LoadObject<USkeletalMesh>(nullptr, Path);
	if (!Skin) { return; }
	UAssetKeepAliveSubsystem::Keep(this, Skin); // rooten: overleeft de LoadMap-GC (laadtijd-fix)
	const double _SkinT1 = FPlatformTime::Seconds();

	// Third-person body (co-op + jij in 3rd-person) EN first-person mesh (jouw eigen view, hoofd verborgen).
	double _AbpT0 = _SkinT1, _AbpT1 = _SkinT1, _SetT1 = _SkinT1;
	if (USkeletalMeshComponent* M = GetMesh())
	{
		M->SetSkeletalMeshAsset(Skin);
		_SetT1 = FPlatformTime::Seconds();
		// Zet de ECHTE locomotie-ABP (ABP_Unarmed, SK_Mannequin) op de body -> vloeiende, richting-gebaseerde
		// beweging (walk/run/idle/jump) die OOK op de simulated proxy draait (velocity repliceert). Werkt op
		// ALLE skins: Manny/Quinn native, en Casual/Tony via compatibele-skeletons (UE4_Mannequin_Skeleton_Main
		// + Citizens staan in SK_Mannequin's compat-lijst). Veel beter dan de kale single-node 'walk-forward'.
		bBodyHasLocoAbp = false;
		{
			_AbpT0 = FPlatformTime::Seconds();
			// GEEN static (unrooted -> kan dangelen na GC); het keep-alive-subsystem root 'm netjes met eigenaar.
			TSubclassOf<UAnimInstance> LocoAbp = LoadClass<UAnimInstance>(nullptr,
				TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"));
			UAssetKeepAliveSubsystem::Keep(this, LocoAbp.Get()); // rooten: ABP-compile niet nóg een keer
			_AbpT1 = FPlatformTime::Seconds();
			if (LocoAbp) { M->SetAnimInstanceClass(LocoAbp); bBodyHasLocoAbp = true; } // anders: single-node fallback
		}
		ProxyAnimState = -1; // forceer her-evaluatie in UpdateProxyAnim (anim-mode kan zijn gewisseld)
	}
	// Eén samenvattende regel (geen spam bij outfit-wissels): waar de tijd zat. Na de keep-alive-fix
	// horen de load-stappen op de tweede map-load ~0.00s te zijn.
	UE_LOG(LogThePlugSIM, Display, TEXT("[BOOTMARK] ApplySkinMesh: mesh-load %.2fs, SetSkeletalMeshAsset %.2fs, ABP-load %.2fs (+%.2fs sinds start)"),
		_SkinT1 - _SkinT0, _SetT1 - _SkinT1, _AbpT1 - _AbpT0, FPlatformTime::Seconds() - GStartTime);
	if (FirstPersonMesh)
	{
		// Gamer Girl (skin 3): SK_GamerGirl heeft een verwrongen bind/ref-pose. De FP-mesh draait GEEN eigen
		// animatie (alleen ref-pose), dus toont 'm verdraaid (de TP-mesh animeert wel via ABP_Unarmed -> die is
		// goed). Gebruik voor de FP-view een neutrale Manny-mesh: nette ref-pose + correcte head-socket-camera.
		if (PlayerSkin == 3)
		{
			USkeletalMesh* FpSkin = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
			UAssetKeepAliveSubsystem::Keep(this, FpSkin); // rooten over map-loads (laadtijd-fix)
			FirstPersonMesh->SetSkeletalMeshAsset(FpSkin ? FpSkin : Skin);
		}
		else
		{
			FirstPersonMesh->SetSkeletalMeshAsset(Skin);
		}
		// Verberg ALLEEN het hoofd (anders zie je je eigen kop). De nek + rest blijven, zodat je bij omlaag kijken
		// een COMPLEET lichaam ziet (geen gat in je nek). De camera volgt nu je hoofd, dus de nek blobt niet meer.
		static const TCHAR* HideBones[] = { TEXT("head"), TEXT("Head") };
		for (const TCHAR* B : HideBones)
		{
			if (FirstPersonMesh->GetBoneIndex(FName(B)) != INDEX_NONE)
			{
				FirstPersonMesh->HideBoneByName(FName(B), EPhysBodyOp::PBO_None);
			}
		}
	}

	// First-person camera-afstand per skin: de UE5-mannequin-skins (Manny/Quinn/Tony) hebben bredere schouders +
	// andere proporties dan de Casual-skins, waardoor je eigen body/armen pal naast de camera in beeld steken
	// (o.a. de linkerarm die naar voren komt als je recht vooruit kijkt). Zet de camera voor die skins wat verder
	// naar voren (voor de borst) zodat de armen netjes achter de camera hangen. Casual-skins (2-4) zijn al goed.
	if (FirstPersonCameraComponent)
	{
		const bool bUE5Mannequin = (PlayerSkin <= 1 || PlayerSkin == 5 || PlayerSkin == 3); // 0 Manny, 1 Quinn, 5 Tony, 3 Gamer (FP-mesh = Manny)
		FVector Rel = FirstPersonCameraComponent->GetRelativeLocation();
		Rel.X = bUE5Mannequin ? 22.f : 8.f;
		Rel.Y = 0.f; // gecentreerd; de links-zwaar-look kwam van de asymmetrische idle-pose (apart gefixt), niet de camera
		FirstPersonCameraComponent->SetRelativeLocation(Rel);
	}

	// Outfit-parts (Wardrobe): oude parts opruimen en de gekozen kleding/haar aanhangen (leader-pose volgt
	// de body). Geldt alleen voor de Casual-skins (2-4); Manny/Quinn hebben (nog) geen losse outfits.
	for (USkeletalMeshComponent* C : OutfitComps) { if (C) { C->DestroyComponent(); } }
	OutfitComps.Reset();
	if (PlayerSkin == 2) // Casual-girl (skin 2): female per-part. Gamer_Girl/School_Girl (3/4) zijn complete meshes.
	{
		AttachOutfitParts(GetMesh(), false, false);       // female (Casual girl)
		AttachOutfitParts(FirstPersonMesh, true, false);
		SyncOutfitViewFlags();
	}
	else if (PlayerSkin == 6) // Citizen_man: male per-part (zelfde leader-pose-aanpak als de vrouw)
	{
		AttachOutfitParts(GetMesh(), false, true);
		AttachOutfitParts(FirstPersonMesh, true, true);
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
	UE_LOG(LogTemp, Verbose, TEXT("SOFTPHYS done: %d soft-bones, %d met physics-body, van %d totaal"), Enabled, WithBody, NumBones);
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
		// Telefoon-prop (texting) volgt de body-zichtbaarheid, net als de outfit-parts - anders zweeft er
		// na een B-toggle midden in het texten een onzichtbaar/dubbel toestel.
		if (TextingPhoneProp)
		{
			TextingPhoneProp->SetOwnerNoSee(M->bOwnerNoSee);
			TextingPhoneProp->FirstPersonPrimitiveType = M->FirstPersonPrimitiveType;
			TextingPhoneProp->MarkRenderStateDirty();
		}
	}
	SyncOutfitViewFlags(); // outfit-parts dezelfde view-instellingen geven
}

void AThePlugSIMCharacter::OnRep_Skin() { ApplySkinMesh(); }

void AThePlugSIMCharacter::ServerSetSkin_Implementation(uint8 NewSkin)
{
	PlayerSkin = (NewSkin > 6) ? 6 : NewSkin;
	ApplySkinMesh(); // server lokaal toepassen; repliceert naar clients -> OnRep_Skin
}

void AThePlugSIMCharacter::RestoreSkin(uint8 S)
{
	PlayerSkin = (S > 6) ? 6 : S;
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
		UAssetKeepAliveSubsystem::Keep(this, PartMesh); // outfit-parts rooten over map-loads (laadtijd-fix)
		USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this);
		C->SetupAttachment(BodyComp);
		C->RegisterComponent();
		C->SetSkeletalMeshAsset(PartMesh);
		C->SetLeaderPoseComponent(BodyComp); // volgt de body-animatie bone-voor-bone
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OutfitComps.Add(C);
	};
	if (!bMale) { Attach(WeedOutfit::UnderwearPath); } // female-underwear niet op de male-body
	const bool bCitMan = (PlayerSkin == 6);
	for (int32 SlotIdx = 0; SlotIdx < WeedOutfit::SlotCount(); ++SlotIdx)
	{
		// Citizen_man's hoofd is een LOSSE part (slot 6); op de FIRST-person-mesh sla je 'm over zodat je je
		// eigen kop niet ziet (de body is headless, dus de body-bone-hide werkt hier niet).
		if (bFirstPerson && bCitMan && SlotIdx == 6) { continue; }
		const WeedOutfit::FPart& P = bMale
			? (bCitMan ? WeedOutfit::PartAtM(SlotIdx, GetOutfitPart(SlotIdx), WeedOutfit::EMaleKind::CitizenMan)
			           : WeedOutfit::PartAt(SlotIdx, GetOutfitPart(SlotIdx), true))
			: WeedOutfit::PartAt(SlotIdx, GetOutfitPart(SlotIdx), false);
		Attach(P.Path);
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
	// ANTI-CRASH: Gamer/School-varianten zijn complete meshes met Chaos-Cloth (o.a. de dress). Elke variant-swap
	// recreeert de clothing-actors; te snel achter elkaar wisselen liet de render-thread op vrijgegeven cloth-data
	// crashen (access violation in de Renderer). Throttle de variant-swap tot ~3/s zodat een recreatie eerst afrondt.
	if ((PlayerSkin == 3 || PlayerSkin == 4) && Slot == 0)
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Now - LastGirlVariantSwap < 0.35) { return; }
		LastGirlVariantSwap = Now;
	}
	int32 Max;
	if (PlayerSkin == 6)      { Max = WeedOutfit::PartCountM(Slot, WeedOutfit::EMaleKind::CitizenMan); }
	else if (PlayerSkin == 5) { Max = WeedOutfit::PartCount(Slot, true); }
	else if ((PlayerSkin == 3 || PlayerSkin == 4) && Slot == 0) { Max = WeedOutfit::GirlVariantCount(PlayerSkin); } // Gamer/School: slot 0 = variant-index
	else                      { Max = WeedOutfit::PartCount(Slot, false); }
	const uint8 Clamped = (uint8)FMath::Clamp<int32>(Index, 0, Max - 1);
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

	// FP-camera: tijdens een SPRONG (airborne) brengen we de camera wat OMHOOG, zodat je over je optrekkende
	// body/benen heen kijkt i.p.v. er middenin. Op de grond terug naar de vaste ooghoogte (stabiel, geen
	// loop-bob). Smoothing ertussen -> vloeiende sprong-beweging. Alleen de lokale speler.
	if (IsLocallyControlled() && FirstPersonCameraComponent && GetCharacterMovement())
	{
		// FP-camera: VASTE ooghoogte (stabiel) + procedurele loop-bob. GEEN head-bone-tracking (die jitterde door
		// root-motion/animatie: DesiredRelZ sprong 55<->122). Tijdens een sprong stijgt de camera vanzelf mee met de
		// capsule -> blijft op ooghoogte t.o.v. het lichaam (geen aparte lift meer nodig).
		// Per-model FP-camera-tuning (capsule-relatief): voor-offset X + ooghoogte Z. Andere skins = andere lengte/
		// proporties -> met één vaste waarde kijk je in je nek/lichaam. TUNEBAAR per skin (pas aan op feedback).
		float CamX = 8.f, EyeHeight = 62.f;
		switch (PlayerSkin)
		{
		case 0: case 1: CamX = 22.f; EyeHeight = 72.f; break; // Manny/Quinn (UE5, lang)
		case 3:         CamX = 22.f; EyeHeight = 72.f; break; // Gamer Girl (FP-mesh = Manny)
		case 5:         CamX = 22.f; EyeHeight = 70.f; break; // Tony (Citizens, man)
		case 6:         CamX = 14.f; EyeHeight = 68.f; break; // Citizen_man (man)
		case 2:         CamX = 8.f;  EyeHeight = 64.f; break; // Casual girl
		case 4:         CamX = 8.f;  EyeHeight = 62.f; break; // School girl (eigen skelet)
		default:        break;
		}
		const bool bAir = GetCharacterMovement()->IsFalling();

		// De camera zit ECHT op je hoofd: we lezen de head-bone van de FP-mesh (owner-zichtbaar -> animeert ALTIJD;
		// de body-mesh is voor jou verborgen en tickt in singleplayer NIET -> bevroren ref-pose, daarom deed 't eerder niks).
		// De camera volgt de NATUURLIJKE hoofdbeweging van de animatie -> echte head-bob bij lopen + automatisch correct
		// bij springen (je lichaam schuift nooit in beeld). Rotatie blijft van de muis (bUsePawnControlRotation).
		FVector HeadCapNow = GroundHeadCap; bool bHaveHead = false;
		if (FirstPersonMesh && FirstPersonMesh->GetBoneIndex(FName("head")) != INDEX_NONE)
		{
			HeadCapNow = FVector(GetCapsuleComponent()->GetComponentTransform().InverseTransformPosition(FirstPersonMesh->GetSocketLocation(FName("head"))));
			bHaveHead = true;
		}
		// Stilstaande grond-pose als referentie (traag -> middelt de loop-bob eruit i.p.v. 'm na te jagen).
		if (!bAir && bHaveHead) { GroundHeadCap = FMath::VInterpTo(GroundHeadCap, HeadCapNow, DeltaSeconds, 3.f); }

		// Verplaatsing van 't hoofd t.o.v. de stilstaande pose = natuurlijke head-bob (lopen) + sprong-beweging.
		// Head-bob UIT (telefoon-setting): op de grond geen bob (stabiel), maar in de lucht volgt de camera nog steeds
		// je hoofd zodat je lichaam niet in beeld komt bij een sprong.
		const bool bBobOn = Phone ? Phone->GetHeadBob() : true;
		FVector TargetOff = FVector::ZeroVector;
		if (bHaveHead && (bAir || bBobOn)) { TargetOff = HeadCapNow - GroundHeadCap; }
		// In de LUCHT schalen we 't laterale (Y) volgen met je KIJKRICHTING: kijk je VOORUIT dan volgen we Y niet
		// (anders rukt de zijwaartse jump-anim-pop de camera opzij - "jerkt naar links"; je lichaam zit dan toch onder
		// beeld). Kijk je OMLAAG (naar je lichaam) dan volgen we Y VOLLEDIG, zodat je lichaam op z'n plek blijft en je
		// 'm gewoon de sprong-animatie ziet doen i.p.v. weg te zwiepen. Z (sprong+bob) en X (anti-"achter je rug") altijd.
		if (bAir)
		{
			const float Pitch = FRotator::NormalizeAxis(GetControlRotation().Pitch);
			TargetOff.Y *= FMath::GetMappedRangeValueClamped(FVector2D(-45.f, -12.f), FVector2D(1.f, 0.f), Pitch);
			// De GASP-jump trekt je schouders/bovenlijf tot bóven je hoofd; head-tracking alleen is dan niet hoog genoeg.
			// Extra OMHOOG als je VOORUIT/level kijkt -> je optrekkende lichaam blijft onder de voorwaartse view. Omlaag
			// kijkend GEEN extra lift (dan zie je je lichaam gewoon normaal op ooghoogte). Vloeiend tussen via de pitch.
			TargetOff.Z += 18.f * FMath::GetMappedRangeValueClamped(FVector2D(-38.f, -8.f), FVector2D(0.f, 1.f), Pitch);
		}
		SmoothedJumpOff = FMath::VInterpTo(SmoothedJumpOff, TargetOff, DeltaSeconds, bAir ? 14.f : 20.f);

		FVector RelLoc;
		RelLoc.X = CamX + SmoothedJumpOff.X;            // volg ook voor/achter -> nooit "achter je rug"
		RelLoc.Y = SmoothedJumpOff.Y;                   // lateraal mee met 't hoofd
		RelLoc.Z = EyeHeight + SmoothedJumpOff.Z;       // verticaal: echte head-bob + sprong
		FirstPersonCameraComponent->SetRelativeLocation(RelLoc);
	}

	// LOKALE speler: de template-ABP-idle staat scheef (1 voet voor). Bij stilstaan spelen we een symmetrische
	// idle; zodra je beweegt/springt terug naar de locomotie-ABP. Alleen op state-wissel (geen per-frame
	// gethrash) - zelfde patroon als de proxy-texting-switch in UpdateProxyAnim.
	if (IsLocallyControlled() && bBodyHasLocoAbp)
	{
		// Speel de symmetrische idle (stilstaan) OF een RUSTIGE fall-loop (sprong) als DYNAMISCHE MONTAGE op de
		// 'DefaultSlot' OVER de loco-ABP heen. Voor de sprong bypasst dit de schokkerige GASP-jump-start (MM_Jump),
		// die je lichaam opzij/omhoog rukte; in plaats daarvan een kalme MM_Fall_Loop -> geen jerk in de lucht.
		// De ABP blijft in Blueprint-modus draaien. (Vereist een DefaultSlot in de ABP; zo niet, montage toont niks.)
		if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			const bool bMoving = GetVelocity().SizeSquared2D() > 100.f; // > ~10 cm/s
			const bool bFalling = GetCharacterMovement() && GetCharacterMovement()->IsFalling();
			// 2 = sprong/val (kalme fall-pose), 1 = stilstaan (symmetrische idle), 0 = loco-ABP (lopen/rennen).
			// Telefoon open is hier GEEN uitzondering meer: de texting-montage zit op de UpperBody-slot
			// (eigen slot-groep), dus de idle op DefaultSlot draait er gewoon onder door.
			int32 Want = 0;
			if (bFalling && ProxyJump) { Want = 2; }
			else if (!bMoving && LocalIdleAnim) { Want = 1; }
			if (Want != LocalIdleState)
			{
				LocalIdleState = Want;
				if (Want == 1)
				{
					LocalPoseMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(LocalIdleAnim, FName("DefaultSlot"), 0.25f, 0.25f, 1.0f, 99999);
					// KRITISCH: root motion uit (GASP-clips hebben root motion; die zou je op je plek houden).
					if (LocalPoseMontage) { LocalPoseMontage->bEnableRootMotionTranslation = false; LocalPoseMontage->bEnableRootMotionRotation = false; }
				}
				else if (Want == 2)
				{
					// Kalme fall-loop i.p.v. de schokkerige jump-start. Snelle blend-in (0.1s) om de pop af te vangen.
					LocalPoseMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(ProxyJump, FName("DefaultSlot"), 0.1f, 0.2f, 1.0f, 99999);
					if (LocalPoseMontage) { LocalPoseMontage->bEnableRootMotionTranslation = false; LocalPoseMontage->bEnableRootMotionRotation = false; }
				}
				else if (LocalPoseMontage)
				{
					// GERICHT stoppen (niet StopAllMontages: die zou ook de texting-montage op de
					// UpperBody-slot killen) -> blend vloeiend terug naar de loco-ABP.
					AnimInst->Montage_Stop(0.2f, LocalPoseMontage);
					LocalPoseMontage = nullptr;
				}
			}
		}
	}

	// F9: dev-overlay met positie + mesh-onder-crosshair (en logt de plek naar MarkedSpots.txt).
	// F7: vlieg-modus (markers zetten op hoogte); Space = stijgen, Ctrl = dalen.
	if (IsLocallyControlled())
	{
		if (const APlayerController* PCk = Cast<APlayerController>(GetController()))
		{
			// ALLE F-toetsen (F7 fly/noclip, F9 spot-overlay) zijn dev-tools: alleen met dev-tools AAN
			// (Ctrl+Shift+F10-chord of `WeedDev`). In een normale playthrough doen ze niets.
			const AWeedShopGameState* GSdev = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
			const bool bDevKeys = GSdev && GSdev->AreDevToolsEnabled();

			// F9: spot-overlay aan/uit + markeer je plek (basis voor routes/zones/shops). De rest (menu-cam,
			// bezorg-/meet-plek, build-area, activity-NPC, register-home, furniture-template) zit nu in het F10-dev-menu.
			const bool bF9 = bDevKeys && PCk->IsInputKeyDown(EKeys::F9);
			if (bF9 && !bSpotKeyWasDown && Phone) { Phone->ToggleSpotInfo(); }
			bSpotKeyWasDown = bF9;

			// F7: vlieg-/noclip-modus aan/uit (Space = op, Ctrl = neer).
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
					CM->SetMovementMode(bFlying ? MOVE_Walking : MOVE_Flying);
					// Noclip is een APARTE gerepliceerde vlag (niet aan MOVE_Flying gekoppeld): fly aan = noclip aan,
					// fly uit = noclip uit. Zo zien andere spelers je door muren gaan, maar raakt het settle-zweven
					// (dat ook MOVE_Flying gebruikt) de collision nooit.
					SetDevNoClip(!bFlying);
					UWeedToast::NotifyPawn(this, -1, 2.f, FColor::Cyan, bFlying ? TEXT("Fly mode OFF") : TEXT("Fly mode ON + noclip (Space = up, Ctrl = down, F7 = off)"));
				}
			}
			bFlyKeyWasDown = bF7;

			// (Activity-NPC, build-area, register-home, furniture-template, menu-cam, bezorg-/meet-plek: nu knoppen
			//  in het F10-dev-menu i.p.v. losse hotkeys. F10 opent het menu.)

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
			const bool bDown = PC->IsInputKeyDown(EKeys::Q) && !bUiOpen && !Active.IsNone() && Active != FName(TEXT("Cash"));
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

	// Actieve-item-id 1x per tick naar string (FName->FString alloceert); hieronder 3x gebruikt
	// (roken / rollen / joint overhandigen). Posities/staat blijven per frame vers gelezen.
	const FString ActiveIdStr = Inventory ? Inventory->GetActiveItemId().ToString() : FString();

	// Roken = rechtermuisknop inhouden met een joint in de hand. Duidelijke voortgangsbalk via de HUD,
	// zodat je niet per ongeluk je eigen joint oprookt.
	{
		const bool bUiOpen = Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen()
			|| Phone->IsInventoryOpen() || Phone->IsPotUpgradeOpen());
		const bool bJointInHand = Inventory && ActiveIdStr.StartsWith(TEXT("Joint_"));
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
		const bool bPapersInHand = Inventory && ActiveIdStr.StartsWith(TEXT("Papers_"));
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
		// Stoned = XP-bonus op basis van de THC% van je wiet. De bonus wordt nu PER-VERDIENER toegepast bij
		// ULevelComponent::AddXP (via GetStonedXpMultiplier op de earner-pawn) i.p.v. hier als gedeelde
		// crew-multiplier -> in co-op geen crew-breed meeliften + geen race tussen host- en joiner-pawn.
	}

	// Joint overhandigen: korte LMB-hold terwijl je een joint vasthoudt en een klant aankijkt.
	{
		const bool bUiOpenG = Phone && (Phone->IsOpen() || Phone->IsRollOpen() || Phone->IsDealOpen()
			|| Phone->IsInventoryOpen() || Phone->IsPotUpgradeOpen() || Phone->IsAtmOpen() || Phone->IsPackOpen());
		const bool bJointG = Inventory && ActiveIdStr.StartsWith(TEXT("Joint_"));
		ACustomerBase* FocusCust = nullptr;
		if (CachedInteraction) // in BeginPlay gecached; focus zelf wordt gewoon per frame vers gelezen
		{
			FocusCust = Cast<ACustomerBase>(CachedInteraction->GetFocusedActor());
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
	// Shift ingehouden = rennen, loslaten = weer lopen.
	Input->BindKey(EKeys::LeftShift,  IE_Pressed,  this, &AThePlugSIMCharacter::StartSprint);
	Input->BindKey(EKeys::LeftShift,  IE_Released, this, &AThePlugSIMCharacter::StopSprint);
	Input->BindKey(EKeys::RightMouseButton, IE_Pressed,  this, &AThePlugSIMCharacter::OnSecondaryPressed);
	Input->BindKey(EKeys::RightMouseButton, IE_Released, this, &AThePlugSIMCharacter::OnSecondaryReleased);
	Input->BindKey(EKeys::LeftMouseButton,  IE_Pressed,  this, &AThePlugSIMCharacter::OnPrimaryClick);
	Input->BindKey(EKeys::LeftMouseButton,  IE_Released, this, &AThePlugSIMCharacter::OnPrimaryReleased);

	// M: fullscreen stadskaart aan/uit.
	if (Ph) { Input->BindKey(EKeys::M, IE_Pressed, Ph, &UPhoneClientComponent::ToggleMapOverlay); }

	// B: wissel tussen first-person en third-person (om jezelf / je skin te bekijken).
	Input->BindKey(EKeys::B, IE_Pressed, this, &AThePlugSIMCharacter::ToggleThirdPerson);

	// Dev: F10 = het DEV-MENU (één sidebar met ALLE dev-tools). F9 = spot-overlay, F7 = fly (per-tick, elders).
	// Alle andere dev-acties zijn nu knoppen IN het menu i.p.v. losse hotkeys (de WeedXxx-console-commando's blijven).
	if (Ph) { Input->BindKey(EKeys::F10, IE_Pressed, Ph, &UPhoneClientComponent::ToggleDevMenu); }

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
			// Open meteen de deal-kaart bij deze klant, zodat je de reactie ZIET (anders "lijkt het of
			// er niks gebeurt"). De server-reactie (Say + relatie) verschijnt dan in het venster.
			if (Phone)
			{
				if (ACustomerBase* C = Cast<ACustomerBase>(Focus)) { Phone->OpenDeal(C); }
			}
		}
	}
}

void AThePlugSIMCharacter::GiveJointToCustomer(ACustomerBase* Customer)
{
	// Vanuit het praat-venster: geef precies deze klant een joint (zelfde server-flow als de hold).
	if (Customer) { ServerGiveSample(Customer); }
}

void AThePlugSIMCharacter::GiveJointToCustomerId(ACustomerBase* Customer, FName JointId)
{
	// Vanuit de deal-kiezer: geef precies DEZE gekozen joint (geen hand-item nodig).
	if (Customer) { ServerGiveSampleId(Customer, JointId); }
}

void AThePlugSIMCharacter::ServerGiveSample_Implementation(AActor* Target)
{
	ACustomerBase* Customer = Cast<ACustomerBase>(Target);
	if (!Customer || !Inventory) { return; }
	// Hold-flow: geef de joint die je NU in je hand hebt (geselecteerd op de hotbar).
	const FName HandId = Inventory->GetActiveItemId();
	GiveSampleCore(Customer, HandId.ToString().StartsWith(TEXT("Joint_")) ? HandId : NAME_None);
}

void AThePlugSIMCharacter::ServerGiveSampleId_Implementation(AActor* Target, FName JointId)
{
	// Deal-kiezer: geef precies de gekozen joint (geen hand-item nodig).
	ACustomerBase* Customer = Cast<ACustomerBase>(Target);
	if (!Customer) { return; }
	GiveSampleCore(Customer, JointId);
}

void AThePlugSIMCharacter::GiveSampleCore(ACustomerBase* Customer, FName JointId)
{
	if (!Customer || !Inventory)
	{
		return;
	}

	// Reikwijdte-check.
	if (FVector::DistSquared(GetActorLocation(), Customer->GetActorLocation()) > FMath::Square(400.f))
	{
		return;
	}

	// Sample-cooldown: voorkom dat je een NPC instant maxet met een stapel joints (per-NPC, dag-cyclus-tijd).
	if (AWeedShopGameState* GScd = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (GScd->GetNpcRegistry() && !Customer->NpcId.IsNone() && GScd->GetNpcRegistry()->IsOnSampleCooldown(Customer->NpcId))
		{
			if (GEngine) { UWeedToast::NotifyPawn(this,-1, 2.5f, FColor::Orange, TEXT("They're still smoking the last one - give it a minute.")); }
			return;
		}
	}

	// Een sample is een gedraaide joint - hier de gekozen/vastgehouden joint-id.
	const FName BestJoint = JointId.ToString().StartsWith(TEXT("Joint_")) ? JointId : NAME_None;
	const int32 BestGrams = BestJoint.IsNone() ? 0 : UInventoryComponent::JointGrams(BestJoint); // "Joint_SilverHaze_3g" -> 3

	// Wiet-kwaliteit van de joint (0..1) vóór we 'm weghalen — slechte wiet verslaaft/bindt minder.
	const float WeedQ = FMath::Clamp(Inventory->GetItemQualityPct(BestJoint) / 100.f, 0.f, 1.f);

	if (BestJoint.IsNone() || !Inventory->RemoveItem(BestJoint, 1))
	{
		if (GEngine)
		{
			UWeedToast::NotifyPawn(this,-1, 2.5f, FColor::Orange, TEXT("No joint to give — roll one first (R)."));
		}
		return;
	}

	// Effectieve kwaliteit = wiet-kwaliteit geschaald met het aantal gram (zelfde formule als de
	// joint-sterkte): een dun jointje voelt zwakker en bindt/verslaaft daardoor minder.
	const float Quality = UPhoneClientComponent::JointIntensity(BestGrams, 0.f, WeedQ * 100.f);
	// Joints zijn er VOORAL om te verslaven (de haak). Verslaving = hoofd-gain; loyaliteit/respect verdien
	// je via VERKOOP, dus een gratis joint geeft daar hooguit een vleugje van.
	float AddGain = 4.f + Quality * 12.f;   // VERSLAVING dominant (top-joint ~16, brak ~4)
	float LoyGain = Quality * 3.f;          // alleen een vleugje goodwill bij goeie wiet (top ~3)
	float RespGain = Quality * 2.5f;        // beetje respect voor goeie wiet (top ~2.5)

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
			GS->GetNpcRegistry()->ApplyStats(Customer->NpcId, R + RespGain, L + LoyGain, A + AddGain, this); // this = dealende speler-pawn -> per-speler OwnerPlayerId in competitive
		}
		GS->GetNpcRegistry()->MarkSampled(Customer->NpcId); // start de per-NPC sample-cooldown
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

	// NPC praat terug in het venster. Ruime variatie zodat het niet snel herhaalt; de index komt
	// automatisch uit de array-lengte (UE_ARRAY_COUNT), dus uitbreiden hoeft nooit met de hand.
	{
		static const TCHAR* Good[] = {
			TEXT("Ahh, that's the good stuff. Respect."),
			TEXT("Damn, that hits nice!"),
			TEXT("Yesss, exactly what I needed."),
			TEXT("That's fire, my man."),
			TEXT("Whoa, smooth. You know your stuff."),
			TEXT("Now THAT's a joint."),
			TEXT("Mmm, proper. Keep this around."),
			TEXT("Cheers, that's premium."),
			TEXT("Oh yeah, that's the one."),
			TEXT("Clean burn, real nice taste.")
		};
		static const TCHAR* Bad[] = {
			TEXT("Pfff, that's weak, bro."),
			TEXT("Meh... barely felt that."),
			TEXT("Come on, I've had better."),
			TEXT("That ain't it, man."),
			TEXT("Tastes like lawn clippings."),
			TEXT("Nah, this is mid at best."),
			TEXT("You call this loud? It's a whisper."),
			TEXT("Save the schwag for the tourists."),
			TEXT("Barely a buzz, come on."),
			TEXT("I've smoked better hedge trimmings.")
		};
		// "Nu even genoeg"-afsluiter: de NPC gaat hierna op sample-cooldown, dus de laatste zin
		// spreekt uit waarom de give-knop straks weg is (zonder te veranderen hoe het getoond wordt).
		static const TCHAR* CooldownTail[] = {
			TEXT("Let me enjoy this one first."),
			TEXT("Gimme a minute with this."),
			TEXT("Catch me after I finish this."),
			TEXT("Hold off, I'm set for now."),
			TEXT("That's me for now, cheers.")
		};
		const FString Base = bLikedIt
			? Good[FMath::RandRange(0, UE_ARRAY_COUNT(Good) - 1)]
			: Bad[FMath::RandRange(0, UE_ARRAY_COUNT(Bad) - 1)];
		const FString Tail = CooldownTail[FMath::RandRange(0, UE_ARRAY_COUNT(CooldownTail) - 1)];
		Customer->Say(Base + TEXT(" ") + Tail);
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
				GS->GetNpcRegistry()->ApplyStats(Customer->NpcId, R2, L2, FMath::Max(A2, Customer->AddictionToBuy), this); // this = dealende speler-pawn -> per-speler OwnerPlayerId in competitive
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
		GS->GetLeveling()->AddXP(bConverted ? 25 : 3, GetStonedXpMultiplier()); // stoned-bonus van DEZE speler
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

void AThePlugSIMCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// CO-OP STANDAARD-GENDER: 2 spelers samen = standaard 1 man + 1 vrouw. De host (lokale controller op de
	// listen-server) blijft Manny (man, skin 0); een JOINER (remote controller) krijgt standaard Quinn (vrouw,
	// skin 1). Alleen op een VERSE game (een geladen game herstelt de gekozen skin via RestoreSkin); de speler
	// kan 't altijd zelf wijzigen via de wardrobe (ServerSetSkin).
	if (HasAuthority() && NewController && NewController->IsA<APlayerController>())
	{
		bool bFresh = true;
		if (UGameInstance* GI = GetGameInstance())
		{ if (USaveGameSubsystem* Sv = GI->GetSubsystem<USaveGameSubsystem>()) { bFresh = Sv->IsFreshGame(); } }
		if (bFresh)
		{
			// GESKINDE man/vrouw, GEEN grijze mannequins: host/solo (lokale controller) = man (Tony, skin 5);
			// een JOINER (remote controller) = vrouw (Casual-girl, skin 2). Altijd zelf wijzigbaar via de wardrobe.
			PlayerSkin = NewController->IsLocalController() ? 5 : 2;
			ApplySkinMesh();
		}
	}
}

void AThePlugSIMCharacter::BeginPlay()
{
	// [BOOTMARK]-attributie (zelfde stijl als GameMode/GameState): waar de map-load-tijd in de
	// character-boot zit. Super / ApplySkinMesh / RestorePlayerByPawn / EnsureWidget apart getimed.
	double _BmT = FPlatformTime::Seconds();
	Super::BeginPlay();
	UE_LOG(LogThePlugSIM, Display, TEXT("[BOOTMARK] Character Super::BeginPlay %.2fs (+%.2fs sinds start)"), FPlatformTime::Seconds() - _BmT, FPlatformTime::Seconds() - GStartTime);
	if (UCapsuleComponent* Cap = GetCapsuleComponent()) { Cap->OnComponentHit.AddDynamic(this, &AThePlugSIMCharacter::OnCapsuleBump); }

	_BmT = FPlatformTime::Seconds();
	ApplySkinMesh(); // skin toepassen (default man; save-restore/keuze overschrijft via RestoreSkin/OnRep)
	UE_LOG(LogThePlugSIM, Display, TEXT("[BOOTMARK] Character ApplySkinMesh %.2fs (+%.2fs sinds start)"), FPlatformTime::Seconds() - _BmT, FPlatformTime::Seconds() - GStartTime);

	// Interactie-component (op de BP-character gezet) 1x opzoeken i.p.v. FindComponentByClass per tick.
	CachedInteraction = FindComponentByClass<UInteractionComponent>();

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
		M->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -98.f), FRotator(0.f, -90.f, 0.f)); // -98 i.p.v. -96: 2cm ONDER de capsule-bodem -> voeten op de vloer i.p.v. zwevend. CharacterMovement houdt de capsule ~2cm boven de grond (MAX_FLOOR_DIST); zonder deze compensatie zweeft de schaduw los van de voeten bij een lage zon. Zelfde 2cm-truc als de NPC's (-90 vs -88 capsule).
		// (De walk/idle/jump-fallback voor ANDERE spelers wordt lazy aangezet in UpdateProxyAnim zodra die
		//  pawn niet-lokaal blijkt - werkt zo op host EN client, ongeacht Authority/SimulatedProxy.)
	}

	// Forceer de movement-instellingen at RUNTIME (een Blueprint-default kan de constructor overschrijven).
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->GravityScale = 1.0f;
		Move->JumpZVelocity = 350.0f;
		Move->AirControl = 0.5f;
		if (Move->MovementMode == MOVE_None) { Move->SetMovementMode(MOVE_Walking); }
		// Expliciet solide collision bij spawn (tenzij dev-noclip aanstaat). Niet afhankelijk van MOVE_Flying:
		// een pawn die via het settle-systeem kort in MOVE_Flying staat hoort gewoon solide te blijven.
		ApplyNoclipCollision(bDevNoClip);
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

	// Startvoorraad (concept): wat vloei en 2 streetweed-zaadjes (de Silver Haze-wiet komt via de save-init).
	if (HasAuthority() && Inventory)
	{
		// Lean begin: net genoeg om je eerste plant te kweken en je eerste joint te draaien.
		Inventory->AddItem(FName(TEXT("Papers_Small")), 3);
		Inventory->AddItem(FName(TEXT("Seed_Streetweed")), 2);
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
				// Eerst proberen te herstellen uit de save (load-game). Op een VERSE Normal-game is dat een no-op
				// en landen i.p.v. de eenmalige Normal-extras (Silver Haze, papers, start-cash) — zo spawnt ook
				// co-op P2 met dezelfde startspullen als P1. De helper dedupt per speler + is no-op op load/clients.
				_BmT = FPlatformTime::Seconds();
				Sv->RestorePlayerByPawn(this);
				Sv->GrantNormalStartExtrasForPawn(this);
				UE_LOG(LogThePlugSIM, Display, TEXT("[BOOTMARK] Character RestorePlayerByPawn %.2fs (+%.2fs sinds start)"), FPlatformTime::Seconds() - _BmT, FPlatformTime::Seconds() - GStartTime);
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
		_BmT = FPlatformTime::Seconds();
		Phone->EnsureWidget();
		UE_LOG(LogThePlugSIM, Display, TEXT("[BOOTMARK] Character Phone->EnsureWidget %.2fs (+%.2fs sinds start)"), FPlatformTime::Seconds() - _BmT, FPlatformTime::Seconds() - GStartTime);
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
			UAssetKeepAliveSubsystem::Keep(this, IMC); // rooten over map-loads (laadtijd-fix)
			if (UInputAction* IA = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Interact.IA_Interact")))
			{
				UAssetKeepAliveSubsystem::Keep(this, IA);
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

void AThePlugSIMCharacter::HandleActivityKey()
{
	UWorld* W = GetWorld();
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!W || !PC) { return; }

	// Menu al open? Laat het de input afhandelen (Klaar-knop sluit).
	if (ActivityEditor && ActivityEditor->IsInViewport()) { return; }

	AActivitySpotManager* Mgr = nullptr;
	for (TActorIterator<AActivitySpotManager> It(W); It; ++It) { Mgr = *It; break; }
	if (!Mgr)
	{
		UWeedToast::NotifyPawn(this, -1, 3.f, FColor::Orange, TEXT("Activity spots are host-only."));
		return;
	}

	// Camera-trace: kijk ik naar een bestaande activity-NPC?
	FVector CamLoc = GetActorLocation(); FRotator CamRot = GetControlRotation();
	if (PC->PlayerCameraManager) { CamLoc = PC->PlayerCameraManager->GetCameraLocation(); CamRot = PC->PlayerCameraManager->GetCameraRotation(); }
	const FVector End = CamLoc + CamRot.Vector() * 600.f;
	FHitResult Hit;
	FCollisionQueryParams Q(FName(TEXT("ActivityPick")), false, this);
	ACustomerBase* Aimed = nullptr;
	if (W->LineTraceSingleByChannel(Hit, CamLoc, End, ECC_Pawn, Q))
	{
		Aimed = Cast<ACustomerBase>(Hit.GetActor());
		if (Aimed && !Aimed->IsActivityNpc()) { Aimed = nullptr; }
	}

	if (Aimed)
	{
		// Open het instel-menu voor deze NPC.
		if (!ActivityEditor)
		{
			ActivityEditor = CreateWidget<UActivitySpotEditorWidget>(PC, UActivitySpotEditorWidget::StaticClass());
		}
		if (ActivityEditor)
		{
			ActivityEditor->Setup(Mgr, Aimed);
			if (!ActivityEditor->IsInViewport()) { ActivityEditor->AddToViewport(60); }
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
			PC->bShowMouseCursor = true;
		}
		return;
	}

	// Niks aangewezen -> plaats een nieuwe activity-NPC (hele dag, eerste anim) op deze plek/kijkrichting.
	Mgr->AddSpotLive(GetActorLocation(), GetControlRotation().Yaw, 0, 0.f, 24.f);
	UWeedToast::NotifyPawn(this, -1, 4.f, FColor::Green, TEXT("Activity NPC placed. Aim at it + F10 to set animation / time."));
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

void AThePlugSIMCharacter::WeedSaveNoBuild()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Latch de huidige F9-markers (MarkedSpots.txt) als NO-BUILD-zones in een EIGEN bestand (NoBuildZones.txt) dat
	// GEEN ander dev-tool leegmaakt - zo verdwijnen je zones nooit meer. Paren markers = boxen; append (meerdere muren).
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	const FString MapName = W->GetOutermost()->GetName();
	FString Add; int32 N = 0;
	for (const FString& L : Lines)
	{
		if (L.Contains(MapName) && L.Contains(TEXT("pos=("))) { Add += L + TEXT("\n"); ++N; }
	}
	if (N < 2)
	{
		UWeedToast::NotifyPawn(this, -1, 5.f, FColor::Orange,
			TEXT("Mark at least 2 spots with F9 first (the diagonal corners of the area/wall), then run WeedSaveNoBuild."));
		return;
	}
	const FString File = FPaths::ProjectSavedDir() / TEXT("NoBuildZones.txt");
	FFileHelper::SaveStringToFile(Add, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
	// MarkedSpots leeg zodat de volgende zone vers begint (zoals de andere save-acties).
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UWeedToast::NotifyPawn(this, -1, 5.f, FColor::Green,
		FString::Printf(TEXT("No-build zone saved (%d corners). Permanent - run WeedClearNoBuild to reset."), N));
}

void AThePlugSIMCharacter::WeedClearNoBuild()
{
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("NoBuildZones.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UWeedToast::NotifyPawn(this, -1, 3.f, FColor::Cyan, TEXT("All no-build zones cleared."));
}

void AThePlugSIMCharacter::WeedSaveCompSpawns()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// COMPETITIVE co-op spawn-punten uit de F9-markers (MarkedSpots.txt) van DEZE map. Marker 1 = host-kamer,
	// marker 2 = joiner-kamer. Per marker omlaag tracen naar de vloer zodat de spawn netjes op de grond staat.
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	const FString MapName = W->GetOutermost()->GetName();
	TArray<FVector> Spots;
	for (const FString& L : Lines)
	{
		if (!L.Contains(MapName)) { continue; }
		int32 s = L.Find(TEXT("pos=("));
		if (s == INDEX_NONE) { continue; }
		s += 5;
		const int32 e = L.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, s);
		if (e == INDEX_NONE) { continue; }
		TArray<FString> N; L.Mid(s, e - s).ParseIntoArray(N, TEXT(","), true);
		if (N.Num() < 3) { continue; }
		// GEEN downtrace: hou de capsule-positie (zelfde referentie als de 703-anchor) zodat de meubel-Z klopt.
		Spots.Add(FVector(FCString::Atof(*N[0].TrimStartAndEnd()), FCString::Atof(*N[1].TrimStartAndEnd()), FCString::Atof(*N[2].TrimStartAndEnd())));
	}
	if (Spots.Num() < 2)
	{
		UWeedToast::NotifyPawn(this, -1, 6.f, FColor::Orange,
			TEXT("Set 2 spots with F9 first: marker 1 in your apartment, marker 2 in the room next door. Then WeedSaveCompSpawns."));
		return;
	}
	FString Out;
	for (int32 i = 0; i < 2; ++i) { Out += FString::Printf(TEXT("%.0f,%.0f,%.0f\n"), Spots[i].X, Spots[i].Y, Spots[i].Z); }
	FFileHelper::SaveStringToFile(Out, *(FPaths::ProjectSavedDir() / TEXT("CompSpawns.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	// MarkedSpots leeg zodat de volgende dev-tool vers begint (zoals de andere save-acties).
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UWeedToast::NotifyPawn(this, -1, 8.f, FColor::Green,
		TEXT("Competitive spawns saved: marker 1 = host room, marker 2 = neighbour (mirror). Start a FRESH Competitive game to see it."));
}

void AThePlugSIMCharacter::WeedClearCompSpawns()
{
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("CompSpawns.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UWeedToast::NotifyPawn(this, -1, 3.f, FColor::Cyan, TEXT("Competitive spawns cleared."));
}

void AThePlugSIMCharacter::WeedSaveMenuCam()
{
	// Leg de exacte camera-stand vast (locatie + rotatie van de PlayerCameraManager) als hoofdmenu-achtergrond.
	APlayerController* PC = Cast<APlayerController>(GetController());
	FVector CamLoc = GetActorLocation(); FRotator CamRot = GetControlRotation();
	if (PC && PC->PlayerCameraManager) { CamLoc = PC->PlayerCameraManager->GetCameraLocation(); CamRot = PC->PlayerCameraManager->GetCameraRotation(); }
	const FString MapPath = GetWorld() ? GetWorld()->GetOutermost()->GetName() : TEXT("?");
	// Eén regel (laatste wint): map|X|Y|Z|Pitch|Yaw|Roll. Overschrijft -> altijd de meest recente plek.
	const FString Line = FString::Printf(TEXT("%s|%.1f|%.1f|%.1f|%.2f|%.2f|%.2f"),
		*MapPath, CamLoc.X, CamLoc.Y, CamLoc.Z, CamRot.Pitch, CamRot.Yaw, CamRot.Roll);
	const FString File = FPaths::ProjectSavedDir() / TEXT("MenuCam.txt");
	FFileHelper::SaveStringToFile(Line, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UWeedToast::NotifyPawn(this, -1, 4.f, FColor::Green,
		TEXT("Menu camera saved here. It's now the main-menu backdrop on this map."));
}

void AThePlugSIMCharacter::WeedDev()
{
	// Dev-tools sessie-breed aan/uit (zelfde vlag als de Ctrl+Shift+F10-chord). Editor-gemak:
	// `WeedDev` in de console i.p.v. de chord. De toast komt lokaal (de vlag zelf repliceert).
	if (!Phone) { return; }
	const AWeedShopGameState* GSd = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const bool bOn = GSd && GSd->AreDevToolsEnabled();
	Phone->ServerSetDevTools(!bOn);
	UWeedToast::NotifyPawn(this, -1, 3.f, FColor::Cyan, !bOn ? TEXT("Dev tools enabled") : TEXT("Dev tools disabled"));
}

void AThePlugSIMCharacter::WeedRegisterHome()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Dev-tool (F6): alleen met dev-tools AAN.
	const AWeedShopGameState* GSd = W->GetGameState<AWeedShopGameState>();
	if (!GSd || !GSd->AreDevToolsEnabled()) { return; }
	for (TActorIterator<ADoorRetrofitter> It(W); It; ++It) { It->RegisterHomeAtPlayer(this); return; }
	UWeedToast::Notify(-1, 3.f, FColor::Orange, TEXT("WeedRegisterHome: geen DoorRetrofitter (alleen op de pack-map)."));
}

void AThePlugSIMCharacter::WeedMarkDeliveryPoint()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const AWeedShopGameState* GSd = W->GetGameState<AWeedShopGameState>();
	if (!GSd || !GSd->AreDevToolsEnabled()) { return; }
	// Eén regel (laatste wint), per map: map|X|Y|Z. FindDeliveryPoint leest dit als HOOGSTE prioriteit,
	// dus alle pakketjes landen voortaan op deze exacte plek (bv. helemaal beneden bij de hotel-ingang).
	const FVector L = GetActorLocation();
	const FString MapPath = W->GetOutermost()->GetName();
	const FString Line = FString::Printf(TEXT("%s|%.1f|%.1f|%.1f"), *MapPath, L.X, L.Y, L.Z);
	const FString File = FPaths::ProjectSavedDir() / TEXT("DeliveryPoint.txt");
	FFileHelper::SaveStringToFile(Line, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UWeedToast::NotifyPawn(this, -1, 5.f, FColor::Green,
		TEXT("Delivery point set here - all packages now drop at this spot (e.g. the hotel entrance)."));
}

void AThePlugSIMCharacter::WeedAddMeetSpot()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const AWeedShopGameState* GSd = W->GetGameState<AWeedShopGameState>();
	if (!GSd || !GSd->AreDevToolsEnabled()) { return; }
	// Append (meerdere plekken per map): map|X|Y|Z. "Come by"-afspraak-NPC's kiezen willekeurig één van deze
	// logische plekken (bij een winkel, steegje, hotel-hal) i.p.v. op een dak of midden op de weg.
	const FVector L = GetActorLocation();
	const FString MapPath = W->GetOutermost()->GetName();
	const FString Line = FString::Printf(TEXT("%s|%.1f|%.1f|%.1f"), *MapPath, L.X, L.Y, L.Z) + LINE_TERMINATOR;
	const FString File = FPaths::ProjectSavedDir() / TEXT("MeetSpots.txt");
	FFileHelper::SaveStringToFile(Line, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
	// Tel hoeveel er nu staan (voor deze map).
	int32 Count = 0; TArray<FString> All;
	if (FFileHelper::LoadFileToStringArray(All, *File)) { for (const FString& R : All) { if (R.TrimStartAndEnd().StartsWith(MapPath + TEXT("|"))) { ++Count; } } }
	UWeedToast::NotifyPawn(this, -1, 4.f, FColor::Cyan,
		FString::Printf(TEXT("Meet spot added (%d on this map). Appointment NPCs will wait at these spots."), Count));
}

void AThePlugSIMCharacter::WeedMarkBuildArea()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const AWeedShopGameState* GSd = W->GetGameState<AWeedShopGameState>();
	if (!GSd || !GSd->AreDevToolsEnabled()) { return; }
	const FVector L = GetActorLocation();
	const FVector Corner(L.X, L.Y, L.Z - GetSimpleCollisionHalfHeight()); // voet-hoogte = vloer
	if (!bHaveBuildCorner1)
	{
		BuildCorner1 = Corner; bHaveBuildCorner1 = true;
		UWeedToast::NotifyPawn(this, -1, 5.f, FColor::Cyan,
			TEXT("Build-area corner 1 set. Walk to the OPPOSITE corner of your room and press Ctrl+F9 again."));
		return;
	}
	// 2e hoek -> schrijf de box: map|x1|y1|z1|x2|y2|z2 (laatste wint). BuildComponent leest dit en staat dan
	// ALLEEN bouwen toe binnen deze box (jouw markers zijn leidend, niet de home-heuristiek).
	const FString MapPath = W->GetOutermost()->GetName();
	const FString Line = FString::Printf(TEXT("%s|%.1f|%.1f|%.1f|%.1f|%.1f|%.1f"),
		*MapPath, BuildCorner1.X, BuildCorner1.Y, BuildCorner1.Z, Corner.X, Corner.Y, Corner.Z);
	const FString File = FPaths::ProjectSavedDir() / TEXT("BuildArea.txt");
	FFileHelper::SaveStringToFile(Line, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	bHaveBuildCorner1 = false;
	UWeedToast::NotifyPawn(this, -1, 6.f, FColor::Green,
		TEXT("Build area set! You can now only build INSIDE these 2 corners (your room). Re-mark anytime."));
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
		// Nieuwe huiskamer-meubels (decoratie) - ook in de dev-pack zodat je er meteen mee kunt inrichten.
		Give(TEXT("Furn_ChairPlastic"), 3); Give(TEXT("Furn_ChairGarden"), 3); Give(TEXT("Furn_ChairWood"), 3);
		Give(TEXT("Furn_TableSmall"), 2);   Give(TEXT("Furn_TableRound"), 2);  Give(TEXT("Furn_CoffeeTable"), 2);
		Give(TEXT("Furn_Desk"), 2);         Give(TEXT("Furn_Bench"), 2);       Give(TEXT("Furn_Sofa"), 2);
		Give(TEXT("Furn_TV"), 2);           Give(TEXT("Furn_TVStand"), 2);     Give(TEXT("Furn_Bookshelf"), 2);
		Give(TEXT("Furn_Dresser"), 2);      Give(TEXT("Furn_Nightstand"), 2);  Give(TEXT("Furn_FloorLamp"), 3);
		Give(TEXT("Furn_Plant"), 3);        Give(TEXT("Furn_Planter"), 2);     Give(TEXT("Furn_DecoPot"), 3);
		Give(TEXT("Furn_Crate"), 3);
	}

	UWeedToast::Notify(-1, 3.f, FColor::Cyan, FString::Printf(TEXT("%d furniture cleared + furniture set (incl. sink) back in inventory."), N));
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
	// In plaats-modus: bevestig de plaatsing. MAAR kijk je een NIET-plaatsbaar wereld-object aan (deur,
	// lift, ...), dan interact je daar gewoon mee (deur openen) i.p.v. te plaatsen - zo loop je door je huis
	// terwijl je een item vasthoudt. (GetFocusedActor levert alleen interactables; plaatsbare dingen als een
	// pot/rek negeren we zodat je er nog steeds naast kunt plaatsen.)
	if (Build && Build->IsPlacing())
	{
		if (UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>())
		{
			if (AActor* Focus = IC->GetFocusedActor())
			{
				if (!Build->IsPickable(Focus)) { IC->TryInteract(); return; }
			}
		}
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
	if (Phone->IsLinkModeActive()) { Phone->ExitLinkMode(); return; } // Esc = klaar met lampen linken (markers weg)
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
		// Niet-plaatsbaar wereld-object aankijken (deur/lift) -> interact i.p.v. plaatsen bevestigen.
		if (UInteractionComponent* ICp = FindComponentByClass<UInteractionComponent>())
		{
			if (AActor* Focus = ICp->GetFocusedActor())
			{
				if (!Build->IsPickable(Focus)) { ICp->TryInteract(); return; }
			}
		}
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
	const int32 Grams = UInventoryComponent::JointGrams(JointId); // "Joint_5g" / "Joint_SilverHaze_5g" -> 5
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
