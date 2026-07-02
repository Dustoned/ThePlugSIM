// UAssetKeepAliveSubsystem — houdt runtime-geladen assets (skin-meshes, ABP's, outfit-parts,
// widget-klassen, input-assets) GEROOT over map-loads heen. Zonder root purgt de LoadMap-GC de
// hele skin/ABP-keten en wordt die op de vólgende map opnieuw geladen + gecompileerd (gemeten:
// ~26s stil blok in AThePlugSIMCharacter::BeginPlay, op menu- ÉN wereld-load). GameInstance-
// subsystem = overleeft level-wissels; bij Deinitialize (game afsluiten) komt alles gewoon vrij.
// Nette variant van "static rooten": een eigenaar met een duidelijke levensduur, geen dangelende
// statics na GC (zie de oude comment bij de ABP-load in ApplySkinMesh).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "AssetKeepAliveSubsystem.generated.h"

UCLASS()
class WEEDSHOPCORE_API UAssetKeepAliveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// Root dit asset tot het einde van de game-sessie (dedup; null-veilig). Nooit tussentijds
	// vrijgeven: het hele punt is dat de LoadMap-GC deze assets NIET meer mag purgen.
	void KeepAlive(UObject* Obj);

	// Convenience voor call-sites: pak het subsystem via de world-context en root het asset.
	// Null-veilig (geen wereld/GameInstance = stille no-op, bv. in de editor-preview).
	static void Keep(const UObject* WorldContext, UObject* Obj);

private:
	// Geroote assets. TStrongObjectPtr = GC-root zonder UPROPERTY-boekhouding.
	TArray<TStrongObjectPtr<UObject>> Kept;
};
