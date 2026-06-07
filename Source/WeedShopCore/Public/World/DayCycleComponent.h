// UDayCycleComponent — doorlopende real-time dag/nacht-klok (20 min licht / 10 min donker),
// server-authoritative en gerepliceerd zodat alle co-op-spelers dezelfde tijd zien.
//
// CO-OP: de server telt de tijd op en repliceert TimeOfDaySeconds; clients lezen die waarde.
// Lighting (zon-rotatie) leest GetCycleFraction(); ambient/nacht-effecten luisteren op
// OnDayNightChanged. Geplaatst op de GameState (zie AWeedShopGameState), niet als WorldSubsystem,
// omdat subsystems niet repliceren.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DayCycleComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDayNightChanged, bool, bIsNight);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UDayCycleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDayCycleComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Duur van de licht- en donker-fase (seconden). Default 20 min / 10 min.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|DayNight")
	float DayLengthSeconds = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|DayNight")
	float NightLengthSeconds = 600.f;

	// Vuurt wanneer het van dag naar nacht of omgekeerd wisselt (server + clients).
	UPROPERTY(BlueprintAssignable, Category = "WeedShop|DayNight")
	FOnDayNightChanged OnDayNightChanged;

	UFUNCTION(BlueprintPure, Category = "WeedShop|DayNight")
	bool IsNight() const { return TimeOfDaySeconds >= DayLengthSeconds; }

	// 0..1 over de volledige cyclus — handig om de directional light mee te roteren.
	UFUNCTION(BlueprintPure, Category = "WeedShop|DayNight")
	float GetCycleFraction() const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|DayNight")
	float GetTimeOfDaySeconds() const { return TimeOfDaySeconds; }

	// Welke dag het is (begint op 1, +1 elke keer dat de cyclus rondloopt).
	UFUNCTION(BlueprintPure, Category = "WeedShop|DayNight")
	int32 GetDayNumber() const { return DayNumber; }

	// Wanneer de zon opkomt / ondergaat op de 24-uurs klok. De lichtfase (20 min) wordt over de
	// dag-uren gemapt, de donkerfase (10 min) over de nacht-uren -> een normale klok.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|DayNight")
	float SunriseHour = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|DayNight")
	float SunsetHour = 20.f;

	// Huidige kloktijd op een normale 24-uurs klok (0..24), geschaald naar de 20/10-cyclus.
	UFUNCTION(BlueprintPure, Category = "WeedShop|DayNight")
	float GetClockHour() const
	{
		const float DayHours = SunsetHour - SunriseHour;            // bv. 14 daglicht-uren
		const float NightHours = 24.f - DayHours;                   // bv. 10 nacht-uren
		if (!IsNight())
		{
			const float f = TimeOfDaySeconds / FMath::Max(1.f, DayLengthSeconds); // 0..1 overdag
			return SunriseHour + f * DayHours;                      // sunrise -> sunset
		}
		const float f = (TimeOfDaySeconds - DayLengthSeconds) / FMath::Max(1.f, NightLengthSeconds);
		return FMath::Fmod(SunsetHour + f * NightHours, 24.f);      // sunset -> sunrise
	}

	// Server-only: zet de tijd direct (voor save/load-herstel).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|DayNight")
	void SetTimeOfDaySeconds(float NewTime);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server: bij een nieuwe dag elke speler z'n huur laten verwerken.
	void OnNewDay(int32 NewDay);

	UPROPERTY(ReplicatedUsing = OnRep_Time)
	float TimeOfDaySeconds = 0.f;

	// Dag-teller (server telt op bij elke cyclus-wrap; gerepliceerd naar clients).
	UPROPERTY(Replicated)
	int32 DayNumber = 1;

	UFUNCTION()
	void OnRep_Time();

	float CycleLength() const { return FMath::Max(1.f, DayLengthSeconds + NightLengthSeconds); }

	// Broadcast OnDayNightChanged bij een dag<->nacht-overgang.
	void CheckTransition();

	bool bWasNight = false;
};
