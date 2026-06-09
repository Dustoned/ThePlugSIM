#include "World/CityDoor.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Customer/CustomerBase.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

ACityDoor::ACityDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // lokaal (cosmetisch + lokale collision); ieder z'n eigen deur

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Hinge);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))) { Panel->SetStaticMesh(Cube); }
	Panel->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Nabijheid-zone: detecteert pawns die voor de deur staan (auto-open).
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->InitSphereRadius(150.f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
}

void ACityDoor::BeginPlay()
{
	Super::BeginPlay();
	if (Trigger)
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ACityDoor::OnTriggerBegin);
		Trigger->OnComponentEndOverlap.AddDynamic(this, &ACityDoor::OnTriggerEnd);
	}
}

void ACityDoor::OnTriggerBegin(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (Cast<ACustomerBase>(Other)) { ++NpcNear; }
	else if (Cast<APawn>(Other))    { ++OtherNear; }
}

void ACityDoor::OnTriggerEnd(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32)
{
	if (Cast<ACustomerBase>(Other)) { NpcNear = FMath::Max(0, NpcNear - 1); }
	else if (Cast<APawn>(Other))    { OtherNear = FMath::Max(0, OtherNear - 1); }
}

void ACityDoor::Setup(float Width, float Height, const FLinearColor& Color)
{
	if (!Panel) { return; }
	// Scharnier aan de -X-kant: paneel loopt van de hinge (X=0) naar +X.
	Panel->SetRelativeScale3D(FVector(Width, 8.f, Height) / 100.f);
	Panel->SetRelativeLocation(FVector(Width * 0.5f, 0.f, Height * 0.5f));
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* M = Panel->CreateDynamicMaterialInstance(0, Base)) { M->SetVectorParameterValue(TEXT("Color"), Color); }
	}
	// Zet de nabijheid-zone midden voor de deuropening (dekt beide kanten van de deur).
	if (Trigger)
	{
		Trigger->SetRelativeLocation(FVector(Width * 0.5f, 0.f, Height * 0.5f));
		Trigger->SetSphereRadius(FMath::Max(130.f, Width * 0.6f));
	}
}

FString ACityDoor::ResidentNameForIndex(int32 Index)
{
	static const TCHAR* First[] = {
		TEXT("Jan"), TEXT("Piet"), TEXT("Kees"), TEXT("Henk"), TEXT("Cor"), TEXT("Sjonnie"), TEXT("Henkie"), TEXT("Appie"),
		TEXT("Bertus"), TEXT("Guus"), TEXT("Klaas"), TEXT("Mees"), TEXT("Sjakie"), TEXT("Dirk"), TEXT("Wim"), TEXT("Bram"),
		TEXT("Joost"), TEXT("Sven"), TEXT("Tim"), TEXT("Rick"), TEXT("Bas"), TEXT("Daan"), TEXT("Niels"), TEXT("Koen"),
		TEXT("Gijs"), TEXT("Teun"), TEXT("Luuk"), TEXT("Stijn"), TEXT("Jasper"), TEXT("Ruben"), TEXT("Lars"), TEXT("Mats"),
		TEXT("Cas"), TEXT("Sander"), TEXT("Bart"), TEXT("Wout"), TEXT("Tijn"), TEXT("Siem"), TEXT("Boaz"), TEXT("Jules"),
		TEXT("Sam"), TEXT("Mick"), TEXT("Thijs"), TEXT("Ravi"), TEXT("Roel"), TEXT("Maarten"), TEXT("Freek"), TEXT("Jelle"),
		TEXT("Floris"), TEXT("Hugo"), TEXT("Pim"), TEXT("Joris"), TEXT("Tom"), TEXT("Wessel"), TEXT("Lucas"), TEXT("Milan"),
		TEXT("Finn"), TEXT("Noud"), TEXT("Sanne"), TEXT("Emma"), TEXT("Lotte"), TEXT("Fleur"), TEXT("Iris"), TEXT("Roos"),
		TEXT("Femke"), TEXT("Tessa"), TEXT("Maud"), TEXT("Nina"), TEXT("Lieke"), TEXT("Nora") };
	static const TCHAR* Last[] = {
		TEXT("Vapehoven"), TEXT("Kushman"), TEXT("Hashberg"), TEXT("Bongers"), TEXT("Highsma"), TEXT("Wietveld"), TEXT("Blunt"), TEXT("Stoner"),
		TEXT("Greenwood"), TEXT("Hazelaar"), TEXT("Spliffstra"), TEXT("Dankzaad"), TEXT("Nugteren"), TEXT("Pofadder"), TEXT("Knaller"), TEXT("Patatje"),
		TEXT("Frikandel"), TEXT("Stamppot"), TEXT("Bitterbal"), TEXT("Kroketberg"), TEXT("Pindakaas"), TEXT("Hagelslag"), TEXT("Stroopwafel"), TEXT("Kaaskop"),
		TEXT("Klompenburg"), TEXT("Tulpman"), TEXT("Windmolen"), TEXT("Fietsbel"), TEXT("Gouda"), TEXT("Poffertje"), TEXT("Bamischijf"), TEXT("Kapsalon"),
		TEXT("Snackbar"), TEXT("Wietstra"), TEXT("Blowman"), TEXT("Puffinga"), TEXT("Dampkring"), TEXT("Rookwolk"), TEXT("Jointsma"), TEXT("Dabberhof"),
		TEXT("Bongerd"), TEXT("Wietema"), TEXT("Hennep"), TEXT("Grasveld"), TEXT("Blowinga"), TEXT("Smoorman"), TEXT("Peukstra"), TEXT("Asbakker"),
		TEXT("Vloeitje"), TEXT("Aansteker"), TEXT("Hasjman"), TEXT("Wiedman"), TEXT("Coffeeshop"), TEXT("Theehuis"), TEXT("Spacecake"), TEXT("Brownie"),
		TEXT("Koekenbakker"), TEXT("Pannekoek"), TEXT("Oliebol"), TEXT("Kroket"), TEXT("Kibbeling"), TEXT("Lekkerbek"), TEXT("Haringman"), TEXT("Mosselman"),
		TEXT("Patatkraam"), TEXT("Mayoman"), TEXT("Currysaus"), TEXT("Berenburg"), TEXT("Jenever"), TEXT("Klompmaker"), TEXT("Polderman"), TEXT("Dijkgraaf"),
		TEXT("Grachtgordel"), TEXT("Fietspad"), TEXT("Marktplein"), TEXT("Tulpenveld"), TEXT("Molenaar"), TEXT("Kaasboer"), TEXT("Melkboer"), TEXT("Groenteman"),
		TEXT("Slagerman"), TEXT("Bakkerman"), TEXT("Sjekkie"), TEXT("Shagman"), TEXT("Grinder"), TEXT("Bongwater"), TEXT("Waterpijp"), TEXT("Stickie"),
		TEXT("Dampman"), TEXT("Rookgordel"), TEXT("Blowveld"), TEXT("Hasjbrik"), TEXT("Wietpot"), TEXT("Hasjpijp"), TEXT("Nederwiet"), TEXT("Skunkstra"),
		TEXT("Paddoman"), TEXT("Truffel"), TEXT("Jointman"), TEXT("Vuurtje") };
	const int32 NF = (int32)UE_ARRAY_COUNT(First);
	const int32 NL = (int32)UE_ARRAY_COUNT(Last);
	const int32 I = FMath::Max(0, Index);
	return FString::Printf(TEXT("%s %s"), First[I % NF], Last[(I * 37 + I / NF) % NL]);
}

FString ACityDoor::FriendlyNpcName(FName NpcId)
{
	const FString S = NpcId.ToString();
	if (S.StartsWith(TEXT("Resident_")))
	{
		return ResidentNameForIndex(FCString::Atoi(*S.RightChop(9)));
	}
	return S.IsEmpty() ? FString(TEXT("Customer")) : S;
}

void ACityDoor::Interact_Implementation(APawn* InstigatorPawn)
{
	if (bLocked) { return; } // bewoner-deur: op slot voor de speler
	bOpen = !bOpen; // F = open/dicht
}

FText ACityDoor::GetInteractionPrompt_Implementation() const
{
	if (bLocked)
	{
		if (bForSale) { return FText::FromString(TEXT("FOR SALE - buy via phone (Upgrades)")); }
		return FText::FromString(ResidentName.IsEmpty()
			? FString(TEXT("LOCKED"))
			: FString::Printf(TEXT("LOCKED - %s lives here"), *ResidentName));
	}
	if (bPlayerHome) { return bOpen ? FText::FromString(TEXT("Your home - close")) : FText::FromString(TEXT("Your home - open")); }
	return bOpen ? FText::FromString(TEXT("Close door")) : FText::FromString(TEXT("Open door"));
}

void ACityDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Speler: geen auto-open (open/sluit zelf met F = bOpen). NPC's: openen elke deur die ze tegenkomen ZODAT
	// ze niet vastlopen bij gebouw-ingangen/gangen - behalve JOUW eigen huis (dat opent niet vanzelf voor NPC's).
	const bool bEffectiveOpen = bOpen || (NpcNear > 0 && !bPlayerHome);
	const float Target = bEffectiveOpen ? -95.f : 0.f;
	CurAngle = FMath::FInterpTo(CurAngle, Target, DeltaSeconds, 7.f);
	if (Hinge) { Hinge->SetRelativeRotation(FRotator(0.f, CurAngle, 0.f)); }

	// Blokkeer de speler ALLEEN als de deur helemaal dicht is; zodra 'ie open(t) negeert het paneel
	// de pawn zodat het zwaaiende paneel je niet wegduwt en je er vrij door kunt lopen.
	if (Panel)
	{
		const bool bBlockPawn = (!bEffectiveOpen && FMath::Abs(CurAngle) < 2.f);
		Panel->SetCollisionResponseToChannel(ECC_Pawn, bBlockPawn ? ECR_Block : ECR_Ignore);
	}
}
