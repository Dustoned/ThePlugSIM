#include "World/WorldItemPickup.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Inventory/InventoryComponent.h"
#include "Placement/PropMeshKit.h"
#include "UI/WeedToast.h"
#include "UI/WeedUiStyle.h"
#include "Engine/Engine.h"

AWorldItemPickup::AWorldItemPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
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
}

void AWorldItemPickup::OnRep_Item() { RefreshVisual(); }

void AWorldItemPickup::RefreshVisual()
{
	if (Mesh && !ItemId.IsNone()) { PropKit::ApplyItemModel(Mesh, ItemId, 1.6f); } // iets groter zodat 't goed zichtbaar/raakbaar is
}

void AWorldItemPickup::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority() || !InstigatorPawn) { return; }
	UInventoryComponent* Inv = InstigatorPawn->FindComponentByClass<UInventoryComponent>();
	if (!Inv) { return; }
	if (Inv->AddItem(ItemId, Qty, Thc, Qual))
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
