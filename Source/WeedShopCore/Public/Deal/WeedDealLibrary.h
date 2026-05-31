// UWeedDealLibrary — de tunebare deal-kern uit de brief. De acceptatie-kans is een functie van
// prijs t.o.v. markt + respect/loyaliteit/verslaving. Pure functie zodat de UI 'm live kan tonen
// terwijl de speler de prijs-slider beweegt, en de klant-AI 'm gebruikt om te beslissen.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WeedDealLibrary.generated.h"

// Uitkomst van een bod.
UENUM(BlueprintType)
enum class EDealResult : uint8
{
	Accepted	UMETA(DisplayName = "Geaccepteerd"),
	Haggle		UMETA(DisplayName = "Dingt af (te duur)"),
	Refused		UMETA(DisplayName = "Geweigerd"),
	NoStock		UMETA(DisplayName = "Geen voorraad")
};

UCLASS()
class WEEDSHOPCORE_API UWeedDealLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Acceptatie-% (0..100). Formule uit de brief (Sectie 3), attributen 0..100.
	//   prijsRatio = ask / markt
	//   basis      = 70 - (ratio - 1) * 100
	//   eind       = basis + respect*0.15 + loyaliteit*0.15 + verslaving*0.25
	//   + kwaliteit-term: slechte wiet schrikt vooral mensen met lage verslaving af; verslaafden
	//     malen er minder om. Quality01 in 0..1 (negatief = neutraal). -> clamp 0..100
	UFUNCTION(BlueprintPure, Category = "WeedShop|Deal")
	static float CalculateAcceptanceChance(float MarketPriceCents, float AskPriceCents,
		float Respect, float Loyalty, float Addiction, float Quality01 = -1.f);
};
