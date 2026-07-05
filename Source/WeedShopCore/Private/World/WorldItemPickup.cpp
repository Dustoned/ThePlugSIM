#include "World/WorldItemPickup.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Inventory/InventoryComponent.h"
#include "Economy/EconomyComponent.h"
#include "Placement/PropMeshKit.h"
#include "UI/WeedToast.h"
#include "UI/WeedUiStyle.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

AWorldItemPickup::AWorldItemPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true); // server simuleert de drop; de vallende/settelde stand repliceert naar clients

	// Body = de (onzichtbare) physics-doos die valt + ALLE collision draagt, en de root, zodat replicated movement
	// 'm volgt. Klein (~14cm) zodat 'ie ongeveer om het kleine model heen valt.
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetBoxExtent(FVector(7.f, 7.f, 6.f));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_PhysicsBody);
	Body->SetCollisionResponseToAllChannels(ECR_Block);        // valt op de vloer + line-trace (ECC_Visibility) raakt 'm
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore); // duwt spelers niet weg (co-op: geen zwevende speler)
	// NIET op de CDO: SetMassOverrideInKg vraagt het physics-materiaal op, en tijdens de CDO-constructie
	// (o.a. bij het cooken) is GEngine nog niet geinitialiseerd -> "GetSimplePhysicalMaterial: GEngine not
	// initialized" = cook-error. Op echte instances (runtime) is GEngine wel op, dus daar draait 't gewoon.
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		Body->SetMassOverrideInKg(NAME_None, 0.4f, true);
		Body->SetLinearDamping(0.6f);
		Body->SetAngularDamping(0.8f);
		Body->SetUseCCD(true);
	}
	// SetSimulatePhysics zelf gebeurt server-side in Setup() (niet in de CDO ivm replicatie-volgorde).

	// Mesh = ANKER; het echte 3D-model wordt er runtime als losse onderdelen onder gebouwd (PropKit::BuildItemModel),
	// klein geschaald. Zelf geen collision - die zit op Body.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Anchor"));
	Mesh->SetupAttachment(Body);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AWorldItemPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWorldItemPickup, ItemId);
	DOREPLIFETIME(AWorldItemPickup, Qty);
	DOREPLIFETIME(AWorldItemPickup, Thc);
	DOREPLIFETIME(AWorldItemPickup, Qual);
}

void AWorldItemPickup::Setup(FName InItemId, int32 InQty, float InThc, float InQual)
{
	ItemId = InItemId; Qty = FMath::Max(1, InQty); Thc = InThc; Qual = InQual;
	RefreshVisual();

	// Server simuleert de drop: het doosje valt het laatste stukje + tuimelt natuurlijk neer; na settelen bevriezen
	// we de physics (geen eindeloze sim). Clients volgen via replicated movement, dus NIET client-side simuleren.
	if (HasAuthority() && Body)
	{
		Body->SetSimulatePhysics(true);
		Body->SetPhysicsAngularVelocityInDegrees(FVector(
			FMath::RandRange(-140.f, 140.f), FMath::RandRange(-140.f, 140.f), FMath::RandRange(-90.f, 90.f)));
		GetWorldTimerManager().SetTimer(FreezeTimer, this, &AWorldItemPickup::FreezePhysics, 3.f, false);
	}
}

void AWorldItemPickup::OnRep_Item() { RefreshVisual(); }

void AWorldItemPickup::RefreshVisual()
{
	// Klein, herkenbaar model uit losse onderdelen onder het anker. GEEN collision op de delen: de Body-doos draagt
	// de collision + physics (anders zouden de delen met de physics-body botsen).
	if (Mesh && !ItemId.IsNone())
	{
		PropKit::BuildItemModel(this, Mesh, ItemId, ItemScale, /*bFirstPerson*/ false, /*bCollision*/ false);
		AutoFitBody(); // de physics-doos passend om dit specifieke model schalen
	}
}

void AWorldItemPickup::AutoFitBody()
{
	if (!Mesh || !Body) { return; }
	// Gecombineerde lokale bounds van alle model-onderdelen (relatief t.o.v. het anker, dat op de Body-origin zit).
	FBox Local(ForceInit);
	TArray<USceneComponent*> Kids;
	Mesh->GetChildrenComponents(true, Kids);
	for (USceneComponent* K : Kids)
	{
		UStaticMeshComponent* MC = Cast<UStaticMeshComponent>(K);
		UStaticMesh* SM = MC ? MC->GetStaticMesh() : nullptr;
		if (!SM) { continue; }
		const FBoxSphereBounds B = SM->GetBounds();
		const FVector Sc = MC->GetRelativeScale3D();
		const FVector Ext = B.BoxExtent * Sc;
		const FVector Ctr = MC->GetRelativeLocation() + B.Origin * Sc;
		Local += FBox(Ctr - Ext, Ctr + Ext);
	}
	if (!Local.IsValid) { return; }
	FVector Half = ClampVector(Local.GetExtent(), FVector(3.f), FVector(60.f)); // min 3cm (settle-baar), max 60cm
	Body->SetBoxExtent(Half);
	// Schuif de visuele delen zo dat hun midden op de Body-origin valt: doos omhult het model + nette physics.
	Mesh->SetRelativeLocation(-Local.GetCenter());
}

void AWorldItemPickup::FreezePhysics()
{
	// Na het settelen: physics uit. Body blijft QueryAndPhysics -> nog steeds aan te kijken/op te pakken.
	if (Body) { Body->SetSimulatePhysics(false); }
}

AWorldItemPickup* AWorldItemPickup::SpawnDrop(UWorld* W, const FVector& Loc, FName ItemId, int32 Qty, float Thc, float Quality)
{
	// Server-only: wereld-actors spawnen mag nooit op een client (replicatie brengt de drop naar iedereen).
	if (!W || W->GetNetMode() == NM_Client || ItemId.IsNone() || Qty <= 0) { return nullptr; }
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWorldItemPickup* P = W->SpawnActor<AWorldItemPickup>(AWorldItemPickup::StaticClass(), FTransform(FRotator::ZeroRotator, Loc), SP);
	if (P) { P->Setup(ItemId, Qty, Thc, Quality); }
	return P;
}

bool AWorldItemPickup::GiveOrDrop(UInventoryComponent* Inv, APawn* Pawn, FName ItemId, int32 Qty, float Thc, float Quality)
{
	if (!Inv || ItemId.IsNone() || Qty <= 0) { return false; }
	// Server-only semantiek (kan wereld-actors spawnen): op een client niks muteren.
	if (Inv->GetOwnerRole() != ROLE_Authority) { return false; }
	// Cash is een SPIEGEL van het economy-saldo -> NOOIT als wereld-item droppen (dat zou geld dupliceren).
	if (ItemId == FName(TEXT("Cash"))) { return Inv->AddItem(ItemId, Qty, Thc, Quality); }
	// bQuietOnFull: AddItem zwijgt bij een volle inventory -> hieronder tonen we EEN nette "bij je voeten"-toast
	// (anders dubbel: AddItem's "geen slots"/"te zwaar" PLUS de drop-melding).
	if (Inv->AddItem(ItemId, Qty, Thc, Quality, /*bQuietOnFull=*/true)) { return true; }

	// Past niet -> bij de voeten neerleggen (zelfde plek-berekening als ServerDropStack): nooit stil loot kwijt.
	AActor* At = Pawn ? static_cast<AActor*>(Pawn) : Inv->GetOwner();
	UWorld* W = At ? At->GetWorld() : nullptr;
	if (!At || !W) { return false; }
	FVector Fwd = At->GetActorForwardVector(); Fwd.Z = 0.f; Fwd = Fwd.GetSafeNormal();
	FVector Loc = At->GetActorLocation() + Fwd * 90.f;
	Loc.Z -= (At->GetSimpleCollisionHalfHeight() - 12.f); // bij de voeten neerleggen
	AWorldItemPickup* P = SpawnDrop(W, Loc, ItemId, Qty, Thc, Quality);
	if (P && GEngine)
	{
		UWeedToast::NotifyPawn(At, -1, 3.f, FColor::Orange, TEXT("Inventory full - dropped at your feet."));
	}
	return P != nullptr;
}

void AWorldItemPickup::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority() || !InstigatorPawn) { return; }
	// Cash gaat naar de ECONOMY van de oppakker (niet de inventory-mirror): zo werkt geld droppen tussen spelers.
	if (ItemId == FName(TEXT("Cash")))
	{
		if (UEconomyComponent* Eco = InstigatorPawn->FindComponentByClass<UEconomyComponent>())
		{
			Eco->AddMoneyUntracked((int64)Qty * 100); // Qty = euro's -> cents (untracked: telt niet als witwas-omzet)
			if (GEngine) { UWeedToast::NotifyPawn(InstigatorPawn, -1, 2.5f, FColor(120, 255, 140), FString::Printf(TEXT("Picked up EUR %d cash"), Qty)); }
			Destroy();
		}
		return;
	}
	UInventoryComponent* Inv = InstigatorPawn->FindComponentByClass<UInventoryComponent>();
	if (!Inv) { return; }
	if (Inv->AddItem(ItemId, Qty, Thc, Qual, /*bQuietOnFull=*/true)) // stil: hieronder EEN nette "geen ruimte"-toast i.p.v. dubbel
	{
		Destroy();
	}
	else if (GEngine)
	{
		UWeedToast::NotifyPawn(InstigatorPawn, -1, 2.f, FColor::Orange, TEXT("No room in your inventory."));
	}
}

FText AWorldItemPickup::GetInteractionPrompt_Implementation() const
{
	const FString Name = WeedUI::PrettyItemName(ItemId);
	return FText::FromString(Qty > 1
		? FString::Printf(TEXT("Pick up %s  (%dx)"), *Name, Qty)
		: FString::Printf(TEXT("Pick up %s"), *Name));
}
