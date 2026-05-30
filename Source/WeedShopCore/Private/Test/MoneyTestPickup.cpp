#include "Test/MoneyTestPickup.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"

AMoneyTestPickup::AMoneyTestPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	// Engine-kubus zodat het object meteen zichtbaar is én de interactie-trace (Visibility) raakt.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
	}
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AMoneyTestPickup::Interact_Implementation(APawn* InstigatorPawn)
{
	// De interactie-component routeert deze aanroep via de server; hier hebben we authority.
	if (!HasAuthority())
	{
		return;
	}

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (!GS || !GS->GetEconomy())
	{
		UE_LOG(LogWeedShop, Warning,
			TEXT("MoneyTestPickup: geen AWeedShopGameState/Economy. Zet de Game State Class op AWeedShopGameState."));
		return;
	}

	GS->GetEconomy()->AddMoneyUntracked(AmountCents);
	UE_LOG(LogWeedShop, Log, TEXT("MoneyTestPickup: +%d cents -> saldo nu %lld cents"),
		AmountCents, (long long)GS->GetEconomy()->GetBalanceCents());
}

FText AMoneyTestPickup::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("WeedShop", "MoneyTestPrompt", "Pak geld (test)");
}
