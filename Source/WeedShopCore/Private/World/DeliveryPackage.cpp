#include "World/DeliveryPackage.h"

#include "WeedShopCore.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Game/WeedShopGameState.h"
#include "Progression/StoreComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Engine/Engine.h"

ADeliveryPackage::ADeliveryPackage()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	// Kartonnen doosje: bruinig, ~40cm kubus.
	Mesh->SetWorldScale3D(FVector(0.4f, 0.4f, 0.4f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MatFinder.Succeeded()) { Mesh->SetMaterial(0, MatFinder.Object); }
}

void ADeliveryPackage::SetupOrder(int32 InOrderId, const TArray<FName>& InIds, const TArray<int32>& InQtys, UPhoneClientComponent* InPhone)
{
	OrderId = InOrderId;
	Ids = InIds;
	Qtys = InQtys;
	Phone = InPhone;
}

int32 ADeliveryPackage::TotalItems() const
{
	int32 N = 0;
	for (int32 Q : Qtys) { N += FMath::Max(0, Q); }
	return N;
}

void ADeliveryPackage::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority()) { return; }

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Store || !Inv) { return; }

	// Lever zoveel als past/betaalbaar is; de rest blijft in de doos.
	TArray<FName> RemIds; TArray<int32> RemQ;
	int32 Delivered = 0;
	for (int32 i = 0; i < Ids.Num(); ++i)
	{
		const int32 Want = Qtys.IsValidIndex(i) ? Qtys[i] : 0;
		int32 Got = 0;
		for (int32 q = 0; q < Want; ++q)
		{
			if (Store->BuyAny(Ids[i], Inv)) { ++Got; }
			else { break; } // geen plek of geen geld -> stop met deze regel
		}
		Delivered += Got;
		if (Got < Want) { RemIds.Add(Ids[i]); RemQ.Add(Want - Got); }
	}

	if (RemIds.Num() == 0)
	{
		// Alles uitgepakt -> doos weg, pending-regel opruimen.
		if (Phone.IsValid()) { Phone->OnPackagePickedUp(OrderId); }
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
				FString::Printf(TEXT("Unpacked the delivery: %d item(s)."), Delivered));
		}
		Destroy();
	}
	else
	{
		// Niet alles paste -> rest blijft in de doos.
		Ids = RemIds; Qtys = RemQ;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange,
				FString::Printf(TEXT("Took %d item(s); %d left in the box (make room / cash)."), Delivered, TotalItems()));
		}
	}
}

FText ADeliveryPackage::GetInteractionPrompt_Implementation() const
{
	return FText::FromString(FString::Printf(TEXT("Pick up package  (%d item(s))"), TotalItems()));
}
