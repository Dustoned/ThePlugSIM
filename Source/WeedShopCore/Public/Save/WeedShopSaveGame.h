// UWeedShopSaveGame — opslagcontainer voor de gedeelde voortgang. Bewust uitbreidbaar; nu
// alleen kas + dag-tijd. Later: voorraad, planten, klant-relaties.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "WeedShopSaveGame.generated.h"

UCLASS()
class WEEDSHOPCORE_API UWeedShopSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 SaveVersion = 1;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int64 BalanceCents = 0; // cash (zwart)

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int64 BankCents = 0;    // bank (wit)

	UPROPERTY(VisibleAnywhere, Category = "Save")
	bool bBankAppUnlocked = false; // telefoon-upgrade gekocht?

	UPROPERTY(VisibleAnywhere, Category = "Save")
	float TimeOfDaySeconds = 0.f;
};
