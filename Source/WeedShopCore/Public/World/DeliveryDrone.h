// ADeliveryDrone — bezorgdrone die tijdens de bestel-countdown naar de voordeur vliegt, daar het
// pakketje (ADeliveryPackage) laat vallen op het moment dat de levertijd om is, en daarna weer
// wegvliegt. Puur server-gedreven beweging (gerepliceerd); de rotors draaien cosmetisch overal.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DeliveryDrone.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UPhoneClientComponent;

UCLASS()
class WEEDSHOPCORE_API ADeliveryDrone : public AActor
{
	GENERATED_BODY()

public:
	ADeliveryDrone();

	virtual void Tick(float DeltaSeconds) override;

	// Server: configureer de vlucht + de te bezorgen bestelling.
	void Setup(const FVector& Start, const FVector& Drop, float FlightTime, int32 InOrderId,
		const TArray<FName>& InIds, const TArray<int32>& InQtys, UPhoneClientComponent* InPhone);

protected:
	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Rotors;

	FVector StartLoc = FVector::ZeroVector;
	FVector DropLoc = FVector::ZeroVector;
	float TotalTime = 10.f;
	float Elapsed = 0.f;
	float ReturnElapsed = 0.f;
	bool bDropped = false;

	int32 OrderId = 0;
	TArray<FName> Ids;
	TArray<int32> Qtys;
	TWeakObjectPtr<UPhoneClientComponent> Phone;

	static constexpr float HoverHeight = 280.f; // hoogte boven het droppunt tijdens het vliegen
	static constexpr float ReturnTime = 4.f;     // sec wegvliegen voor het verdwijnt
};
