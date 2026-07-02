#include "World/DeliveryDrone.h"

#include "World/DeliveryPackage.h"
#include "Phone/PhoneClientComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

ADeliveryDrone::ADeliveryDrone()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(SceneRoot);
	if (CubeFinder.Succeeded()) { Body->SetStaticMesh(CubeFinder.Object); }
	Body->SetWorldScale3D(FVector(0.7f, 0.7f, 0.18f)); // plat platformpje
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (MatFinder.Succeeded()) { Body->SetMaterial(0, MatFinder.Object); }

	// Vier rotors op de hoeken (cosmetisch, draaien in Tick).
	const FVector Corners[4] = {
		FVector( 40.f,  40.f, 14.f), FVector(-40.f,  40.f, 14.f),
		FVector( 40.f, -40.f, 14.f), FVector(-40.f, -40.f, 14.f) };
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* R = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Rotor%d"), i));
		R->SetupAttachment(SceneRoot);
		if (CylFinder.Succeeded()) { R->SetStaticMesh(CylFinder.Object); }
		R->SetRelativeLocation(Corners[i]);
		R->SetWorldScale3D(FVector(0.5f, 0.5f, 0.04f));
		R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (MatFinder.Succeeded()) { R->SetMaterial(0, MatFinder.Object); }
		Rotors.Add(R);
	}
}

void ADeliveryDrone::Setup(const FVector& Start, const FVector& Drop, float FlightTime, int32 InOrderId,
	const TArray<FName>& InIds, const TArray<int32>& InQtys, UPhoneClientComponent* InPhone)
{
	StartLoc = Start;
	DropLoc = Drop;
	TotalTime = FMath::Max(1.f, FlightTime);
	OrderId = InOrderId;
	Ids = InIds;
	Qtys = InQtys;
	Phone = InPhone;
	SetActorLocation(StartLoc);
}

void ADeliveryDrone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Rotors draaien (cosmetisch, overal).
	for (UStaticMeshComponent* R : Rotors)
	{
		if (R) { R->AddLocalRotation(FRotator(0.f, DeltaSeconds * 1400.f, 0.f)); }
	}

	// Beweging alleen op de server (repliceert naar clients).
	if (!HasAuthority()) { return; }

	const FVector Hover = DropLoc + FVector(0.f, 0.f, HoverHeight);

	if (!bDropped)
	{
		Elapsed += DeltaSeconds;
		const float A = FMath::Clamp(Elapsed / TotalTime, 0.f, 1.f);
		const float S = FMath::SmoothStep(0.f, 1.f, A); // soepel in/uit
		FVector Pos = FMath::Lerp(StartLoc, Hover, S);
		// Laatste stukje: zak omlaag richting het droppunt zodat het pakket netjes valt.
		if (A > 0.85f)
		{
			const float D = (A - 0.85f) / 0.15f;
			Pos.Z = FMath::Lerp(Hover.Z, DropLoc.Z + 60.f, D);
		}
		// Kijkrichting volgt de bewegingsrichting (grof).
		const FVector Flat = (Hover - GetActorLocation()) * FVector(1.f, 1.f, 0.f);
		if (!Flat.IsNearlyZero()) { SetActorRotation(Flat.Rotation()); }
		SetActorLocation(Pos);

		if (A >= 1.f)
		{
			bDropped = true;
			// Laat het pakketje vallen op het droppunt.
			if (UWorld* World = GetWorld())
			{
				FActorSpawnParameters P;
				P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				// Iets hoger spawnen dan de doos-halfhoogte zodat 'ie zichtbaar valt/tuimelt/settelt (physics in SetupOrder).
				ADeliveryPackage* Pkg = World->SpawnActor<ADeliveryPackage>(
					ADeliveryPackage::StaticClass(), FTransform(FRotator::ZeroRotator, DropLoc + FVector(0.f, 0.f, 60.f)), P);
				if (Pkg) { Pkg->SetupOrder(OrderId, Ids, Qtys, Phone.Get()); }
			}
			if (Phone.IsValid()) { Phone->NotifyDroneArrived(OrderId); }
		}
	}
	else
	{
		// Wegvliegen omhoog/terug en daarna verdwijnen.
		ReturnElapsed += DeltaSeconds;
		const FVector Away = Hover + (StartLoc - Hover) * 0.6f + FVector(0.f, 0.f, 400.f);
		SetActorLocation(FMath::Lerp(GetActorLocation(), Away, FMath::Clamp(DeltaSeconds * 1.5f, 0.f, 1.f)));
		if (ReturnElapsed >= ReturnTime) { Destroy(); }
	}
}
