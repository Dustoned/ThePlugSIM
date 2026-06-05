#include "World/RoadNavZone.h"

#include "NavModifierComponent.h"
#include "Components/SceneComponent.h"

UNavArea_Road::UNavArea_Road()
{
	// Extreem duurder dan normale grond: de stoep (default area, kosten 1) wint vrijwel altijd.
	// Een straat oversteken kan nog steeds, maar de router heeft een harde prikkel om asfalt-tijd
	// zo kort mogelijk te houden.
	DefaultCost = 650.f;
	FixedAreaEnteringCost = 1200.f;
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
