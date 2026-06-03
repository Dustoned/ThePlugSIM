#include "World/CeilingLamp.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

ACeilingLamp::ACeilingLamp()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	// Kapje: kegel met de punt omhoog (wijde kant omlaag), donker.
	Shade = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shade"));
	Shade->SetupAttachment(Root);
	if (Cone) { Shade->SetStaticMesh(Cone); }
	Shade->SetRelativeRotation(FRotator(180.f, 0.f, 0.f)); // omdraaien -> wijde kant onder
	Shade->SetRelativeScale3D(FVector(0.28f, 0.28f, 0.22f));
	Shade->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Shade->SetCollisionResponseToAllChannels(ECR_Block);
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = Shade->CreateDynamicMaterialInstance(0, BaseMat);
		if (M) { M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.07f, 0.07f, 0.09f)); }
	}

	// Lampje onderin het kapje (warm/geel).
	Bulb = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bulb"));
	Bulb->SetupAttachment(Root);
	if (Sphere) { Bulb->SetStaticMesh(Sphere); }
	Bulb->SetRelativeLocation(FVector(0.f, 0.f, -8.f));
	Bulb->SetRelativeScale3D(FVector(0.12f, 0.12f, 0.12f));
	Bulb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = Bulb->CreateDynamicMaterialInstance(0, BaseMat);
		if (M) { M->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.5f)); }
	}

	// Warme neerwaartse spot.
	Spot = CreateDefaultSubobject<USpotLightComponent>(TEXT("Spot"));
	Spot->SetupAttachment(Root);
	Spot->SetRelativeLocation(FVector(0.f, 0.f, -10.f));
	Spot->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f)); // naar beneden
	Spot->SetIntensity(6000.f);
	Spot->SetLightColor(FLinearColor(1.f, 0.82f, 0.5f));
	Spot->SetAttenuationRadius(1100.f);
	Spot->SetInnerConeAngle(28.f);
	Spot->SetOuterConeAngle(52.f);
	Spot->SetCastShadows(false);
}

void ACeilingLamp::BeginPlay()
{
	Super::BeginPlay();
}
