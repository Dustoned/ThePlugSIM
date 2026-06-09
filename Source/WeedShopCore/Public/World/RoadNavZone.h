// Rijweg-nav-area + onzichtbare zone-actor. De CityGenerator legt over elke straat (de asfaltbanen
// tussen de bouwblokken) een ARoadNavZone die de navmesh daar als hoge-kosten "rijweg" markeert.
// Gevolg: NPC's lopen bij voorkeur over de stoep en steken een straat alleen over als dat echt
// korter is (= oversteken naar de volgende stoep), i.p.v. midden over de weg te dwalen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavAreas/NavArea.h"
#include "RoadNavZone.generated.h"

// Hoge traversal-kosten -> paden mijden de rijweg tenzij oversteken korter is.
UCLASS()
class WEEDSHOPCORE_API UNavArea_Road : public UNavArea
{
	GENERATED_BODY()
public:
	UNavArea_Road();
};

// Centrum-zone rond het park: matig-hoge kosten zodat DOORGAAND verkeer niet dwars door het centrum
// routeert (dat gaf een prop/RVO-gridlock bij het park). Park-bezoekers gaan er nog heen (bestemming),
// maar wie er alleen langs zou komen loopt nu eromheen.
UCLASS()
class WEEDSHOPCORE_API UNavArea_CityCenter : public UNavArea
{
	GENERATED_BODY()
public:
	UNavArea_CityCenter();
};

UCLASS()
class WEEDSHOPCORE_API ARoadNavZone : public AActor
{
	GENERATED_BODY()
public:
	ARoadNavZone();

	// HalfExtent = halve XYZ-omvang van het straatvak (rond de actor-locatie). AreaOverride != null ->
	// gebruik die nav-area i.p.v. de standaard rijweg-area (bv. de centrum-zone).
	void SetupZone(const FVector& HalfExtent, TSubclassOf<UNavArea> AreaOverride = nullptr);

protected:
	UPROPERTY() TObjectPtr<class UNavModifierComponent> Modifier;
};
