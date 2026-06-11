#include "World/DoorRetrofitter.h"

#include "WeedShopCore.h"
#include "World/CityDoor.h"
#include "World/DayNightController.h"
#include "World/PackElevator.h"
#include "World/PackElevatorButton.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
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
	// Renderer-warnings (bv. ForwardShadingPriority) lopen BUITEN ClearOnScreenDebugMessages om ->
	// alle on-screen debug-meldingen uitschakelen in pack-maps. Onze UMG-UI blijft gewoon werken.
	GAreScreenMessagesEnabled = false;

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
		// DICHT-stand bepalen: de map parkeert deuren (half/heel) OPEN als decor. Dicht = blad
		// evenwijdig aan het KOZIJN -> zoek het dichtstbijzijnde deur-frame en lijn het yaw daarop
		// uit (de hinge-pivot staat vast, dus alleen de rotatie hoeft recht).
		FTransform LeafTM = Comp->GetComponentTransform();
		float SwingOverride = 0.f; // 0 = gebruik de standaard draairichting van het blad-type
		{
			FRotator R = LeafTM.GetRotation().Rotator();
			const float ParkedYaw = R.Yaw;
			float ClosedYaw = FMath::GridSnap(R.Yaw, 90.f); // fallback zonder frame
			float BestD = 160.f;
			FVector BestFrameLoc = FVector::ZeroVector;
			float BestFrameYaw = 0.f;
			bool bHaveFrame = false;
			for (TActorIterator<AActor> FrIt(W); FrIt; ++FrIt)
			{
				if (!IsValid(*FrIt)) { continue; }
				TInlineComponentArray<UStaticMeshComponent*> FrComps(*FrIt);
				for (UStaticMeshComponent* Fr : FrComps)
				{
					if (!Fr || !Fr->GetStaticMesh()) { continue; }
					if (!Fr->GetStaticMesh()->GetName().Contains(TEXT("DoorFrame"))) { continue; }
					const FVector FrL = Fr->GetComponentLocation();
					if (FMath::Abs(FrL.Z - LeafTM.GetLocation().Z) > 60.f) { continue; }
					const float Dd = FVector::Dist2D(FrL, LeafTM.GetLocation());
					if (Dd >= BestD) { continue; }
					BestD = Dd; BestFrameLoc = FrL; BestFrameYaw = Fr->GetComponentRotation().Yaw; bHaveFrame = true;
				}
			}
			if (bHaveFrame)
			{
				// Dicht = blad IN de opening: het blad strekt zich lokaal -Y uit vanaf het scharnier,
				// dus de span-richting moet van het scharnier naar het KOZIJN-MIDDEN wijzen (de andere
				// deurpost). Dat maakt de keuze frameYaw vs frameYaw+180 ondubbelzinnig - "dichtstbij
				// huidige hoek" koos bij wijd-open geparkeerde deuren de verkeerde kant (blad over de muur).
				const FVector ToCenter = (BestFrameLoc - LeafTM.GetLocation()).GetSafeNormal2D();
				const FVector Span0 = FRotator(0.f, BestFrameYaw, 0.f).RotateVector(FVector(0.f, -1.f, 0.f));
				ClosedYaw = (FVector::DotProduct(FVector(Span0.X, Span0.Y, 0.f), ToCenter) >= 0.f) ? BestFrameYaw : BestFrameYaw + 180.f;
			}
			R.Yaw = ClosedYaw;
			LeafTM.SetRotation(R.Quaternion());
			// De map parkeerde de deur OPEN naar z'n legale kant (daar is bewezen ruimte, geen kozijn/
			// muur) -> gebruik die richting als draairichting van de werkende deur.
			const float ParkedDelta = FMath::FindDeltaAngleDegrees(ClosedYaw, ParkedYaw);
			if (FMath::Abs(ParkedDelta) > 25.f)
			{
				SwingOverride = (ParkedDelta > 0.f) ? FMath::Abs(Leaf->OpenDeg) : -FMath::Abs(Leaf->OpenDeg);
			}
		}
		SMA->SetActorHiddenInGame(true);
		SMA->SetActorEnableCollision(false);
		Converted.Add(SMA);

		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACityDoor* Door = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), LeafTM, SP))
		{
			Door->SetActorScale3D(LeafTM.GetScale3D());
			Door->SetupLeaf(Comp->GetStaticMesh(), (SwingOverride != 0.f) ? SwingOverride : Leaf->OpenDeg);
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
		ADirectionalLight* OurMoon = DN->GetMoon();
		for (TActorIterator<ADirectionalLight> It(W); It; ++It)
		{
			if (*It == AdoptedSun || *It == OurMoon) { continue; }
			if (ULightComponent* LC = It->GetLightComponent())
			{
				if (LC->IsVisible())
				{
					LC->SetVisibility(false);
					LC->SetIntensity(0.f);
				}
			}
		}
		// GESTAPELDE LIGHTING-SCENARIO'S: de map streamt Sunny/Sunny_02..04/Sunset.. allemaal TEGELIJK
		// in - elk met eigen fog (roze sunset-tint!), post-process en atmosfeer. Die optelsom gaf de
		// gigantische gloed-bal + roze waas. Alleen het basis-scenario (Lighting_Sunny) mag blijven.
		auto IsSurplusScenario = [](const AActor* A) -> bool
		{
			const ULevel* Lvl = A ? A->GetLevel() : nullptr;
			const FString Pkg = Lvl ? Lvl->GetOutermost()->GetName() : FString();
			if (!Pkg.Contains(TEXT("/Maps/Lighting/"))) { return false; }    // niet uit een scenario
			return !Pkg.EndsWith(TEXT("Lighting_Sunny"));                     // basis-scenario houden
		};
		for (TActorIterator<AExponentialHeightFog> It(W); It; ++It)
		{
			UExponentialHeightFogComponent* FC = It->GetComponent();
			if (!FC) { continue; }
			if (IsSurplusScenario(*It))
			{
				if (FC->IsVisible()) { FC->SetVisibility(false); }
			}
			else if (!bFogTamed)
			{
				// Basis-fog houden voor diepte, maar de ZON-GLOED eruit: de directional inscattering +
				// volumetric fog (getuned voor de oude HDRI-koepel) vulden de halve hemel met een
				// lichtbal. De SkyAtmosphere geeft zelf al nette afstands-haze.
				FC->SetDirectionalInscatteringColor(FLinearColor(0.16f, 0.14f, 0.12f)); // subtiele warme zon-gloed (vol = mega-bal, zwart = doods)
				FC->SetVolumetricFog(false);
				bFogTamed = true;
			}
		}
		for (TActorIterator<APostProcessVolume> It(W); It; ++It)
		{
			if (IsSurplusScenario(*It) && It->bEnabled)
			{
				It->bEnabled = false;
				It->BlendWeight = 0.f;
			}
		}

		// BINNEN-LAMPEN: de map heeft GEEN gebakken lighting, maar de point/spot-lampen staan op
		// Static mobility - die geven runtime dus letterlijk geen licht (zwarte plafonds ondanks
		// lampen). Op Movable zetten = echt licht; schaduwen uit voor performance (honderden lampjes).
		for (TActorIterator<AActor> ItL(W); ItL; ++ItL)
		{
			AActor* AL = *ItL;
			if (!IsValid(AL)) { continue; }
			TInlineComponentArray<ULocalLightComponent*> Lights(AL);
			for (ULocalLightComponent* LC : Lights)
			{
				if (!LC || IndoorLightsFixed.Contains(LC)) { continue; }
				IndoorLightsFixed.Add(LC);
				if (LC->Mobility != EComponentMobility::Movable)
				{
					LC->SetMobility(EComponentMobility::Movable);
					LC->SetCastShadows(false);
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

	// CloneRooms(); // UIT op verzoek: kamer-klonen gaf muren/vloeren op verkeerde plekken. Code blijft staan voor een latere, betere aanpak (kamer op maat van het slot).
	// BuildMarkedRooms(); // UIT: marker-kopie werkte niet lekker - vervangen door de dev building-tool (muren/vloeren tekenen via het bouw-systeem)
	VerticalReplicate();

	if (NewThisPass > 0)
	{
		UE_LOG(LogWeedShop, Log, TEXT("DoorRetrofitter: %d nieuwe deuren werkend gemaakt (totaal %d)"), NewThisPass, TotalConverted);
	}
}

// Kamers BOUWEN van losse onderdelen binnen gemarkeerde rechthoeken. Werkwijze voor de speler:
// vlieg (F7) of loop naar een lege kamer-ruimte, zet F9 op de ene hoek, F9 op de tegenoverliggende
// hoek (zelfde verdieping) - elk PAAR opeenvolgende marks wordt een kamer: vloer, muren (met opening
// bij een deur-frame aan de rand), plafond en een lamp. As-aligned (de hotels zijn dat ook).
// Kamer-KOPIEERDER, volledig speler-gestuurd: het EERSTE paar F9-marks = de BRON-rechthoek (zet ze
// om de echte ingerichte kamer heen, badkamer incl.). Elk VOLGEND paar = een doel-plek: alles binnen
// de bron-rechthoek wordt 1-op-1 daarheen gekopieerd (verschuiving, geen rotatie - markeer doelen met
// dezelfde orientatie t.o.v. hun deur). Vloer-hoogte snapt automatisch op de echte vloer. Verborgen
// deur-bladen worden mee-gekopieerd en door de leaf-converter weer werkende deuren.
// VERTICALE VERDIEPING-KOPIE: zet 1 enkele F9-marker midden in een ingerichte kamer/verdieping.
// Alles in die verdieping-slice (box om de marker) wordt 1-op-1 naar de verdiepingen erboven en
// eronder gekopieerd - ZONDER draaien of schuiven, puur +/-350cm per verdieping. Meshes die daar al
// staan (gevel, ramen, lift, gang) worden overgeslagen (dedupe), dus alleen het ontbrekende
// interieur wordt aangevuld. Werkt alleen als het bestand precies 1 marker voor deze map bevat.
void ADoorRetrofitter::VerticalReplicate()
{
	if (bVertCloneDone) { return; }
	UWorld* W = GetWorld();
	if (!W) { return; }
	const FString MapPath = W->GetOutermost()->GetName();

	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	TArray<FVector> Marks;
	for (const FString& Line : Lines)
	{
		if (!Line.Contains(MapPath)) { continue; }
		const int32 PIdx = Line.Find(TEXT("pos=("));
		if (PIdx == INDEX_NONE) { continue; }
		FString PosStr = Line.Mid(PIdx + 5);
		int32 Close = INDEX_NONE;
		if (PosStr.FindChar(TEXT(')'), Close)) { PosStr = PosStr.Left(Close); }
		TArray<FString> Parts;
		PosStr.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() >= 3) { Marks.Add(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]))); }
	}
	if (Marks.Num() != 2) { return; } // 2 markers: verste hoek kamer <-> verste hoek badkamer

	// EEN strakke rechthoek tussen de 2 markers (+30cm marge): alles daarbinnen gaat mee, alles
	// daarbuiten (gevel-details, balkons, terras) blijft VOLLEDIG met rust.
	FBox2D Outer(ForceInit);
	Outer += FVector2D(Marks[0].X, Marks[0].Y);
	Outer += FVector2D(Marks[1].X, Marks[1].Y);
	Outer = Outer.ExpandBy(30.f);
	auto InRects = [&Outer](const FVector& L) -> bool
	{
		return L.X >= Outer.Min.X && L.X <= Outer.Max.X && L.Y >= Outer.Min.Y && L.Y <= Outer.Max.Y;
	};
	// Alleen capteren als de speler dichtbij de bron is - anders is de bron half-gestreamd en
	// kopieer je halve kamers.
	{
		APlayerController* PC = W->GetFirstPlayerController();
		APawn* P = PC ? PC->GetPawn() : nullptr;
		const FVector2D OC = Outer.GetCenter();
		if (!P || FVector::Dist2D(P->GetActorLocation(), FVector(OC.X, OC.Y, 0.f)) > 2800.f) { return; }
	}
	const float Feet = Marks[0].Z - 98.f;
	const float SrcZ = 480.f + 350.f * FMath::RoundToFloat((Feet - 480.f) / 350.f); // verdieping-grid

	// Bron-slice verzamelen (incl. verborgen geconverteerde deur-bladen -> werkende deuren in de kopie).
	// MET materialen: de map geeft bv. het raam-glas op ingerichte verdiepingen een OVERRIDE (helder
	// glas) - zonder die override valt geplakt glas terug op het standaard-materiaal (parallax/nep-3D).
	struct FSliceEntry { UStaticMesh* Mesh; FTransform TM; FVector BO; TArray<UMaterialInterface*> Mats; };
	TArray<FSliceEntry> Slice;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) { continue; }
		if (A->IsA(ACityDoor::StaticClass()) || A->IsA(APackElevator::StaticClass()) || A->IsA(APackElevatorButton::StaticClass())) { continue; }
		const bool bHiddenActor = A->IsHidden();
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh()) { continue; }
			const FString MeshName = Comp->GetStaticMesh()->GetName();
			if (bHiddenActor && !MeshName.StartsWith(TEXT("SM_Door"))) { continue; }
			if (MeshName.Contains(TEXT("Camera")) || MeshName.Contains(TEXT("SecurityCam")) || MeshName.Contains(TEXT("MatineeCam"))
				|| MeshName.Contains(TEXT("DomeCam")) || MeshName.Contains(TEXT("SecurityLight"))) { continue; } // (editor-)camera's/spots horen niet in kopieen
			// Buiten-meubilair niet mee-kopieren (zweefde op elke verdieping boven het terras).
			if (MeshName.Contains(TEXT("Umbrella")) || MeshName.Contains(TEXT("Parasol")) || MeshName.Contains(TEXT("Lounger"))
				|| MeshName.Contains(TEXT("SunBed")) || MeshName.Contains(TEXT("Sunbed")) || MeshName.Contains(TEXT("Chair"))
				|| MeshName.Contains(TEXT("Table")) || MeshName.Contains(TEXT("Awning")) || MeshName.Contains(TEXT("Pool"))) { continue; }
			// Op het VISUELE MIDDELPUNT (bounds) testen, niet de pivot: pivots zitten op hoeken/
			// uiteinden, waardoor randstukken met pivot net buiten de rechthoek afvielen (halve kamers).
			const FVector L = Comp->Bounds.Origin;
			if (!InRects(L)) { continue; }
			if (L.Z < SrcZ - 20.f || L.Z > SrcZ + 335.f) { continue; } // alleen deze verdieping-slice
			FSliceEntry E;
			E.Mesh = Comp->GetStaticMesh();
			E.TM = Comp->GetComponentTransform();
			E.BO = L;
			for (int32 Mi = 0; Mi < Comp->GetNumMaterials(); ++Mi) { E.Mats.Add(Comp->GetMaterial(Mi)); }
			Slice.Add(E);
		}
	}
	// Wachten tot de bron-verdieping volledig ingestreamd is (3 scans dezelfde telling).
	if (Slice.Num() == VertLastCount) { ++VertStableStreak; } else { VertStableStreak = 0; }
	VertLastCount = Slice.Num();
	if (Slice.Num() < 30 || VertStableStreak < 3)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("VertClone: bron-slice %d meshes (streak %d) - wacht"), Slice.Num(), VertStableStreak);
		return;
	}
	bVertCloneDone = true;

	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 TotalPlaced = 0;
	for (int32 N = -8; N <= 8; ++N)
	{
		if (N == 0) { continue; }
		const float Dz = N * 350.f;
		const float TgtZ = SrcZ + Dz;
		if (TgtZ < 470.f) { continue; } // begane grond heeft een andere hoogte (430) - overslaan

		// Bestaat deze verdieping (heeft het gebouw hier uberhaupt geometrie)? En dedupe-index bouwen:
		// mesh-naam + positie op 10cm-grid van alles wat er al staat.
		TMap<uint64, UStaticMeshComponent*> Existing; // hash -> bestaande comp (voor materiaal-sync)
		int32 ExistCount = 0;
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			AActor* A = *It;
			if (!IsValid(A)) { continue; }
			TInlineComponentArray<UStaticMeshComponent*> Comps(A);
			for (UStaticMeshComponent* Comp : Comps)
			{
				if (!Comp || !Comp->GetStaticMesh()) { continue; }
				const FVector L = Comp->Bounds.Origin; // zelfde basis als de bron-capture
				// RUIME zone voor de bestaat-deze-verdieping-check: lege verdiepingen hebben binnen de
				// kamer-rechthoek juist bijna niets (daarom zijn ze leeg) - maar de gang/gevel ernaast
				// bewijst dat de verdieping bestaat. De dedupe-index blijft op de strakke zone.
				if (L.X < Outer.Min.X - 1300.f || L.X > Outer.Max.X + 1300.f || L.Y < Outer.Min.Y - 1300.f || L.Y > Outer.Max.Y + 1300.f) { continue; }
				if (L.Z < TgtZ - 25.f || L.Z > TgtZ + 340.f) { continue; }
				++ExistCount;
				if (L.X < Outer.Min.X - 50.f || L.X > Outer.Max.X + 50.f || L.Y < Outer.Min.Y - 50.f || L.Y > Outer.Max.Y + 50.f) { continue; }
				const uint64 H = GetTypeHash(Comp->GetStaticMesh()->GetFName())
					^ (uint64)(FMath::RoundToInt(L.X / 10.f) * 73856093)
					^ (uint64)(FMath::RoundToInt(L.Y / 10.f) * 19349663)
					^ (uint64)(FMath::RoundToInt(L.Z / 10.f) * 83492791);
				Existing.Add(H, Comp);
			}
		}
		if (ExistCount < 25) { continue; } // geen verdieping hier (boven het dak / onder de grond)

		int32 Placed = 0;
		for (const FSliceEntry& M : Slice)
		{
			FTransform NewTM = M.TM;
			NewTM.AddToTranslation(FVector(0.f, 0.f, Dz));
			const FVector NL = M.BO + FVector(0.f, 0.f, Dz); // bounds-center op de doel-verdieping
			const uint64 H = GetTypeHash(M.Mesh->GetFName())
				^ (uint64)(FMath::RoundToInt(NL.X / 10.f) * 73856093)
				^ (uint64)(FMath::RoundToInt(NL.Y / 10.f) * 19349663)
				^ (uint64)(FMath::RoundToInt(NL.Z / 10.f) * 83492791);
			if (UStaticMeshComponent** Found = Existing.Find(H))
			{
				// Staat hier al (gevel/raam/gang): NIET dubbel plaatsen, maar wel de bron-materialen
				// overnemen - zo wordt parallax-glas (nep-3D) hier helder glas, zonder iets te slopen.
				if (*Found)
				{
					for (int32 Mi = 0; Mi < M.Mats.Num() && Mi < (*Found)->GetNumMaterials(); ++Mi)
					{
						if (M.Mats[Mi] && (*Found)->GetMaterial(Mi) != M.Mats[Mi]) { (*Found)->SetMaterial(Mi, M.Mats[Mi]); }
					}
				}
				continue;
			}
			// SETBACK-check: alleen plakken als er gebouw ONDER dit punt zit (trapsgewijze gebouwen
			// zijn boven smaller - anders zweven vloeren in de lucht boven terrassen).
			{
				FHitResult GroundHit;
				FCollisionQueryParams GQP(SCENE_QUERY_STAT(VertCloneGround), false);
				const FVector GStart(NL.X, NL.Y, TgtZ + 90.f);
				if (!W->LineTraceSingleByChannel(GroundHit, GStart, GStart - FVector(0.f, 0.f, 470.f), ECC_Visibility, GQP))
				{
					continue; // open lucht eronder -> hier bestaat het gebouw niet meer
				}
			}
			AStaticMeshActor* SMA = W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), NewTM, SP);
			if (!SMA) { continue; }
			if (UStaticMeshComponent* C = SMA->GetStaticMeshComponent())
			{
				C->SetMobility(EComponentMobility::Movable);
				C->SetStaticMesh(M.Mesh);
				C->SetCanEverAffectNavigation(false);
				// Bron-materialen overnemen (incl. heldere glas-overrides van de map-makers).
				for (int32 Mi = 0; Mi < M.Mats.Num(); ++Mi)
				{
					if (M.Mats[Mi]) { C->SetMaterial(Mi, M.Mats[Mi]); }
				}
			}
			++Placed;
		}
		TotalPlaced += Placed;
		UE_LOG(LogWeedShop, Warning, TEXT("VertClone: verdieping %+d (Z %.0f): %d meshes aangevuld (%d bestonden al)"), N, TgtZ, Placed, ExistCount);
	}
	UE_LOG(LogWeedShop, Warning, TEXT("VertClone: KLAAR - %d meshes totaal aangevuld vanaf slice Z %.0f (%d bron-meshes)"), TotalPlaced, SrcZ, Slice.Num());
}

void ADoorRetrofitter::BuildMarkedRooms()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const FString MapPath = W->GetOutermost()->GetName();

	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	TArray<FVector> Marks;
	for (const FString& Line : Lines)
	{
		if (!Line.Contains(MapPath)) { continue; }
		const int32 PIdx = Line.Find(TEXT("pos=("));
		if (PIdx == INDEX_NONE) { continue; }
		FString PosStr = Line.Mid(PIdx + 5);
		int32 Close = INDEX_NONE;
		if (PosStr.FindChar(TEXT(')'), Close)) { PosStr = PosStr.Left(Close); }
		TArray<FString> Parts;
		PosStr.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() >= 3) { Marks.Add(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]))); }
	}
	if (Marks.Num() < 4) { return; } // bron-paar + minstens 1 doel-paar

	auto NearestFloorZ = [&](const FVector& Around, float Fallback) -> float
	{
		float Best = Fallback, BestDz = 220.f;
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			if (!IsValid(*It)) { continue; }
			TInlineComponentArray<UStaticMeshComponent*> Comps(*It);
			for (UStaticMeshComponent* Comp : Comps)
			{
				if (!Comp || !Comp->GetStaticMesh()) { continue; }
				if (!Comp->GetStaticMesh()->GetName().StartsWith(TEXT("SM_Floor"))) { continue; }
				const FVector L = Comp->GetComponentLocation();
				if (FVector::Dist2D(L, Around) > 1500.f) { continue; }
				const float Dz = FMath::Abs(L.Z - Fallback);
				if (Dz < BestDz) { BestDz = Dz; Best = L.Z; }
			}
		}
		return Best;
	};

	// BRON: eerste paar.
	const FVector SA = Marks[0], SB = Marks[1];
	if (FMath::Abs(SA.Z - SB.Z) > 200.f || FVector::Dist2D(SA, SB) < 250.f) { return; }
	const float SX0 = FMath::Min(SA.X, SB.X), SX1 = FMath::Max(SA.X, SB.X);
	const float SY0 = FMath::Min(SA.Y, SB.Y), SY1 = FMath::Max(SA.Y, SB.Y);
	const float SrcFloorZ = NearestFloorZ(FVector((SX0 + SX1) * 0.5f, (SY0 + SY1) * 0.5f, 0.f), FMath::Min(SA.Z, SB.Z) - 98.f);

	// Bron-inhoud verzamelen (incl. verborgen deur-bladen zodat de kopie werkende deuren krijgt).
	TArray<TPair<UStaticMesh*, FTransform>> SourceSet;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) { continue; }
		if (A->IsA(ACityDoor::StaticClass()) || A->IsA(APackElevator::StaticClass()) || A->IsA(APackElevatorButton::StaticClass())) { continue; }
		const bool bHiddenActor = A->IsHidden();
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh()) { continue; }
			const FString MeshName = Comp->GetStaticMesh()->GetName();
			// Verborgen actors alleen meenemen als het (geconverteerde) deur-bladen zijn.
			if (bHiddenActor && !MeshName.StartsWith(TEXT("SM_Door"))) { continue; }
			const FVector L = Comp->GetComponentLocation();
			if (L.X < SX0 - 12.f || L.X > SX1 + 12.f || L.Y < SY0 - 12.f || L.Y > SY1 + 12.f) { continue; }
			if (L.Z < SrcFloorZ - 8.f || L.Z > SrcFloorZ + 332.f) { continue; }
			SourceSet.Add(TPair<UStaticMesh*, FTransform>(Comp->GetStaticMesh(), Comp->GetComponentTransform()));
		}
	}
	if (SourceSet.Num() == LastSourceCount) { ++SourceStableStreak; } else { SourceStableStreak = 0; }
	LastSourceCount = SourceSet.Num();
	if (SourceSet.Num() < 8 || SourceStableStreak < 2) { return; } // wachten tot de bron volledig gestreamd is

	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// DOELEN: elk volgend paar = kopie-plek (verschuiving t.o.v. de bron-min-hoek).
	for (int32 Pi = 2; Pi + 1 < Marks.Num(); Pi += 2)
	{
		const FVector& TA = Marks[Pi];
		const FVector& TB = Marks[Pi + 1];
		if (FMath::Abs(TA.Z - TB.Z) > 200.f || FVector::Dist2D(TA, TB) < 250.f) { continue; }
		const float TX0 = FMath::Min(TA.X, TB.X);
		const float TY0 = FMath::Min(TA.Y, TB.Y);
		const FIntPoint Key(FMath::RoundToInt(TX0 / 100.f), FMath::RoundToInt(TY0 / 100.f));
		if (BuiltRects.Contains(Key)) { continue; }
		BuiltRects.Add(Key);

		const float TFloorZ = NearestFloorZ(FVector(TX0 + (SX1 - SX0) * 0.5f, TY0 + (SY1 - SY0) * 0.5f, 0.f), FMath::Min(TA.Z, TB.Z) - 98.f);
		// GRID-SNAP: het hele gebouw staat op een 1m-grid - door de verschuiving op 100cm te snappen
		// vallen alle gekopieerde muren exact in lijn met de muren eronder en de buitengevel
		// (en sluiten ramen er straks strak op aan). Marks hoeven dus maar ~50cm nauwkeurig.
		const FVector Offset(FMath::GridSnap(TX0 - SX0, 100.f), FMath::GridSnap(TY0 - SY0, 100.f), TFloorZ - SrcFloorZ);

		int32 Placed = 0;
		for (const TPair<UStaticMesh*, FTransform>& M : SourceSet)
		{
			FTransform NewTM = M.Value;
			NewTM.AddToTranslation(Offset);
			AStaticMeshActor* SMA = W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), NewTM, SP);
			if (!SMA) { continue; }
			if (UStaticMeshComponent* C = SMA->GetStaticMeshComponent())
			{
				C->SetMobility(EComponentMobility::Movable);
				C->SetStaticMesh(M.Key);
				C->SetCanEverAffectNavigation(false);
			}
			++Placed;
		}
		UE_LOG(LogWeedShop, Warning, TEXT("RoomBuilder: kopie geplaatst op (%.0f, %.0f, %.0f) - %d meshes (bron %d)"), TX0, TY0, TFloorZ, Placed, SourceSet.Num());
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
	if (SourceSet.Num() == LastSourceCount) { ++SourceStableStreak; } else { SourceStableStreak = 0; }
	LastSourceCount = SourceSet.Num();
	if (SourceSet.Num() < 34 || SourceStableStreak < 2) // 3 scans dezelfde telling = echt klaar met streamen
	{
		UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: bron-set %d meshes (streak %d) - wacht op stabiel/compleet"), SourceSet.Num(), SourceStableStreak);
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

	FString RoomCloneLog;

	int32 NewRooms = 0;
	for (const FTransform& Target : TargetFrames)
	{
		const FVector FrameLoc = Target.GetLocation();
		// TESTFASE: eerst alleen de ene gemarkeerde deur (bij spot -2704,-1595) - daarna uitrollen.
		if (FVector::Dist2D(FrameLoc, FVector(-2704.f, -1595.f, 0.f)) > 250.f) { continue; }
		UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: test-frame gevonden op (%.0f, %.0f, %.0f)"), FrameLoc.X, FrameLoc.Y, FrameLoc.Z);
		const FIntPoint Key(FMath::RoundToInt(FrameLoc.X / 100.f), FMath::RoundToInt(FrameLoc.Y / 100.f));
		if (ClonedRooms.Contains(Key)) { continue; }

		// DETERMINISTISCHE orientatie: meet vlak bij de deur welke kant de GANG is (daar ligt vloer -
		// daar loop je immers) en welke kant de lege kamer (void). Probes ver de kamer in waren
		// afhankelijk van streaming-volgorde -> per sessie een andere (soms 180 graden gedraaide) kamer.
		const FVector Through = Target.GetUnitAxis(EAxis::X).GetSafeNormal2D();
		const bool bFloorA = HasFloorBelow(FrameLoc + Through * 130.f);
		const bool bFloorB = HasFloorBelow(FrameLoc - Through * 130.f);
		if (bFloorA == bFloorB)
		{
			RoomCloneLog += FString::Printf(TEXT("ONDUIDELIJK frame=(%.0f, %.0f, %.0f) vloerA=%d vloerB=%d"), FrameLoc.X, FrameLoc.Y, FrameLoc.Z, bFloorA ? 1 : 0, bFloorB ? 1 : 0) + LINE_TERMINATOR;
			UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: frame (%.0f, %.0f) onduidelijk (vloer beide kanten: %d) - overslaan"), FrameLoc.X, FrameLoc.Y, bFloorA ? 1 : 0);
			continue; // geen eenduidige gang/void-kant -> hier hoort geen kloon
		}
		const FVector RoomDir = bFloorA ? -Through : Through;

		// Kies de orientatie (0 of 180 graden) waarvan het kamer-centrum aan de gemeten void-kant ligt.
		FTransform Best = FTransform::Identity;
		bool bHaveCand = false;
		for (int32 Flip = 0; Flip < 2; ++Flip)
		{
			FTransform Cand = Target;
			if (Flip) { Cand.SetRotation(Cand.GetRotation() * FQuat(FVector::UpVector, PI)); }
			const FVector CandRoomDir = (Cand.TransformPosition(RefRel[0]) - FrameLoc).GetSafeNormal2D();
			if (FVector::DotProduct(CandRoomDir, RoomDir) < 0.5f) { continue; } // kamer zou de gang-kant op staan
			Best = Cand; bHaveCand = true;
			break;
		}
		if (!bHaveCand) { continue; }

		// VOLLEDIGE footprint-check: bemonster het hele kamer-oppervlak in een raster van ~90cm.
		// Hoeken-checks misten gangen die DWARS door het oppervlak lopen - daardoor viel de kloon-muur
		// over de gang en blokkeerde 'ie andere deuren. Nu: elk bezet rastercel = afkeuren.
		int32 GridTotal = 0, GridOccupied = 0;
		for (float Gx = SrcBox.Min.X + 70.f; Gx < SrcBox.Max.X - 30.f; Gx += 90.f)
		{
			for (float Gy = SrcBox.Min.Y + 70.f; Gy < SrcBox.Max.Y - 30.f; Gy += 90.f)
			{
				const FVector Rel = SrcInv.TransformPosition(FVector(Gx, Gy, C.Z));
				const FVector Wp = Best.TransformPosition(Rel);
				++GridTotal;
				if (HasFloorBelow(Wp)) { ++GridOccupied; }
			}
		}
		if (GridOccupied > 0)
		{
			ClonedRooms.Add(Key); // dit slot past gewoon niet - niet blijven proberen
			RoomCloneLog += FString::Printf(TEXT("PAST-NIET frame=(%.0f, %.0f, %.0f) bezet=%d/%d roomdir=(%.2f, %.2f)"), FrameLoc.X, FrameLoc.Y, FrameLoc.Z, GridOccupied, GridTotal, RoomDir.X, RoomDir.Y) + LINE_TERMINATOR;
			UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: frame (%.0f, %.0f) past niet - %d/%d rastercellen bezet"), FrameLoc.X, FrameLoc.Y, GridOccupied, GridTotal);
			continue;
		}
		const int32 BestVoids = 4; // footprint volledig leeg

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
			RoomCloneLog += FString::Printf(TEXT("OVERLAP frame=(%.0f, %.0f, %.0f) box=(%.0f, %.0f)-(%.0f, %.0f)"), FrameLoc.X, FrameLoc.Y, FrameLoc.Z, NewBox.Min.X, NewBox.Min.Y, NewBox.Max.X, NewBox.Max.Y) + LINE_TERMINATOR;
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
		RoomCloneLog += FString::Printf(TEXT("GEKLOOND frame=(%.0f, %.0f, %.0f) frameyaw=%.0f kloonyaw=%.0f roomdir=(%.2f, %.2f) box=(%.0f, %.0f)-(%.0f, %.0f) meshes=%d"),
			FrameLoc.X, FrameLoc.Y, FrameLoc.Z, Target.GetRotation().Rotator().Yaw, Best.GetRotation().Rotator().Yaw,
			RoomDir.X, RoomDir.Y, NewBox.Min.X, NewBox.Min.Y, NewBox.Max.X, NewBox.Max.Y, PlacedMeshes) + LINE_TERMINATOR;
		UE_LOG(LogWeedShop, Warning, TEXT("RoomCloner: kamer gekloond bij frame (%.0f, %.0f, %.0f) - %d meshes, %d/4 hoeken void"), FrameLoc.X, FrameLoc.Y, FrameLoc.Z, PlacedMeshes, BestVoids);
	}

	if (RoomCloneLog.Len() > 0)
	{
		FFileHelper::SaveStringToFile(RoomCloneLog, *(FPaths::ProjectSavedDir() / TEXT("RoomClone.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
}
