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

	// LIFT (per-client deterministisch gespawnd, net als de deuren): de gedeelde staat is de DOEL-verdieping
	// per lift-id. Elke lokale cabine (host EN joiner) leest z'n doel elke tick uit deze set -> ze rijden naar
	// EXACT dezelfde verdieping. De cosmetische interp (CabZ/deur-slide) blijft lokaal.
	// Onbekend id -> INDEX_NONE = "geen server-doel", de lift laat z'n eigen begin-doel staan.
	int32 GetElevatorFloor(uint32 ElevId) const;

	// Server: schrijf de doel-verdieping van een lift (door de knop-interactie aangeroepen). Repliceert.
	void ServerSetElevatorFloor(uint32 ElevId, int32 Floor);

	// LIVE CABINE-HOOGTE per lift-id (co-op rubber-band-fix H.4): de cabine (APackElevator) is niet-gerepliceerd
	// en werd op host EN joiner ONAFHANKELIJK lokaal geinterpoleerd -> tijdens de rit stonden de twee cabines op
	// VERSCHILLENDE Z (interp-fase verschilt door replicatie-latency van het doel) -> de speler als movement-base
	// op de cab-vloer kreeg een base-relatieve correctie -> teleport terug. Fix: de SERVER interpoleert en schrijft
	// de live CabZ hier; de CLIENT interpoleert NIET maar volgt deze waarde -> host en joiner staan op EXACT
	// dezelfde Z -> de base-correctie is een no-op.
	// Fallback = de meegegeven waarde (huidige lokale CabZ) als er nog geen server-waarde binnen is.
	float GetElevatorZ(uint32 ElevId, float Fallback) const;

	// Server: schrijf de live cabine-hoogte van een lift (elke tick door APackElevator op de server). Repliceert.
	void SetElevatorZ(uint32 ElevId, float Z);

	// WEER (co-op-sync H.12): de DayNightController is per-proces niet-gerepliceerd en koos het weer lokaal random
	// -> host en joiner kregen een ANDER weer. Zelfde patroon als de lift-CabZ: de SERVER kiest de preset-index
	// (uit de deterministische WeatherPresets-volgorde) + de overgangs-duur en schrijft ze hier; de CLIENT kiest
	// NIET zelf maar leest deze index en past hetzelfde preset toe zodra het wijzigt. -1 = "nog geen keuze".
	int32 GetWeatherIndex() const { return WeatherIndex; }
	float GetWeatherDuration() const { return WeatherDuration; }

	// Server: schrijf de gekozen weer-index + overgangs-duur (door de DayNightController op de server). Repliceert.
	void SetWeather(int32 Index, float Duration);

	// LAMPEN (co-op-sync): APackLightSwitch is per-proces niet-gerepliceerd + client-lokale interact -> host/joiner
	// (en late joiners) zagen verschillend lamp-aan/uit/dim. Zelfde id-patroon als de deuren: de server bewaart per
	// stabiel lamp-id de aan/uit + helderheid; elke lokale schakelaar leest 'm elke tick + past 'm toe.
	void SetLampState(uint32 LampId, bool bOn, float Brightness01);
	bool GetLampState(uint32 LampId, bool& bOutOn, float& OutBright) const;

private:
	UFUNCTION()
	void OnRep_OpenDoors() {}
	UFUNCTION()
	void OnRep_Elevators() {}

	// Gedeeld weer (H.12): server-gekozen preset-index (index in de deterministische WeatherPresets-volgorde in de
	// DayNightController) + overgangs-duur in seconden. -1 = nog geen keuze. Elke client leest deze in z'n Tick en
	// past 'm toe zodra WeatherIndex wijzigt (bijhouden via een lokale last-seen; geen OnRep nodig).
	UPROPERTY(Replicated)
	int32 WeatherIndex = -1;
	UPROPERTY(Replicated)
	float WeatherDuration = 0.f;

	// Set van OPEN deur-ids (dicht = niet in de lijst). TArray repliceert; klein (alleen open deuren).
	UPROPERTY(ReplicatedUsing = OnRep_OpenDoors)
	TArray<uint32> OpenDoors;

	// Gedeelde lift-staat: id -> doel-verdieping. Losse parallelle arrays (USTRUCT-vrij; TMap repliceert niet).
	// Klein (aantal liften op de map), dus lineair zoeken is prima.
	UPROPERTY(ReplicatedUsing = OnRep_Elevators)
	TArray<uint32> ElevatorIds;
	UPROPERTY(ReplicatedUsing = OnRep_Elevators)
	TArray<int32> ElevatorFloors;
	// Live cabine-hoogte, parallel aan ElevatorIds (zelfde index). Server schrijft, clients lezen -> cabines op
	// exact dezelfde Z (H.4-rubber-band-fix). Geen ReplicatedUsing nodig: PackElevator leest 'm per tick uit.
	UPROPERTY(Replicated)
	TArray<float> ElevatorZ;

	// Gedeelde lamp-staat: parallelle arrays id -> aan/uit (0/1) + helderheid (0..1). Server schrijft, clients lezen.
	UPROPERTY(Replicated)
	TArray<uint32> LampIds;
	UPROPERTY(Replicated)
	TArray<uint8> LampOn;
	UPROPERTY(Replicated)
	TArray<float> LampBright;
};
