#include "World/LampLinkMarker.h"

#include "World/PackLightSwitch.h"
#include "Phone/PhoneClientComponent.h"
#include "UI/WeedToast.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

ALampLinkMarker::ALampLinkMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // pure lokale UI (zoals de schakelaar zelf)

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		Mesh->SetStaticMesh(M);
	}
	Mesh->SetWorldScale3D(FVector(0.34f)); // ~34cm bol over de lightbox
	Mesh->SetCastShadow(false);
	// Aanklikbaar voor de interactie-trace (ECC_Visibility) ook als verborgen; geen fysieke botsing.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetCanEverAffectNavigation(false);

	// Blauwe "ghost"-glow (M_PlacementGhost is altijd zichtbaar + tintbaar via GhostColor; geen materiaal-gok).
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_PlacementGhost.M_PlacementGhost")))
	{
		MID = UMaterialInstanceDynamic::Create(Base, this);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("GhostColor"), FLinearColor(0.10f, 0.45f, 1.0f, 1.f));
			Mesh->SetMaterial(0, MID);
		}
	}

	SetActorHiddenInGame(true); // standaard verborgen; alleen tonen als de lamp gelinkt is (zie RefreshLink)
}

void ALampLinkMarker::Init(APackLightSwitch* Sw, UPhoneClientComponent* InPhone, const FString& InLampKey)
{
	Switch = Sw;
	Phone = InPhone;
	LampKey = InLampKey;
	RefreshLink();
}

void ALampLinkMarker::RefreshLink()
{
	// Blauwe glow alleen bij een gelinkte lamp; anders verborgen (de lamp blijft wel klikbaar via de collision).
	const bool bLinked = Switch.IsValid() && Switch->IsLampLinked(LampKey);
	SetActorHiddenInGame(!bLinked);
}

void ALampLinkMarker::Interact_Implementation(APawn* InstigatorPawn)
{
	if (APackLightSwitch* Sw = Switch.Get())
	{
		Sw->ToggleLampLink(LampKey); // toggelt de set, slaat op, dimt de witte diffuser bij gelinkt (link-preview)
		RefreshLink();               // toon/verberg de blauwe glow over deze lamp
		if (UPhoneClientComponent* P = Phone.Get()) { P->NotifyLinkActivity(); } // reset de 1-min inactiviteits-timer
		const bool bNow = Sw->IsLampLinked(LampKey);
		UWeedToast::NotifyPawn(InstigatorPawn, -1, 1.0f,
			bNow ? FColor(40, 90, 255) : FColor::White,
			bNow ? TEXT("Lamp gelinkt (blauw)") : TEXT("Lamp ontkoppeld"));
	}
}

FText ALampLinkMarker::GetInteractionPrompt_Implementation() const
{
	const bool bLinked = Switch.IsValid() && Switch->IsLampLinked(LampKey);
	return FText::FromString(bLinked ? TEXT("Klik: lamp ontkoppelen") : TEXT("Klik: lamp linken"));
}
