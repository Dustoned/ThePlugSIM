// AMapBorder - speelgebied-grens, door de speler gezet met F9-markers (in volgorde langs de rand).
// Bouwt hoge glazen wand-segmenten tussen de markers (Saved/MapBorder.txt). De wand blokkeert
// altijd, maar is alleen ZICHTBAAR als je dichtbij komt - en geeft een melding als je er tegenaan
// loopt. Persistent per sessie; beheer via de phone (Save/Clear map border).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MapBorder.generated.h"

class UStaticMeshComponent;

UCLASS()
class WEEDSHOPCORE_API AMapBorder : public AActor
{
	GENERATED_BODY()

public:
	AMapBorder();

	// Herlaadt Saved/MapBorder.txt en bouwt de wand-segmenten opnieuw.
	void Rebuild();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Segments;
	TArray<FVector> Points;
	TArray<TPair<FVector, FVector>> SegSpans; // per segment de eindpunten (incl. het sluitstuk)
	float ToastCooldown = 0.f;
};
