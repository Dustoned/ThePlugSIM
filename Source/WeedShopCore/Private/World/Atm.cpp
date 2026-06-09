#include "World/Atm.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Placement/PropMeshKit.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

AAtm::AAtm()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Verborgen collision-kast: draagt footprint/collision. ~78 x 60 x 162 cm, onderkant op de vloer.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Mesh->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded()) { Mesh->SetStaticMesh(CubeFinder.Object); }
	Mesh->SetWorldScale3D(FVector(0.78f, 0.60f, 1.62f));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 81.f)); // midden op halve hoogte -> onderkant op vloer
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetVisibility(false);

	// Samengestelde geldautomaat (front = +X). Maten in cm, vloer = z=0. Een echte vrijstaande pinkast:
	// brede romp + overstekende kap met logo, groot scherm, uitstekende toetsenplank, pas-sleuf en geld-uitgifte.
	Deco = PropKit::MakeDeco(this, Root, TEXT("Deco"));
	const FLinearColor BodyC(0.20f, 0.22f, 0.26f);    // staal
	const FLinearColor Dark(0.08f, 0.09f, 0.11f);     // trim/sleuven/kap
	const FLinearColor ScreenC(0.22f, 0.58f, 0.98f);  // blauw scherm
	const FLinearColor Money(0.18f, 0.66f, 0.36f);    // groen logo
	const FLinearColor Key(0.34f, 0.35f, 0.38f);      // toetsen
	const float FX = 27.f; // voorvlak romp op +X (halve diepte 54)

	PropKit::AddPart(this, Deco, TEXT("Plinth"),   PropKit::Cube(), FVector(76.f, 58.f, 14.f),  FVector(0.f, 0.f, 7.f),    Dark);
	PropKit::AddPart(this, Deco, TEXT("Cabinet"),  PropKit::Cube(), FVector(72.f, 54.f, 132.f), FVector(0.f, 0.f, 80.f),   BodyC);
	PropKit::AddPart(this, Deco, TEXT("Header"),   PropKit::Cube(), FVector(78.f, 60.f, 16.f),  FVector(0.f, 0.f, 154.f),  Dark);   // overstekende kap
	PropKit::AddPart(this, Deco, TEXT("Logo"),     PropKit::Cube(), FVector(3.f, 54.f, 10.f),   FVector(30.5f, 0.f, 154.f), Money);  // logo-strip op de kap
	PropKit::AddPart(this, Deco, TEXT("Screen"),   PropKit::Cube(), FVector(3.f, 52.f, 36.f),   FVector(FX + 0.5f, 0.f, 120.f), ScreenC); // groot scherm
	PropKit::AddPart(this, Deco, TEXT("CardSlot"), PropKit::Cube(), FVector(4.f, 18.f, 3.f),    FVector(FX + 1.f, 0.f, 110.f), Dark);  // pas-sleuf
	PropKit::AddPart(this, Deco, TEXT("Shelf"),    PropKit::Cube(), FVector(18.f, 46.f, 6.f),   FVector(FX + 8.f, 0.f, 98.f),  BodyC * 0.85f); // uitstekende toetsenplank
	PropKit::AddPart(this, Deco, TEXT("Keypad"),   PropKit::Cube(), FVector(8.f, 32.f, 9.f),    FVector(FX + 10.f, 0.f, 103.f), Key);  // toetsen op de plank
	PropKit::AddPart(this, Deco, TEXT("CashSlot"), PropKit::Cube(), FVector(4.f, 44.f, 7.f),    FVector(FX + 1.f, 0.f, 74.f),  Dark);  // geld-uitgifte
}

void AAtm::Interact_Implementation(APawn* InstigatorPawn)
{
	// Het openen van de Bank-app gebeurt lokaal in de character (UI-actie); hier niets te doen.
}

FText AAtm::GetInteractionPrompt_Implementation() const
{
	if (bSafeMode) { return NSLOCTEXT("WeedShop", "SafePrompt", "Open safe - stash cash (robbery-proof)"); }
	return NSLOCTEXT("WeedShop", "AtmPrompt", "Use ATM - deposit cash (bank)");
}

void AAtm::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAtm, bSafeMode);
	DOREPLIFETIME(AAtm, SafeCapacityCents);
}

int64 AAtm::SafeCapacityForItem(FName ItemId)
{
	const FString S = ItemId.ToString();
	if (S == TEXT("Safe_Small"))  { return 1000000;   } // EUR 10.000
	if (S == TEXT("Safe_Medium")) { return 5000000;   } // EUR 50.000
	if (S == TEXT("Safe_Large"))  { return 25000000;  } // EUR 250.000
	if (S == TEXT("Safe_Vault"))  { return 100000000; } // EUR 1.000.000
	return 0;
}
