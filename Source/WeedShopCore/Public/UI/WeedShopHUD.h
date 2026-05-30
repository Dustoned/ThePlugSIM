// AWeedShopHUD — eenvoudige on-screen overlay puur in C++ (Canvas DrawText, geen UMG nodig).
// Toont de gedeelde kas, dag/nacht, de voorraad van de speler en de interactie-prompt.
// Bedoeld als snelle, zichtbare feedback; een nette UMG-HUD kan dit later vervangen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "WeedShopHUD.generated.h"

UCLASS()
class WEEDSHOPCORE_API AWeedShopHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void SetPhoneOpen(bool bOpen) { bPhoneOpen = bOpen; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	bool IsPhoneOpen() const { return bPhoneOpen; }

	// 0 = Upgrades, 1 = Suppliers (zaden).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void SetPhoneTab(int32 Tab) { PhoneTab = Tab; }

protected:
	// Tekent het telefoon-paneel (catalogus, genummerd 1..N) voor de actieve tab.
	void DrawPhone();

	bool bPhoneOpen = false;
	int32 PhoneTab = 0;
};
