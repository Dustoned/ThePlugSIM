#include "World/DoorRetrofitter.h"

#include "WeedShopCore.h"
#include "World/CityDoor.h"
#include "World/DayNightController.h"
#include "World/PackElevator.h"
#include "World/PackElevatorButton.h"
#include "World/RoomStamper.h"
#include "World/MapBorder.h"
#include "Customer/CustomerSpawner.h"
#include "Customer/CustomerBase.h"
#include "Economy/EconomyComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "NavigationSystem.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
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
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Misc/PackageName.h"
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

	// Verse bake-export per sessie: de append-file stapelde anders alle (ook oude, foute) runs op
	// en de bake bakte die hele historie mee.
	IFileManager::Get().Delete(*(FPaths::ProjectSavedDir() / TEXT("RoomBake.txt")), false, true, true);

	// GEBAKKEN KAMERS: onze eigen BakedRooms-level (gevuld via Tools/bake_rooms.py) als overlay
	// over de pack-map laden - permanent map-onderdeel dat ook in packaged builds meegaat. De
	// runtime-jobs deduppen er automatisch tegen (zelfde mesh+positie = overslaan).
	if (FPackageName::DoesPackageExist(TEXT("/Game/_Project/Maps/BakedRooms")))
	{
		bool bBakedOk = false;
		BakedOverlay = ULevelStreamingDynamic::LoadLevelInstance(GetWorld(), TEXT("/Game/_Project/Maps/BakedRooms"),
			FVector::ZeroVector, FRotator::ZeroRotator, bBakedOk);
		UE_LOG(LogWeedShop, Log, TEXT("BakedRooms-overlay laden gestart: %d"), bBakedOk ? 1 : 0);
	}

	// Map-grens herstellen (als de speler er een gezet heeft; Rebuild is no-op zonder bestand).
	if (UWorld* WB = GetWorld()) { WB->SpawnActor<AMapBorder>(AMapBorder::StaticClass(), FTransform::Identity); }

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
				FString TagStr;
				for (const FName& Tg : It->Tags) { TagStr += Tg.ToString() + TEXT(","); }
				Out += FString::Printf(TEXT("%s | pos=(%.0f, %.0f, %.0f) | rot=(%.0f, %.0f, %.0f) | vis=%d hid=%d | %s | tags=%s"),
					*Comp->GetStaticMesh()->GetName(), L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll,
					Comp->IsVisible() ? 1 : 0, It->IsHidden() ? 1 : 0,
					*It->GetClass()->GetName(), *TagStr);
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

	// KAART-GEBIED: staat er een spelers-grens (MapBorder.txt), dan is DAT de kaart - gecentreerd
	// op de ring en precies zo breed. Zonder grens: wereld-omvang uit de static-mesh-actors.
	FBox B(ForceInit);
	{
		TArray<FString> BorderLines;
		FFileHelper::LoadFileToStringArray(BorderLines, *(WeedData::File(TEXT("MapBorder.txt"))));
		for (const FString& BLine : BorderLines)
		{
			TArray<FString> Pc;
			BLine.ParseIntoArray(Pc, TEXT(","));
			if (Pc.Num() >= 3) { B += FVector(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2])); }
		}
	}
	const bool bFromBorder = (B.IsValid != 0);
	if (!bFromBorder)
	{
		for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
		{
			if (IsValid(*It)) { B += It->GetActorLocation(); }
		}
	}
	float TopZ = 10000.f;
	if (B.IsValid)
	{
		const FVector C = B.GetCenter();
		MapCenter = FVector2D(C.X, C.Y);
		const FVector E = B.GetExtent();
		MapOrtho = bFromBorder
			? FMath::Max(E.X, E.Y) * 2.1f                              // grens-ring = de kaart
			: FMath::Clamp(FMath::Max(E.X, E.Y) * 2.3f, 20000.f, 300000.f);
		TopZ = B.Max.Z + 20000.f;
		UE_LOG(LogWeedShop, Warning, TEXT("MapCapture: centrum=(%.0f, %.0f) ortho=%.0f topZ=%.0f bron=%s"),
			MapCenter.X, MapCenter.Y, MapOrtho, TopZ, bFromBorder ? TEXT("border") : TEXT("actors"));
	}

	MapRT = NewObject<UTextureRenderTarget2D>(this, TEXT("PackMapRT"));
	MapRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	MapRT->ClearColor = FLinearColor(0.04f, 0.05f, 0.07f, 1.f);
	MapRT->InitAutoFormat(4096, 4096); // hoge resolutie: de kaart is nu zoombaar tot straat-niveau
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
	MapCapture->ShowFlags.SetDynamicShadows(false); // geen slagschaduwen op de luchtfoto (boardwalk-schaduw over het strand)
	MapCapture->ShowFlags.SetDistanceFieldAO(false);  // weg-meshes onder het strand lekken een donkere AO-afdruk op het zand
	MapCapture->ShowFlags.SetLumenGlobalIllumination(false);
	MapCapture->ShowFlags.SetGlobalIllumination(false);
	MapCapture->ShowFlags.SetAmbientOcclusion(false);
	MapCapture->ShowFlags.SetScreenSpaceAO(false);
	MapCapture->ShowFlags.SetContactShadows(false);
	// (Belichting/post-processing AAN laten: FinalColor-captures renderen zwart zonder lighting.
	// Tijd-onafhankelijk: CaptureMapNow zet de belichting 1 frame in de foto-stand van de
	// dag/nacht-controller - de kaart ziet er dus altijd uit als een rustige ochtend.)
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
	// SPLINE-RESTJES verbergen: de makers lieten een pad-spline onder het strand liggen waarvan
	// de donkergrijze SplineEditorMesh-previews door de luchtfoto heen tekenden als een rare
	// "weg-schaduw" op het zand. Voor elke capture opnieuw (vangt ook laat-gestreamde restjes).
	{
		int32 NHidden = 0;
		for (TObjectIterator<UStaticMeshComponent> RIt; RIt; ++RIt)
		{
			UStaticMeshComponent* C = *RIt;
			if (!C || C->GetWorld() != GetWorld() || !C->GetStaticMesh()) { continue; }
			if (C->GetStaticMesh()->GetName() != TEXT("SplineEditorMesh")) { continue; }
			if (C->IsVisible()) { C->SetVisibility(false); ++NHidden; }
		}
		if (NHidden > 0)
		{
			UE_LOG(LogWeedShop, Warning, TEXT("SplineEditorMesh-restjes verborgen: %d"), NHidden);
		}
	}
	// Altijd dezelfde rustige ochtend-belichting op de foto, ongeacht de kloktijd (middagzon
	// blies de kaart naar wit; nacht maakte hem zwart). Tick van de controller herstelt direct.
	if (ADayNightController* DN = ADayNightController::GetLocal(GetWorld()))
	{
		DN->ApplyMapPhotoLight();
	}
	MapCapture->CaptureScene();
}

bool ADoorRetrofitter::FindStreetPoint(float WorldY, FVector& Out) const
{
	UWorld* W = GetWorld();
	if (!W) { return false; }
	// De BOULEVARD van deze map is een verhoogd dek (Z ~480-620, waar de speler loopt). Het
	// onderniveau (Z ~0-50) is de strand-/service-laag - daar horen geen wandelaars. Zoek per
	// kandidaat-X (waaier rond de strip) de BOVENSTE vlakke hit op dek-hoogte, liefst een
	// straat/stoep-mesh.
	static const float XOffsets[] = { -2000.f, -1400.f, -2600.f, -800.f, -3200.f, -200.f, 400.f, 1000.f, 1600.f };
	FVector Fallback = FVector::ZeroVector;
	bool bHaveFallback = false;
	for (float Dx : XOffsets)
	{
		FHitResult H;
		const FVector S(Dx, WorldY, 3000.f);
		if (!W->LineTraceSingleByChannel(H, S, S - FVector(0.f, 0.f, 3500.f), ECC_Visibility)) { continue; }
		if (H.ImpactNormal.Z < 0.7f) { continue; }                                  // muren overslaan
		if (H.ImpactPoint.Z < 300.f || H.ImpactPoint.Z > 800.f) { continue; }       // alleen DEK-hoogte
		const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(H.GetComponent());
		const FString Nm = (SMC && SMC->GetStaticMesh()) ? SMC->GetStaticMesh()->GetName() : FString();
		if (Nm.Contains(TEXT("Street")) || Nm.Contains(TEXT("Sidewalk")) || Nm.Contains(TEXT("Road")) || Nm.Contains(TEXT("ConcretePath")) || Nm.Contains(TEXT("Floor")))
		{
			Out = H.ImpactPoint + FVector(0.f, 0.f, 60.f);
			return true;
		}
		if (!bHaveFallback) { Fallback = H.ImpactPoint + FVector(0.f, 0.f, 60.f); bHaveFallback = true; }
	}
	if (bHaveFallback) { Out = Fallback; return true; } // dek gevonden maar geen straat-mesh: beter dan niets
	return false;
}

void ADoorRetrofitter::ScanAndConvert()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	++ScanPass;
	if (ScanPass == 1)
	{
		// SPELER-ROUTES eerst: F9-marker-ringen uit NpcRoute.txt ("---" scheidt routes). Elke
		// route is een GESLOTEN ring (eerste en laatste marker verbonden); langs alle segmenten
		// worden tussenpunten bijgevuld zodat de nav-dekking en spawn-spreiding de hele ring
		// rond lopen. Geen routes -> de standaard boulevard-punten met dek-zoeker.
		{
			TArray<FString> RouteLines;
			FFileHelper::LoadFileToStringArray(RouteLines, *WeedData::File(TEXT("NpcRoute.txt")));
			TArray<TArray<FVector>> Routes;
			TArray<FVector> CurRoute;
			for (const FString& RL : RouteLines)
			{
				if (RL.TrimStartAndEnd().StartsWith(TEXT("---")))
				{
					if (CurRoute.Num() >= 2) { Routes.Add(CurRoute); }
					CurRoute.Reset();
					continue;
				}
				TArray<FString> Pc;
				RL.ParseIntoArray(Pc, TEXT(","));
				if (Pc.Num() >= 3) { CurRoute.Add(FVector(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]))); }
			}
			if (CurRoute.Num() >= 2) { Routes.Add(CurRoute); }
			for (const TArray<FVector>& Route : Routes)
			{
				const int32 NSeg = (Route.Num() >= 3) ? Route.Num() : Route.Num() - 1; // ring: laatste->eerste erbij
				for (int32 si = 0; si < NSeg; ++si)
				{
					const FVector A = Route[si];
					const FVector B = Route[(si + 1) % Route.Num()];
					PendingSpawnerPoints.Add(A);
					// Tussenpunten om de ~40m zodat dekking en spawns het hele segment vullen.
					const float SegLen = FVector::Dist2D(A, B);
					const int32 NMid = FMath::FloorToInt(SegLen / 4000.f);
					for (int32 mi = 1; mi <= NMid; ++mi)
					{
						PendingSpawnerPoints.Add(FMath::Lerp(A, B, float(mi) / float(NMid + 1)));
					}
				}
			}
			// Deduplicatie: punten die te dicht op elkaar liggen (markers + interpolatie) samenvoegen.
			for (int32 i = PendingSpawnerPoints.Num() - 1; i >= 0; --i)
			{
				for (int32 j = 0; j < i; ++j)
				{
					if (FVector::Dist2D(PendingSpawnerPoints[i], PendingSpawnerPoints[j]) < 1500.f)
					{
						PendingSpawnerPoints.RemoveAt(i);
						break;
					}
				}
			}
			if (PendingSpawnerPoints.Num() > 0)
			{
				// Klanten-budget over de punten verdelen (niet 4x zoveel NPC's omdat er meer punten zijn).
				RouteCustomersPerPoint = FMath::Clamp(40 / PendingSpawnerPoints.Num(), 2, 5);
				UE_LOG(LogWeedShop, Warning, TEXT("NPC-routes: %d ringen -> %d spawn-punten (%d klanten per punt)"), Routes.Num(), PendingSpawnerPoints.Num(), RouteCustomersPerPoint);
			}
			else
			{
				PendingSpawnerYs = { -20000.f, -8000.f, 0.f, 12000.f, 24000.f };
			}
		}
	}
	// Route-punten van de speler: direct plaatsen (positie is al ground-truth op de stoep).
	for (int32 i = PendingSpawnerPoints.Num() - 1; i >= 0; --i)
	{
		const FVector Pt = PendingSpawnerPoints[i] + FVector(0.f, 0.f, 60.f);
		ACustomerSpawner* CSr = W->SpawnActorDeferred<ACustomerSpawner>(ACustomerSpawner::StaticClass(), FTransform(Pt));
		if (!CSr) { continue; }
		CSr->bSpawnResidents = false;
		CSr->MaxCustomers = RouteCustomersPerPoint;
		CSr->SpotRadius = 500.f;
		CSr->ActivationRange = 10000.f;
		CSr->FinishSpawning(FTransform(Pt));
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
		{
			NavSys->RegisterNavigationInvoker(CSr, 9000.f, 11000.f);
		}
		UE_LOG(LogWeedShop, Warning, TEXT("Pack-map: route-spawner op (%.0f, %.0f, %.0f)"), Pt.X, Pt.Y, Pt.Z);
		if (!bWalkersSpawned)
		{
			bWalkersSpawned = true;
			SetActorLocation(Pt);
			if (UNavigationSystemV1* NavSys2 = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
			{
				NavSys2->RegisterNavigationInvoker(this, 9000.f, 11000.f);
			}
		}
		PendingSpawnerPoints.RemoveAt(i);
	}

	// WALK-THROUGHS: via de Test-tab gemarkeerde objecten (NoCollide.txt) doorloopbaar maken.
	// Periodiek herhaald zodat ook later-gestreamde meshes hun pawn-collision verliezen.
	if (!bNoCollideLoaded)
	{
		bNoCollideLoaded = true;
		FFileHelper::LoadFileToStringArray(NoCollideLines, *(WeedData::File(TEXT("NoCollide.txt"))));
	}
	if (ScanPass % 5 == 1) { ApplyInstantGlass(); }

	if (NoCollideLines.Num() > 0 && (ScanPass % 5 == 1))
	{
		for (const FString& NL : NoCollideLines)
		{
			TArray<FString> NP;
			NL.ParseIntoArray(NP, TEXT("|"));
			if (NP.Num() < 2) { continue; }
			TArray<FString> NC;
			NP[1].ParseIntoArray(NC, TEXT(","));
			if (NC.Num() < 3) { continue; }
			const FVector Want(FCString::Atof(*NC[0]), FCString::Atof(*NC[1]), FCString::Atof(*NC[2]));
			for (TObjectIterator<UStaticMeshComponent> CIt; CIt; ++CIt)
			{
				UStaticMeshComponent* C = *CIt;
				if (!C || C->GetWorld() != W || !C->GetStaticMesh()) { continue; }
				if (C->GetStaticMesh()->GetName() != NP[0]) { continue; }
				bool bMatch = FVector::DistSquared(C->GetComponentLocation(), Want) < 120.f * 120.f;
				if (!bMatch)
				{
					if (UInstancedStaticMeshComponent* IC = Cast<UInstancedStaticMeshComponent>(C))
					{
						const int32 NI = IC->GetInstanceCount();
						for (int32 ii = 0; ii < NI && !bMatch; ++ii)
						{
							FTransform IT;
							if (IC->GetInstanceTransform(ii, IT, true) && FVector::DistSquared(IT.GetLocation(), Want) < 120.f * 120.f) { bMatch = true; }
						}
					}
				}
				if (bMatch && C->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Ignore)
				{
					C->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				}
			}
		}
	}

	// KLANTEN + NAVMESH op de pack-map: VASTE spawn-punten verspreid langs de boulevard (niet de
	// speler-positie - die staat tegenwoordig bij sessiestart al in het starter-appartement).
	// Elk punt zoekt zelf de stoep via down-traces op straat-meshes en krijgt een eigen
	// nav-invoker, zodat de navmesh overal rond de spawners groeit en NPC's daar rondlopen.
	if (PendingSpawnerYs.Num() > 0)
	{
		for (int32 i = PendingSpawnerYs.Num() - 1; i >= 0; --i)
		{
			FVector Street;
			if (!FindStreetPoint(PendingSpawnerYs[i], Street)) { continue; } // nog niet gestreamd - volgende pass
			ACustomerSpawner* CSw = W->SpawnActorDeferred<ACustomerSpawner>(ACustomerSpawner::StaticClass(), FTransform(Street));
			if (!CSw) { continue; }
			CSw->bSpawnResidents = false; // bewoners-lite draait via de woningen-pass
			CSw->MaxCustomers = 5;
			CSw->SpotRadius = 500.f; // dicht bij de stoep blijven
			CSw->ActivationRange = 10000.f; // pas spawnen als een speler in de buurt is (streaming)
			CSw->FinishSpawning(FTransform(Street));
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
			{
				NavSys->RegisterNavigationInvoker(CSw, 9000.f, 11000.f);
			}
			UE_LOG(LogWeedShop, Warning, TEXT("Pack-map: klanten-spawner + nav-invoker op (%.0f, %.0f, %.0f)"), Street.X, Street.Y, Street.Z);
			if (!bWalkersSpawned)
			{
				// Retrofitter zelf als extra invoker-anker op het eerste gevonden straat-punt.
				bWalkersSpawned = true;
				SetActorLocation(Street);
				if (UNavigationSystemV1* NavSys2 = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
				{
					NavSys2->RegisterNavigationInvoker(this, 9000.f, 11000.f);
				}
			}
			PendingSpawnerYs.RemoveAt(i);
		}
	}

	// WONINGEN: elke apartment-voordeur (Door_Apartment02) hoort bij een bewoner en zit OP SLOT
	// ("LOCKED - <naam> lives here"), behalve de STARTER: de hoogste woning van de stad (bovenste
	// verdieping van de Ventura-toren) is van jou - die deur opent gewoon, prompt "Your home".
	// Draait opnieuw zodra het aantal deuren verandert (streaming/stamps laden na elkaar in).
	{
		TArray<ACityDoor*> Apt;
		TArray<ACityDoor*> Balc;
		for (TActorIterator<ACityDoor> It(W); It; ++It)
		{
			if (!IsValid(*It)) { continue; }
			if (It->ActorHasTag(TEXT("AptDoor"))) { Apt.Add(*It); }
			else if (It->ActorHasTag(TEXT("BalcDoor"))) { Balc.Add(*It); }
		}
		if (Apt.Num() > 0 && Apt.Num() + Balc.Num() != LastAptDoorCount)
		{
			LastAptDoorCount = Apt.Num() + Balc.Num();
			Apt.Sort([](const ACityDoor& A, const ACityDoor& B) { return A.GetActorLocation().Z > B.GetActorLocation().Z; });
			ACityDoor* Starter = Apt[0];
			StarterDoor = Starter;
			// NUMMERING zoals een echt complex: deuren clusteren per GEBOUW (XY-afstand), binnen een
			// gebouw per verdieping (Z-banden) een volgnummer -> Apt 101, 102, 201, ... Bordje op de deur.
			TArray<TArray<ACityDoor*>> Buildings;
			for (ACityDoor* D : Apt)
			{
				TArray<ACityDoor*>* Found = nullptr;
				for (TArray<ACityDoor*>& Bld : Buildings)
				{
					if (FVector::Dist2D(Bld[0]->GetActorLocation(), D->GetActorLocation()) < 3000.f) { Found = &Bld; break; }
				}
				if (Found) { Found->Add(D); }
				else { TArray<ACityDoor*> NewB; NewB.Add(D); Buildings.Add(NewB); }
			}
			for (TArray<ACityDoor*>& Bld : Buildings)
			{
				TArray<float> Floors;
				for (ACityDoor* D : Bld)
				{
					const float Z = D->GetActorLocation().Z;
					if (!Floors.ContainsByPredicate([Z](float F) { return FMath::Abs(F - Z) < 150.f; })) { Floors.Add(Z); }
				}
				Floors.Sort();
				auto FloorOf = [&Floors](const ACityDoor& D)
				{
					const float Z = D.GetActorLocation().Z;
					for (int32 f = 0; f < Floors.Num(); ++f) { if (FMath::Abs(Floors[f] - Z) < 150.f) { return f; } }
					return 0;
				};
				Bld.Sort([&FloorOf](const ACityDoor& A, const ACityDoor& B)
				{
					const int32 FA = FloorOf(A), FB = FloorOf(B);
					if (FA != FB) { return FA < FB; }
					const FVector LA = A.GetActorLocation(), LB = B.GetActorLocation();
					if (!FMath::IsNearlyEqual(LA.Y, LB.Y, 50.f)) { return LA.Y < LB.Y; }
					return LA.X < LB.X;
				});
				int32 CurFloor = -1, Unit = 0;
				for (ACityDoor* D : Bld)
				{
					const int32 FloorIdx = FloorOf(*D);
					if (FloorIdx != CurFloor) { CurFloor = FloorIdx; Unit = 0; }
					++Unit;
					D->SetAptNumber((FloorIdx + 1) * 100 + Unit);
					if (D == Starter) { D->SetPlayerHome(); continue; }
					// Bewoner-naam stabiel op POSITIE (niet op lijst-volgorde): zelfde naam per deur, elke sessie.
					const FVector L = D->GetActorLocation();
					const int32 NameIdx = FMath::Abs(FMath::RoundToInt(L.X * 0.13f) + FMath::RoundToInt(L.Y * 0.31f) + FMath::RoundToInt(L.Z * 0.77f));
					D->SetResident(ACityDoor::ResidentNameForIndex(NameIdx));
				}
			}
			// SCHUIFPUIEN: elke balkon-pui hoort bij de dichtstbijzijnde voordeur op (ongeveer)
			// dezelfde verdieping - zelfde bewoner, zelfde slot. De pui van het starter-huis blijft
			// gewoon van jou (open/dicht zoals normaal).
			int32 NBalcLocked = 0;
			for (ACityDoor* Bd : Balc)
			{
				const FVector BL = Bd->GetActorLocation();
				ACityDoor* Near = nullptr;
				float BestD = 1600.f;
				for (ACityDoor* A : Apt)
				{
					const FVector AL = A->GetActorLocation();
					if (FMath::Abs(AL.Z - BL.Z) > 260.f) { continue; }
					const float Dd = FVector::Dist2D(AL, BL);
					if (Dd < BestD) { BestD = Dd; Near = A; }
				}
				if (!Near) { continue; } // losse pui (winkel/lobby): met rust laten
				if (Near == Starter) { Bd->SetPlayerHome(); continue; }
				const FVector L = Near->GetActorLocation();
				const int32 NameIdx = FMath::Abs(FMath::RoundToInt(L.X * 0.13f) + FMath::RoundToInt(L.Y * 0.31f) + FMath::RoundToInt(L.Z * 0.77f));
				Bd->SetResident(ACityDoor::ResidentNameForIndex(NameIdx));
				++NBalcLocked;
			}
			// BEWONERS-LITE: een deel van de BEGANE-GROND woningen (Apt 1xx) krijgt een ECHT
			// rondlopende bewoner - zelfde naam als op het deurbordje (NpcId Resident_<idx> mapt
			// via FriendlyNpcName op dezelfde naam). Boven-verdiepingen blijven virtueel (NPC's
			// kunnen geen lift bedienen) en boven de cap blijven woningen leeg: daar bouwt de
			// speler nog kamers bij - die krijgen vanzelf een bewoner zodra ze er zijn. De
			// straat-spawners blijven onafhankelijk hiervan gewone klanten leveren.
			{
				TArray<ACityDoor*> Ground;
				for (ACityDoor* A : Apt)
				{
					if (A == Starter) { continue; }
					if (A->GetAptNumber() < 100 || A->GetAptNumber() >= 200) { continue; }
					Ground.Add(A);
				}
				Ground.Sort([](const ACityDoor& A, const ACityDoor& B)
				{
					const FVector LA = A.GetActorLocation(), LB = B.GetActorLocation();
					if (!FMath::IsNearlyEqual(LA.X, LB.X, 50.f)) { return LA.X < LB.X; }
					return LA.Y < LB.Y;
				});
				int32 NLive = 0;
				for (TActorIterator<ACustomerBase> CIt(W); CIt; ++CIt)
				{
					if (IsValid(*CIt) && CIt->IsResident()) { ++NLive; }
				}
				const int32 LiteCap = 10;
				int32 NNew = 0;
				for (ACityDoor* A : Ground)
				{
					if (NLive >= LiteCap) { break; }
					if (A->ActorHasTag(TEXT("ResidentNpc"))) { continue; }
					// Kamer-kant vs straat-kant van de voordeur (zelfde meting als de starter-teleport).
					const FVector DL = A->GetActorLocation();
					const FVector Fw = A->GetActorForwardVector();
					const FVector Rt = A->GetActorRightVector();
					auto Openness = [&](const FVector& P)
					{
						float Sum = 0.f;
						const FVector Dirs[4] = { Fw, -Fw, Rt, -Rt };
						for (const FVector& Dir : Dirs)
						{
							FHitResult H;
							const FVector S = P + FVector(0.f, 0.f, 120.f);
							Sum += W->LineTraceSingleByChannel(H, S, S + Dir * 1200.f, ECC_Visibility) ? H.Distance : 1200.f;
						}
						return Sum;
					};
					const FVector CandA = DL + Fw * 240.f;
					const FVector CandB = DL - Fw * 240.f;
					const bool bAInside = Openness(CandA) <= Openness(CandB);
					const FVector Inside = bAInside ? CandA : CandB;
					const FVector Front = bAInside ? CandB : CandA;
					FActorSpawnParameters RP;
					RP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					ACustomerBase* Cb = W->SpawnActor<ACustomerBase>(ACustomerBase::StaticClass(), FTransform(Inside + FVector(0.f, 0.f, 100.f)), RP);
					if (!Cb) { continue; }
					const int32 NameIdx = FMath::Abs(FMath::RoundToInt(DL.X * 0.13f) + FMath::RoundToInt(DL.Y * 0.31f) + FMath::RoundToInt(DL.Z * 0.77f));
					Cb->NpcId = FName(*FString::Printf(TEXT("Resident_%d"), NameIdx));
					Cb->SetupResident(Front, Inside, FString::FromInt(A->GetAptNumber()), Front);
					A->Tags.Add(TEXT("ResidentNpc"));
					++NLive;
					++NNew;
				}
				if (NNew > 0)
				{
					UE_LOG(LogWeedShop, Warning, TEXT("Bewoners-lite: %d nieuwe bewoners (totaal %d/%d) op begane-grond woningen"), NNew, NLive, LiteCap);
				}
			}
			const FVector Top = Starter->GetActorLocation();
			UE_LOG(LogWeedShop, Warning, TEXT("Woningen: %d voordeuren + %d schuifpuien op slot (bewoners) in %d gebouwen, starter-huis = Apt %d op (%.0f, %.0f, %.0f)"), Apt.Num() - 1, NBalcLocked, Buildings.Num(), Starter->GetAptNumber(), Top.X, Top.Y, Top.Z);
		}
	}

	// STARTER-HUIS: je begint IN je eigen appartement, en er loopt HUUR: EUR 500 per 31 dagen.
	// Genoeg cash op de vervaldag = automatisch geind; te weinig = deur op slot tot je aan de
	// deur betaalt (F). Dagen-resterend overleeft sessies via Saved/RentState.txt.
	if (StarterDoor.IsValid())
	{
		APlayerController* PCr = W->GetFirstPlayerController();
		APawn* Pr = PCr ? PCr->GetPawn() : nullptr;
		// 1x per sessie naar binnen: de KAMER-kant van de voordeur vinden door aan beide kanten
		// de vrije ruimte te meten - een kamer is krap, de galerij/gang is lang.
		if (!bMovedIntoHome && Pr && bWalkersSpawned)
		{
			bMovedIntoHome = true;
			const FVector DL = StarterDoor->GetActorLocation();
			const FVector Fw = StarterDoor->GetActorForwardVector();
			const FVector Rt = StarterDoor->GetActorRightVector();
			auto Openness = [&](const FVector& P)
			{
				float Sum = 0.f;
				const FVector Dirs[4] = { Fw, -Fw, Rt, -Rt };
				for (const FVector& Dir : Dirs)
				{
					FHitResult H;
					const FVector S = P + FVector(0.f, 0.f, 120.f);
					Sum += W->LineTraceSingleByChannel(H, S, S + Dir * 1200.f, ECC_Visibility) ? H.Distance : 1200.f;
				}
				return Sum;
			};
			const FVector CandA = DL + Fw * 240.f;
			const FVector CandB = DL - Fw * 240.f;
			const FVector Inside = (Openness(CandA) <= Openness(CandB)) ? CandA : CandB;
			Pr->SetActorLocation(Inside + FVector(0.f, 0.f, 110.f), false, nullptr, ETeleportType::TeleportPhysics);
			if (UPhoneClientComponent* Phw = Pr->FindComponentByClass<UPhoneClientComponent>())
			{
				Phw->Toast(FString::Printf(TEXT("Welcome home - Apt %d. Rent: EUR 500 due every 31 days."), StarterDoor->GetAptNumber()), FColor::Cyan, 6.f);
			}
		}
		// Huur-administratie op de dag-teller.
		AWeedShopGameState* GSr = W->GetGameState<AWeedShopGameState>();
		UDayCycleComponent* DCr = GSr ? GSr->GetDayCycle() : nullptr;
		if (DCr)
		{
			const int32 Day = DCr->GetDayNumber();
			if (RentDueDay < 0)
			{
				int32 DaysLeft = 31;
				FString RS;
				if (FFileHelper::LoadFileToString(RS, *(FPaths::ProjectSavedDir() / TEXT("RentState.txt"))))
				{
					DaysLeft = FMath::Clamp(FCString::Atoi(*RS), 0, 31);
				}
				RentDueDay = Day + DaysLeft;
			}
			const int32 Left = RentDueDay - Day;
			if (Left != LastRentSeenLeft)
			{
				LastRentSeenLeft = Left;
				FFileHelper::SaveStringToFile(FString::FromInt(FMath::Max(0, Left)), *(FPaths::ProjectSavedDir() / TEXT("RentState.txt")));
				if (Left > 0 && Left <= 7 && Pr)
				{
					if (UPhoneClientComponent* Ph7 = Pr->FindComponentByClass<UPhoneClientComponent>())
					{
						Ph7->Toast(FString::Printf(TEXT("Rent: EUR 500 due in %d day%s"), Left, Left == 1 ? TEXT("") : TEXT("s")), FColor::Yellow, 4.f);
					}
				}
			}
			if (Left <= 0 && !StarterDoor->IsRentOverdue())
			{
				UEconomyComponent* Ec = Pr ? Pr->FindComponentByClass<UEconomyComponent>() : nullptr;
				UPhoneClientComponent* Phr = Pr ? Pr->FindComponentByClass<UPhoneClientComponent>() : nullptr;
				if (Ec && Ec->RemoveMoney(50000))
				{
					RentDueDay = Day + 31;
					if (Phr) { Phr->Toast(TEXT("Rent paid: EUR 500. Next rent in 31 days."), FColor::Green, 5.f); }
				}
				else
				{
					StarterDoor->SetRentOverdue(50000);
					if (Phr) { Phr->Toast(TEXT("RENT OVERDUE - EUR 500. Your door is locked: pay at the door (F)."), FColor::Red, 7.f); }
				}
			}
			// Aan de deur betaald? Dan loopt de volgende maand weer.
			if (StarterDoor->ConsumeRentJustPaid())
			{
				RentDueDay = Day + 31;
				FFileHelper::SaveStringToFile(FString::FromInt(31), *(FPaths::ProjectSavedDir() / TEXT("RentState.txt")));
				if (Pr)
				{
					if (UPhoneClientComponent* Php = Pr->FindComponentByClass<UPhoneClientComponent>())
					{
						Php->Toast(TEXT("Rent paid: EUR 500. Next rent in 31 days."), FColor::Green, 5.f);
					}
				}
			}
		}
	}

	// HANDMATIG VERGRENDELDE DEUREN (Lock door in crosshair): op slot zoals een bewoner-deur maar
	// zonder naam. Elke pass en NA de woningen-pass, zodat een handmatig slot altijd wint.
	if (!bLockedDoorsLoaded)
	{
		bLockedDoorsLoaded = true;
		FFileHelper::LoadFileToStringArray(LockedDoorLines, *(WeedData::File(TEXT("LockedDoors.txt"))));
	}
	if (LockedDoorLines.Num() > 0)
	{
		for (const FString& LL : LockedDoorLines)
		{
			TArray<FString> LP;
			LL.ParseIntoArray(LP, TEXT(","));
			if (LP.Num() < 3) { continue; }
			const FVector Want(FCString::Atof(*LP[0]), FCString::Atof(*LP[1]), FCString::Atof(*LP[2]));
			for (TActorIterator<ACityDoor> It(W); It; ++It)
			{
				if (IsValid(*It) && !It->IsLocked() && FVector::DistSquared(It->GetActorLocation(), Want) < 120.f * 120.f)
				{
					It->SetResident(FString());
				}
			}
		}
	}

	// PUI-BLADEN op het echte gevel-gat centreren (gemeten, niet gegokt).
	FixBalconyPuiPositions();

	// VANGNET: NPC's die door niet-gestreamde grond zijn gezakt (Z onder de wereld) terugzetten
	// op het dichtstbijzijnde spawner-punt. Residents gaan terug naar hun huis-positie via de
	// normale resident-logica zodra ze weer op de navmesh staan.
	{
		TArray<FVector> SpawnerLocs;
		for (TActorIterator<ACustomerSpawner> SIt(W); SIt; ++SIt)
		{
			if (IsValid(*SIt)) { SpawnerLocs.Add(SIt->GetActorLocation()); }
		}
		if (SpawnerLocs.Num() > 0)
		{
			int32 NRescued = 0;
			for (TActorIterator<ACustomerBase> CIt(W); CIt; ++CIt)
			{
				ACustomerBase* Cb = *CIt;
				if (!IsValid(Cb)) { continue; }
				const FVector L = Cb->GetActorLocation();
				FVector Best = SpawnerLocs[0];
				float BestD = TNumericLimits<float>::Max();
				for (const FVector& SL : SpawnerLocs)
				{
					const float Dd = FVector::DistSquared2D(SL, L);
					if (Dd < BestD) { BestD = Dd; Best = SL; }
				}
				// Onder dek-niveau (ruim onder de dichtstbijzijnde spawner) = onder de map beland.
				if (L.Z > Best.Z - 180.f) { continue; }
				Cb->SetActorLocation(Best + FVector(0.f, 0.f, 120.f), false, nullptr, ETeleportType::TeleportPhysics);
				++NRescued;
			}
			if (NRescued > 0)
			{
				UE_LOG(LogWeedShop, Warning, TEXT("NPC-vangnet: %d doorgevallen NPC's teruggezet op de boulevard"), NRescued);
			}
			// DIAGNOSE (om de ~30s): waar lopen ze, en hoe ver onder/boven het dichtstbijzijnde
			// spawn-punt? Zo zien we direct wie er nog onder de map zit (bewoner vs klant).
			if (ScanPass % 15 == 2)
			{
				for (TActorIterator<ACustomerBase> CIt2(W); CIt2; ++CIt2)
				{
					ACustomerBase* Cb2 = *CIt2;
					if (!IsValid(Cb2)) { continue; }
					const FVector L2 = Cb2->GetActorLocation();
					float NearZ = L2.Z;
					float BestD2 = TNumericLimits<float>::Max();
					for (const FVector& SL : SpawnerLocs)
					{
						const float Dd2 = FVector::DistSquared2D(SL, L2);
						if (Dd2 < BestD2) { BestD2 = Dd2; NearZ = SL.Z; }
					}
					UE_LOG(LogWeedShop, Warning, TEXT("NPC %s res=%d op (%.0f, %.0f, %.0f) dZ=%.0f"),
						*Cb2->NpcId.ToString(), Cb2->IsResident() ? 1 : 0, L2.X, L2.Y, L2.Z, L2.Z - NearZ);
				}
			}
		}
	}

	// Kaart een keer per sessie schieten - de foto-stand maakt de capture tijd-onafhankelijk,
	// dus ook wie 's nachts spawnt heeft meteen een dag-kaart. Wel even wachten tot de
	// dag/nacht-controller zijn zon heeft gespawnd (paar ticks na BeginPlay).
	if (!bDayMapCaptured)
	{
		ADayNightController* DNm = ADayNightController::GetLocal(W);
		if (DNm && DNm->HasPackSun())
		{
			CaptureMapNow();
			bDayMapCaptured = true;
		}
	}

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
		// Blad uit een GESPIEGELDE stempel dat dicht geparkeerd staat (geen zwaai-hint): in een
		// gespiegelde kamer hoort de standaard-zwaai precies andersom, anders slaat de deur door
		// het kozijn.
		if (SwingOverride == 0.f && SMA->ActorHasTag(TEXT("MirroredDoor")))
		{
			SwingOverride = -Leaf->OpenDeg;
		}
		SMA->SetActorHiddenInGame(true);
		SMA->SetActorEnableCollision(false);
		Converted.Add(SMA);

		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACityDoor* Door = W->SpawnActor<ACityDoor>(ACityDoor::StaticClass(), LeafTM, SP))
		{
			Door->SetActorScale3D(LeafTM.GetScale3D());
			Door->Tags.Append(SMA->Tags); // stamp-id mee: undo/verwijderen pakt dan ook de werkende deur
			if (FString(Leaf->MeshName).Contains(TEXT("Door_Apartment02")))
			{
				Door->Tags.Add(TEXT("AptDoor")); // woning-voordeur: slot-pass maakt hier bewoners van
			}
			else if (FString(Leaf->MeshName).Contains(TEXT("BalconyDoor")))
			{
				Door->Tags.Add(TEXT("BalcDoor")); // schuifpui: slot-pass koppelt 'm aan de bewoner van de woning
			}
			Door->SetupLeaf(Comp->GetStaticMesh(), (SwingOverride != 0.f) ? SwingOverride : Leaf->OpenDeg);
			// Balkon-puien zijn SCHUIFdeuren: blad glijdt opzij langs z'n eigen breedte (geen zwaai die
			// het terras of de kamer in slaat).
			if (FString(Leaf->MeshName).Contains(TEXT("BalconyDoor")))
			{
				const float LeafWidth = Comp->GetStaticMesh()->GetBoundingBox().GetSize().Y;
				Door->SetSlideMode(FMath::Max(100.f, LeafWidth - 8.f));
				// Dicht-positie wordt NIET hier gegokt: FixBalconyPuiPositions meet het echte gat
				// in de gevel met dwars-traces en centreert de bladen daarop.
			}
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
			// INTERIEUR-glas van DEURPARTIJEN mag NOOIT blokkeren (SM_Base_Door_xx_Glass_Interior):
			// dat is de vaste nep-interieur plaat ACHTER de werkende deuren. Sommige staan in de
			// map ZELF al op blokkeren - dus actief op doorlaatbaar zetten, niet alleen overslaan.
			if (MeshName.Contains(TEXT("door")) && MeshName.Contains(TEXT("interior")))
			{
				if (Comp->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
				{
					Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				}
				continue;
			}
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
	ADayNightController* DN = ADayNightController::GetLocal(W);
	if (DN && !DN->IsPackMinimal()) // minimal-modus: stock-belichting volledig met rust laten
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
	ApplySavedStamps();

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
// Verzamelt alle kamer-jobs: opgeslagen jobs uit Saved/RoomJobs.txt (1 regel per job:
// "x,y,z|x,y,z|x,y,z") + de huidige 3 losse markers als concept-job. Elke job bouwt z'n
// verdiepingen zodra de speler in de buurt van die bron komt.
void ADoorRetrofitter::FixBalconyPuiPositions()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Onbehandelde pui-deuren verzamelen; traces negeren ALLE werkende deuren (anders meet je
	// je eigen blad in plaats van de gevel).
	TArray<ACityDoor*> Puis;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(PuiHole), false);
	for (TActorIterator<ACityDoor> It(W); It; ++It)
	{
		if (!IsValid(*It)) { continue; }
		Q.AddIgnoredActor(*It);
		if (It->ActorHasTag(TEXT("BalcDoor")) && !It->ActorHasTag(TEXT("PuiFixed"))) { Puis.Add(*It); }
	}
	if (Puis.Num() == 0) { return; }

	TSet<ACityDoor*> Done;
	for (ACityDoor* D : Puis)
	{
		if (Done.Contains(D)) { continue; }
		UStaticMeshComponent* Pn = D->GetPanel();
		if (!Pn || !Pn->GetStaticMesh()) { continue; }
		const FVector C0 = Pn->Bounds.Origin;
		const float Wd = FMath::Max(80.f, Pn->Bounds.BoxExtent.Y * 2.f);
		const FVector AxW = D->GetActorRotation().RotateVector(FVector(0.f, 1.f, 0.f));
		const FVector AxN = D->GetActorRotation().RotateVector(FVector(1.f, 0.f, 0.f));
		// Dwars-door-de-gevel traces langs de breedte: vrij = deuropening, geraakt = vast glas/muur.
		const float Range = 340.f, Step = 20.f;
		const int32 NSamp = FMath::RoundToInt(Range * 2.f / Step) + 1;
		TArray<bool> Free;
		Free.Reserve(NSamp);
		for (int32 i = 0; i < NSamp; ++i)
		{
			const FVector P = C0 + AxW * (-Range + i * Step);
			FHitResult H;
			Free.Add(!W->LineTraceSingleByChannel(H, P - AxN * 120.f, P + AxN * 120.f, ECC_Pawn, Q));
		}
		// Langste vrije strook die aan BEIDE kanten door geraakte samples begrensd wordt (een
		// strook tot de scan-rand betekent: buurt nog niet ingestreamd -> volgende pass opnieuw).
		int32 BestS = -1, BestL = 0;
		{
			int32 i = 0;
			while (i < NSamp)
			{
				if (!Free[i]) { ++i; continue; }
				int32 j = i;
				while (j < NSamp && Free[j]) { ++j; }
				const bool bBounded = (i > 0) && (j < NSamp);
				if (bBounded && (j - i) > BestL) { BestS = i; BestL = j - i; }
				i = j;
			}
		}
		if (BestS < 0) { continue; } // nog niet beslisbaar - volgende pass
		const float HoleW = BestL * Step;
		const float HoleC = -Range + (BestS + BestL * 0.5f - 0.5f) * Step;
		// Partner-blad van dezelfde pui (tweede rail, vlak ernaast geparkeerd)? Op PANEEL-midden
		// vergelijken - de actor-pivots (scharnieren) zitten bij gespiegelde bladen ~1,5m uit elkaar.
		ACityDoor* Mate = nullptr;
		for (ACityDoor* M : Puis)
		{
			if (M == D || Done.Contains(M)) { continue; }
			UStaticMeshComponent* MP = M->GetPanel();
			if (MP && FVector::Dist(MP->Bounds.Origin, C0) < 60.f) { Mate = M; break; }
		}
		auto Apply = [&](ACityDoor* Dr, float TargetS)
		{
			UStaticMeshComponent* P2 = Dr->GetPanel();
			const FVector Cc = P2 ? P2->Bounds.Origin : Dr->GetActorLocation();
			const FVector LocalD = Dr->GetActorRotation().UnrotateVector((C0 + AxW * TargetS) - Cc);
			Dr->SetSlideClosedOffset(FVector(0.f, LocalD.Y, 0.f));
			Dr->SetOpenSwing((LocalD.Y <= 0.f) ? 95.f : -95.f); // open = terugschuiven richting parkeer-kant
			Dr->Tags.Add(TEXT("PuiFixed"));
			Done.Add(Dr);
		};
		if (HoleW < 50.f)
		{
			// Geen gat: blad staat al goed (dicht geparkeerd). Niets verschuiven.
			D->Tags.Add(TEXT("PuiFixed"));
			Done.Add(D);
			if (Mate) { Mate->Tags.Add(TEXT("PuiFixed")); Done.Add(Mate); }
			continue;
		}
		if (Mate && HoleW > Wd * 1.2f)
		{
			// Twee bladen verdelen het gat: zij aan zij.
			Apply(D, HoleC - Wd * 0.5f + 4.f);
			Apply(Mate, HoleC + Wd * 0.5f - 4.f);
		}
		else
		{
			Apply(D, HoleC);
			if (Mate) { Apply(Mate, HoleC); }
		}
		UE_LOG(LogWeedShop, Warning, TEXT("PuiHole: gat %.0f breed op s=%.0f bij (%.0f, %.0f, %.0f) - %s"),
			HoleW, HoleC, C0.X, C0.Y, C0.Z, Mate ? TEXT("2 bladen verdeeld") : TEXT("1 blad gecentreerd"));
	}
}

void ADoorRetrofitter::ApplyInstantGlass()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Kolom-rechthoeken 1x opbouwen uit de kamer-jobs (zelfde rechthoek als RunVertJob gebruikt).
	if (!bGlassRectsLoaded)
	{
		bGlassRectsLoaded = true;
		TArray<FString> JobLines;
		FFileHelper::LoadFileToStringArray(JobLines, *(WeedData::File(TEXT("RoomJobs.txt"))));
		for (const FString& JL : JobLines)
		{
			TArray<FString> Triples;
			JL.ParseIntoArray(Triples, TEXT("|"));
			if (Triples.Num() != 3) { continue; }
			TArray<FVector> JM;
			for (const FString& T : Triples)
			{
				TArray<FString> Cs;
				T.ParseIntoArray(Cs, TEXT(","));
				if (Cs.Num() >= 3) { JM.Add(FVector(FCString::Atof(*Cs[0]), FCString::Atof(*Cs[1]), FCString::Atof(*Cs[2]))); }
			}
			if (JM.Num() != 3) { continue; }
			FBox B(ForceInit);
			B += JM[0];
			B += JM[1];
			B = B.ExpandBy(FVector(130.f, 130.f, 0.f));
			// Kolom-hoogte: van bron tot cap-marker, met ruime marge naar boven en onder.
			B.Min.Z = FMath::Min3(JM[0].Z, JM[1].Z, JM[2].Z) - 500.f;
			B.Max.Z = FMath::Max3(JM[0].Z, JM[1].Z, JM[2].Z) + 900.f;
			GlassRects.Add(B);
		}
	}
	if (GlassRects.Num() == 0) { return; }
	UMaterialInterface* Clear = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CityBeachStrip/Materials/Glass/MI_Window_TwoSided.MI_Window_TwoSided"));
	if (!Clear) { return; }
	int32 NSwapped = 0;
	for (TObjectIterator<UStaticMeshComponent> CIt; CIt; ++CIt)
	{
		UStaticMeshComponent* C = *CIt;
		if (!C || C->GetWorld() != W || !C->GetStaticMesh()) { continue; }
		// Gedeelde instanties (ISM) overslaan: een materiaal-wissel daar raakt de hele stad.
		if (Cast<UInstancedStaticMeshComponent>(C)) { continue; }
		const FVector L = C->Bounds.Origin;
		bool bIn = false;
		for (const FBox& B : GlassRects)
		{
			if (B.IsInsideOrOn(L)) { bIn = true; break; }
		}
		if (!bIn) { continue; }
		for (int32 Si = 0; Si < C->GetNumMaterials(); ++Si)
		{
			UMaterialInterface* M = C->GetMaterial(Si);
			const FString MN = M ? M->GetName() : FString();
			if (MN.Contains(TEXT("TwoSided"))) { continue; }
			// Alleen de nep-glas materialen: parallax (MI_Window exact) en de cubemap-nepkamers.
			if (MN == TEXT("MI_Window") || MN.Contains(TEXT("ApartmentWindows")) || MN.Contains(TEXT("ShopWindows")))
			{
				C->SetMaterial(Si, Clear);
				++NSwapped;
			}
		}
	}
	if (NSwapped > 0)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("InstantGlass: %d raam-slots direct helder gemaakt (%d kolommen)"), NSwapped, GlassRects.Num());
	}
}

void ADoorRetrofitter::VerticalReplicate()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const FString MapPath = W->GetOutermost()->GetName();

	// WACHT op de gebakken overlay: de jobs deduppen tegen de gebakken actors - draait een job
	// EERDER dan de (async ladende) overlay, dan bouwt 'ie alles dubbel (z-fighting, dubbele deuren).
	if (BakedOverlay.IsValid() && !BakedOverlay->IsLevelLoaded()) { return; }

	TArray<TArray<FVector>> Jobs;
	// 1) Opgeslagen jobs.
	{
		TArray<FString> JobLines;
		FFileHelper::LoadFileToStringArray(JobLines, *(WeedData::File(TEXT("RoomJobs.txt"))));
		for (const FString& JL : JobLines)
		{
			TArray<FString> Triples;
			JL.ParseIntoArray(Triples, TEXT("|"));
			if (Triples.Num() != 3) { continue; }
			TArray<FVector> JM;
			for (const FString& T : Triples)
			{
				TArray<FString> Cs;
				T.ParseIntoArray(Cs, TEXT(","));
				if (Cs.Num() >= 3) { JM.Add(FVector(FCString::Atof(*Cs[0]), FCString::Atof(*Cs[1]), FCString::Atof(*Cs[2]))); }
			}
			if (JM.Num() == 3) { Jobs.Add(JM); }
		}
	}
	// 2) Concept-job: de huidige 3 losse markers.
	{
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
		if (Marks.Num() == 3) { Jobs.Add(Marks); }
	}

	// Welke jobs zijn al GEBAKKEN (permanent in BakedRooms.umap)? Die mogen nooit meer spawnen -
	// de gebakken geometrie maakt de keuring soepeler waardoor eerder-afgekeurde rand-stukken
	// ineens slagen en er troep bovenop gebouwd wordt. Alleen de raam-verberging blijft.
	TSet<FString> BakedJobs;
	{
		TArray<FString> BakedLines;
		FFileHelper::LoadFileToStringArray(BakedLines, *(WeedData::File(TEXT("BakedJobs.txt"))));
		for (const FString& BL : BakedLines) { BakedJobs.Add(BL.TrimStartAndEnd()); }
	}

	for (const TArray<FVector>& JM : Jobs)
	{
		const FString JobId = FString::Printf(TEXT("%d_%d_%d"), FMath::RoundToInt(JM[0].X), FMath::RoundToInt(JM[0].Y), FMath::RoundToInt(JM[2].Z));
		if (!DoneJobs.Contains(JobId)) { RunVertJob(JM, JobId, BakedJobs.Contains(JobId)); }
	}
}

void ADoorRetrofitter::RunVertJob(const TArray<FVector>& Marks, const FString& JobId, bool bBakedJob)
{
	UWorld* W = GetWorld();
	if (!W || Marks.Num() != 3) { return; }

	// EEN strakke rechthoek tussen de 2 markers (+30cm marge): alles daarbinnen gaat mee, alles
	// daarbuiten (gevel-details, balkons, terras) blijft VOLLEDIG met rust.
	FBox2D Outer(ForceInit);
	Outer += FVector2D(Marks[0].X, Marks[0].Y);
	Outer += FVector2D(Marks[1].X, Marks[1].Y);
	Outer = Outer.ExpandBy(130.f); // ruim genoeg om de OMSLUITENDE muren + gevel-binnenkant te pakken (middelpunt ligt op de muur-lijn), nog binnen het balkon
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
	// Marker 3 bepaalt de vul-richting en -grens:
	// - BOVEN de bron (sta/zweef op het DAK): vult omhoog, bovenste kamer komt 1 onder de marker.
	// - ONDER de bron (sta op de VLOER van de laagste gewenste kamer): vult omlaag t/m die verdieping.
	const float CapFeet = Marks[2].Z - 98.f;
	const float CapStorey = 480.f + 350.f * FMath::RoundToFloat((CapFeet - 480.f) / 350.f);
	const int32 CapDelta = FMath::RoundToInt((CapStorey - SrcZ) / 350.f);
	const int32 CapN = (CapDelta >= 0)
		? FMath::Clamp(CapDelta - 1, 1, 30)    // omhoog: dak-conventie
		: FMath::Clamp(CapDelta, -30, -1);     // omlaag: vloer-conventie

	// Bron-slice verzamelen (incl. verborgen geconverteerde deur-bladen -> werkende deuren in de kopie).
	// MET materialen: de map geeft bv. het raam-glas op ingerichte verdiepingen een OVERRIDE (helder
	// glas) - zonder die override valt geplakt glas terug op het standaard-materiaal (parallax/nep-3D).
	struct FSliceEntry { UStaticMesh* Mesh; FTransform TM; FVector BO; FVector Ext = FVector::ZeroVector; TArray<UMaterialInterface*> Mats; bool bSyncOnly = false; };
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
			if (bHiddenActor && !MeshName.Contains(TEXT("Door"))) { continue; } // alle geconverteerde bladen (ook BalconyDoor) mee
			// COMPONENT-onzichtbare stukken overslaan (behalve deur-bladen): de stempel-gevelfix
			// verbergt vervangen gevel-ramen via SetVisibility - die horen NIET in de slice, anders
			// gelden ze op de doelverdiepingen als origineel (rare-formaat ramen blijven daar staan
			// en de raam-verberging slaat ze over).
			if (!Comp->IsVisible() && !MeshName.Contains(TEXT("Door"))) { continue; }
			if (MeshName.Contains(TEXT("Camera")) || MeshName.Contains(TEXT("SecurityCam")) || MeshName.Contains(TEXT("MatineeCam"))
				|| MeshName.Contains(TEXT("DomeCam")) || MeshName.Contains(TEXT("SecurityLight"))) { continue; } // (editor-)camera's/spots horen niet in kopieen
			// Balkon-railing niet mee-kopieren (stond bij de badkamer-rand binnen de rechthoek en
			// werd op elke verdieping tegen de muur geplakt).
			if (MeshName.Contains(TEXT("Railing")) || MeshName.Contains(TEXT("Handrail")) || MeshName.Contains(TEXT("Balustrade"))) { continue; }
			// Muur-afdek/dakrand-stukjes (SM_Top_*, parapet-kapjes van de terras-rand bij de bron) horen
			// alleen op dakranden - niet op elke verdieping tegen de gevel.
			if (MeshName.StartsWith(TEXT("SM_Top_"))) { continue; }
			// Buiten-meubilair niet mee-kopieren (zweefde op elke verdieping boven het terras).
			if (MeshName.Contains(TEXT("Umbrella")) || MeshName.Contains(TEXT("Parasol")) || MeshName.Contains(TEXT("Lounger"))
				|| MeshName.Contains(TEXT("SunBed")) || MeshName.Contains(TEXT("Sunbed")) || MeshName.Contains(TEXT("Chair"))
				|| MeshName.Contains(TEXT("Table")) || MeshName.Contains(TEXT("Awning")) || MeshName.Contains(TEXT("Pool"))) { continue; }
			// Op het VISUELE MIDDELPUNT (bounds) testen, niet de pivot: pivots zitten op hoeken/
			// uiteinden, waardoor randstukken met pivot net buiten de rechthoek afvielen (halve kamers).
			const FVector L = Comp->Bounds.Origin;
			if (!InRects(L)) { continue; }
			if (L.Z < SrcZ - 20.f || L.Z > SrcZ + 520.f) { continue; }
			FSliceEntry E;
			E.Mesh = Comp->GetStaticMesh();
			E.TM = Comp->GetComponentTransform();
			E.BO = L;
			E.Ext = Comp->Bounds.BoxExtent;
			// Boven de eigen verdieping-band (SrcZ+335): SYNC-ONLY - die bovenste gevel-glas-rij krijgt
			// alleen het heldere materiaal op BESTAANDE meshes, maar wordt nooit gespawnd. Anders plak je
			// per verdieping stukjes van de volgende en bouwt het gebouw zichzelf eindeloos omhoog.
			E.bSyncOnly = (L.Z > SrcZ + 335.f);
			for (int32 Mi = 0; Mi < Comp->GetNumMaterials(); ++Mi) { E.Mats.Add(Comp->GetMaterial(Mi)); }
			Slice.Add(E);
		}
	}
	// Wachten tot de bron-verdieping volledig ingestreamd is (3 scans dezelfde telling).
	int32& JLast = JobLastCount.FindOrAdd(JobId, -1);
	int32& JStreak = JobStreak.FindOrAdd(JobId, 0);
	if (Slice.Num() == JLast) { ++JStreak; } else { JStreak = 0; }
	JLast = Slice.Num();
	if (Slice.Num() < 30 || JStreak < 3)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("VertClone[%s]: bron-slice %d meshes (streak %d) - wacht"), *JobId, Slice.Num(), JStreak);
		return;
	}
	DoneJobs.Add(JobId);
	FString BakeOut; // export voor de editor-bake (permanent in de map zetten)

	// Hash-set van alles wat de BRON-verdieping wel heeft: elementen die op een doel-verdieping
	// bestaan maar hier niet, zijn door de makers verwijderd (nep-glas e.d.) -> spiegelen.
	TSet<uint64> SliceHashes;
	for (const FSliceEntry& E2 : Slice)
	{
		const uint64 SH = GetTypeHash(E2.Mesh->GetFName())
			^ (uint64)(FMath::RoundToInt(E2.BO.X / 10.f) * 73856093)
			^ (uint64)(FMath::RoundToInt(E2.BO.Y / 10.f) * 19349663)
			^ (uint64)(FMath::RoundToInt(E2.BO.Z / 10.f) * 83492791);
		SliceHashes.Add(SH);
	}

	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 TotalPlaced = 0;
	// VAN BOVEN NAAR BENEDEN tot de hoogte-marker: de bovenste verdieping heeft het echte dak/
	// penthouse boven zich, en elke gevulde verdieping legt het plafond voor de verdieping eronder.
	// Zo werkt de plafond-boven-je-test (de enige die niet te foppen is) op elke verdieping.
	const int32 NFrom = (CapN > 0) ? 1 : CapN;
	const int32 NTo = (CapN > 0) ? CapN : -1;
	for (int32 N = NFrom; N <= NTo; ++N)
	{
		if (N == 0) { continue; }
		const float Dz = N * 350.f;
		const float TgtZ = SrcZ + Dz;
		if (TgtZ < 470.f) { continue; } // begane grond heeft een andere hoogte (430) - overslaan

		// Bestaat deze verdieping (heeft het gebouw hier uberhaupt geometrie)? En dedupe-index bouwen:
		// mesh-naam + positie op 10cm-grid van alles wat er al staat.
		TMap<uint64, UStaticMeshComponent*> Existing; // hash -> bestaande comp (voor materiaal-sync)
		TArray<TPair<UStaticMeshComponent*, uint64>> WindowCandidates; // (comp, hash van bron-positie)
		int32 ExistCount = 0;
		int32 TightCount = 0; // bestaande meshes BINNEN de rechthoek zelf (gevel van deze verdieping)
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			AActor* A = *It;
			if (!IsValid(A)) { continue; }
			// ONZE werkende deuren NOOIT als raam-kandidaat zien: een dichtgeschoven pui-blad staat
			// niet meer op de bron-positie en werd dan als "door de maker verwijderd glas" verborgen
			// - weg schuifdeur. Deuren zijn van ons, niet van de gevel.
			if (A->IsA(ACityDoor::StaticClass())) { continue; }
			TInlineComponentArray<UStaticMeshComponent*> Comps(A);
			for (UStaticMeshComponent* Comp : Comps)
			{
				if (!Comp || !Comp->GetStaticMesh()) { continue; }
				const FVector L = Comp->Bounds.Origin; // zelfde basis als de bron-capture
				// RUIME zone voor de bestaat-deze-verdieping-check: lege verdiepingen hebben binnen de
				// kamer-rechthoek juist bijna niets (daarom zijn ze leeg) - maar de gang/gevel ernaast
				// bewijst dat de verdieping bestaat. De dedupe-index blijft op de strakke zone.
				if (L.X < Outer.Min.X - 1300.f || L.X > Outer.Max.X + 1300.f || L.Y < Outer.Min.Y - 1300.f || L.Y > Outer.Max.Y + 1300.f) { continue; }
				if (L.Z < TgtZ - 25.f || L.Z > TgtZ + 525.f) { continue; }
				++ExistCount;
				if (L.X < Outer.Min.X - 50.f || L.X > Outer.Max.X + 50.f || L.Y < Outer.Min.Y - 50.f || L.Y > Outer.Max.Y + 50.f) { continue; }
				++TightCount;
				{
					const FString GN = Comp->GetStaticMesh()->GetName();
					// Strikt BINNEN de kopie-rechthoek en in de eigen verdieping-band: de 50cm-buffer
					// eromheen ving buur-ramen die geen bron-tegenhanger hebben -> die werden onterecht
					// verborgen. Buur-vakken blijven nu met rust.
					// Upper_Wall doet ook mee: de stempel-gevelfix verbergt op de bron ook muursegmenten
				// (vervangen gevel) - hun doel-tegenhangers stonden anders als raar muurstukje voor
				// de gekopieerde ramen. De bron-spiegel-hash houdt het exact: stukken die op de bron
				// gewoon zichtbaar zijn blijven altijd staan.
				if ((GN.Contains(TEXT("Glass")) || GN.Contains(TEXT("Window")) || GN.Contains(TEXT("BalconyDoor")) || GN.Contains(TEXT("Upper_Wall"))) && InRects(L) && (L.Z - TgtZ) < 340.f)
					{
						// Hash van de BRON-positie (pos - Dz): bestaat die niet in de bron-slice, dan
						// heeft de maker dit element op de ingerichte verdieping VERWIJDERD -> wij ook.
						const uint64 BackH = GetTypeHash(Comp->GetStaticMesh()->GetFName())
							^ (uint64)(FMath::RoundToInt(L.X / 10.f) * 73856093)
							^ (uint64)(FMath::RoundToInt(L.Y / 10.f) * 19349663)
							^ (uint64)(FMath::RoundToInt((L.Z - Dz) / 10.f) * 83492791);
						WindowCandidates.Add(TPair<UStaticMeshComponent*, uint64>(Comp, BackH));
					}
				}
				const uint64 H = GetTypeHash(Comp->GetStaticMesh()->GetFName())
					^ (uint64)(FMath::RoundToInt(L.X / 10.f) * 73856093)
					^ (uint64)(FMath::RoundToInt(L.Y / 10.f) * 19349663)
					^ (uint64)(FMath::RoundToInt(L.Z / 10.f) * 83492791);
				Existing.Add(H, Comp);
			}
		}
		// (Geen dak-heuristieken: marker 3 bepaalt de top. ExistCount/TightCount alleen ter info.)

		int32 Placed = 0;
		if (!bBakedJob) // gebakken: geometrie staat al in de map - nooit meer bouwen
		{
		// FASE 1: keuren - past de kamer hier (vrijwel) volledig? Zo niet: hele verdieping overslaan.
		// Half-passende kamers (smallere toren boven het podium) gaven alleen maar troep; een lege
		// verdieping kan later z'n eigen (passende) bron-job krijgen via de building-tool.
		int32 FitEligible = 0, FitPass = 0;
		TArray<bool> PieceFits;
		PieceFits.SetNum(Slice.Num());
		for (int32 Si = 0; Si < Slice.Num(); ++Si)
		{
			const FSliceEntry& M = Slice[Si];
			PieceFits[Si] = false;
			if (M.bSyncOnly) { continue; }
			const FVector NLf = M.BO + FVector(0.f, 0.f, Dz);
			++FitEligible;
			// PLAFOND-BOVEN-JE-TEST: binnen het gebouw zit recht boven elk stuk (centrum + 4 hoeken,
			// 15cm ingetrokken) de vloerplaat van de verdieping erboven (net gebouwd - we gaan top-down)
			// of het echte dak. Buiten het gebouw is boven je open lucht. Straal-checks opzij faalden
			// in de dichte straat (overburen binnen bereik); deze is niet te foppen.
			FCollisionQueryParams GQP(SCENE_QUERY_STAT(VertCloneFit), false);
			const float FEx = FMath::Max(0.f, M.Ext.X - 15.f);
			const float FEy = FMath::Max(0.f, M.Ext.Y - 15.f);
			const FVector2D FitPts[5] = {
				FVector2D(NLf.X, NLf.Y),
				FVector2D(NLf.X + FEx, NLf.Y + FEy), FVector2D(NLf.X + FEx, NLf.Y - FEy),
				FVector2D(NLf.X - FEx, NLf.Y + FEy), FVector2D(NLf.X - FEx, NLf.Y - FEy) };
			const float TopZ = FMath::Max(NLf.Z + M.Ext.Z + 8.f, TgtZ + 8.f);
			bool bCovered = true;
			for (const FVector2D& TP : FitPts)
			{
				FHitResult UpHit;
				const FVector UStart(TP.X, TP.Y, TopZ);
				// LANG omhoog kijken (26m): lege verdiepingen hebben onderling geen platen, maar binnen
				// de gebouw-voetafdruk raakt de straal uiteindelijk altijd het dak/penthouse. Buiten de
				// voetafdruk (doorstekende stukken) is het lucht tot in de hemel -> afgekeurd.
				const FVector UEnd(TP.X, TP.Y, TopZ + 2600.f);
				if (!W->LineTraceSingleByChannel(UpHit, UStart, UEnd, ECC_Visibility, GQP)) { bCovered = false; break; }
			}
			if (bCovered) { PieceFits[Si] = true; ++FitPass; }
		}
		if (FitEligible > 0 && FitPass < FitEligible * 9 / 10)
		{
			UE_LOG(LogWeedShop, Warning, TEXT("VertClone: verdieping %+d (Z %.0f) overgeslagen - kamer past hier niet (%d/%d stukken)"), N, TgtZ, FitPass, FitEligible);
			continue;
		}

		for (int32 Si = 0; Si < Slice.Num(); ++Si)
		{
			const FSliceEntry& M = Slice[Si];
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
			if (M.bSyncOnly) { continue; } // alleen materiaal-sync, nooit spawnen
			if (!PieceFits[Si]) { continue; } // uitslag van de keuring (fase 1)
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
			// Export-regel voor de editor-bake: mesh|pos|rot|scale|materialen.
			{
				const FVector SL = NewTM.GetLocation();
				const FRotator SR = NewTM.GetRotation().Rotator();
				const FVector SS = NewTM.GetScale3D();
				FString MatList;
				for (int32 Mi = 0; Mi < M.Mats.Num(); ++Mi)
				{
					if (Mi > 0) { MatList += TEXT(";"); }
					MatList += M.Mats[Mi] ? M.Mats[Mi]->GetPathName() : TEXT("-");
				}
				BakeOut += FString::Printf(TEXT("SPAWN|%s|%.2f,%.2f,%.2f|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%s"),
					*M.Mesh->GetPathName(), SL.X, SL.Y, SL.Z, SR.Pitch, SR.Yaw, SR.Roll, SS.X, SS.Y, SS.Z, *MatList);
				BakeOut += LINE_TERMINATOR;
			}
			++Placed;
		}
		// Kamer geplaatst -> SPIEGEL DE BRON: elk raam/glas-element dat hier staat maar op de bron-
		// verdieping NIET (de makers haalden het daar weg voor de ingerichte look) verbergen we ook.
		} // einde niet-gebakken bouw-pad
		else
		{
			// GEBAKKEN verdieping: alleen ontbrekende DEUR-bladen bijvullen (bv. balkon-puien die in
			// eerdere bakes niet meegingen door het te smalle blad-filter). Dedupe voorkomt dubbelen;
			// de leaf-converter maakt er daarna werkende (schuif)deuren van.
			for (const FSliceEntry& M : Slice)
			{
				if (M.bSyncOnly || !M.Mesh->GetName().Contains(TEXT("Door"))) { continue; }
				FTransform NewTM = M.TM;
				NewTM.AddToTranslation(FVector(0.f, 0.f, Dz));
				const FVector NL = M.BO + FVector(0.f, 0.f, Dz);
				const uint64 H = GetTypeHash(M.Mesh->GetFName())
					^ (uint64)(FMath::RoundToInt(NL.X / 10.f) * 73856093)
					^ (uint64)(FMath::RoundToInt(NL.Y / 10.f) * 19349663)
					^ (uint64)(FMath::RoundToInt(NL.Z / 10.f) * 83492791);
				if (Existing.Contains(H)) { continue; }
				AStaticMeshActor* SMA = W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), NewTM, SP);
				if (!SMA) { continue; }
				if (UStaticMeshComponent* C = SMA->GetStaticMeshComponent())
				{
					C->SetMobility(EComponentMobility::Movable);
					C->SetStaticMesh(M.Mesh);
					C->SetCanEverAffectNavigation(false);
					for (int32 Mi = 0; Mi < M.Mats.Num(); ++Mi)
					{
						if (M.Mats[Mi]) { C->SetMaterial(Mi, M.Mats[Mi]); }
					}
				}
				++Placed;
			}
		}

		int32 GlassHidden = 0;
		// Alleen raam-spiegelen als er een substantiele kamer staat (geplaatst of gebakken).
		if (bBakedJob || Placed > Slice.Num() / 2)
		{
			for (const TPair<UStaticMeshComponent*, uint64>& WC : WindowCandidates)
			{
				if (WC.Key && WC.Key->IsVisible() && !SliceHashes.Contains(WC.Value))
				{
					WC.Key->SetVisibility(false);
					WC.Key->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					const FVector HL = WC.Key->Bounds.Origin;
					BakeOut += FString::Printf(TEXT("HIDE|%s|%.1f,%.1f,%.1f"), *WC.Key->GetStaticMesh()->GetName(), HL.X, HL.Y, HL.Z);
					BakeOut += LINE_TERMINATOR;
					++GlassHidden;
				}
			}
		}
		TotalPlaced += Placed;
		UE_LOG(LogWeedShop, Warning, TEXT("VertClone: verdieping %+d (Z %.0f): %d meshes aangevuld (%d bestonden al, %d nep-glas verborgen)"), N, TgtZ, Placed, ExistCount, GlassHidden);
	}
	if (BakeOut.Len() > 0)
	{
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("JOB|%s"), *JobId) + LINE_TERMINATOR + BakeOut,
			*(FPaths::ProjectSavedDir() / TEXT("RoomBake.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
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

// Geplaatste kamer-stempels (RoomStamps.txt) elke sessie herbouwen, tot ze gebakken zijn
// (dan staat hun STAMP-id in BakedJobs.txt en staat de geometrie al in de map).
void ADoorRetrofitter::ApplySavedStamps()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	if (BakedOverlay.IsValid() && !BakedOverlay->IsLevelLoaded()) { return; } // wacht op de bake-overlay
	if (!bBakedWindowsReal && BakedOverlay.IsValid() && BakedOverlay->GetLoadedLevel())
	{
		bBakedWindowsReal = true;
		MakeBakedWindowsReal();
	}

	TSet<FString> BakedJobs;
	{
		TArray<FString> BakedLines;
		FFileHelper::LoadFileToStringArray(BakedLines, *(WeedData::File(TEXT("BakedJobs.txt"))));
		for (const FString& BL : BakedLines) { BakedJobs.Add(BL.TrimStartAndEnd()); }
	}

	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(WeedData::File(TEXT("RoomStamps.txt"))));
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FString& Line : Lines)
	{
		TArray<FString> P;
		Line.ParseIntoArray(P, TEXT("|"));
		if (P.Num() < 3) { continue; }
		TArray<FString> Lp;
		P[1].ParseIntoArray(Lp, TEXT(","));
		if (Lp.Num() < 3) { continue; }
		const FVector AL(FCString::Atof(*Lp[0]), FCString::Atof(*Lp[1]), FCString::Atof(*Lp[2]));
		const float AY = FCString::Atof(*P[2]);
		const bool bMirror = P.Num() >= 4 && P[3].TrimStartAndEnd() == TEXT("M");
		const FString StampId = FString::Printf(TEXT("STAMP_%d_%d_%d"), FMath::RoundToInt(AL.X), FMath::RoundToInt(AL.Y), FMath::RoundToInt(AY));
		if (AppliedStamps.Contains(StampId)) { continue; }
		AppliedStamps.Add(StampId);
		const bool bBakedStamp = BakedJobs.Contains(StampId); // gebakken: geometrie staat al in de map

		TArray<FStampPiece> Pieces;
		if (!ARoomStamper::LoadTemplate(P[0], Pieces)) { continue; }
		const FTransform Anchor(FRotator(0.f, AY, 0.f), AL);
		FString StampBakeOut; // bake-export van deze stempel (RoomBake.txt wordt per sessie geleegd)
		int32 Placed = 0;
		if (!bBakedStamp)
		for (const FStampPiece& Piece : Pieces)
		{
			const FTransform NewTM = (bMirror ? ARoomStamper::MirrorPieceTM(Piece) : Piece.RelTM) * Anchor;
			AStaticMeshActor* SMA = W->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), NewTM, SP);
			if (!SMA) { continue; }
			SMA->Tags.Add(FName(*StampId)); // voor undo/verwijderen via de phone
			if (bMirror)
			{
				const FString MNm = Piece.Mesh ? Piece.Mesh->GetName() : FString();
				if (MNm.Contains(TEXT("Door")) && !MNm.Contains(TEXT("DoorFrame")) && !MNm.Contains(TEXT("Wall")))
				{
					SMA->Tags.Add(TEXT("MirroredDoor")); // converter: standaard-zwaai omklappen
				}
			}
			if (UStaticMeshComponent* C = SMA->GetStaticMeshComponent())
			{
				C->SetMobility(EComponentMobility::Movable);
				C->SetStaticMesh(Piece.Mesh);
				C->SetCanEverAffectNavigation(false);
				for (int32 Mi = 0; Mi < Piece.Mats.Num(); ++Mi)
				{
					if (Piece.Mats[Mi]) { C->SetMaterial(Mi, Piece.Mats[Mi]); }
				}
			}
			{
				const FVector SL = NewTM.GetLocation();
				const FRotator SR = NewTM.GetRotation().Rotator();
				const FVector SS = NewTM.GetScale3D();
				FString MatList;
				for (int32 Mi = 0; Mi < Piece.Mats.Num(); ++Mi)
				{
					if (Mi > 0) { MatList += TEXT(";"); }
					MatList += Piece.Mats[Mi] ? Piece.Mats[Mi]->GetPathName() : TEXT("-");
				}
				StampBakeOut += FString::Printf(TEXT("SPAWN|%s|%.2f,%.2f,%.2f|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%s"),
					*Piece.Mesh->GetPathName(), SL.X, SL.Y, SL.Z, SR.Pitch, SR.Yaw, SR.Roll, SS.X, SS.Y, SS.Z, *MatList);
				StampBakeOut += LINE_TERMINATOR;
			}
			++Placed;
		}
		// Bake-export: sessie-herbouwde stempels horen ook in RoomBake.txt (het bestand wordt per
		// sessie geleegd; zonder her-export zou een stempel uit een eerdere sessie nooit baken).
		if (!bBakedStamp && Placed > 0)
		{
			FFileHelper::SaveStringToFile(FString::Printf(TEXT("JOB|%s"), *StampId) + LINE_TERMINATOR + StampBakeOut,
				*(FPaths::ProjectSavedDir() / TEXT("RoomBake.txt")),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		}

		// Gevel-ramen elke sessie opnieuw laten kloppen (verbergen/look-herstel is niet bakbaar).
		ARoomStamper::ApplyWindowFix(W, P[0], Anchor, bMirror);
		UE_LOG(LogWeedShop, Warning, TEXT("RoomStamper: sessie-herbouw '%s' op (%.0f, %.0f) - %d stukken"), *P[0], AL.X, AL.Y, Placed);
	}

	// Late streaming: gevel-ramen die bij de eerste pass nog niet geladen waren alsnog fixen.
	if (!bStampFixTimersSet)
	{
		bStampFixTimersSet = true;
		GetWorldTimerManager().SetTimer(StampFixT1, this, &ADoorRetrofitter::RefreshStampWindowFixes, 15.f, false);
		GetWorldTimerManager().SetTimer(StampFixT2, this, &ADoorRetrofitter::RefreshStampWindowFixes, 45.f, false);
	}
}

void ADoorRetrofitter::RefreshStampWindowFixes()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(WeedData::File(TEXT("RoomStamps.txt"))));
	for (const FString& Line : Lines)
	{
		TArray<FString> P;
		Line.ParseIntoArray(P, TEXT("|"));
		if (P.Num() < 3) { continue; }
		TArray<FString> Lp;
		P[1].ParseIntoArray(Lp, TEXT(","));
		if (Lp.Num() < 3) { continue; }
		const FVector AL(FCString::Atof(*Lp[0]), FCString::Atof(*Lp[1]), FCString::Atof(*Lp[2]));
		const float AY = FCString::Atof(*P[2]);
		const bool bMirror = P.Num() >= 4 && P[3].TrimStartAndEnd() == TEXT("M");
		ARoomStamper::ApplyWindowFix(W, P[0], FTransform(FRotator(0.f, AY, 0.f), AL), bMirror);

		// DIAGNOSE: alles rond de stempel dumpen (naam, positie, zichtbaar, materiaal, eigenaar)
		// zodat we exact zien welke meshes er bij de buitenmuur staan/verbergen.
		FString Dump;
		int32 DumpCount = 0;
		for (TActorIterator<AActor> DIt(W); DIt; ++DIt)
		{
			AActor* DA = *DIt;
			if (!IsValid(DA)) { continue; }
			TInlineComponentArray<UStaticMeshComponent*> DComps(DA);
			for (UStaticMeshComponent* DC : DComps)
			{
				if (!DC || !DC->GetStaticMesh()) { continue; }
				const FVector DL = DC->Bounds.Origin;
				if (FVector::Dist2D(DL, AL) > 1300.f || DL.Z < AL.Z - 60.f || DL.Z > AL.Z + 420.f) { continue; }
				FString TagStr;
				for (const FName& Tg : DA->Tags) { TagStr += Tg.ToString() + TEXT(","); }
				UMaterialInterface* M0 = DC->GetMaterial(0);
				Dump += FString::Printf(TEXT("%s | (%.0f, %.0f, %.0f) | vis=%d hid=%d | mat=%s | tags=%s | actor=%s"),
					*DC->GetStaticMesh()->GetName(), DL.X, DL.Y, DL.Z,
					DC->IsVisible() ? 1 : 0, DA->IsHidden() ? 1 : 0,
					M0 ? *M0->GetName() : TEXT("-"), *TagStr, *DA->GetClass()->GetName());
				Dump += LINE_TERMINATOR;
				++DumpCount;
				if (DumpCount > 400) { break; }
			}
			if (DumpCount > 400) { break; }
		}
		FFileHelper::SaveStringToFile(Dump, *(FPaths::ProjectSavedDir() / TEXT("StampAreaDump.txt")));
		UE_LOG(LogWeedShop, Warning, TEXT("RoomStamper: area-dump %d comps -> StampAreaDump.txt"), DumpCount);
	}
}

// Gebakken kamers zijn ECHTE interieurs: hun raam-stukken kwamen uit de oude pipeline en dragen
// nog het parallax nep-glas (MI_Window) of nep-kamer cubemaps (MI_ApartmentWindows/MI_ShopWindows).
// Een pass over de bake-overlay zodra die geladen is - dus exact het moment waarop de ramen
// verschijnen - zet ze om naar het echte tweezijdige glas. Geen vertraging zichtbaar.
void ADoorRetrofitter::MakeBakedWindowsReal()
{
	ULevel* L = BakedOverlay.IsValid() ? BakedOverlay->GetLoadedLevel() : nullptr;
	if (!L) { return; }
	UMaterialInterface* Clear = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/CityBeachStrip/Materials/Glass/MI_Window_TwoSided.MI_Window_TwoSided"));
	if (!Clear) { return; }

	int32 Swapped = 0;
	for (AActor* A : L->Actors)
	{
		if (!IsValid(A)) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh()) { continue; }
			const FString MN = Comp->GetStaticMesh()->GetName();
			if (!(MN.Contains(TEXT("Window")) || MN.Contains(TEXT("Glass")) || MN.Contains(TEXT("BalconyDoor")))) { continue; }
			for (int32 Mi = 0; Mi < Comp->GetNumMaterials(); ++Mi)
			{
				UMaterialInterface* Cur = Comp->GetMaterial(Mi);
				if (!Cur) { continue; }
				const FString CurName = Cur->GetName();
				if (CurName == TEXT("MI_Window") || CurName.Contains(TEXT("ApartmentWindows")) || CurName.Contains(TEXT("ShopWindows")))
				{
					Comp->SetMaterial(Mi, Clear);
					++Swapped;
				}
			}
		}
	}
	UE_LOG(LogWeedShop, Warning, TEXT("BakedRooms: %d nep-glas slots omgezet naar echt doorzichtig glas"), Swapped);
}
