// WeedShopCore — module-header. Declareert de gedeelde log-categorie voor alle game-logica.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Gebruik in code als:  UE_LOG(LogWeedShop, Log, TEXT("..."));
DECLARE_LOG_CATEGORY_EXTERN(LogWeedShop, Log, All);

// Roep dit aan vlak vóór een level-reload die IN-GAME gaat (New Game/Load/Continue): de eerstvolgende
// map-load toont dan het laadscherm. De boot naar het hoofdmenu zet dit NIET -> daar geen laadscherm.
WEEDSHOPCORE_API void WeedShop_RequestGameLoadingScreen();
