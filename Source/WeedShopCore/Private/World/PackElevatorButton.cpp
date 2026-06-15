#include "World/PackElevatorButton.h"

#include "World/PackElevator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"

APackElevatorButton::APackElevatorButton()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCanEverAffectNavigation(false);
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorCallButton01.SM_ElevatorCallButton01")))
	{
		Mesh->SetStaticMesh(M);
	}
}

void APackElevatorButton::Setup(APackElevator* InElevator, int32 InFloorIdx)
{
	Elevator = InElevator;
	FloorIdx = InFloorIdx;
}

void APackElevatorButton::SetupSign(const FVector& SignWorldLoc, const FRotator& SignRot, float Scale)
{
	if (!DigitMesh)
	{
		DigitMesh = NewObject<UStaticMeshComponent>(this);
		DigitMesh->SetupAttachment(GetRootComponent());
		DigitMesh->RegisterComponent();
		DigitMesh->SetMobility(EComponentMobility::Movable);
		DigitMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DigitMesh->SetCanEverAffectNavigation(false);
		// Cijfer-VLAK zwart maken (de cijfer-textuur kwam er toch niet uit). Het cijfer zelf zetten we er
		// wit (TextRender) overheen -> zwart scherm + wit oplichtend nummer.
		static UMaterialInterface* Black = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_DigitBlack.M_DigitBlack"));
		if (Black) { DigitMesh->SetMaterial(0, Black); }
	}
	DigitMesh->SetWorldLocationAndRotation(SignWorldLoc, SignRot);
	DigitMesh->SetWorldScale3D(FVector(Scale)); // digit-mesh is 3x5cm -> opschalen naar leesbaar formaat
	SignLocW = SignWorldLoc;
	SignRotW = SignRot;
	bHaveSign = true;
	// Cabine-knop: het CIJFER is de knop - aankijken van het cijfer moet de interact-prompt geven.
	if (bCabMode)
	{
		DigitMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
}

void APackElevatorButton::SetArrow(int32 Dir)
{
	if (bCabMode || !bHaveSign || Dir == CurArrow) { return; }
	CurArrow = Dir;
	if (!ArrowText)
	{
		ArrowText = NewObject<UTextRenderComponent>(this);
		ArrowText->SetupAttachment(GetRootComponent());
		ArrowText->RegisterComponent();
		ArrowText->SetMobility(EComponentMobility::Movable);
		// Naast het verdieping-bordje boven de deur, zelfde kant op kijkend.
		ArrowText->SetWorldLocationAndRotation(SignLocW + SignRotW.RotateVector(FVector(2.f, -26.f, 0.f)), SignRotW);
		ArrowText->SetWorldSize(24.f);
		ArrowText->SetHorizontalAlignment(EHTA_Center);
		ArrowText->SetVerticalAlignment(EVRTA_TextCenter);
	}
	if (Dir == 0)
	{
		ArrowText->SetVisibility(false);
		return;
	}
	ArrowText->SetVisibility(true);
	ArrowText->SetText(FText::AsCultureInvariant(Dir > 0 ? TEXT("^") : TEXT("v")));
	ArrowText->SetTextRenderColor(Dir > 0 ? FColor(110, 255, 140) : FColor(255, 170, 90));
}

void APackElevatorButton::SetHighlight(bool bOn)
{
	if (!GlowLight)
	{
		if (!bOn) { return; }
		GlowLight = NewObject<UPointLightComponent>(this);
		GlowLight->SetupAttachment(DigitMesh ? DigitMesh.Get() : Cast<USceneComponent>(GetRootComponent()));
		GlowLight->RegisterComponent();
		GlowLight->SetMobility(EComponentMobility::Movable);
		GlowLight->SetRelativeLocation(FVector(2.f, 0.f, 0.f)); // net voor het cijfer-plaatje
		GlowLight->bUseInverseSquaredFalloff = false;
		GlowLight->LightFalloffExponent = 2.f;
		GlowLight->SetAttenuationRadius(50.f);
		GlowLight->SetLightColor(FLinearColor(1.f, 0.82f, 0.45f));
		GlowLight->SetCastShadows(false);
		GlowLight->SetIntensity(0.f);
	}
	GlowLight->SetIntensity(bOn ? 5.f : 0.f);
}

void APackElevatorButton::SetDigit(int32 Digit)
{
	if (!DigitMesh || Digit == CurDigit) { return; }
	CurDigit = Digit;
	const FString Path = FString::Printf(TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorNumber_%d.SM_ElevatorNumber_%d"),
		FMath::Clamp(Digit, 0, 9), FMath::Clamp(Digit, 0, 9));
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path))
	{
		DigitMesh->SetStaticMesh(M);
		// SetStaticMesh kan de materiaal-override resetten -> het zwarte vlak opnieuw zetten.
		static UMaterialInterface* Black = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_DigitBlack.M_DigitBlack"));
		if (Black) { DigitMesh->SetMaterial(0, Black); }
	}
	// Zwart vlak + WIT cijfer er net voor -> ziet eruit als een verlicht nummer (wit op zwart).
	if (!DigitText)
	{
		DigitText = NewObject<UTextRenderComponent>(this);
		DigitText->SetupAttachment(GetRootComponent());
		DigitText->RegisterComponent();
		DigitText->SetMobility(EComponentMobility::Movable);
		DigitText->SetHorizontalAlignment(EHTA_Center);
		DigitText->SetVerticalAlignment(EVRTA_TextCenter);
		DigitText->SetTextRenderColor(FColor(245, 245, 250));
		DigitText->SetCanEverAffectNavigation(false);
		DigitText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Zelf-oplichtend (unlit emissive) tekst-materiaal: het cijfer is overal helder wit, ook in een
		// donkere gang (het default tekst-materiaal is BELICHT -> daar werd het cijfer dof).
		static UMaterialInterface* TextGlow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_DigitTextGlow.M_DigitTextGlow"));
		if (TextGlow) { DigitText->SetTextMaterial(TextGlow); }
	}
	const FVector Fwd = DigitMesh->GetForwardVector(); // +X = uit het vlak naar de kijker
	DigitText->SetWorldLocationAndRotation(DigitMesh->GetComponentLocation() + Fwd * 3.f, DigitMesh->GetComponentRotation());
	DigitText->SetWorldSize(FMath::Max(8.f, DigitMesh->GetComponentScale().X * 4.2f));
	DigitText->SetText(FText::AsNumber(FMath::Clamp(Digit, 0, 9)));
}

void APackElevatorButton::Interact_Implementation(APawn* InstigatorPawn)
{
	if (APackElevator* E = Elevator.Get()) { E->CallToFloor(FloorIdx); }
}

void APackElevatorButton::SetCabMode()
{
	bCabMode = true;
}

FText APackElevatorButton::GetInteractionPrompt_Implementation() const
{
	if (bCabMode)
	{
		return FText::FromString(FloorIdx == 0
			? FString(TEXT("Go to ground floor (0)"))
			: FString::Printf(TEXT("Go to floor %d"), FloorIdx));
	}
	return FText::FromString(TEXT("Call elevator"));
}
