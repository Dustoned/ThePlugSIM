#include "World/RoadNavZone.h"

#include "NavModifierComponent.h"
#include "Components/SceneComponent.h"

UNavArea_Road::UNavArea_Road()
{
	// Duurder dan normale grond: de stoep (default area, kosten 1) krijgt duidelijke voorrang.
	// Een straat oversteken kan nog steeds zonder de pathfinder zwaar te maken bij 40 bewoners.
	DefaultCost = 90.f;
	FixedAreaEnteringCost = 180.f;
	DrawColor = FColor(50, 50, 60);
}

ARoadNavZone::ARoadNavZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // navmesh wordt lokaal per client opgebouwd (net als de stad)

	USceneComponent* Rt = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Rt);

	Modifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("RoadModifier"));
	Modifier->SetAreaClass(UNavArea_Road::StaticClass());
}

void ARoadNavZone::SetupZone(const FVector& HalfExtent)
{
	if (!Modifier)
	{
		return;
	}
	// Geen collision-component op deze actor -> de modifier valt terug op FailsafeExtent en past de
	// rijweg-area toe over een box (actor-locatie ± HalfExtent).
	Modifier->FailsafeExtent = HalfExtent;
	Modifier->RefreshNavigationModifiers();
}
