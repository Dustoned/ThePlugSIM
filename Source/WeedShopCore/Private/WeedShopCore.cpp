// WeedShopCore — module-implementatie. Registreert de module en definieert de log-categorie.

#include "WeedShopCore.h"

DEFINE_LOG_CATEGORY(LogWeedShop);

// Secundaire game-module (de primaire blijft 'ThePlugSIM' uit de template).
IMPLEMENT_MODULE(FDefaultGameModuleImpl, WeedShopCore);
