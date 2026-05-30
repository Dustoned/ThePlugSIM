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

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Time)
	float TimeOfDaySeconds = 0.f;

	UFUNCTION()
	void OnRep_Time();

	float CycleLength() const { return FMath::Max(1.f, DayLengthSeconds + NightLengthSeconds); }

	// Broadcast OnDayNightChanged bij een dag<->nacht-overgang.
	void CheckTransition();

	bool bWasNight = false;
};
