#include "Interaction/InteractionComponent.h"

#include "WeedShopCore.h"
#include "Interaction/Interactable.h"
#include "World/CityDoor.h" // IsLocked()-check: gelockte deuren niet via WorldSync open-toggelen
#include "Customer/CustomerBase.h"
#include "Phone/PhoneClientComponent.h"
#include "Game/WeedShopGameState.h"
#include "World/WorldSyncComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Co-op: de component moet repliceren zodat de Server-RPC gerouteerd kan worden.
	SetIsReplicatedByDefault(true);
}

void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner() || !GetOwner()->IsA(APawn::StaticClass()))
	{
		UE_LOG(LogWeedShop, Warning, TEXT("UInteractionComponent must be on a Pawn, not on %s."),
			*GetNameSafe(GetOwner()));
	}
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Focus + prompt zijn puur lokaal: alleen voor de speler die deze pawn bestuurt.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn && OwnerPawn->IsLocallyControlled())
	{
		UpdateFocus();
	}

	// Edge-latch reset: input wordt vóór de component-tick verwerkt. Als TryInteract deze
	// frame NIET is aangeroepen, is de toets losgelaten -> latch vrijgeven zodat de volgende
	// indruk weer telt. Zo blijft ingedrukt houden bij precies één interactie.
	if (!bInteractRequestedThisFrame)
	{
		bInteractLatched = false;
	}
	bInteractRequestedThisFrame = false;
}

bool UInteractionComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return false;
	}

	// Voor spelers geeft de controller het echte camerastandpunt (first-person camera).
	if (const AController* Controller = OwnerPawn->GetController())
	{
		Controller->GetPlayerViewPoint(OutLocation, OutRotation);
		return true;
	}

	// Terugval: ogen van de actor.
	OwnerPawn->GetActorEyesViewPoint(OutLocation, OutRotation);
	return true;
}

void UInteractionComponent::UpdateFocus()
{
	FVector ViewLocation;
	FRotator ViewRotation;

	AActor* NewFocus = nullptr;

	if (GetViewPoint(ViewLocation, ViewRotation))
	{
		const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * InteractionDistance;

		FCollisionQueryParams Params(SCENE_QUERY_STAT(WeedShopInteractionTrace), /*bTraceComplex=*/false);
		Params.AddIgnoredActor(GetOwner());

		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, TraceChannel, Params))
		{
			AActor* HitActor = Hit.GetActor();
			if (HitActor && HitActor->Implements<UInteractable>())
			{
				NewFocus = HitActor;
			}
		}
	}

	if (NewFocus == FocusedActor.Get())
	{
		return;
	}

	FocusedActor = NewFocus;

	FText Prompt = FText::GetEmpty();
	if (NewFocus)
	{
		Prompt = IInteractable::Execute_GetInteractionPrompt(NewFocus);
	}

	OnFocusedInteractableChanged.Broadcast(NewFocus, Prompt);
}

void UInteractionComponent::TryInteract()
{
	// Markeer dat de toets deze frame "Triggered" was; de tick reset de latch bij loslaten.
	bInteractRequestedThisFrame = true;

	// Al deze indruk afgehandeld? Dan negeren tot de toets is losgelaten (geen spam bij houden).
	if (bInteractLatched)
	{
		return;
	}

	AActor* Target = FocusedActor.Get();
	if (!Target)
	{
		// Niets om mee te interacten, maar deze indruk is wel "verbruikt": niet blijven proberen.
		bInteractLatched = true;
		return;
	}

	// Korte anti-dubbel cooldown: vangt toevallige dubbele triggers binnen enkele frames af.
	if (const UWorld* World = GetWorld())
	{
		const double Now = World->GetTimeSeconds();
		if (Now - LastInteractTime < InteractCooldown)
		{
			return;
		}
		LastInteractTime = Now;
	}

	// Deze indruk is nu afgehandeld; pas na loslaten weer toegestaan.
	bInteractLatched = true;

	// Klant -> open lokaal het deal-paneel (prijs-slider) i.p.v. direct verkopen. De bevestiging
	// gaat daarna via een Server-RPC. Dit draait op de lokaal bestuurde speler.
	if (ACustomerBase* Cust = Cast<ACustomerBase>(Target))
	{
		if (Cust->IsShopkeeper()) { return; } // verkoper achter de balie: niet dealen (spreek de balie aan)
		if (UPhoneClientComponent* Phone = GetOwner() ? GetOwner()->FindComponentByClass<UPhoneClientComponent>() : nullptr)
		{
			Phone->OpenDeal(Cust);
			return;
		}
	}

	// CO-OP GEDEELDE DEUR: niet-gerepliceerde deur op een deterministische positie -> stuur het stabiele id naar
	// de server, die toggelt de gedeelde open-set (WorldSync). Zo zien BEIDE spelers de deur open/dicht gaan en
	// klopt de collision op elk scherm. (We kunnen de actor zelf niet over een RPC sturen: bReplicates=false.)
	if (const IInteractable* AsI = Cast<IInteractable>(Target))
	{
		const uint32 DoorId = AsI->GetWorldSyncDoorId();
		if (DoorId != 0)
		{
			// OP SLOT (bewoner/te-huur/huur-achterstand)? NIET via WorldSync open-toggelen - laat de deur zelf
			// beslissen (blokkeren, of huur betalen aan je eigen deur). Anders open je elke gelockte deur gewoon.
			if (const ACityDoor* Dr = Cast<ACityDoor>(Target))
			{
				if (Dr->IsLocked()) { PerformInteract(Target); return; }
			}
			ServerToggleDoor(DoorId);
			return;
		}
		// Overige niet-gerepliceerde lokale objecten (lampen/liften - nog niet gedeeld): lokaal uitvoeren.
		if (AsI->IsClientLocalInteract())
		{
			PerformInteract(Target);
			return;
		}
	}

	// Host / single-player heeft authority -> meteen uitvoeren. Client -> via de server.
	if (GetOwnerRole() == ROLE_Authority)
	{
		PerformInteract(Target);
	}
	else
	{
		ServerInteract(Target);
	}
}

void UInteractionComponent::ServerInteract_Implementation(AActor* Target)
{
	// Validatie op de server: bestaat het, is het interact-baar, en staat de speler dichtbij genoeg?
	if (!IsValid(Target) || !Target->Implements<UInteractable>())
	{
		return;
	}

	if (!IsWithinReach(Target))
	{
		return;
	}

	PerformInteract(Target);
}

void UInteractionComponent::ServerToggleDoor_Implementation(uint32 DoorId)
{
	const UWorld* W = GetWorld();
	AWeedShopGameState* GS = W ? W->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS && GS->GetWorldSync()) { GS->GetWorldSync()->ServerToggleDoor(DoorId); }
}

void UInteractionComponent::PerformInteract(AActor* Target)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	IInteractable::Execute_Interact(Target, OwnerPawn);
}

bool UInteractionComponent::IsWithinReach(const AActor* Target) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !Target)
	{
		return false;
	}

	// Ruime marge bovenop de trace-afstand om client/server-latency en hitbox-grootte op te vangen.
	const float MaxDist = InteractionDistance * 1.5f;
	return FVector::DistSquared(OwnerActor->GetActorLocation(), Target->GetActorLocation())
		<= FMath::Square(MaxDist);
}
