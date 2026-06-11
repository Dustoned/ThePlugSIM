#include "World/PackElevator.h"

#include "WeedShopCore.h"
#include "World/PackElevatorButton.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

namespace
{
	constexpr float CabSpeed = 240.f;     // cm/s verticaal
	constexpr float DoorSlideTime = 0.7f; // seconden open/dicht
	constexpr float DwellSeconds = 6.f;   // deuren open laten staan zonder passagier
	constexpr float DepartSeconds = 1.6f; // na instappen: zo lang wachten en dan vertrekken
}

APackElevator::APackElevator()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // lokaal/cosmetisch, net als ACityElevator/ACityDoor

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Cab = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cab"));
	Cab->SetupAttachment(Root);
	Cab->SetMobility(EComponentMobility::Movable);
	Cab->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Cab->SetCanEverAffectNavigation(false);
	if (UStaticMesh* CabMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorCabin.SM_ElevatorCabin")))
	{
		Cab->SetStaticMesh(CabMesh);
	}
}

void APackElevator::Setup(const TArray<float>& InFloors, const FVector& InSlideDir, const TArray<FElevPanelInit>& InPanels, const FVector& CabCenterXY, const FVector& OpeningDir)
{
	Floors = InFloors;
	Floors.Sort();
	CabXY = FVector(CabCenterXY.X, CabCenterXY.Y, 0.f);
	SlideDir = InSlideDir;
	SlideDir.Z = 0.f;
	SlideDir.Normalize();

	Panels.Reset();
	for (const FElevPanelInit& P : InPanels)
	{
		if (!P.Comp) { continue; }
		// BELANGRIJK: de map-panelen zijn Static - zonder Movable doet SetWorldLocation stilletjes NIETS.
		P.Comp->SetMobility(EComponentMobility::Movable);
		FPanelRef R;
		R.Comp = P.Comp;
		R.ClosedPos = P.ClosedPos; // ECHTE dicht-stand (de map parkeert ze half-open)
		R.FloorIdx = P.FloorIdx;
		R.SlideDist = P.SlideDist;
		Panels.Add(R);
	}

	CurFloor = 0;
	TargetFloor = 0;
	CabZ = Floors.Num() ? Floors[0] : 0.f;
	DoorOpen = 1.f; // begane grond: deuren open, klaar om in te stappen
	DwellTimer = DwellSeconds;
	SetActorLocation(FVector(CabXY.X, CabXY.Y, CabZ));
	// Cabine-mesh: pivot zit OP de open kant (lokale bounds X -200..0, opening = +X-vlak).
	// CabCenterXY is het deurvlak (frame-pivot), dus actor-pivot daar neerzetten en de lokale +X
	// naar de gang draaien -> cabine valt precies in de schacht, opening tegen de deur.
	FVector Open2D = OpeningDir; Open2D.Z = 0.f;
	if (Open2D.Normalize()) { SetActorRotation(Open2D.Rotation()); }
	if (Cab) { Cab->SetRelativeLocation(FVector::ZeroVector); }

	// Verdieping-display IN de cabine: tegen de achterwand, op ooghoogte, kijkend naar de opening.
	if (Cab && !CabDigit)
	{
		CabDigit = NewObject<UStaticMeshComponent>(this);
		CabDigit->SetupAttachment(Cab);
		CabDigit->RegisterComponent();
		CabDigit->SetMobility(EComponentMobility::Movable);
		CabDigit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CabDigit->SetCanEverAffectNavigation(false);
		CabDigit->SetRelativeLocation(FVector(-192.f, 0.f, 175.f));
		CabDigit->SetRelativeRotation(FRotator::ZeroRotator); // lokale +X = naar de opening
		CabDigit->SetRelativeScale3D(FVector(5.f));
	}
	// Cabine-schuifdeuren: 2 panelen op de open kant (lokale X ~ -8), samen 136 breed gecentreerd.
	// Ze rijden mee met de cabine en schuiven synchroon met de hal-deuren open/dicht.
	if (Cab && !CabDoorFront)
	{
		// Hal-schuifrichting (pocket-kant) omzetten naar cabine-lokale Y (opening ligt langs lokale Y).
		const FVector LocalSlide = GetActorRotation().UnrotateVector(SlideDir);
		CabSlideSignY = (LocalSlide.Y >= 0.f) ? 1.f : -1.f;
		UStaticMesh* DoorMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorDoor.SM_ElevatorDoor"));
		auto MakeCabDoor = [&](const TCHAR* Name, const FVector& RelPos) -> UStaticMeshComponent*
		{
			UStaticMeshComponent* D = NewObject<UStaticMeshComponent>(this, Name);
			D->SetupAttachment(Cab);
			D->RegisterComponent();
			D->SetMobility(EComponentMobility::Movable);
			D->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			D->SetCanEverAffectNavigation(false);
			if (DoorMesh) { D->SetStaticMesh(DoorMesh); }
			D->SetRelativeLocation(RelPos);
			return D;
		};
		// Deur-mesh dekt lokaal [pivot-68, pivot]: pivots op 0 en +68 dekken samen [-68, +68].
		// FRONT = de helft die het verst van de pocket ligt (moet de hele opening oversteken, 146);
		// BACK = de pocket-kant-helft (73). Pocket op +Y -> front = pivot 0; pocket op -Y -> front = pivot 68.
		if (CabSlideSignY > 0.f)
		{
			CabDoorFrontBase = FVector(-8.f, 0.f, 0.f);
			CabDoorBackBase  = FVector(-8.f, 68.f, 0.f);
		}
		else
		{
			CabDoorFrontBase = FVector(-8.f, 68.f, 0.f);
			CabDoorBackBase  = FVector(-8.f, 0.f, 0.f);
		}
		CabDoorFront = MakeCabDoor(TEXT("CabDoorFront"), CabDoorFrontBase);
		CabDoorBack  = MakeCabDoor(TEXT("CabDoorBack"), CabDoorBackBase);
	}

	BuildCabButtonPanel();
	UpdateSigns();
}

void APackElevator::CallToFloor(int32 FloorIdx)
{
	if (!Floors.IsValidIndex(FloorIdx)) { return; }
	TargetFloor = FloorIdx;
}

void APackElevator::BuildCabButtonPanel()
{
	UWorld* W = GetWorld();
	if (!W || !Cab || Floors.Num() < 2) { return; }
	// Paneel op de zijwand naast de opening (lokale Y-wand aan de pocket-tegenovergestelde kant,
	// zodat 'ie niet achter de openschuivende cabine-deur verdwijnt): kolommen de diepte in, rijen omhoog.
	const float WallY = (CabSlideSignY > 0.f) ? -96.f : 96.f; // tegenover de pocket-kant
	const float FaceYaw = (WallY < 0.f) ? 90.f : -90.f;       // knop kijkt de cabine in
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Fi = 0; Fi < Floors.Num(); ++Fi)
	{
		const int32 Col = Fi / 4;
		const int32 Row = Fi % 4;
		const FVector RelPos(-34.f - Col * 26.f, WallY, 105.f + Row * 26.f);
		APackElevatorButton* Btn = W->SpawnActor<APackElevatorButton>(APackElevatorButton::StaticClass(), Cab->GetComponentTransform(), SP);
		if (!Btn) { continue; }
		Btn->Setup(this, Fi);
		Btn->SetCabMode();
		Btn->AttachToComponent(Cab, FAttachmentTransformRules::SnapToTargetIncludingScale);
		Btn->SetActorRelativeLocation(RelPos);
		Btn->SetActorRelativeRotation(FRotator(0.f, FaceYaw, 0.f));
		// Vast verdieping-cijfer boven de knop (klein), kijkend dezelfde kant op.
		const FTransform CabTM = Cab->GetComponentTransform();
		const FVector SignWorld = CabTM.TransformPosition(RelPos + FVector(0.f, (WallY < 0.f) ? 2.f : -2.f, 17.f));
		const FRotator SignRot = (CabTM.GetRotation() * FRotator(0.f, FaceYaw, 0.f).Quaternion()).Rotator();
		Btn->SetupSign(SignWorld, SignRot, 2.5f);
		Btn->SetDigit(Fi);
	}
}

void APackElevator::RegisterButton(APackElevatorButton* Btn)
{
	if (Btn)
	{
		Buttons.Add(Btn);
		Btn->SetDigit(CurFloor);
	}
}

void APackElevator::UpdateSigns()
{
	for (const TWeakObjectPtr<APackElevatorButton>& B : Buttons)
	{
		APackElevatorButton* Btn = B.Get();
		if (Btn && !Btn->IsCabButton()) { Btn->SetDigit(CurFloor); } // cab-knoppen houden hun vaste label
	}
	if (CabDigit && CabDigitShown != CurFloor)
	{
		CabDigitShown = CurFloor;
		const int32 D = FMath::Clamp(CurFloor, 0, 9);
		const FString Path = FString::Printf(TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorNumber_%d.SM_ElevatorNumber_%d"), D, D);
		if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path)) { CabDigit->SetStaticMesh(M); }
	}
}

bool APackElevator::IsPawnAboard() const
{
	UWorld* W = GetWorld();
	if (!W) { return false; }
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (!P) { continue; }
		const FVector L = P->GetActorLocation();
		if (FVector::Dist2D(L, GetActorLocation()) < 130.f && FMath::Abs(L.Z - (CabZ + 100.f)) < 160.f)
		{
			return true;
		}
	}
	return false;
}

void APackElevator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Floors.Num() < 2) { return; }

	const float TargetZ = Floors[TargetFloor];
	bMoving = !FMath::IsNearlyEqual(CabZ, TargetZ, 1.f);

	// Deuren: open op een verdieping met stilstaande cabine, dicht tijdens het rijden.
	const bool bWantOpen = !bMoving;
	DoorOpen = FMath::FInterpConstantTo(DoorOpen, bWantOpen ? 1.f : 0.f, DeltaSeconds, 1.f / DoorSlideTime);
	for (const FPanelRef& R : Panels)
	{
		UStaticMeshComponent* C = R.Comp.Get();
		if (!C) { continue; }
		// Alleen de panelen van de verdieping waar de cabine is (of vertrekt) bewegen mee; rest dicht.
		const bool bThisFloor = (R.FloorIdx == CurFloor);
		const float Amount = bThisFloor ? DoorOpen : 0.f;
		C->SetWorldLocation(R.ClosedPos + SlideDir * (R.SlideDist * Amount));
	}
	// Cabine-deuren schuiven synchroon mee (lokaal langs de opening).
	if (CabDoorFront) { CabDoorFront->SetRelativeLocation(CabDoorFrontBase + FVector(0.f, CabSlideSignY * 146.f * DoorOpen, 0.f)); }
	if (CabDoorBack)  { CabDoorBack->SetRelativeLocation(CabDoorBackBase + FVector(0.f, CabSlideSignY * 73.f * DoorOpen, 0.f)); }

	if (bMoving)
	{
		// Pas rijden als de deuren dicht zijn.
		if (DoorOpen > 0.02f) { return; }
		CabZ = FMath::FInterpConstantTo(CabZ, TargetZ, DeltaSeconds, CabSpeed);
		SetActorLocation(FVector(CabXY.X, CabXY.Y, CabZ));
		if (FMath::IsNearlyEqual(CabZ, TargetZ, 1.f))
		{
			CabZ = TargetZ;
			CurFloor = TargetFloor;
			DwellTimer = DwellSeconds;
			BoardedTimer = 0.f;
			UpdateSigns(); // bordjes boven de deuren: cabine is nu op deze verdieping
		}
		return;
	}

	// Geen automatisch rijden: de lift vertrekt alleen via de cabine-knoppen (kies verdieping)
	// of een call-knop op een verdieping. Echte lift-ervaring.
}

void APackElevator::Interact_Implementation(APawn* InstigatorPawn)
{
	// Geen actie op de cabine zelf: de verdieping kies je met de knoppen in de cabine.
	// (Eerst deed F hier "volgende verdieping" - dan ging de lift omhoog waar je ook klikte.)
}

FText APackElevator::GetInteractionPrompt_Implementation() const
{
	if (bMoving) { return FText::FromString(TEXT("Elevator moving...")); }
	return FText::FromString(FString::Printf(TEXT("Elevator - floor %d of 0-%d (use the buttons inside)"), CurFloor, Floors.Num() - 1));
}
