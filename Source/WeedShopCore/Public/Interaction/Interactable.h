// IInteractable — alles in de wereld waar de speler op kan interacten (toonbank, grow-pot,
// drogerek, kassa, schap). Implementeer in C++ of in Blueprint.
//
// Editor-koppeling: niets nodig op de interface zelf. Maak je interact-bare object een
// Blueprint die deze interface implementeert (Class Settings -> Interfaces -> Add -> Interactable),
// of laat een C++-class ervan erven en override de *_Implementation-functies.
//
// In UE 5.7 genereert UnrealHeaderTool zelf een lege default-implementatie voor elke
// BlueprintNativeEvent hieronder; je hoeft die dus NIET in een .cpp te definiëren.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

class IInteractable
{
	GENERATED_BODY()

public:
	// Uitgevoerd wanneer de speler interact (bv. E indrukt) terwijl hij dit object aankijkt.
	// Gating (mag het nu wel/niet) doe je binnen je eigen override.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeedShop|Interaction")
	void Interact(APawn* InstigatorPawn);

	// Korte regel voor de prompt-UI, bv. "Druk E om te oogsten". Leeg = geen prompt.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "WeedShop|Interaction")
	FText GetInteractionPrompt() const;

	// CO-OP: true = deze interactie draait LOKAAL op de speler die 'm doet (geen Server-RPC). Bedoeld voor
	// NIET-gerepliceerde, per-speler wereld-objecten (deuren/liften/schakelaars): ieder bedient z'n eigen
	// lokale kopie zodat visueel + collision per scherm kloppen. (Een server-RPC zou de host-kopie openen, en
	// een non-replicated actor heeft sowieso geen net-identiteit om te referencen.) Default false = server-auth.
	virtual bool IsClientLocalInteract() const { return false; }

	// CO-OP GEDEELDE DEUR: != 0 betekent dat dit een niet-gerepliceerde, per-client deterministische deur is die
	// via een STABIEL POSITIE-ID gesynct wordt. De interactie stuurt dan dit id naar de server (i.p.v. de
	// actor, die geen net-identiteit heeft) -> server toggelt de gedeelde open-set -> alle clients volgen.
	virtual uint32 GetWorldSyncDoorId() const { return 0; }
};
