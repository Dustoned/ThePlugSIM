#include "World/PackElevator.h"

#include "WeedShopCore.h"
#include "World/PackElevatorButton.h"
#include "World/WorldSyncComponent.h"
#include "Game/WeedShopGameState.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"

namespace
{
	constexpr float CabSpeed = 240.f;     // cm/s verticaal
	constexpr float DoorSlideTime = 0.7f; // seconden open/dicht
	constexpr float DwellSeconds = 6.f;   // deuren open laten staan zonder passagier
	constexpr float DepartSeconds = 1.6f; // na instappen: zo lang wachten en dan vertrekken
}

APackElevator::APackElevator()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // lokaal/cosmetisch, net als ACityElevator/ACityDoor

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Cab = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cab"));
	Cab->SetupAttachment(Root);
	Cab->SetMobility(EComponentMobility::Movable);
	Cab->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Cab->SetCanEverAffectNavigation(false);
	if (UStaticMesh* CabMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorCabin.SM_ElevatorCabin")))
	{
		Cab->SetStaticMesh(CabMesh);
	}
}

void APackElevator::Setup(const TArray<float>& InFloors, const FVector& InSlideDir, const TArray<FElevPanelInit>& InPanels, const FVector& CabCenterXY, const FVector& OpeningDir)
{
	if (!GetWorld()) { return; } // co-op: niet bouwen als de actor (nog) niet in een geldige wereld zit
	Floors = InFloors;
	Floors.Sort();
	CabXY = FVector(CabCenterXY.X, CabCenterXY.Y, 0.f);
	SlideDir = InSlideDir;
	SlideDir.Z = 0.f;
	SlideDir.Normalize();

	Panels.Reset();
	for (const FElevPanelInit& P : InPanels)
	{
		// IsValid (niet alleen null): een map-paneel kan tijdens streaming pending-kill zijn. Schrijven
		// (SetMobility) door zo'n vrijgegeven component corrumpeert de heap -> crash bij de volgende NewObject.
		if (!IsValid(P.Comp)) { continue; }
		// BELANGRIJK: de map-panelen zijn Static - zonder Movable doet SetWorldLocation stilletjes NIETS.
		P.Comp->SetMobility(EComponentMobility::Movable);
		FPanelRef R;
		R.Comp = P.Comp;
		R.ClosedPos = P.ClosedPos; // ECHTE dicht-stand (de map parkeert ze half-open)
		R.FloorIdx = P.FloorIdx;
		R.SlideDist = P.SlideDist;
		Panels.Add(R);
	}

	CurFloor = 0;
	TargetFloor = 0;
	ShownFloor = 0;
	CabZ = Floors.Num() ? Floors[0] : 0.f;
	DoorOpen = 1.f; // begane grond: deuren open, klaar om in te stappen
	DwellTimer = DwellSeconds;
	SetActorLocation(FVector(CabXY.X, CabXY.Y, CabZ));
	// Cabine-mesh: pivot zit OP de open kant (lokale bounds X -200..0, opening = +X-vlak).
	// CabCenterXY is het deurvlak (frame-pivot), dus actor-pivot daar neerzetten en de lokale +X
	// naar de gang draaien -> cabine valt precies in de schacht, opening tegen de deur.
	FVector Open2D = OpeningDir; Open2D.Z = 0.f;
	if (Open2D.Normalize()) { SetActorRotation(Open2D.Rotation()); }
	if (Cab) { Cab->SetRelativeLocation(FVector::ZeroVector); }

	// CO-OP: stabiel gedeeld lift-id uit het cabine-centrum (XY, Z=0) + de opening-yaw. CabXY en de yaw
	// (uit OpeningDir) zijn deterministisch identiek op host EN joiner (DoorRetrofitter spawnt de lift op
	// beide processen op exact dezelfde plek), dus dit id matcht -> de WorldSync-doelverdieping klopt overal.
	ElevSyncId = UWorldSyncComponent::MakeId(CabXY, GetActorRotation().Yaw);

	// Verdieping-display IN de cabine: tegen de achterwand, op ooghoogte, kijkend naar de opening.
	if (Cab && !CabDigit)
	{
		CabDigit = NewObject<UStaticMeshComponent>(this);
		CabDigit->SetupAttachment(Cab);
		CabDigit->RegisterComponent();
		CabDigit->SetMobility(EComponentMobility::Movable);
		CabDigit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CabDigit->SetCanEverAffectNavigation(false);
		CabDigit->SetRelativeLocation(FVector(-192.f, 0.f, 175.f));
		CabDigit->SetRelativeRotation(FRotator::ZeroRotator); // lokale +X = naar de opening
		CabDigit->SetRelativeScale3D(FVector(5.f));
		// Eerst een echte mesh (cijfer 0) zodat slot 0 bestaat vóór SetMaterial (geen material/render op een
		// mesh-loze component). UpdateSigns wisselt 'm later naar de juiste verdieping.
		if (UStaticMesh* M0 = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorNumber_0.SM_ElevatorNumber_0")))
		{
			CabDigit->SetStaticMesh(M0);
		}
		// Cabine-cijfer-VLAK zwart maken; het cijfer komt er wit (TextRender) overheen -> zwart scherm,
		// wit oplichtend nummer.
		UMaterialInterface* Black = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_DigitBlack.M_DigitBlack")); // GEEN static: dangelt na GC bij stream-out -> heap-corruptie
		if (Black) { CabDigit->SetMaterial(0, Black); }
		// Richting-pijl naast het display: ^ of v zolang de lift rijdt.
		CabArrow = NewObject<UTextRenderComponent>(this);
		CabArrow->SetupAttachment(Cab);
		CabArrow->RegisterComponent();
		CabArrow->SetMobility(EComponentMobility::Movable);
		CabArrow->SetRelativeLocation(FVector(-192.f, -32.f, 178.f));
		CabArrow->SetRelativeRotation(FRotator::ZeroRotator);
		CabArrow->SetWorldSize(26.f);
		CabArrow->SetHorizontalAlignment(EHTA_Center);
		CabArrow->SetVerticalAlignment(EVRTA_TextCenter);
		CabArrow->SetVisibility(false);
		// Zwart cijfer over de witte cabine-plaat (lokale +X = naar de opening = naar de kijker).
		CabDigitText = NewObject<UTextRenderComponent>(this);
		CabDigitText->SetupAttachment(Cab);
		CabDigitText->RegisterComponent();
		CabDigitText->SetMobility(EComponentMobility::Movable);
		CabDigitText->SetRelativeLocation(FVector(-192.f + 3.f, 0.f, 175.f));
		CabDigitText->SetRelativeRotation(FRotator::ZeroRotator);
		CabDigitText->SetWorldSize(22.f);
		CabDigitText->SetHorizontalAlignment(EHTA_Center);
		CabDigitText->SetVerticalAlignment(EVRTA_TextCenter);
		CabDigitText->SetTextRenderColor(FColor(245, 245, 250));
		UMaterialInterface* TextGlow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_DigitTextGlow.M_DigitTextGlow")); // GEEN static: dangelt na GC -> heap-corruptie
		if (TextGlow) { CabDigitText->SetTextMaterial(TextGlow); }
	}
	// Cabine-schuifdeuren: 2 panelen op de open kant (lokale X ~ -8), samen 136 breed gecentreerd.
	// Ze rijden mee met de cabine en schuiven synchroon met de hal-deuren open/dicht.
	if (Cab && !CabDoorFront)
	{
		// Hal-schuifrichting (pocket-kant) omzetten naar cabine-lokale Y (opening ligt langs lokale Y).
		const FVector LocalSlide = GetActorRotation().UnrotateVector(SlideDir);
		CabSlideSignY = (LocalSlide.Y >= 0.f) ? 1.f : -1.f;
		UStaticMesh* DoorMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorDoor.SM_ElevatorDoor"));
		auto MakeCabDoor = [&](const FVector& RelPos) -> UStaticMeshComponent*
		{
			UStaticMeshComponent* D = NewObject<UStaticMeshComponent>(this); // auto-naam (geen vaste naam-collisie)
			D->SetupAttachment(Cab);
			D->RegisterComponent();
			D->SetMobility(EComponentMobility::Movable);
			D->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			D->SetCanEverAffectNavigation(false);
			if (DoorMesh) { D->SetStaticMesh(DoorMesh); }
			D->SetRelativeLocation(RelPos);
			return D;
		};
		// Deur-mesh dekt lokaal [pivot-68, pivot]: pivots op 0 en +68 dekken samen [-68, +68].
		// FRONT = de helft die het verst van de pocket ligt (moet de hele opening oversteken, 146);
		// BACK = de pocket-kant-helft (73). Pocket op +Y -> front = pivot 0; pocket op -Y -> front = pivot 68.
		if (CabSlideSignY > 0.f)
		{
			CabDoorFrontBase = FVector(-8.f, 0.f, 0.f);
			CabDoorBackBase  = FVector(-8.f, 68.f, 0.f);
		}
		else
		{
			CabDoorFrontBase = FVector(-8.f, 68.f, 0.f);
			CabDoorBackBase  = FVector(-8.f, 0.f, 0.f);
		}
		CabDoorFront = MakeCabDoor(CabDoorFrontBase);
		CabDoorBack  = MakeCabDoor(CabDoorBackBase);
	}

	BuildCabButtonPanel();
	UpdateSigns();
}

void APackElevator::CallToFloor(int32 FloorIdx)
{
	if (!Floors.IsValidIndex(FloorIdx)) { return; }
	TargetFloor = FloorIdx;
	// Lift staat al hier: deuren (weer) openen in plaats van rijden.
	if (FloorIdx == CurFloor && !bMoving) { DwellTimer = DwellSeconds; }
}

void APackElevator::BuildCabButtonPanel()
{
	UWorld* W = GetWorld();
	if (!W || !Cab || Floors.Num() < 2) { return; }
	// Paneel op de zijwand naast de opening (lokale Y-wand aan de pocket-tegenovergestelde kant,
	// zodat 'ie niet achter de openschuivende cabine-deur verdwijnt): kolommen de diepte in, rijen omhoog.
	const float WallY = (CabSlideSignY > 0.f) ? -96.f : 96.f; // tegenover de pocket-kant
	const float FaceYaw = (WallY < 0.f) ? 90.f : -90.f;       // knop kijkt de cabine in
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Layout als een ECHTE lift: rijen van 2, onderaan beginnen (0 links-onder, 1 rechts-onder,
	// daarboven 2-3, enz). Welke X-kolom 'links' is hangt af van welke wand het paneel heeft.
	for (int32 Fi = 0; Fi < Floors.Num(); ++Fi)
	{
		const int32 Row = Fi / 2;
		const int32 VCol = Fi % 2; // 0 = links gezien vanuit de cabine, 1 = rechts
		const float XNear = -34.f, XFar = -60.f;
		const float ColX = (WallY < 0.f) ? (VCol == 0 ? XFar : XNear) : (VCol == 0 ? XNear : XFar);
		const FVector RelPos(ColX, WallY, 105.f + Row * 26.f);
		APackElevatorButton* Btn = W->SpawnActor<APackElevatorButton>(APackElevatorButton::StaticClass(), Cab->GetComponentTransform(), SP);
		if (!Btn) { continue; }
		Btn->Setup(this, Fi);
		Btn->SetCabMode();
		Btn->AttachToComponent(Cab, FAttachmentTransformRules::SnapToTargetIncludingScale);
		Btn->SetActorRelativeLocation(RelPos);
		Btn->SetActorRelativeRotation(FRotator(0.f, FaceYaw, 0.f));
		// Het CIJFER zit OP de knop (niet erboven): je klikt gewoon op het nummer waar je heen wilt.
		const FTransform CabTM = Cab->GetComponentTransform();
		const FVector SignWorld = CabTM.TransformPosition(RelPos + FVector(0.f, (WallY < 0.f) ? 2.4f : -2.4f, 0.f));
		const FRotator SignRot = (CabTM.GetRotation() * FRotator(0.f, FaceYaw, 0.f).Quaternion()).Rotator();
		Btn->SetupSign(SignWorld, SignRot, 3.0f);
		Btn->SetDigit(Fi);
		Buttons.Add(Btn); // mee in de lijst voor doel-gloei (UpdateSigns slaat cab-knoppen al over)
	}
}

void APackElevator::RegisterButton(APackElevatorButton* Btn)
{
	if (Btn)
	{
		Buttons.Add(Btn);
		Btn->SetDigit(ShownFloor);
	}
}

void APackElevator::UpdateSigns()
{
	for (const TWeakObjectPtr<APackElevatorButton>& B : Buttons)
	{
		APackElevatorButton* Btn = B.Get();
		if (Btn && !Btn->IsCabButton()) { Btn->SetDigit(ShownFloor); } // cab-knoppen houden hun vaste label
	}
	if (CabDigit && CabDigitShown != ShownFloor)
	{
		CabDigitShown = ShownFloor;
		const int32 D = FMath::Clamp(ShownFloor, 0, 9);
		const FString Path = FString::Printf(TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorNumber_%d.SM_ElevatorNumber_%d"), D, D);
		if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path))
		{
			CabDigit->SetStaticMesh(M);
			UMaterialInterface* Black = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Project/Materials/M_DigitBlack.M_DigitBlack")); // GEEN static: dangelt na GC bij stream-out -> heap-corruptie
			if (Black) { CabDigit->SetMaterial(0, Black); } // override kan door SetStaticMesh resetten
		}
		if (CabDigitText) { CabDigitText->SetText(FText::AsNumber(D)); }
	}
}

bool APackElevator::IsPawnAboard() const
{
	UWorld* W = GetWorld();
	if (!W) { return false; }
	// CO-OP: de cabine is per-client lokaal en de "niet-opsluiten"-dwell gaat over de speler op DIT scherm.
	// Filter dus op de LOKAAL bestuurde pawn (host: de host-speler; joiner: de joiner-speler) - de andere
	// speler zit in z'n eigen lokale cabine-kopie. Blind over alle PlayerControllers zou op de listen-server
	// ook de joiner meetellen, terwijl die z'n eigen cabine-instantie heeft.
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController()) { continue; }
		const APawn* P = PC->GetPawn();
		if (!P) { continue; }
		const FVector L = P->GetActorLocation();
		if (FVector::Dist2D(L, GetActorLocation()) < 130.f && FMath::Abs(L.Z - (CabZ + 100.f)) < 160.f)
		{
			return true;
		}
	}
	return false;
}

void APackElevator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Floors.Num() < 2) { return; }

	// CO-OP: de DOEL-verdieping komt uit de gedeelde WorldSync (door de knop via een Server-RPC gezet) ->
	// host EN joiner rijden hun lokale cabine naar EXACT dezelfde verdieping. De deur-slide-interp blijft lokaal.
	// De LIVE CabZ is NIET meer puur lokaal: de server interpoleert 'm en schrijft 'm naar WorldSync, de client
	// leest 'm ervan (H.4-rubber-band-fix, zie onder). WorldSync 1x resolven en cachen (net als ACityDoor).
	// De PackElevator zelf is per-proces niet-gerepliceerd, dus HasAuthority() deugt NIET (net als DoorRetrofitter);
	// server-detectie via de net-mode van de wereld.
	const bool bIsClient = GetWorld() && GetWorld()->GetNetMode() == NM_Client;
	UWorldSyncComponent* MutableWS = nullptr; // alleen server schrijft hierop (SetElevatorZ)
	if (ElevSyncId != 0)
	{
		if (!CachedWorldSync.IsValid())
		{
			const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
			CachedWorldSync = GS ? GS->GetWorldSync() : nullptr;
		}
		if (const UWorldSyncComponent* WS = CachedWorldSync.Get())
		{
			MutableWS = const_cast<UWorldSyncComponent*>(WS); // WorldSync leeft op de GameState; server-schrijf is authority-gated in SetElevatorZ
			const int32 WsFloor = WS->GetElevatorFloor(ElevSyncId);
			// INDEX_NONE = nog geen server-doel gezet -> laat het lokale begin-doel (begane grond) staan.
			if (Floors.IsValidIndex(WsFloor))
			{
				TargetFloor = WsFloor;
				// Doel veranderd (ook naar de huidige verdieping toe) -> deuren weer openen (dwell re-armen),
				// zodat een call naar de verdieping waar de cabine al staat op ELK scherm de deuren opent.
				if (WsFloor != LastWsFloor)
				{
					LastWsFloor = WsFloor;
					if (WsFloor == CurFloor && !bMoving) { DwellTimer = FMath::Max(DwellTimer, DwellSeconds); }
				}
			}
			// CLIENT: NIET lokaal interpoleren maar de cabine direct op de server-Z zetten -> host en joiner staan
			// op EXACT dezelfde hoogte (base-desync weg). Fallback = de huidige lokale CabZ als er nog geen
			// server-waarde binnen is (voorkomt een sprong naar 0 vlak na spawn).
			if (bIsClient)
			{
				CabZ = WS->GetElevatorZ(ElevSyncId, CabZ);
				SetActorLocation(FVector(CabXY.X, CabXY.Y, CabZ));
			}
		}
	}

	// SERVER: publiceer de live cabine-Z elke tick (ook stilstaand) zodat een client die MIDDEN in een sessie
	// joint direct de juiste rust-hoogte leest i.p.v. de 0-fallback (SetElevatorZ is authority-gated).
	if (!bIsClient && MutableWS) { MutableWS->SetElevatorZ(ElevSyncId, CabZ); }

	const float TargetZ = Floors[TargetFloor];
	bMoving = !FMath::IsNearlyEqual(CabZ, TargetZ, 1.f);

	// RICHTING-PIJL (^/v) op elke verdieping + in de cabine zolang de lift onderweg is.
	const int32 ArrowDir = bMoving ? ((TargetZ > CabZ) ? 1 : -1) : 0;
	if (ArrowDir != LastArrowDir)
	{
		LastArrowDir = ArrowDir;
		for (const TWeakObjectPtr<APackElevatorButton>& B : Buttons)
		{
			if (APackElevatorButton* Btn = B.Get()) { Btn->SetArrow(ArrowDir); }
		}
		if (CabArrow)
		{
			CabArrow->SetVisibility(ArrowDir != 0);
			if (ArrowDir != 0)
			{
				CabArrow->SetText(FText::AsCultureInvariant(ArrowDir > 0 ? TEXT("^") : TEXT("v")));
				CabArrow->SetTextRenderColor(ArrowDir > 0 ? FColor(110, 255, 140) : FColor(255, 170, 90));
			}
		}
	}
	// DOEL-GLOEI: de cabine-knop van de verdieping waar de lift heen gaat licht op.
	const int32 GlowFloor = (bMoving || TargetFloor != CurFloor) ? TargetFloor : -1;
	if (GlowFloor != LastGlowFloor)
	{
		LastGlowFloor = GlowFloor;
		for (const TWeakObjectPtr<APackElevatorButton>& B : Buttons)
		{
			APackElevatorButton* Btn = B.Get();
			if (Btn && Btn->IsCabButton()) { Btn->SetHighlight(Btn->GetFloorIdx() == GlowFloor); }
		}
	}

	// LIVE VERDIEPING-TELLER: displays tellen mee met de cabine (0, 1, 2, ...) in plaats van
	// pas het eindcijfer te tonen bij aankomst.
	if (bMoving)
	{
		int32 Nearest = ShownFloor;
		float BestD = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Floors.Num(); ++i)
		{
			const float Dz = FMath::Abs(Floors[i] - CabZ);
			if (Dz < BestD) { BestD = Dz; Nearest = i; }
		}
		if (Nearest != ShownFloor)
		{
			ShownFloor = Nearest;
			UpdateSigns();
		}
	}

	// Deuren: open na aankomst (dwell-tijd) of zolang er iemand in de cabine staat - daarna
	// netjes DICHT, ook op de begane grond. Een call-knop op de verdieping opent ze weer.
	if (!bMoving)
	{
		if (IsPawnAboard()) { DwellTimer = FMath::Max(DwellTimer, 1.5f); } // niemand opsluiten
		DwellTimer -= DeltaSeconds;
	}
	const bool bWantOpen = !bMoving && DwellTimer > 0.f;
	DoorOpen = FMath::FInterpConstantTo(DoorOpen, bWantOpen ? 1.f : 0.f, DeltaSeconds, 1.f / DoorSlideTime);
	for (const FPanelRef& R : Panels)
	{
		UStaticMeshComponent* C = R.Comp.Get();
		if (!C) { continue; }
		// Alleen de panelen van de verdieping waar de cabine is (of vertrekt) bewegen mee; rest dicht.
		const bool bThisFloor = (R.FloorIdx == CurFloor);
		const float Amount = bThisFloor ? DoorOpen : 0.f;
		C->SetWorldLocation(R.ClosedPos + SlideDir * (R.SlideDist * Amount));
	}
	// Cabine-deuren schuiven synchroon mee (lokaal langs de opening).
	if (CabDoorFront) { CabDoorFront->SetRelativeLocation(CabDoorFrontBase + FVector(0.f, CabSlideSignY * 146.f * DoorOpen, 0.f)); }
	if (CabDoorBack)  { CabDoorBack->SetRelativeLocation(CabDoorBackBase + FVector(0.f, CabSlideSignY * 78.f * DoorOpen, 0.f)); }

	if (bMoving)
	{
		// Pas rijden als de deuren dicht zijn.
		if (DoorOpen > 0.02f) { return; }
		// SERVER: interpoleer de cabine lokaal EN publiceer de live Z naar WorldSync zodat de client 'm 1-op-1
		// volgt (base-desync-fix). CLIENT: NIET interpoleren - CabZ is bovenaan al uit WorldSync gezet; hier
		// alleen de aankomst-afhandeling (deuren/verdieping) laten meelopen op de gesyncte Z.
		if (!bIsClient)
		{
			CabZ = FMath::FInterpConstantTo(CabZ, TargetZ, DeltaSeconds, CabSpeed);
			SetActorLocation(FVector(CabXY.X, CabXY.Y, CabZ));
			// Verse post-interp Z meteen publiceren (de top-of-tick-publish gebruikt de vorige-frame-waarde) ->
			// de client volgt met minimale lag.
			if (MutableWS) { MutableWS->SetElevatorZ(ElevSyncId, CabZ); }
		}
		if (FMath::IsNearlyEqual(CabZ, TargetZ, 1.f))
		{
			CabZ = TargetZ;
			if (!bIsClient)
			{
				SetActorLocation(FVector(CabXY.X, CabXY.Y, CabZ));
				if (MutableWS) { MutableWS->SetElevatorZ(ElevSyncId, CabZ); }
			}
			CurFloor = TargetFloor;
			ShownFloor = CurFloor;
			DwellTimer = DwellSeconds;
			BoardedTimer = 0.f;
			UpdateSigns(); // bordjes boven de deuren: cabine is nu op deze verdieping
		}
		return;
	}

	// Geen automatisch rijden: de lift vertrekt alleen via de cabine-knoppen (kies verdieping)
	// of een call-knop op een verdieping. Echte lift-ervaring.
}

void APackElevator::Interact_Implementation(APawn* InstigatorPawn)
{
	// Geen actie op de cabine zelf: de verdieping kies je met de knoppen in de cabine.
	// (Eerst deed F hier "volgende verdieping" - dan ging de lift omhoog waar je ook klikte.)
}

FText APackElevator::GetInteractionPrompt_Implementation() const
{
	// Geen prompt op de cabine zelf: iedereen weet hoe een lift werkt; je kiest de verdieping met de
	// knoppen. (De knoppen tonen hun eigen prompt.)
	return FText::GetEmpty();
}
