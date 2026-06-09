#include "World/RoadNavZone.h"

#include "NavModifierComponent.h"
#include "Components/SceneComponent.h"

UNavArea_Road::UNavArea_Road()
{
	// Duurder dan normale grond: de stoep (default area, kosten 1) krijgt duidelijke voorrang,
	// maar oversteken moet wel aantrekkelijk blijven wanneer dat de normale route is.
	DefaultCost = 240.f;
	FixedAreaEnteringCost = 650.f;
	DrawColor = FColor(50, 50, 60);
}

UNavArea_CityCenter::UNavArea_CityCenter()
{
	// Matig-hoge kosten + flinke instap-toll: doorgaand verkeer kiest een route ERLANGS i.p.v. dwars door
	// het centrum (geen prop bij het park meer). Niet zo duur als een rijweg, zodat park-bezoekers er nog
	// gewoon heen kunnen (het is hun bestemming, niet doorgaand).
	DefaultCost = 28.f;
	FixedAreaEnteringCost = 900.f;
	DrawColor = FColor(70, 40, 70);
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

void ARoadNavZone::SetupZone(const FVector& HalfExtent, TSubclassOf<UNavArea> AreaOverride)
{
	if (!Modifier)
	{
		return;
	}
	if (AreaOverride)
	{
		Modifier->SetAreaClass(AreaOverride);
	}
	// Geen collision-component op deze actor -> de modifier valt terug op FailsafeExtent en past de
	// area toe over een box (actor-locatie ± HalfExtent).
	Modifier->FailsafeExtent = HalfExtent;
	Modifier->RefreshNavigationModifiers();
}
