#include "World/WaterSink.h"
#include "UI/WeedToast.h"

#include "Cultivation/WaterCanComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"
#include "Placement/PropMeshKit.h"

AWaterSink::AWaterSink()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Verborgen collision-doos, gecentreerd op de root (plaatsing zet de root op vloer + halve hoogte).
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
		Mesh->SetRelativeScale3D(FVector(0.84f, 0.56f, 1.0f)); // 84 x 56 x 100 cm collision (echte aanrecht-hoogte)
		Mesh->SetRelativeLocation(FVector::ZeroVector);
	}
	Mesh->SetVisibility(false);

	// Samengesteld gootsteen-kastje op ECHTE aanrecht-hoogte (werkblad ~90 cm). Root in het MIDDEN (vloer = -50). Maten in cm.
	Deco = PropKit::MakeDeco(this, Root, TEXT("Deco"));
	const float Floor = -50.f;
	const FLinearColor Cabinet(0.82f, 0.80f, 0.76f); // licht kastje
	const FLinearColor Counter(0.55f, 0.57f, 0.60f); // grijs werkblad
	const FLinearColor Steel(0.70f, 0.72f, 0.75f);   // rvs
	const FLinearColor Basin(0.30f, 0.32f, 0.35f);   // donkere bak
	const FLinearColor Seam(0.45f, 0.44f, 0.42f);

	PropKit::AddPart(this, Deco, TEXT("Cabinet"),  PropKit::Cube(),     FVector(80.f, 54.f, 84.f), FVector(0.f, 0.f, Floor + 42.f), Cabinet);
	PropKit::AddPart(this, Deco, TEXT("DoorSeam"), PropKit::Cube(),     FVector(2.f, 55.f, 76.f),  FVector(0.f, 0.f, Floor + 40.f), Seam);
	PropKit::AddPart(this, Deco, TEXT("Counter"),  PropKit::Cube(),     FVector(84.f, 56.f, 6.f),  FVector(0.f, 0.f, Floor + 88.f), Counter);
	PropKit::AddPart(this, Deco, TEXT("BasinRim"), PropKit::Cube(),     FVector(48.f, 42.f, 7.f),  FVector(0.f, 0.f, Floor + 87.f), Steel);
	PropKit::AddPart(this, Deco, TEXT("BasinHole"),PropKit::Cube(),     FVector(38.f, 32.f, 6.f),  FVector(0.f, 0.f, Floor + 86.f), Basin);
	PropKit::AddPart(this, Deco, TEXT("FaucetBase"),PropKit::Cube(),    FVector(8.f, 8.f, 5.f),    FVector(0.f, -18.f, Floor + 93.f), Steel);
	PropKit::AddPart(this, Deco, TEXT("FaucetNeck"),PropKit::Cylinder(),FVector(5.f, 5.f, 24.f),   FVector(0.f, -18.f, Floor + 106.f), Steel);
	PropKit::AddPart(this, Deco, TEXT("FaucetSpout"),PropKit::Cylinder(),FVector(4.5f, 4.5f, 18.f),FVector(0.f, -9.f, Floor + 116.f), Steel, FRotator(0.f, 0.f, 90.f));
}

void AWaterSink::Interact_Implementation(APawn* InstigatorPawn)
{
	if (!HasAuthority())
	{
		return;
	}
	// Je moet een waterfles ECHT in je hand hebben (geselecteerd op de hotbar) om te vullen — niet
	// alleen ergens in je inventory.
	UInventoryComponent* Inv = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	const bool bHoldingBottle = Inv && Inv->GetActiveItemId().ToString().StartsWith(TEXT("WaterBottle"));
	UWaterCanComponent* Can = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UWaterCanComponent>() : nullptr;
	if (!bHoldingBottle || !Can || !Can->HasBottle())
	{
		if (GEngine)
		{
			UWeedToast::NotifyPawn(InstigatorPawn,-1, 2.5f, FColor::Orange, TEXT("Hold a water bottle (hotbar) to fill it."));
		}
		return;
	}
	Can->Fill();
	if (GEngine)
	{
		const int32 Cur = Can->GetCharges(); const int32 Max = Can->GetMaxCharges();
		UWeedToast::NotifyPawn(InstigatorPawn, 70111, Cur >= Max ? 1.6f : 1.15f, FColor(120, 200, 255), (Cur >= Max)
			? FString::Printf(TEXT("Bottle full %d/%d"), Cur, Max)
			: FString::Printf(TEXT("Bottle %d/%d"), Cur, Max), TEXT("drop"));
	}
}

FText AWaterSink::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("WeedShop", "FillBottle", "Fill water bottle");
}
