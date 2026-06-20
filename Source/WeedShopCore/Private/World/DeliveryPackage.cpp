#include "World/DeliveryPackage.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	// Kartonnen doos: bruine kubus ~42x42x40cm, gecentreerd op de actor-origin (zodat de drone-drop-offset blijft
	// kloppen) met de collision erop.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	Mesh->SetRelativeScale3D(FVector(0.42f, 0.42f, 0.40f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (MatFinder.Succeeded())
	{
		if (UMaterialInstanceDynamic* M = Mesh->CreateDynamicMaterialInstance(0, MatFinder.Object))
		{
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.46f, 0.32f, 0.18f)); // karton-bruin
		}
	}

	// Pakkettape: beige kruis bovenop (2 dunne strips, net over de rand zoals echte tape). Geen collision.
	TapeX = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TapeX"));
	TapeY = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TapeY"));
	UStaticMeshComponent* Tapes[2] = { TapeX, TapeY };
	const FVector TapeScale[2] = { FVector(0.46f, 0.10f, 0.04f), FVector(0.10f, 0.46f, 0.04f) };
	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* T = Tapes[i];
		T->SetupAttachment(Root);
		if (CubeFinder.Succeeded()) { T->SetStaticMesh(CubeFinder.Object); }
		T->SetRelativeScale3D(TapeScale[i]);
		T->SetRelativeLocation(FVector(0.f, 0.f, 20.f)); // op het deksel (doos-halfhoogte = 20cm)
		T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (MatFinder.Succeeded())
		{
			if (UMaterialInstanceDynamic* TM = T->CreateDynamicMaterialInstance(0, MatFinder.Object))
			{
				TM->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.80f, 0.72f, 0.55f)); // beige tape
			}
		}
	}
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
			// Al betaald bij checkout -> gewoon toevoegen. GrantAny pakt supply/zaden (pack-grootte); lukt dat niet
			// (bv. een MEUBEL/MACHINE of ander cart-item), dan direct AddItem. Faalt alleen nog bij te weinig ruimte.
			if (Store->GrantAny(Ids[i], Inv) || Inv->AddItem(Ids[i], 1)) { ++Got; }
			else { break; } // geen plek -> rest blijft in de doos
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
			UWeedToast::Notify(-1, 3.f, FColor::Green,
				FString::Printf(TEXT("Unpacked the delivery: %d item(s)."), Delivered));
		}
		Destroy();
	}
	else
	{
		// Niet alles paste -> rest blijft in de doos. Bijna altijd: inventory vol (rugzak-slots op).
		Ids = RemIds; Qtys = RemQ;
		if (GEngine)
		{
			UWeedToast::Notify(-1, 5.f, FColor::Orange, Delivered > 0
				? FString::Printf(TEXT("Took %d item(s); %d still in the box - inventory full. Free up a slot and interact again."), Delivered, TotalItems())
				: FString::Printf(TEXT("Inventory full - %d item(s) stay in the box. Free up a backpack slot, then interact again."), TotalItems()));
		}
	}
}

FText ADeliveryPackage::GetInteractionPrompt_Implementation() const
{
	return FText::FromString(FString::Printf(TEXT("Pick up package  (%d item(s))"), TotalItems()));
}
