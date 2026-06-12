#include "World/CityDoor.h"
#include "Components/TextRenderComponent.h"
#include "Economy/EconomyComponent.h"
#include "Phone/PhoneClientComponent.h"

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
	// BELANGRIJK: de deur mag de navmesh NIET blokkeren. Anders dicht het paneel het navmesh-gat in de muur
	// en is er geen pad naar binnen (NPC's moesten daarom teleporteren). De collision die de SPELER blokkeert
	// als de deur dicht is, staat hier los van - die regelen we via de ECC_Pawn-response in Tick.
	Panel->SetCanEverAffectNavigation(false);

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

void ACityDoor::SetSlideMode(float InSlideDist)
{
	bSlideMode = true;
	SlideDist = InSlideDist;
}

void ACityDoor::SetSlideClosedOffset(const FVector& LocalOff)
{
	SlideBase = LocalOff;
	if (Hinge) { Hinge->SetRelativeLocation(SlideBase); } // meteen in de dichte stand zetten
}

void ACityDoor::SetupLeaf(UStaticMesh* LeafMesh, float OpenDeg, float TriggerRadius)
{
	if (!Panel || !LeafMesh) { return; }
	Panel->SetStaticMesh(LeafMesh);
	Panel->SetRelativeScale3D(FVector(1.f));
	Panel->SetRelativeLocation(FVector::ZeroVector); // pivot van het blad = het scharnier
	OpenSwingDeg = OpenDeg;
	if (Trigger)
	{
		Trigger->SetRelativeLocation(FVector::ZeroVector);
		Trigger->SetSphereRadius(TriggerRadius);
	}
}

void ACityDoor::AddLeafExtra(UStaticMesh* Mesh, const FTransform& WorldTM)
{
	if (!Mesh || !Hinge) { return; }
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Hinge);
	C->RegisterComponent();
	C->SetStaticMesh(Mesh);
	C->SetWorldTransform(WorldTM);
	// Open-geparkeerde pui: het paneel is naar de dichte stand verschoven (SlideBase), maar het
	// glas-mesh staat in de map nog op de geparkeerde stand - zelfde verschuiving meegeven.
	if (!SlideBase.IsNearlyZero())
	{
		C->AddWorldOffset(GetActorRotation().RotateVector(SlideBase));
	}
	// Het glas van het blad moet net zo blokkeren als het paneel - anders stap je bij een dichte
	// deur dwars door de ruit. De pawn-response volgt het paneel (toggle in Tick).
	C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	C->SetCollisionResponseToAllChannels(ECR_Block);
	C->SetCanEverAffectNavigation(false);
	LeafExtras.Add(C);
}

void ACityDoor::SetAptNumber(int32 Num)
{
	AptNumber = Num;
	if (!Panel || !Panel->GetStaticMesh() || !Hinge) { return; }
	// Bordje OP het deurblad (beide kanten), op ooghoogte gecentreerd - draait netjes met de deur mee.
	const FBox B = Panel->GetStaticMesh()->GetBoundingBox();
	if (AptTexts.Num() == 0)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
			T->SetupAttachment(Hinge);
			T->RegisterComponent();
			AptTexts.Add(T);
		}
	}
	const float Yc = (B.Min.Y + B.Max.Y) * 0.5f;
	const float Zc = FMath::Min(B.Max.Z - 30.f, B.Max.Z * 0.8f);
	for (int32 Side = 0; Side < AptTexts.Num(); ++Side)
	{
		UTextRenderComponent* T = AptTexts[Side];
		if (!T) { continue; }
		const bool bFront = (Side == 0);
		T->SetRelativeLocation(FVector(bFront ? B.Max.X + 1.2f : B.Min.X - 1.2f, Yc, Zc));
		T->SetRelativeRotation(FRotator(0.f, bFront ? 0.f : 180.f, 0.f));
		T->SetText(FText::AsCultureInvariant(FString::FromInt(Num)));
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextCenter);
		T->SetWorldSize(20.f);
		T->SetTextRenderColor(FColor(255, 214, 120));
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
	// Achterstallige huur: F aan je eigen voordeur = betalen (cash), daarna is de deur weer van jou.
	if (bRentDue)
	{
		UEconomyComponent* Ec = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UEconomyComponent>() : nullptr;
		UPhoneClientComponent* Ph = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UPhoneClientComponent>() : nullptr;
		if (Ec && Ec->RemoveMoney(RentCents))
		{
			bRentDue = false;
			bLocked = false;
			bRentJustPaid = true;
		}
		else if (Ph)
		{
			Ph->Toast(FString::Printf(TEXT("Not enough cash - rent is EUR %d"), (int32)(RentCents / 100)), FColor::Red, 3.f);
		}
		return;
	}
	if (bLocked) { return; } // bewoner-deur: op slot voor de speler
	// Draairichting is vast (de legale kant, afgeleid van hoe de map de deur parkeerde) -
	// dynamisch van-de-speler-af kiezen duwde deuren door het kozijn.
	bOpen = !bOpen; // F = open/dicht
}

FText ACityDoor::GetInteractionPrompt_Implementation() const
{
	const FString AptLbl = (AptNumber > 0) ? FString::Printf(TEXT("Apt %d - "), AptNumber) : FString();
	if (bRentDue)
	{
		return FText::FromString(FString::Printf(TEXT("%sRENT OVERDUE - F: pay EUR %d"), *AptLbl, (int32)(RentCents / 100)));
	}
	if (bLocked)
	{
		if (bForSale) { return FText::FromString(FString::Printf(TEXT("%sFOR SALE - buy via phone (Upgrades)"), *AptLbl)); }
		return FText::FromString(ResidentName.IsEmpty()
			? FString::Printf(TEXT("%sLOCKED"), *AptLbl)
			: FString::Printf(TEXT("%sLOCKED - %s lives here"), *AptLbl, *ResidentName));
	}
	if (bPlayerHome) { return FText::FromString(FString::Printf(TEXT("%syour home - %s"), *AptLbl, bOpen ? TEXT("close") : TEXT("open"))); }
	return bOpen ? FText::FromString(TEXT("Close door")) : FText::FromString(TEXT("Open door"));
}

void ACityDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Speler: geen auto-open (open/sluit zelf met F = bOpen). NPC's: openen elke deur die ze tegenkomen ZODAT
	// ze niet vastlopen bij gebouw-ingangen/gangen - behalve JOUW eigen huis (dat opent niet vanzelf voor NPC's).
	const bool bEffectiveOpen = bOpen || (NpcNear > 0 && !bPlayerHome);
	const float Target = bEffectiveOpen ? OpenSwingDeg : 0.f;
	CurAngle = FMath::FInterpTo(CurAngle, Target, DeltaSeconds, 7.f);
	if (Hinge)
	{
		if (bSlideMode)
		{
			// Schuifdeur: zelfde easing, maar als translatie langs het blad (lokale Y vanaf de pivot)
			// i.p.v. als rotatie. Teken van OpenSwingDeg bepaalt de schuif-kant.
			const float Frac = (OpenSwingDeg != 0.f) ? (CurAngle / OpenSwingDeg) : 0.f;
			const float Dir = (OpenSwingDeg >= 0.f) ? 1.f : -1.f;
			Hinge->SetRelativeLocation(SlideBase + FVector(0.f, Dir * Frac * SlideDist, 0.f));
		}
		else
		{
			Hinge->SetRelativeRotation(FRotator(0.f, CurAngle, 0.f));
		}
	}

	// Paneel is SOLIDE zodra de deur stilstaat (helemaal dicht OF helemaal open tegen de muur), maar negeert
	// pawns TIJDENS het zwaaien - zo duwt het bewegende paneel je niet weg en loop je er vrij doorheen.
	// BELANGRIJK: zodra er NPC's bij de deur zijn (NpcNear) is het paneel ook niet solide - anders blokkeert
	// een opengezwaaide winkel-/bewonersdeur de stoep en lopen passerende NPC's zich er massaal op vast.
	if (Panel)
	{
		const bool bSettled = FMath::Abs(CurAngle - Target) < 2.f;
		const bool bBlockPawn = bSettled && NpcNear == 0;
		Panel->SetCollisionResponseToChannel(ECC_Pawn, bBlockPawn ? ECR_Block : ECR_Ignore);
		// Glas-delen van het blad volgen het paneel (anders loop je door de ruit van een dichte deur).
		for (UStaticMeshComponent* Extra : LeafExtras)
		{
			if (Extra) { Extra->SetCollisionResponseToChannel(ECC_Pawn, bBlockPawn ? ECR_Block : ECR_Ignore); }
		}
	}
}
