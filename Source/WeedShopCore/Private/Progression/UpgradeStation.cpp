#include "Progression/UpgradeStation.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Game/WeedShopGameState.h"
#include "Progression/UpgradeComponent.h"
#include "Data/UpgradeRow.h"
#include "Engine/DataTable.h"

AUpgradeStation::AUpgradeStation()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
	}
	Mesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 0.6f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AUpgradeStation::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority())
	{
		return;
	}
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS && GS->GetUpgrades())
	{
		GS->GetUpgrades()->BuyUpgrade(UpgradeId);
	}
}

FText AUpgradeStation::GetInteractionPrompt_Implementation() const
{
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS && GS->GetUpgrades() && GS->GetUpgrades()->IsPurchased(UpgradeId))
	{
		return FText::FromString(FString::Printf(TEXT("%s (purchased)"), *UpgradeId.ToString()));
	}
	return FText::FromString(FString::Printf(TEXT("Buy upgrade: %s"), *UpgradeId.ToString()));
}
