#include "SmokePuff.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ASmokePuff::ASmokePuff()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded()) { Mesh->SetStaticMesh(SphereFinder.Object); }

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SmokeFinder(TEXT("/Game/_Project/Materials/M_Smoke.M_Smoke"));
	if (SmokeFinder.Succeeded()) { SmokeMat = SmokeFinder.Object; }

	Mesh->SetRelativeScale3D(FVector(StartScale));
}

void ASmokePuff::BeginPlay()
{
	Super::BeginPlay();
	if (SmokeMat)
	{
		MID = UMaterialInstanceDynamic::Create(SmokeMat, this);
		if (MID) { Mesh->SetMaterial(0, MID); }
	}
}

void ASmokePuff::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Life += DeltaSeconds;
	const float A = FMath::Clamp(Life / MaxLife, 0.f, 1.f);

	// Groeien + langzaam opstijgen + uitdijen, en de opacity uitfaden.
	Mesh->SetWorldScale3D(FVector(FMath::Lerp(StartScale, EndScale, A)));
	AddActorWorldOffset(FVector(0.f, 0.f, DeltaSeconds * 22.f));
	if (MID) { MID->SetScalarParameterValue(TEXT("Opacity"), (1.f - A) * 0.5f); }

	if (Life >= MaxLife) { Destroy(); }
}
