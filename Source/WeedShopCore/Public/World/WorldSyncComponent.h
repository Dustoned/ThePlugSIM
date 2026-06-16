// UWorldSyncComponent - gedeelde co-op-state voor NIET-gerepliceerde, per-client deterministisch gespawnde
// wereld-objecten (deuren, later lampen/liften). Zulke actors hebben geen net-identiteit om over een RPC te
// referencen, MAAR ze staan op elke machine op exact dezelfde positie. Daarom syncen we hun toestand via een
// STABIEL POSITIE-ID: de client stuurt het id (uint32) naar de server, de server bewaart de open-set hier
// (gerepliceerd), en elke client leest z'n eigen lokale deur-state op uit deze set. Component op de GameState.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WorldSyncComponent.generated.h"

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UWorldSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldSyncComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Stabiel id uit een wereld-transform (positie + yaw, afgerond) -> identiek op host en alle clients.
	static uint32 MakeId(const FVector& Loc, float Yaw);

	// Is deze deur open? (lokale deuren lezen dit elke tick.)
	bool IsDoorOpen(uint32 DoorId) const { return OpenDoors.Contains(DoorId); }

	// Server: zet/toggle een deur (door de interactie aangeroepen). Repliceert naar alle clients.
	void ServerToggleDoor(uint32 DoorId);
	void ServerSetDoor(uint32 DoorId, bool bOpen);

private:
	UFUNCTION()
	void OnRep_OpenDoors() {}

	// Set van OPEN deur-ids (dicht = niet in de lijst). TArray repliceert; klein (alleen open deuren).
	UPROPERTY(ReplicatedUsing = OnRep_OpenDoors)
	TArray<uint32> OpenDoors;
};
