// IInteractable heeft geen eigen .cpp-implementaties nodig: UnrealHeaderTool genereert in
// UE 5.7 zelf de lege default-bodies voor de BlueprintNativeEvents. Concrete objecten
// overriden de *_Implementation-functies in hun eigen class (C++ of Blueprint).

#include "Interaction/Interactable.h"
