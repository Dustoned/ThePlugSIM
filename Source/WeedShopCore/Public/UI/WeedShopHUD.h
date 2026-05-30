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
};
