#include "World/DoorRetrofitter.h"

#include "WeedShopCore.h"
#include "World/CityDoor.h"
#include "World/DayNightController.h"
#include "World/PackElevator.h"
#include "World/PackElevatorButton.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/Engine.h"

namespace
{
	// Bekende deur-blad-meshes (pivot op het scharnier). L-bladen lopen lokaal -Y vanaf de pivot,
	// R-bladen +Y -> gespiegelde open-draai zodat dubbele deuren netjes dezelfde kant in zwaaien.
	struct FLeafDef { const TCHAR* MeshName; float OpenDeg; };
	const FLeafDef LeafDefs[] = {
		{ TEXT("SM_Base_Door_01_L"), -100.f },
		{ TEXT("SM_Base_Door_01_R"),  100.f },
		{ TEXT("SM_Base_Door_08_L"), -100.f },
		{ TEXT("SM_Base_Door_08_R"),  100.f },
		{ TEXT("SM_Door_Apartment01"), -100.f },                 // appartement-deur
		{ TEXT("SM_Door_Apartment02"), -100.f },                 // appartement-deur (variant)
		{ TEXT("SM_DoorCommerical_01"), -100.f },                // winkeldeur
		{ TEXT("SM_Upper_BalconyDoor_01_3m_Door"), -100.f },     // balkondeur (glas via _Glass)
		{ TEXT("SM_Upper_BalconyDoor_01_3m_Door_b"), -100.f },   // balkondeur variant
	};

	const FLeafDef* MatchLeaf(const UStaticMesh* Mesh)
	{
		if (!Mesh) { return nullptr; }
		const FString Name = Mesh->GetName();
		for (const FLeafDef& D : LeafDefs)
		{
			if (Name == D.MeshName) { return &D; }
		}
		return nullptr;
	}

	// Hoort deze mesh als GLAS bij een van de bekende bladen? (naam == "<blad>_Glass")
	bool IsLeafGlass(const UStaticMesh* Mesh)
	{
		if (!Mesh) { return false; }
		const FString Name = Mesh->GetName();
		for (const FLeafDef& D : LeafDefs)
		{
			if (Name == FString(D.MeshName) + TEXT("_Glass")) { return true; }
		}
		return false;
	}
}

ADoorRetrofitter::ADoorRetrofitter()
{
	PrimaryActorTick.bCanEverTick = true; // per frame de on-screen debug-spam legen
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ADoorRetrofitter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Pack-maps hebben een watermerk-print-loop in hun level-script (tekst is al geblankt) en de engine
	// klaagt over hun lighting-setup. Leeg de on-screen berichten elke frame -> scherm blijft schoon.
	// (Onze eigen toasts zijn UMG-widgets, geen debug-messages - die blijven gewoon zichtbaar.)
	if (GEngine) { GEngine->ClearOnScreenDebugMessages(); }
}

void ADoorRetrofitter::BeginPlay()
{
	Super::BeginPlay();
	// Meteen een eerste pass + daarna periodiek blijven scannen (world partition / level instances
	// streamen gebouwen later in; die deuren pakken we dan alsnog).
	ScanAndConvert();
	GetWorldTimerManager().SetTimer(ScanTimer, this, &ADoorRetrofitter::ScanAndConvert, 2.0f, true);

	// Dev: -ElevScan -> pak de LAATSTE gemarkeerde spot (MarkedSpots.txt) op deze map, teleporteer de
	// speler erheen (streamt het gebouw in) en dump de elevator-meshes naar Saved/ElevScan.txt.
	if (FParse::Param(FCommandLine::Get(), TEXT("ElevScan")))
	{
		const FString MapPath = GetWorld() ? GetWorld()->GetOutermost()->GetName() : FString();
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
		for (int32 i = Lines.Num() - 1; i >= 0; --i)
		{
			if (!Lines[i].Contains(MapPath)) { continue; }
			const int32 PIdx = Lines[i].Find(TEXT("pos=("));
			if (PIdx == INDEX_NONE) { continue; }
			FString PosStr = Lines[i].Mid(PIdx + 5);
			int32 Close = INDEX_NONE;
			if (PosStr.FindChar(TEXT(')'), Close)) { PosStr = PosStr.Left(Close); }
			TArray<FString> Parts;
			PosStr.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() < 3) { continue; }
			ElevScanPos = FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
			bElevScan = true;
			UE_LOG(LogWeedShop, Warning, TEXT("ELEVSCAN gestart: spot (%.0f, %.0f, %.0f)"), ElevScanPos.X, ElevScanPos.Y, ElevScanPos.Z);
			GetWorldTimerManager().SetTimer(ElevScanTimer, this, &ADoorRetrofitter::ElevTeleport, 5.f, false);
			break;
		}
		if (!bElevScan) { UE_LOG(LogWeedShop, Warning, TEXT("ELEVSCAN: geen marked spot voor deze map gevonden")); }
	}

	// Dev: -SpotScan -> teleporteer naar de laatste marked spot en dump alles rond de laatste 2 spots.
	if (FParse::Param(FCommandLine::Get(), TEXT("SpotScan")))
	{
		const FString MapPath2 = GetWorld() ? GetWorld()->GetOutermost()->GetName() : FString();
		TArray<FString> Lines2;
		FFileHelper::LoadFileToStringArray(Lines2, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
		for (int32 i = Lines2.Num() - 1; i >= 0; --i)
		{
			if (!Lines2[i].Contains(MapPath2)) { continue; }
			const int32 PIdx = Lines2[i].Find(TEXT("pos=("));
			if (PIdx == INDEX_NONE) { continue; }
			FString PosStr = Lines2[i].Mid(PIdx + 5);
			int32 Close = INDEX_NONE;
			if (PosStr.FindChar(TEXT(')'), Close)) { PosStr = PosStr.Left(Close); }
			TArray<FString> Parts;
			PosStr.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() < 3) { continue; }
			ElevScanPos = FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
			GetWorldTimerManager().SetTimer(ElevScanTimer, this, &ADoorRetrofitter::ElevTeleport, 5.f, false);
			GetWorldTimerManager().SetTimer(SpotScanTimer, this, &ADoorRetrofitter::SpotDump, 16.f, false);
			UE_LOG(LogWeedShop, Warning, TEXT("SPOTSCAN gepland: spot (%.0f, %.0f, %.0f)"), ElevScanPos.X, ElevScanPos.Y, ElevScanPos.Z);
			break;
		}
	}
}

void ADoorRetrofitter::ElevTeleport()
{
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			if (APawn* Pn = PC->GetPawn()) { Pn->SetActorLocation(ElevScanPos + FVector(0.f, 0.f, 80.f)); }
		}
	}
	GetWorldTimerManager().SetTimer(ElevScanTimer, this, &ADoorRetrofitter::ElevDump, 8.f, false);
}

// -SpotScan: dump alle static meshes rond de laatste 2 marked spots (bron-kamer vs lege kamer)
// naar Saved/SpotScan.txt. Statisch hulpje, aangeroepen vanuit BeginPlay-timer.
void ADoorRetrofitter::SpotDump()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const FString MapPath = W->GetOutermost()->GetName();
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	TArray<FVector> Spots;
	for (int32 i = Lines.Num() - 1; i >= 0 && Spots.Num() < 6; --i)
	{
		if (!Lines[i].Contains(MapPath)) { continue; }
		const int32 PIdx = Lines[i].Find(TEXT("pos=("));
		if (PIdx == INDEX_NONE) { continue; }
		FString PosStr = Lines[i].Mid(PIdx + 5);
		int32 Close = INDEX_NONE;
		if (PosStr.FindChar(TEXT(')'), Close)) { PosStr = PosStr.Left(Close); }
		TArray<FString> Parts;
		PosStr.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() < 3) { continue; }
		Spots.Add(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])));
	}
	FString Out;
	for (int32 SpotIdx = 0; SpotIdx < Spots.Num(); ++SpotIdx)
	{
		Out += FString::Printf(TEXT("=== SPOT %d: (%.0f, %.0f, %.0f) ==="), SpotIdx, Spots[SpotIdx].X, Spots[SpotIdx].Y, Spots[SpotIdx].Z);
		Out += LINE_TERMINATOR;
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			if (!IsValid(*It)) { continue; }
			TInlineComponentArray<UStaticMeshComponent*> Comps(*It);
			for (UStaticMeshComponent* Comp : Comps)
			{
				if (!Comp || !Comp->GetStaticMesh()) { continue; }
				const FVector L = Comp->GetComponentLocation();
				if (FVector::Dist2D(L, Spots[SpotIdx]) > 900.f || FMath::Abs(L.Z - Spots[SpotIdx].Z) > 350.f) { continue; }
				const FRotator R = Comp->GetComponentRotation();
				Out += FString::Printf(TEXT("%s | pos=(%.0f, %.0f, %.0f) | rot=(%.0f, %.0f, %.0f) | scale=(%.2f, %.2f, %.2f)"),
					*Comp->GetStaticMesh()->GetName(), L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll,
					Comp->GetComponentScale().X, Comp->GetComponentScale().Y, Comp->GetComponentScale().Z);
				Out += LINE_TERMINATOR;
			}
		}
	}
	FFileHelper::SaveStringToFile(Out, *(FPaths::ProjectSavedDir() / TEXT("SpotScan.txt")));
	UE_LOG(LogWeedShop, Warning, TEXT("SPOTSCAN klaar (%d spots, %d tekens) -> Saved/SpotScan.txt"), Spots.Num(), Out.Len());
}

void ADoorRetrofitter::ElevDump()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	FString Out;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh()) { continue; }
			const FString MeshName = Comp->GetStaticMesh()->GetName();
			const FVector L = Comp->GetComponentLocation();
			if (FVector::Dist2D(L, ElevScanPos) > 2000.f) { continue; }
			const bool bElevMesh = MeshName.Contains(TEXT("Elevator"));
			const bool bNear = FVector::Dist2D(L, ElevScanPos) < 500.f;
			if (!bElevMesh && !bNear) { continue; }
			Out += FString::Printf(TEXT("%s | pos=(%.0f, %.0f, %.0f) | yaw=%.0f | scale=(%.2f, %.2f, %.2f)"),
				*MeshName, L.X, L.Y, L.Z, Comp->GetComponentRotation().Yaw,
				Comp->GetComponentScale().X, Comp->GetComponentScale().Y, Comp->GetComponentScale().Z);
			Out += LINE_TERMINATOR;
		}
	}
	FFileHelper::SaveStringToFile(Out, *(FPaths::ProjectSavedDir() / TEXT("ElevScan.txt")));
	UE_LOG(LogWeedShop, Warning, TEXT("ELEVSCAN dump klaar (%d tekens) -> Saved/ElevScan.txt"), Out.Len());
}

void ADoorRetrofitter::EnsureMapCapture()
{
	UWorld* W = GetWorld();
	if (!W || MapCapture) { return; }

	// Wereld-omvang bepalen uit de static-mesh-actors (1x): centrum + ortho-breedte van de kaart.
	FBox B(ForceInit);
	for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
	{
		if (IsValid(*It)) { B += It->GetActorLocation(); }
	}
	float TopZ = 10000.f;
	if (B.IsValid)
	{
		const FVector C = B.GetCenter();
		MapCenter = FVector2D(C.X, C.Y);
		const FVector E = B.GetExtent();
		MapOrtho = FMath::Clamp(FMath::Max(E.X, E.Y) * 2.3f, 20000.f, 300000.f);
		TopZ = B.Max.Z + 20000.f;
	}

	MapRT = NewObject<UTextureRenderTarget2D>(this, TEXT("PackMapRT"));
	MapRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	MapRT->ClearColor = FLinearColor(0.04f, 0.05f, 0.07f, 1.f);
	MapRT->InitAutoFormat(1024, 1024);
	MapRT->UpdateResourceImmediate(true);

	MapCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("PackMapCapture"));
	MapCapture->SetupAttachment(GetRootComponent());
	MapCapture->RegisterComponent();
	MapCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	MapCapture->OrthoWidth = MapOrtho;
	MapCapture->TextureTarget = MapRT;
	MapCapture->bCaptureEveryFrame = false;
	MapCapture->bCaptureOnMovement = false;
	// FinalColor ipv BaseColor: de pack-materialen (layered/nanite) renderen zwart in een BaseColor-
	// capture. FinalColor = een echte luchtfoto; exposure vastgepind zodat 'ie dag en nacht leesbaar is.
	MapCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	MapCapture->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	MapCapture->PostProcessSettings.AutoExposureMinBrightness = 0.45f;
	MapCapture->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	MapCapture->PostProcessSettings.AutoExposureMaxBrightness = 0.45f;
	MapCapture->ShowFlags.SetFog(false);          // hoogte-fog wast de luchtfoto anders uit
	MapCapture->ShowFlags.SetAtmosphere(false);   // atmosfeer tussen camera en grond idem
	// Pitch -90 + yaw 0 -> beeld: rechts = wereld +Y, omhoog = wereld +X (klopt met MapWidget::WorldToCanvas).
	MapCapture->SetWorldLocationAndRotation(FVector(MapCenter.X, MapCenter.Y, TopZ), FRotator(-90.f, 0.f, 0.f));
}

void ADoorRetrofitter::CaptureMapNow()
{
	EnsureMapCapture();
	if (!MapCapture) { return; }
	// Characters niet inbakken (live dots tekent de MapWidget zelf).
	MapCapture->HiddenActors.Reset();
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<ACharacter> It(W); It; ++It)
		{
			if (IsValid(*It)) { MapCapture->HiddenActors.Add(*It); }
		}
	}
	MapCapture->CaptureScene();
}

void ADoorRetrofitter::ScanAndConvert()
{
	UWorld* W = GetWorld();
	if (!W) { return; }

	int32 NewThisPass = 0;
	for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
	{
		AStaticMeshActor* SMA = *It;
		if (!IsValid(SMA) || Converted.Contains(SMA)) { continue; }
		UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent();
		const FLeafDef* Leaf = Comp ? MatchLeaf(Comp->GetStaticMesh()) : nullptr;
		if (!Leaf) { continue; }

		// Origineel blad verbergen + collision uit (verwijderen kan niet altijd in gestreamde levels).
		const FTransform LeafTM = Comp->GetComponentTransform();
		SMA->SetActorHiddenInGame(true);
		SMA->SetActorEnableCollision(false);
		Converted.Add(SMA);

		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACityDoor* Door = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), LeafTM, SP))
		{
			Door->SetActorScale3D(LeafTM.GetScale3D());
			Door->SetupLeaf(Comp->GetStaticMesh(), Leaf->OpenDeg);
			SpawnedDoors.Add(Door);
			++NewThisPass;
			++TotalConverted;
		}
	}

	// Glas-pass: losse glas-meshes van de bladen aan de dichtstbijzijnde geconverteerde deur hangen,
	// zodat het raam met de deur mee draait i.p.v. in de lucht te blijven hangen.
	for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
	{
		AStaticMeshActor* SMA = *It;
		if (!IsValid(SMA) || Converted.Contains(SMA)) { continue; }
		UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent();
		if (!Comp || !IsLeafGlass(Comp->GetStaticMesh())) { continue; }

		const FVector GlassLoc = Comp->GetComponentLocation();
		ACityDoor* Best = nullptr; float BestD = 60.f;
		for (const TWeakObjectPtr<ACityDoor>& Dw : SpawnedDoors)
		{
			ACityDoor* D = Dw.Get();
			if (!D) { continue; }
			const float Dist = FVector::Dist(D->GetActorLocation(), GlassLoc);
			if (Dist < BestD) { BestD = Dist; Best = D; }
		}
		if (!Best) { continue; } // blad nog niet geconverteerd (gestreamd?) -> volgende pass opnieuw

		const FTransform GlassTM = Comp->GetComponentTransform();
		SMA->SetActorHiddenInGame(true);
		SMA->SetActorEnableCollision(false);
		Converted.Add(SMA);
		Best->AddLeafExtra(Comp->GetStaticMesh(), GlassTM);
	}

	// Raam-fixup: glas-/raam-COMPONENTEN (van elk actor-type: losse actors, level instances, ISM/HISM)
	// staan in de map op no-collision/pawn-ignore, waardoor je dwars door ramen heen loopt. Forceer
	// blokkeren voor de speler. (Door-glas aan onze scharnieren is van ACityDoor - die slaan we over.)
	int32 GlassFixed = 0;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		// Sla onze deuren, al-geconverteerde originelen (verborgen bladen/glas) en alles wat
		// onzichtbaar is over - anders zet je collision terug op het verborgen deur-glas en
		// staat er een onzichtbare blokkade in de deuropening.
		if (!IsValid(A) || A->IsA(ACityDoor::StaticClass()) || Converted.Contains(A) || A->IsHidden()) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh() || GlassFixedComps.Contains(Comp)) { continue; }
			const FString MeshName = Comp->GetStaticMesh()->GetName().ToLower();
			// HDRI-fotokoepel verbergen: daar zit een vaste felle zon in de FOTO gebakken (beweegt nooit).
			// De map heeft echte SkyAtmosphere-actors - die volgen onze draaiende zon wel.
			if (MeshName.Contains(TEXT("envirodome")) || MeshName.Contains(TEXT("hdri")))
			{
				GlassFixedComps.Add(Comp);
				Comp->SetVisibility(false);
				Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				continue;
			}
			if (!MeshName.Contains(TEXT("glass")) && !MeshName.Contains(TEXT("window"))) { continue; }
			GlassFixedComps.Add(Comp); // niet nogmaals checken
			const bool bNoCollision = Comp->GetCollisionEnabled() == ECollisionEnabled::NoCollision
				|| Comp->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block;
			if (!bNoCollision) { continue; }
			A->SetActorEnableCollision(true);
			Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			++GlassFixed;
		}
	}
	if (GlassFixed > 0)
	{
		UE_LOG(LogWeedShop, Log, TEXT("DoorRetrofitter: %d glas/raam-componenten blokkeren nu de speler"), GlassFixed);
	}

	// Vreemde zonnen/skylights uitschakelen: de pack streamt lighting-scenario's in (meerdere
	// directional lights + skylights). Destroy is onbetrouwbaar op gestreamde actors -> component
	// onzichtbaar + intensiteit 0 werkt altijd. Onze controller adopteert daarna sky/atmosfeer.
	if (ADayNightController* DN = ADayNightController::GetLocal(W))
	{
		DN->TryAdoptSky();
		ADirectionalLight* AdoptedSun = DN->GetSun();
		for (TActorIterator<ADirectionalLight> It(W); It; ++It)
		{
			if (*It == AdoptedSun) { continue; }
			if (ULightComponent* LC = It->GetLightComponent())
			{
				if (LC->IsVisible())
				{
					LC->SetVisibility(false);
					LC->SetIntensity(0.f);
				}
			}
		}
		ASkyLight* AdoptedSky = DN->GetSky();
		for (TActorIterator<ASkyLight> It(W); It; ++It)
		{
			if (*It == AdoptedSky) { continue; }
			if (USkyLightComponent* SC = It->GetLightComponent())
			{
				if (SC->IsVisible())
				{
					SC->SetVisibility(false);
					SC->SetIntensity(0.f);
				}
			}
		}
	}

	// Lift-schachten: groepeer SM_ElevatorDoorFrame01-frames per XY-cluster; zodra een cluster 2 scans
	// stabiel is (alle verdiepingen ingestreamd) spawnen we een werkende APackElevator met de bestaande
	// schuif-panelen + de pack-cabine.
	{
		struct FShaft { FVector Ref = FVector::ZeroVector; float FrameYaw = 0.f; TArray<float> FloorZ; };
		TMap<FIntPoint, FShaft> Shafts;
		TArray<TPair<FVector, UStaticMeshComponent*>> AllPanels; // (positie, paneel)
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			AActor* A = *It;
			if (!IsValid(A) || A->IsA(APackElevator::StaticClass())) { continue; }
			TInlineComponentArray<UStaticMeshComponent*> Comps(A);
			for (UStaticMeshComponent* Comp : Comps)
			{
				if (!Comp || !Comp->GetStaticMesh()) { continue; }
				const FString MeshName = Comp->GetStaticMesh()->GetName();
				const FVector L = Comp->GetComponentLocation();
				if (MeshName == TEXT("SM_ElevatorDoorFrame01"))
				{
					const FIntPoint Key(FMath::RoundToInt(L.X / 100.f), FMath::RoundToInt(L.Y / 100.f));
					FShaft& Sh = Shafts.FindOrAdd(Key);
					Sh.Ref = L;
					Sh.FrameYaw = Comp->GetComponentRotation().Yaw;
					Sh.FloorZ.AddUnique(L.Z);
				}
				else if (MeshName == TEXT("SM_ElevatorDoor"))
				{
					AllPanels.Add(TPair<FVector, UStaticMeshComponent*>(L, Comp));
				}
			}
		}
		for (TPair<FIntPoint, FShaft>& KV : Shafts)
		{
			if (ElevBuilt.Contains(KV.Key)) { continue; }
			int32& Prev = ElevPrevCount.FindOrAdd(KV.Key);
			const int32 Count = KV.Value.FloorZ.Num();
			if (Count < 2 || Count != Prev) { Prev = Count; continue; } // wacht tot 2 scans stabiel
			ElevBuilt.Add(KV.Key);

			TArray<float> Floors = KV.Value.FloorZ;
			Floors.Sort();
			// Panelen koppelen aan verdieping-index (zelfde Z, binnen 250 XY van de schacht).
			TArray<TPair<int32, UStaticMeshComponent*>> RawPanels;
			TArray<FVector> GroundPanelPos;
			for (const TPair<FVector, UStaticMeshComponent*>& P : AllPanels)
			{
				if (FVector::Dist2D(P.Key, KV.Value.Ref) > 250.f) { continue; }
				int32 FloorIdx = INDEX_NONE;
				for (int32 i = 0; i < Floors.Num(); ++i) { if (FMath::Abs(P.Key.Z - Floors[i]) < 60.f) { FloorIdx = i; break; } }
				if (FloorIdx == INDEX_NONE) { continue; }
				RawPanels.Add(TPair<int32, UStaticMeshComponent*>(FloorIdx, P.Value));
				if (FloorIdx == 0) { GroundPanelPos.Add(P.Key); }
			}
			if (RawPanels.Num() == 0) { continue; }

			// FRAME-gebaseerde geometrie (gemeten): frame-pivot = exacte opening-centrum (lokale Y +-77).
			// De paneel-mesh strekt zich lokaal -Y uit vanaf z'n pivot -> de WERELD-spanrichting volgt
			// uit de paneel-rotatie (niet uit parkeer-posities raden: de map parkeert ze vrijwel dicht).
			const FVector OpeningCenter = KV.Value.Ref;
			FVector SpanDir = FVector::XAxisVector;
			if (RawPanels.Num() > 0 && RawPanels[0].Value)
			{
				SpanDir = -RawPanels[0].Value->GetComponentRotation().RotateVector(FVector::YAxisVector).GetSafeNormal2D();
			}
			// Schuiven doen we TERUG langs de span (panelen verdwijnen achter de muur aan de pivot-kant).
			FVector SlideDir = -SpanDir;

			// Dicht: FRONT-pivot op het centrum (dekt centrum..centrum+68), BACK op centrum-68 (dekt de
			// andere helft) -> samen 136 gecentreerd. Open: front 146 langs SlideDir (steekt de hele
			// opening over), back 78 - beide eindigen buiten de opening.
			TArray<FElevPanelInit> Panels;
			for (int32 Fi = 0; Fi < Floors.Num(); ++Fi)
			{
				TArray<UStaticMeshComponent*> FloorPanels;
				for (TPair<int32, UStaticMeshComponent*>& RP : RawPanels)
				{
					if (RP.Key == Fi && RP.Value) { FloorPanels.Add(RP.Value); }
				}
				const FVector CXY(OpeningCenter.X, OpeningCenter.Y, 0.f);
				if (FloorPanels.Num() >= 2)
				{
					const FVector FrontClosed2D = CXY;
					const FVector BackClosed2D  = CXY - SpanDir * 68.f;
					// Koppel componenten aan front/back op MINIMALE verplaatsing vanaf hun map-stand.
					const FVector P0 = FloorPanels[0]->GetComponentLocation();
					const FVector P1 = FloorPanels[1]->GetComponentLocation();
					const float Cost01 = FVector::Dist2D(P0, FrontClosed2D) + FVector::Dist2D(P1, BackClosed2D);
					const float Cost10 = FVector::Dist2D(P1, FrontClosed2D) + FVector::Dist2D(P0, BackClosed2D);
					UStaticMeshComponent* FrontC = (Cost01 <= Cost10) ? FloorPanels[0] : FloorPanels[1];
					UStaticMeshComponent* BackC  = (Cost01 <= Cost10) ? FloorPanels[1] : FloorPanels[0];
					FElevPanelInit F; F.Comp = FrontC; F.FloorIdx = Fi;
					F.ClosedPos = FrontClosed2D + FVector(0.f, 0.f, FrontC->GetComponentLocation().Z);
					F.SlideDist = 146.f;
					FElevPanelInit B; B.Comp = BackC; B.FloorIdx = Fi;
					B.ClosedPos = BackClosed2D + FVector(0.f, 0.f, BackC->GetComponentLocation().Z);
					B.SlideDist = 78.f;
					Panels.Add(F); Panels.Add(B);
				}
				else if (FloorPanels.Num() == 1)
				{
					FElevPanelInit A; A.Comp = FloorPanels[0]; A.FloorIdx = Fi;
					A.ClosedPos = CXY - SpanDir * 34.f + FVector(0.f, 0.f, FloorPanels[0]->GetComponentLocation().Z);
					A.SlideDist = 146.f;
					Panels.Add(A);
				}
			}
			if (Panels.Num() == 0) { continue; }
			// Schacht-kant FYSIEK bepalen: trace vanaf verdieping 1 omlaag aan beide kanten van de deur.
			// De kant ZONDER vloer eronder (gat dat doorloopt) is de schacht.
			const FVector Perp = FVector::CrossProduct(SlideDir, FVector::UpVector).GetSafeNormal2D();
			const float ProbeZ = (Floors.Num() > 1 ? Floors[1] : Floors[0]) + 120.f;
			auto HasFloorBelow = [&](const FVector& Side) -> bool
			{
				FHitResult Hit;
				FCollisionQueryParams QP(SCENE_QUERY_STAT(ElevShaftProbe), false);
				const FVector Start = FVector(OpeningCenter.X, OpeningCenter.Y, ProbeZ) + Side * 110.f;
				return W->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, 320.f), ECC_Visibility, QP);
			};
			const bool bFloorPos = HasFloorBelow(Perp);
			const bool bFloorNeg = HasFloorBelow(-Perp);
			const FVector ShaftSide = (!bFloorPos && bFloorNeg) ? Perp : ((!bFloorNeg && bFloorPos) ? -Perp : Perp);
			// Cabine-pivot = de OPEN kant van de cabine-mesh -> pivot exact op het deurvlak (frame-pivot),
			// 2cm de schacht in tegen z-fighting. De Setup draait de opening naar de gang.
			const FVector CabCenter = OpeningCenter + ShaftSide * 2.f;

			FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (APackElevator* Elev = W->SpawnActor<APackElevator>(APackElevator::StaticClass(), FTransform(FVector(CabCenter.X, CabCenter.Y, Floors[0])), SP))
			{
				Elev->Setup(Floors, SlideDir, Panels, CabCenter, -ShaftSide);
				// Per verdieping: call-knop naast de deur + digit-bordje boven de deur, beide op het
				// gang-muurvlak (frame is 16cm diep; pivot aan een kant -> 17cm uit het centrum).
				for (int32 Fi = 0; Fi < Floors.Num(); ++Fi)
				{
					const FRotator BtnRot = (-ShaftSide).Rotation();
					const FVector WallFace = FVector(OpeningCenter.X, OpeningCenter.Y, 0.f) - ShaftSide * 17.f;
					const FVector BtnLoc = WallFace + SlideDir * 110.f + FVector(0.f, 0.f, Floors[Fi] + 115.f);
					if (APackElevatorButton* Btn = W->SpawnActor<APackElevatorButton>(APackElevatorButton::StaticClass(), FTransform(BtnRot, BtnLoc), SP))
					{
						Btn->Setup(Elev, Fi);
						const FVector SignLoc = WallFace + FVector(0.f, 0.f, Floors[Fi] + 255.f);
						Btn->SetupSign(SignLoc, BtnRot);
						Elev->RegisterButton(Btn);
					}
				}
				UE_LOG(LogWeedShop, Warning, TEXT("PackElevator gebouwd: %d verdiepingen @ (%.0f, %.0f)"), Floors.Num(), CabCenter.X, CabCenter.Y);
			}
		}
	}

	CloneRooms();

	if (NewThisPass > 0)
	{
		UE_LOG(LogWeedShop, Log, TEXT("DoorRetrofitter: %d nieuwe deuren werkend gemaakt (totaal %d)"), NewThisPass, TotalConverted);
	}
}

void ADoorRetrofitter::CloneRooms()
{
	UWorld* W = GetWorld();
	if (!W) { return; }

	// BRON-appartement (1e verdieping hotel, uit de SpotScan). Anker = het DEUR-FRAME (pivot is het
	// midden van de deuropening - deur-BLADEN ankeren niet: hun pivot is het scharnier en de
	// scharnier-kant wisselt per deur, waardoor klonen verschoven/gespiegeld landden).
	const FVector SrcFramePos(-2508.f, -1699.f, 480.f);
	const FBox SrcBox(FVector(-2512.f, -2418.f, 472.f), FVector(-1695.f, -1552.f, 828.f));

	// 1) Bron-frame + alle kandidaat-frames op dezelfde etage verzamelen.
	FTransform SrcFrameTM = FTransform::Identity;
	bool bSrcFound = false;
	TArray<FTransform> TargetFrames;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A) || A->IsHidden()) { continue; }
		if (A->IsA(ACityDoor::StaticClass()) || A->IsA(APackElevator::StaticClass()) || A->IsA(APackElevatorButton::StaticClass())) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh()) { continue; }
			if (Comp->GetStaticMesh()->GetName() != TEXT("SM_DoorFrameCommerical_01")) { continue; }
			const FVector L = Comp->GetComponentLocation();
			if (FMath::Abs(L.Z - SrcFramePos.Z) > 12.f) { continue; } // zelfde etage
			if (FVector::Dist2D(L, SrcFramePos) < 12.f) { SrcFrameTM = Comp->GetComponentTransform(); bSrcFound = true; }
			else { TargetFrames.Add(Comp->GetComponentTransform()); }
		}
	}
	if (!bSrcFound || TargetFrames.Num() == 0)
	{
		if (--CloneLogCooldown <= 0)
		{
			CloneLogCooldown = 5;
			UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: wacht - bronFrame=%d kandidaten=%d"), bSrcFound ? 1 : 0, TargetFrames.Num());
		}
		return;
	}

	// 2) Bron-set: alle zichtbare meshes in het bron-volume.
	TArray<TPair<UStaticMesh*, FTransform>> SourceSet;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A) || A->IsHidden()) { continue; }
		if (A->IsA(ACityDoor::StaticClass()) || A->IsA(APackElevator::StaticClass()) || A->IsA(APackElevatorButton::StaticClass())) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh() || !Comp->IsVisible()) { continue; }
			const FVector L = Comp->GetComponentLocation();
			if (!SrcBox.IsInsideOrOn(L)) { continue; }
			SourceSet.Add(TPair<UStaticMesh*, FTransform>(Comp->GetStaticMesh(), Comp->GetComponentTransform()));
		}
	}
	// Wachten tot de bron VOLLEDIG ingestreamd is: vorige keer kloonde hij met 38 van de ~120 meshes
	// (half-leeg fragment). Pas klonen als de telling 2 scans achter elkaar gelijk is en hoog genoeg.
	if (SourceSet.Num() < 34 || SourceSet.Num() != LastSourceCount) // bron-kamer = 41 meshes totaal (gemeten)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: bron-set %d meshes (vorige %d) - wacht op stabiel/compleet"), SourceSet.Num(), LastSourceCount);
		LastSourceCount = SourceSet.Num();
		return;
	}

	// Referentie-punten van de bron-kamer in FRAME-lokale ruimte (centrum + 4 ingetrokken hoeken).
	const FTransform SrcInv = SrcFrameTM.Inverse();
	const FVector C = SrcBox.GetCenter();
	const FVector E = SrcBox.GetExtent();
	TArray<FVector> RefRel;
	RefRel.Add(SrcInv.TransformPosition(C));
	RefRel.Add(SrcInv.TransformPosition(C + FVector(E.X - 80.f, E.Y - 80.f, 0.f)));
	RefRel.Add(SrcInv.TransformPosition(C + FVector(-(E.X - 80.f), E.Y - 80.f, 0.f)));
	RefRel.Add(SrcInv.TransformPosition(C + FVector(E.X - 80.f, -(E.Y - 80.f), 0.f)));
	RefRel.Add(SrcInv.TransformPosition(C + FVector(-(E.X - 80.f), -(E.Y - 80.f), 0.f)));

	auto HasFloorBelow = [&](const FVector& P) -> bool
	{
		FHitResult Hit;
		FCollisionQueryParams QP(SCENE_QUERY_STAT(RoomVoidProbe), false);
		// Alleen de VLOER-band checken (vloer-oppervlak ~Z+8): niet dieper prikken, anders raak je het
		// plafond van de verdieping eronder (Z~-30) en lijkt een leeg slot bezet.
		const FVector Start(P.X, P.Y, SrcFramePos.Z + 100.f);
		return W->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, 110.f), ECC_Visibility, QP);
	};
	auto RoomBox2D = [&](const FTransform& FrameTM) -> FBox2D
	{
		FBox2D B(ForceInit);
		for (int32 i = 1; i < RefRel.Num(); ++i)
		{
			const FVector Wp = FrameTM.TransformPosition(RefRel[i]);
			B += FVector2D(Wp.X, Wp.Y);
		}
		return B;
	};

	// Al-geplaatste kamer-volumes (incl. de bron zelf) - klonen mogen elkaar niet overlappen.
	TArray<FBox2D> Placed;
	Placed.Add(FBox2D(FVector2D(SrcBox.Min.X, SrcBox.Min.Y), FVector2D(SrcBox.Max.X, SrcBox.Max.Y)));

	int32 NewRooms = 0;
	for (const FTransform& Target : TargetFrames)
	{
		const FVector FrameLoc = Target.GetLocation();
		// TESTFASE: eerst alleen de ene gemarkeerde deur (bij spot -2704,-1595) - daarna uitrollen.
		if (FVector::Dist2D(FrameLoc, FVector(-2704.f, -1595.f, 0.f)) > 250.f) { continue; }
		UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: test-frame gevonden op (%.0f, %.0f, %.0f)"), FrameLoc.X, FrameLoc.Y, FrameLoc.Z);
		const FIntPoint Key(FMath::RoundToInt(FrameLoc.X / 100.f), FMath::RoundToInt(FrameLoc.Y / 100.f));
		if (ClonedRooms.Contains(Key)) { continue; }

		// Beide orientaties proberen (frame kan 180 graden gedraaid geplaatst zijn): de goede orientatie
		// heeft VOID op het kamer-centrum en op de meeste hoeken.
		FTransform Best = FTransform::Identity;
		int32 BestVoids = -1;
		for (int32 Flip = 0; Flip < 2; ++Flip)
		{
			FTransform Cand = Target;
			if (Flip) { Cand.SetRotation(Cand.GetRotation() * FQuat(FVector::UpVector, PI)); }
			const FVector CenterW = Cand.TransformPosition(RefRel[0]);
			if (HasFloorBelow(CenterW)) { continue; } // hier ligt al een kamer/vloer
			int32 Voids = 0;
			for (int32 i = 1; i < RefRel.Num(); ++i)
			{
				if (!HasFloorBelow(Cand.TransformPosition(RefRel[i]))) { ++Voids; }
			}
			if (Voids > BestVoids) { BestVoids = Voids; Best = Cand; }
		}
		if (BestVoids < 2)
		{
			UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: frame (%.0f, %.0f) afgekeurd - beste orientatie %d/4 hoeken void"), FrameLoc.X, FrameLoc.Y, BestVoids);
			continue;
		}

		// Overlap-bewaking: niet klonen als het volume een al-geplaatste kamer raakt.
		const FBox2D NewBox = RoomBox2D(Best);
		bool bOverlaps = false;
		for (const FBox2D& Pb : Placed)
		{
			if (Pb.Intersect(NewBox))
			{
				const float OvW = FMath::Min(Pb.Max.X, NewBox.Max.X) - FMath::Max(Pb.Min.X, NewBox.Min.X);
				const float OvH = FMath::Min(Pb.Max.Y, NewBox.Max.Y) - FMath::Max(Pb.Min.Y, NewBox.Min.Y);
				if (OvW > 60.f && OvH > 60.f) { bOverlaps = true; break; } // >60cm echte overlap
			}
		}
		if (bOverlaps)
		{
			ClonedRooms.Add(Key); // niet blijven proberen
			UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: deur (%.0f, %.0f) overgeslagen - kamer zou overlappen"), FrameLoc.X, FrameLoc.Y);
			continue;
		}

		// Klonen: bron-frame -> deze orientatie.
		ClonedRooms.Add(Key);
		Placed.Add(NewBox);
		const FTransform Delta = SrcInv * Best;
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		int32 PlacedMeshes = 0;
		for (const TPair<UStaticMesh*, FTransform>& M : SourceSet)
		{
			const FTransform NewTM = M.Value * Delta;
			AStaticMeshActor* SMA = W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), NewTM, SP);
			if (!SMA) { continue; }
			if (UStaticMeshComponent* Cm = SMA->GetStaticMeshComponent())
			{
				Cm->SetMobility(EComponentMobility::Movable);
				Cm->SetStaticMesh(M.Key);
				Cm->SetCanEverAffectNavigation(false);
			}
			++PlacedMeshes;
		}
		++NewRooms;
		UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: kamer gekloond bij frame (%.0f, %.0f, %.0f) - %d meshes, %d/4 hoeken void"), FrameLoc.X, FrameLoc.Y, FrameLoc.Z, PlacedMeshes, BestVoids);
	}
}
