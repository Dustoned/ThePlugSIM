// ARoomStamper - de kamer-stempel: kies een opgeslagen kamer-template, zie een live preview aan je
// crosshair (gesnapt op de bouw-grid of DEUR-OP-DEUR op een bestaand deurframe), draai met R en
// plaats met LMB. Geplaatste stempels worden gelogd (Saved/RoomStamps.txt) zodat ze elke sessie
// herbouwd worden tot ze gebakken zijn, en geexporteerd naar RoomBake.txt voor de bake.
//
// Template-formaat (Saved/RoomTemplates/<naam>.txt), anker = entree-deurframe van de bron:
//   PIECE|<meshpad>|relX,relY,relZ|relPitch,relYaw,relRoll|sX,sY,sZ|mat0;mat1;...

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomStamper.generated.h"

class UStaticMeshComponent;

USTRUCT()
struct FStampPiece
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
	FTransform RelTM; // relatief aan het anker (entree-deurframe van de bron)
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Mats;
};

UCLASS()
class WEEDSHOPCORE_API ARoomStamper : public AActor
{
	GENERATED_BODY()

public:
	ARoomStamper();

	// Start de stempel-modus met dit template (bestandsnaam zonder pad/extensie).
	bool BeginStamp(const FString& TemplateName);
	void CancelStamp();
	bool IsStamping() const { return bStamping; }

	// Sla de kamer binnen de huidige 2 markers op als template (anker = deurframe in de rechthoek).
	static bool SaveTemplateFromMarkers(UWorld* W, const FString& TemplateName, FString& OutError);

	// Alle beschikbare template-namen (Saved/RoomTemplates).
	static TArray<FString> ListTemplates();

	// Template-stukken laden (gedeeld met de sessie-herbouw in de retrofitter).
	static bool LoadTemplate(const FString& TemplateName, TArray<FStampPiece>& OutPieces);

	// Geplaatste, nog NIET gebakken stempels (regels uit RoomStamps.txt).
	static TArray<FString> ListPlacedStamps(UWorld* W);
	// Verwijder een geplaatste stempel: actors weg + regel uit RoomStamps.txt + bake-blok uit RoomBake.txt.
	static bool RemoveStamp(UWorld* W, const FString& StampLine);
	// Undo: de laatst geplaatste (niet gebakken) stempel weghalen.
	static bool UndoLastStamp(UWorld* W, FString& OutInfo);
	// Stamp-id (STAMP_x_y_yaw) uit een RoomStamps-regel (template|x,y,z|yaw).
	static FString StampIdFromLine(const FString& Line);

	// Spiegel een piece-transform over het XZ-vlak van het anker (links-rechts t.o.v. de voordeur):
	// locatie Y-negatie, rotator (pitch, -yaw, -roll), scale Y-negatie (mesh zelf gespiegeld).
	static FTransform MirrorRelTM(const FTransform& In);

protected:
	virtual void Tick(float DeltaSeconds) override;

	void PlaceStamp();
	void UpdatePreview();
	FTransform ComputeAnchor() const; // crosshair -> deur-snap of grid-snap

	bool bStamping = false;
	FString ActiveTemplate;
	UPROPERTY() TArray<FStampPiece> Pieces;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PreviewComps;
	FTransform CurrentAnchor;
	FVector CentroidRel = FVector::ZeroVector; // kamer-zwaartepunt relatief aan het anker (voor van-je-af plaatsen)
	float UserYaw = 0.f;     // R-rotatie (90-graden stappen, of 180-flip bij deur-snap)
	bool bSnappedToDoor = false;
	bool bMirrored = false; // T = spiegelen (links-rechts t.o.v. de voordeur)
	bool bRotKeyWas = false, bPlaceKeyWas = false, bCancelKeyWas = false, bMirrorKeyWas = false;
};
