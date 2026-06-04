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

UCLASS()
class WEEDSHOPCORE_API ARoadNavZone : public AActor
{
	GENERATED_BODY()
public:
	ARoadNavZone();

	// HalfExtent = halve XYZ-omvang van het straatvak (rond de actor-locatie).
	void SetupZone(const FVector& HalfExtent);

protected:
	UPROPERTY() TObjectPtr<class UNavModifierComponent> Modifier;
};
