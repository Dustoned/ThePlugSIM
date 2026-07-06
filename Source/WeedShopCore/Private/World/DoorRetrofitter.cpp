#include "World/DoorRetrofitter.h"

#include "WeedShopCore.h"
#include "HAL/IConsoleManager.h" // r.LightMaxDrawDistanceScale tijdelijk uit voor de kaart-capture (nacht-lampen)
#include "RenderingThread.h"      // FlushRenderingCommands: capture afmaken vóór de cull-restore
#include "World/CityDoor.h"
#include "World/DayNightController.h"
#include "World/PackElevator.h"
#include "World/PackElevatorButton.h"
#include "World/RoomStamper.h"
#include "World/MapBorder.h"
#include "Customer/CustomerSpawner.h"
#include "Customer/CustomerBase.h"
#include "World/StoreCounter.h"
#include "World/Atm.h"
#include "Placement/PlaceableProp.h"
#include "World/WaterSink.h"
#include "World/StorageShelf.h"
#include "World/PackLightSwitch.h"
#include "Placement/PlaceableTypes.h"
#include "Save/SaveGameSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h" // IsCompiling()/GetRenderData(): lift pas bouwen als de meshes echt klaar zijn
#include "Economy/EconomyComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "NavigationSystem.h"
#include "Engine/TargetPoint.h"
#include "NavigationPath.h"
#include "Navigation/NavLinkProxy.h"
#include "NavLinkCustomComponent.h"
#include "Game/WeedShopGameState.h"
#include "World/WorldSyncComponent.h" // bestaan-sync van speler-geplaatste lichtschakelaars (server publiceert, client spiegelt)
#include "Npc/NpcRegistryComponent.h"
#include "World/WorldItemPickup.h"        // gescatterde joints (pickup-spawn)
#include "Inventory/InventoryComponent.h" // UInventoryComponent::MakeJointId
#include "Progression/StoreComponent.h"   // seed-catalogus + strain-stats (THC)
#include "Progression/LevelComponent.h"   // speler-level voor tier-schaling van de gescatterde joints
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
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/MovementComponent.h"
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

	// COVER-FASE: de loading-cover staat nog over beeld -> zwaar opbouw-werk mag VERSNELD (scan-hitches
	// en spawn-hitches zijn achter de cover onzichtbaar, en de wereld staat er sneller). Guard op de
	// laad-timer zodat PIE/direct-map-starts (geen cover, timer = 0) de normale cadans houden, plus een
	// tijd-cap zodat de versnelde stand nooit blijft hangen als de cover-vlag om wat voor reden uitblijft.
	bool IsCoverPhase()
	{
		const double E = WeedShop_LoadElapsedSeconds();
		return !WeedShop_IsCrowdSpawned() && E > 0.0 && E < 120.0;
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
	// [BOOTMARK] load-tijd-attributie: dit BeginPlay bouwt de stad SYNCHROON (het grootste blok in de
	// wereld-load); de marks hieronder wijzen de seconden aan de fases toe. Goedkoop (Display, 1x per load).
	const double _RetroT0 = FPlatformTime::Seconds();
	UE_LOG(LogWeedShop, Display, TEXT("[BOOTMARK] DoorRetrofitter::BeginPlay start (+%.2fs sinds start)"), _RetroT0 - GStartTime);
	// Meld de loading-cover dat deze wereld een retrofitter heeft: de cover wacht dan op de stad-ombouw
	// (CityConverted) + crowd (CrowdWarm) + thuis-vloer (RoomReady). Maps zonder retrofitter slaan die
	// gates over (de vlag blijft daar false; reset gebeurt in WeedShop_RequestGameLoadingScreen).
	WeedShop_SetCityRetroActive(true);
	// JOIN-COVER: verse map-load -> de lokale speler staat nog NIET op z'n eindpositie. De cover blijft staan
	// tot de floor-pin / HomePawnNow de lokale pawn geplaatst heeft en de vlag weer true zet.
	WeedShop_SetLocalPawnPlaced(false);
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
	UE_LOG(LogWeedShop, Display, TEXT("[BOOTMARK] Retro: baked-overlay+grens klaar na %.2fs"), FPlatformTime::Seconds() - _RetroT0);

	// HITCH-FIX (stat dumphitches, 07-03): alle crowd/basis-skins 1x voorladen ONDER het laadscherm +
	// keep-alive. Zonder dit deed elke body-materialisatie tijdens het rondlopen een blocking skin-load
	// (Flush Async Loading + skinned-asset-build) op de game-thread = 200-500ms freeze elke paar seconden.
	// Draait op host EN joiner (de joiner laadt skins bij OnRep_Appearance en had dezelfde hitch).
	ACustomerBase::PreloadCrowdSkins(this);
	const double _RetroT1 = FPlatformTime::Seconds();
	UE_LOG(LogWeedShop, Display, TEXT("[BOOTMARK] Retro: PreloadCrowdSkins klaar (+%.2fs binnen BeginPlay)"), _RetroT1 - _RetroT0);

	ScanAndConvert();
	UE_LOG(LogWeedShop, Display, TEXT("[BOOTMARK] Retro: eerste ScanAndConvert-pass %.2fs; BeginPlay totaal %.2fs (+%.2fs sinds start)"),
		FPlatformTime::Seconds() - _RetroT1, FPlatformTime::Seconds() - _RetroT0, FPlatformTime::Seconds() - GStartTime);
	// Zolang de loading-cover op beeld staat scannen we VERSNELD (0.4s i.p.v. 2s): de sweep-hitches
	// zijn achter de cover onzichtbaar en de stad (deuren/glas/lampen/liften) staat er sneller. Na de
	// fade schakelt de adaptieve cadans (onderaan ScanAndConvert) vanzelf terug naar 2s/8s.
	ScanRate = IsCoverPhase() ? 0.4f : 2.0f;
	GetWorldTimerManager().SetTimer(ScanTimer, this, &ADoorRetrofitter::ScanAndConvert, ScanRate, true);
	// Virtuele crowd beweegt op een eigen snelle tik (10x/s, kleine stapjes): vloeiend op de
	// kaart, en het zware werk (traces voor materialiseren) blijft op de 2s-pass.
	GetWorldTimerManager().SetTimer(CrowdMoveTimer, this, &ADoorRetrofitter::TickVirtualMove, 0.1f, true);

	// STREAMING-SIGNAAL: de zware ombouw-sweep (deuren/glas/lampen/liften) hoeft ALLEEN te draaien als er
	// nieuwe geometrie instreamt. World-partition cellen + level-instances vuren deze delegates -> we
	// markeren de wereld 'dirty' en doen dan 1 sweep; staat alles stil, dan slaat de sweep zichzelf over
	// (geen periodieke freeze meer). Eenmaal omgebouwd gebied blijft omgebouwd.
	{
		TWeakObjectPtr<ADoorRetrofitter> WeakThis(this);
		auto MarkDirty = [WeakThis](ULevel*, UWorld* InW)
		{
			ADoorRetrofitter* Self = WeakThis.Get();
			if (Self && InW == Self->GetWorld()) { Self->bWorldDirty = true; }
		};
		FWorldDelegates::LevelAddedToWorld.AddWeakLambda(this, MarkDirty);
		FWorldDelegates::LevelRemovedFromWorld.AddWeakLambda(this, MarkDirty);
	}

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
			UE_LOG(LogWeedShop, Verbose, TEXT("ELEVSCAN gestart: spot (%.0f, %.0f, %.0f)"), ElevScanPos.X, ElevScanPos.Y, ElevScanPos.Z);
			GetWorldTimerManager().SetTimer(ElevScanTimer, this, &ADoorRetrofitter::ElevTeleport, 5.f, false);
			break;
		}
		if (!bElevScan) { UE_LOG(LogWeedShop, Verbose, TEXT("ELEVSCAN: geen marked spot voor deze map gevonden")); }
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
			UE_LOG(LogWeedShop, Verbose, TEXT("SPOTSCAN gepland: spot (%.0f, %.0f, %.0f)"), ElevScanPos.X, ElevScanPos.Y, ElevScanPos.Z);
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
	UE_LOG(LogWeedShop, Verbose, TEXT("SPOTSCAN klaar (%d spots, %d tekens) -> Saved/SpotScan.txt"), Spots.Num(), Out.Len());
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
	UE_LOG(LogWeedShop, Verbose, TEXT("ELEVSCAN dump klaar (%d tekens) -> Saved/ElevScan.txt"), Out.Len());
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
		UE_LOG(LogWeedShop, Display, TEXT("MapCapture: centrum=(%.0f, %.0f) ortho=%.0f topZ=%.0f bron=%s"),
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
	// BaseColor ipv FinalColorLDR: de FinalColor-capture hangt af van de SkyLight-ambient-cubemap,
	// die real-time + time-sliced is en NIET binnen de capture-frame herberekent -> 's nachts blijft
	// de kaart pikzwart (de UDS-naar-middag-push raakt die bottleneck niet). BaseColor = albedo =
	// altijd helder ongeacht tijd/zon/skylight, dus belichtings-onafhankelijk leesbaar. Het
	// M_MapDisplay-materiaal negeert de BaseColor-alpha al (zie MapWidget.cpp), dus de display-kant
	// is klaar.
	// LET OP: een oude waarschuwing zei dat pack/Nanite-materialen zwart kunnen renderen in een
	// BaseColor-capture. Dit is NIET headless te verifieren - de speler checkt overdag EN 's nachts
	// of de kaart de gebouwen/straten toont. Geeft BaseColor toch zwart? Terugval: CaptureSource
	// terug naar SCS_FinalColorLDR en de nacht-zwart aanpakken via een SkyDims-restore + dubbele
	// capture-flush (skylight 1 frame naar dag-cubemap dwingen, twee keer captured met een
	// FlushRenderingCommands ertussen zodat de recompute klaar is voor de tweede capture).
	MapCapture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
	// (Exposure-overrides blijven staan maar zijn irrelevant voor de helderheid bij BaseColor - albedo
	// wordt niet door auto-exposure aangeraakt. Onschadelijk; laten staan als terugval-vangnet.)
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
	// (BaseColor-capture is belichtings-onafhankelijk: de ApplyMapPhotoLight-push en de lampen-cull-
	// omzeiling in CaptureMapNow zijn nu irrelevant voor de helderheid. Ze blijven staan - onschadelijk
	// en handig als terugval-vangnet mocht CaptureSource ooit terug naar FinalColorLDR moeten.)
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
			UE_LOG(LogWeedShop, Log, TEXT("SplineEditorMesh-restjes verborgen: %d"), NHidden);
		}
	}
	// Altijd dezelfde rustige ochtend-belichting op de foto, ongeacht de kloktijd (middagzon
	// blies de kaart naar wit; nacht maakte hem zwart). Tick van de controller herstelt direct.
	if (ADayNightController* DN = ADayNightController::GetLocal(GetWorld()))
	{
		DN->ApplyMapPhotoLight();
	}
	// NACHT-KAART LEESBAAR: de kaart-camera staat heel hoog (TopZ) -> alle straatlampen vallen buiten hun max-draw-
	// distance (de perf-cull) = pikzwarte nacht-foto. Even de lampen-cull UIT rond de capture zodat de straatverlichting
	// in de foto komt (dag = zon, nacht = city-lights). FlushRenderingCommands: capture klaar vóór de cull-restore
	// (anders race: restore op de game-thread vóór de render-thread captured = lampen alsnog gecullt).
	IConsoleVariable* CVLamp = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LightMaxDrawDistanceScale"));
	const float OldLampScale = CVLamp ? CVLamp->GetFloat() : 1.f;
	if (CVLamp) { CVLamp->Set(1000.f, ECVF_SetByConsole); }
	MapCapture->UpdateComponentToWorld(); // transform direct flushen vóór de capture
	MapCapture->CaptureScene();
	FlushRenderingCommands();
	if (CVLamp) { CVLamp->Set(OldLampScale, ECVF_SetByConsole); }
	// (Hercapture-op-volgende-tick verwijderd: de échte 1e-open-scheefheid was de volgorde-bug in MapWidget::BuildBlocks
	//  -- CenterXY werd uit een nog niet berekende MapCenter gelezen. Dat is daar opgelost; hier geen re-capture meer nodig.)
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

bool ADoorRetrofitter::FindNearestRoadPoint(const FVector& From, FVector& Out) const
{
	// 1) Beste bron: de speler-gemarkeerde NPC-route (NpcRings) = de loop-lijn over het MIDDEN van de
	//    boulevard. Zoek het dichtstbijzijnde punt op alle (gesloten) ring-segmenten -> midden van de weg.
	float BestSq = TNumericLimits<float>::Max();
	FVector Best = FVector::ZeroVector;
	bool bGot = false;
	for (const TArray<FVector>& Ring : NpcRings)
	{
		const int32 N = Ring.Num();
		if (N < 2) { continue; }
		for (int32 i = 0; i < N; ++i)
		{
			const FVector P = FMath::ClosestPointOnSegment(From, Ring[i], Ring[(i + 1) % N]);
			const float D = FVector::DistSquared(From, P);
			if (D < BestSq) { BestSq = D; Best = P; bGot = true; }
		}
	}
	if (bGot) { Out = Best + FVector(0.f, 0.f, 30.f); return true; }

	// 2) Geen route gemarkeerd: val terug op de straat-zoeker op de eigen Y (boulevard-dek).
	return FindStreetPoint(From.Y, Out);
}

void ADoorRetrofitter::ScanAndConvert()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	++ScanPass;
	// Werk-meting voor de adaptieve cadans: groeit een van de setup-sets/tellers, dan is er iets
	// nieuws ingestreamd en verwerkt -> snel blijven scannen. Anders terugschakelen naar traag.
	const int32 _scanWork0 = Converted.Num() + ConvScanRejected.Num() + GlassScanSeen.Num()
		+ WinFixSeen.Num() + LampScanSeen.Num() + ElevScanSeen.Num() + ElevBuilt.Num();
	if (ScanPass == 1)
	{
		// Chill-plekken (ChillSpots.txt) laden: hang-plekken voor een deel van de wandelaars.
		{
			TArray<FString> CLines;
			FFileHelper::LoadFileToStringArray(CLines, *WeedData::File(TEXT("ChillSpots.txt")));
			for (const FString& CL : CLines)
			{
				TArray<FString> Pc;
				CL.ParseIntoArray(Pc, TEXT(","));
				if (Pc.Num() >= 3) { LoadedChillSpots.Add(FVector(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]))); }
			}
			if (LoadedChillSpots.Num() > 0)
			{
				UE_LOG(LogWeedShop, Display, TEXT("Chill-plekken: %d geladen"), LoadedChillSpots.Num());
			}
		}
		// Binnen-kettingen (StairsPath.txt) alvast parsen: de bewoners-wachtrij gebruikt ze om
		// te bepalen welke toren-deuren binnen mogen spawnen (en welk pad ze dan aflopen).
		{
			TArray<FString> SLines;
			FFileHelper::LoadFileToStringArray(SLines, *WeedData::File(TEXT("StairsPath.txt")));
			TArray<FVector> Chain;
			for (const FString& SL : SLines)
			{
				if (SL.TrimStartAndEnd().StartsWith(TEXT("---")))
				{
					if (Chain.Num() >= 2) { NpcChains.Add(Chain); }
					Chain.Reset();
					continue;
				}
				TArray<FString> Pc;
				SL.ParseIntoArray(Pc, TEXT(","));
				if (Pc.Num() >= 3) { Chain.Add(FVector(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]))); }
			}
			if (Chain.Num() >= 2) { NpcChains.Add(Chain); }
			// 1) Elke keten van BOVEN naar BENEDEN oriënteren (eerste punt hoogste Z), zodat het
			//    aflopen altijd richting straat gaat - ongeacht in welke richting je 'm markeerde.
			for (TArray<FVector>& Ch : NpcChains)
			{
				if (Ch.Num() >= 2 && Ch[0].Z < Ch.Last().Z)
				{
					for (int32 a = 0, b = Ch.Num() - 1; a < b; ++a, --b) { Ch.Swap(a, b); }
				}
			}
			// 2) Kettingen waarvan het eindpunt aansluit op het beginpunt van een andere aaneenknopen
			//    tot één doorlopende afdaling. Anders strandt een bewoner van de bovenste verdieping op
			//    een tussenbordes waar zijn keten ophoudt i.p.v. door te lopen tot de straat.
			{
				bool bMerged = true;
				while (bMerged)
				{
					bMerged = false;
					for (int32 i = 0; i < NpcChains.Num() && !bMerged; ++i)
					{
						for (int32 j = 0; j < NpcChains.Num(); ++j)
						{
							if (i == j) { continue; }
							if (FVector::Dist(NpcChains[i].Last(), NpcChains[j][0]) < 250.f)
							{
								NpcChains[i].Append(NpcChains[j]);
								NpcChains.RemoveAt(j);
								bMerged = true;
								break;
							}
						}
					}
				}
			}
			if (NpcChains.Num() > 0)
			{
				UE_LOG(LogWeedShop, Display, TEXT("Binnen-kettingen: %d geladen+geknoopt (entry-paden voor toren-bewoners)"), NpcChains.Num());
			}
		}
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
				TArray<FVector> Ring; // de volledige ring OP VOLGORDE (incl. tussenpunten) voor de patrouille
				const int32 NSeg = (Route.Num() >= 3) ? Route.Num() : Route.Num() - 1; // ring: laatste->eerste erbij
				for (int32 si = 0; si < NSeg; ++si)
				{
					const FVector A = Route[si];
					const FVector B = Route[(si + 1) % Route.Num()];
					PendingSpawnerPoints.Add(A);
					Ring.Add(A);
					// Tussenpunten om de ~40m zodat dekking en spawns het hele segment vullen.
					const float SegLen = FVector::Dist2D(A, B);
					const int32 NMid = FMath::FloorToInt(SegLen / 4000.f);
					for (int32 mi = 1; mi <= NMid; ++mi)
					{
						const FVector Mid = FMath::Lerp(A, B, float(mi) / float(NMid + 1));
						PendingSpawnerPoints.Add(Mid);
						Ring.Add(Mid);
					}
				}
				if (Ring.Num() >= 2) { NpcRings.Add(Ring); }
			}
			// UITDUNNEN naar ~1 spawner per 40m: greedy keep-lijst (een punt blijft als hij ver
			// genoeg van alle BEWAARDE punten ligt). De oude variant vergeleek met alle eerdere
			// originele punten en dunde een aaneengesloten keten daardoor uit tot 1 punt.
			{
				TArray<FVector> Kept;
				for (const FVector& Pt0 : PendingSpawnerPoints)
				{
					bool bNear = false;
					for (const FVector& Kp : Kept)
					{
						if (FVector::DistSquared2D(Pt0, Kp) < 4000.f * 4000.f) { bNear = true; break; }
					}
					if (!bNear) { Kept.Add(Pt0); }
				}
				PendingSpawnerPoints = Kept;
			}
			// LOOP-GRAAF bouwen: alle paden (met tussenpunten) als knopen-ketens; knopen dichter
			// dan 7m smelten samen, en knopen van VERSCHILLENDE paden binnen 16m krijgen een
			// verbindings-kant - zo haken dwarsstraten en oversteekplekken vanzelf in de lanen.
			{
				// PAD-SNAP: een tussenpunt klikt zichzelf zijwaarts op de echte pad/straat-mesh.
				// De speler zet rechte lijnen tussen markers; slingert het echte pad (park), dan
				// volgen de knopen nu de slinger in plaats van dwars over het gras te snijden.
				auto SnapToPath = [&](const FVector& P, const FVector& SegDir) -> FVector
				{
					const FVector Side = FVector::CrossProduct(SegDir.GetSafeNormal2D(), FVector::UpVector);
					static const float Offs[] = { 0.f, 150.f, -150.f, 300.f, -300.f, 450.f, -450.f, 650.f, -650.f,
						900.f, -900.f, 1200.f, -1200.f, 1500.f, -1500.f, 2000.f, -2000.f };
					// Twee rondes: eerst zoeken naar een echt PAD (ConcretePath e.d.) - pas als dat
					// nergens ligt genoegen nemen met gewone stoep/straat. Anders snapte een park-
					// slinger naar de boulevard-stoep ernaast in plaats van het witte pad.
					for (int32 Round = 0; Round < 2; ++Round)
					{
						for (float Of : Offs)
						{
							const FVector C2 = P + Side * Of;
							FHitResult H;
							if (!W->LineTraceSingleByChannel(H, C2 + FVector(0.f, 0.f, 400.f), C2 - FVector(0.f, 0.f, 400.f), ECC_Visibility)) { continue; }
							const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(H.GetComponent());
							if (!SMC || !SMC->GetStaticMesh()) { continue; }
							const FString Nm = SMC->GetStaticMesh()->GetName();
							const bool bIsPath = Nm.Contains(TEXT("ConcretePath")) || Nm.Contains(TEXT("Path")) || Nm.Contains(TEXT("Boardwalk"));
							const bool bIsStreet = Nm.Contains(TEXT("Street")) || Nm.Contains(TEXT("Sidewalk")) || Nm.Contains(TEXT("Road"))
								|| Nm.Contains(TEXT("Pavement")) || Nm.Contains(TEXT("Crosswalk"));
							if ((Round == 0 && bIsPath) || (Round == 1 && (bIsPath || bIsStreet)))
							{
								return FVector(C2.X, C2.Y, H.ImpactPoint.Z + 10.f);
							}
						}
					}
					return P; // niets gevonden (bv. nog niet gestreamd): originele lijn aanhouden
				};
				TArray<int32> NodeBlock; // welk pad een knoop maakte (voor de junction-kanten)
				auto GetNode = [&](const FVector& P, int32 BlockId) -> int32
				{
					for (int32 ni = 0; ni < GraphNodes.Num(); ++ni)
					{
						if (FVector::DistSquared2D(GraphNodes[ni], P) < 700.f * 700.f) { return ni; }
					}
					GraphNodes.Add(P);
					NodeBlock.Add(BlockId);
					GraphAdj.AddDefaulted();
					return GraphNodes.Num() - 1;
				};
				auto AddEdge = [&](int32 A2, int32 B2)
				{
					if (A2 == B2) { return; }
					GraphAdj[A2].AddUnique(B2);
					GraphAdj[B2].AddUnique(A2);
				};
				int32 BlockId = 0;
				for (const TArray<FVector>& Route : Routes)
				{
					const int32 NSeg = (Route.Num() >= 3) ? Route.Num() : Route.Num() - 1;
					int32 PrevNode = -1;
					int32 FirstNode = -1;
					for (int32 si = 0; si < NSeg; ++si)
					{
						const FVector A2 = Route[si];
						const FVector B2 = Route[(si + 1) % Route.Num()];
						const int32 NA = GetNode(A2, BlockId);
						if (FirstNode < 0) { FirstNode = NA; }
						if (PrevNode >= 0) { AddEdge(PrevNode, NA); }
						PrevNode = NA;
						const float SegLen = FVector::Dist2D(A2, B2);
						const int32 NMid = FMath::FloorToInt(SegLen / 2500.f); // dichter op elkaar: bochten volgen strakker
						for (int32 mi = 1; mi <= NMid; ++mi)
						{
							const FVector RawMid = FMath::Lerp(A2, B2, float(mi) / float(NMid + 1));
							const int32 NM = GetNode(SnapToPath(RawMid, B2 - A2), BlockId);
							AddEdge(PrevNode, NM);
							PrevNode = NM;
						}
					}
					if (Route.Num() >= 3 && PrevNode >= 0 && FirstNode >= 0)
					{
						const int32 NL2 = GetNode(Route[NSeg % Route.Num()], BlockId);
						AddEdge(PrevNode, NL2);
						AddEdge(NL2, FirstNode);
					}
					else if (Route.Num() == 2 && PrevNode >= 0)
					{
						AddEdge(PrevNode, GetNode(Route[1], BlockId));
					}
					++BlockId;
				}
				// Junction-kanten tussen knopen van verschillende paden die vlak bij elkaar liggen.
				for (int32 a2 = 0; a2 < GraphNodes.Num(); ++a2)
				{
					for (int32 b2 = a2 + 1; b2 < GraphNodes.Num(); ++b2)
					{
						if (NodeBlock[a2] == NodeBlock[b2]) { continue; }
						if (FVector::DistSquared2D(GraphNodes[a2], GraphNodes[b2]) < 1600.f * 1600.f)
						{
							AddEdge(a2, b2);
						}
					}
				}
				UE_LOG(LogWeedShop, Display, TEXT("Loop-graaf: %d knopen uit %d paden"), GraphNodes.Num(), Routes.Num());
				// CROWD SEEDEN: 70 virtuele wandelaars verspreid over de hele graaf - de stad is
				// daarmee vanaf seconde 1 overal "bevolkt", lichamen volgen waar de speler komt.
				if (GraphNodes.Num() >= 2)
				{
					// MAIN STRIP = de oost-zone (X > -1500: boulevard + beide stoepen van de grote
					// weg). 70% van de crowd is "strip-volk" en wordt daar geseed; de rest zwerft.
					TArray<int32> StripNodes;
					TArray<int32> AllNodes;
					for (int32 ni = 0; ni < GraphNodes.Num(); ++ni)
					{
						AllNodes.Add(ni);
						if (GraphNodes[ni].X > -1500.f) { StripNodes.Add(ni); }
					}
					// SCHUDDEN en rond-verdelen: ieder z'n eigen knoop (pas hergebruik als alle
					// knopen op zijn) - geen start-hoopjes meer op dezelfde tegel.
					auto Shuffle = [](TArray<int32>& Arr)
					{
						for (int32 i = Arr.Num() - 1; i > 0; --i)
						{
							Arr.Swap(i, FMath::RandRange(0, i));
						}
					};
					Shuffle(StripNodes);
					Shuffle(AllNodes);
					int32 StripCursor = 0, AllCursor = 0;
					// CO-OP: ALLEEN de server bouwt de virtuele crowd op. De joiner krijgt de fysieke bodies
					// via replicatie (ACustomerBase repliceert nu) - zou de client zelf ook seeden, dan kreeg je
					// twee losse crowds (dubbele/spook-NPC's). De rest van ScanAndConvert (deuren/kamers/glas/
					// lampen) blijft bewust WEL client-lokaal draaien; alleen dit crowd-seed-deel is server-only.
					for (int32 ci = 0; !IsNetMode(NM_Client) && ci < 70; ++ci) // PERSISTENT: alle wandelaars worden 1x een blijvend lichaam
					{
						FVirtualWalker V;
						V.bStripLover = (ci < 49) && StripNodes.Num() > 0;
						if (V.bStripLover)
						{
							V.NextIdx = StripNodes[StripCursor % StripNodes.Num()];
							++StripCursor;
						}
						else
						{
							V.NextIdx = AllNodes.Num() > 0 ? AllNodes[AllCursor % AllNodes.Num()] : 0;
							++AllCursor;
						}
						V.Pos = GraphNodes[V.NextIdx];
						if (GraphAdj[V.NextIdx].Num() > 0)
						{
							V.PrevIdx = V.NextIdx;
							V.NextIdx = GraphAdj[V.NextIdx][FMath::RandRange(0, GraphAdj[V.NextIdx].Num() - 1)];
							// Start ergens ONDERWEG naar de volgende knoop: ook bij knoop-hergebruik
							// staat iedereen dan op een eigen plek langs de lijn.
							V.Pos = FMath::Lerp(V.Pos, GraphNodes[V.NextIdx], FMath::FRandRange(0.1f, 0.9f));
						}
						Crowd.Add(V);
					}
					UE_LOG(LogWeedShop, Display, TEXT("Virtuele crowd: %d wandelaars gespreid geseed (%d strip-vast over %d strip-knopen)"), Crowd.Num(), 105, StripNodes.Num());
				}
			}
			if (PendingSpawnerPoints.Num() > 0)
			{
				// Klanten-budget: doel ~70 NPC's totaal, verdeeld over de spawn-punten.
				RouteCustomersPerPoint = FMath::Clamp(FMath::RoundToInt(70.f / float(PendingSpawnerPoints.Num())), 1, 6);
				UE_LOG(LogWeedShop, Display, TEXT("NPC-routes: %d paden -> %d spawn-punten (%d klanten per punt, doel ~70)"), Routes.Num(), PendingSpawnerPoints.Num(), RouteCustomersPerPoint);
			}
			else
			{
				PendingSpawnerYs = { -20000.f, -8000.f, 0.f, 12000.f, 24000.f };
			}
		}
	}
	// Route-punten van de speler: direct plaatsen (positie is al ground-truth op de stoep).
	// CO-OP: de patrouille-spawners bewegen server-side de NPC-bodies (die repliceren) -> alleen de server
	// maakt ze aan. De client verwerkt de rij niet (draait de bodies niet en zou 'm anders leegpompen).
	for (int32 i = PendingSpawnerPoints.Num() - 1; !IsNetMode(NM_Client) && i >= 0; --i)
	{
		const FVector Pt = PendingSpawnerPoints[i] + FVector(0.f, 0.f, 60.f);
		ACustomerSpawner* CSr = W->SpawnActorDeferred<ACustomerSpawner>(ACustomerSpawner::StaticClass(), FTransform(Pt));
		if (!CSr) { continue; }
		CSr->bSpawnResidents = false;
		CSr->MaxCustomers = 0; // lichamen komen uit de virtuele crowd; spawner = patrouille-aansturing
		CSr->SpotRadius = 500.f;
		CSr->ActivationRange = 0.f; // altijd vullen: de straat-trace weigert niet-gestreamde grond vanzelf
		CSr->ChillSpots = LoadedChillSpots;
		// De volledige loop-graaf meegeven: wandelaars zwerven over het hele netwerk.
		CSr->NetNodes = GraphNodes;
		CSr->NetAdj = GraphAdj;
		CSr->FinishSpawning(FTransform(Pt));
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
		{
			NavSys->RegisterNavigationInvoker(CSr, 9000.f, 11000.f);
		}
		UE_LOG(LogWeedShop, Verbose, TEXT("Pack-map: route-spawner op (%.0f, %.0f, %.0f)"), Pt.X, Pt.Y, Pt.Z); // Verbose: 37x bij opstart, samenvatting = de NPC-routes-regel
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
	// CO-OP: server-only (de spawners sturen de replicerende NPC-bodies aan; de client krijgt ze via replicatie).
	if (!IsNetMode(NM_Client) && PendingSpawnerYs.Num() > 0)
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
			CSw->ActivationRange = 0.f; // altijd vullen: de straat-trace weigert niet-gestreamde grond vanzelf
			CSw->FinishSpawning(FTransform(Street));
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
			{
				NavSys->RegisterNavigationInvoker(CSw, 9000.f, 11000.f);
			}
			UE_LOG(LogWeedShop, Log, TEXT("Pack-map: klanten-spawner + nav-invoker op (%.0f, %.0f, %.0f)"), Street.X, Street.Y, Street.Z);
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
		// PERF: deur-registry (O(deuren)) i.p.v. TActorIterator over alle actors - zelfde set.
		// De registry is per-proces, dus wél op wereld filteren (PIE/co-op-in-1-proces).
		for (const TWeakObjectPtr<ACityDoor>& WD : ACityDoor::GetAll())
		{
			ACityDoor* D = WD.Get();
			if (!IsValid(D) || D->GetWorld() != W) { continue; }
			if (D->ActorHasTag(TEXT("AptDoor"))) { Apt.Add(D); }
			else if (D->ActorHasTag(TEXT("BalcDoor"))) { Balc.Add(D); }
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
					// Starter -> speler-huis. Door competitive geclaimde deuren (bCompForced) NIET aanraken: de
					// B-block in TickCompetitiveRooms regelt die als jouw deur / partner-deur. ALLE andere deuren -
					// ook vlakbij de competitive-kamers - krijgen gewoon een NPC-bewoner (anders is half het gebouw
					// "van een speler"). bCompForced houdt SetResident tegen, dus geen geflikker op de 2 echte deuren.
					if (D == Starter) { D->SetPlayerHome(); continue; }
					if (D->IsCompForced()) { continue; }
					// Bewoner-naam stabiel op POSITIE (niet op lijst-volgorde): zelfde naam per deur, elke sessie.
					// ResidentNameForDoor kiest de gender-correcte naam (voorspelde skin van Resident_<idx>) zodat
					// bordje en rondlopende bewoner nooit verschillen.
					const FVector L = D->GetActorLocation();
					const int32 NameIdx = FMath::Abs(FMath::RoundToInt(L.X * 0.13f) + FMath::RoundToInt(L.Y * 0.31f) + FMath::RoundToInt(L.Z * 0.77f));
					D->SetResident(ACityDoor::ResidentNameForDoor(W, NameIdx));
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
				Bd->SetResident(ACityDoor::ResidentNameForDoor(W, NameIdx));
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
					// Op NpcId tellen (IsResidentWalker): IsResident() is altijd false sinds de vereenvoudiging,
					// dus NLive stond vast op 0 en de LiteCap hield geen rekening met al-levende bewoners.
					if (IsValid(*CIt) && CIt->IsResidentWalker()) { ++NLive; }
				}
				const int32 LiteCap = 12;
				int32 NNew = 0;
				// TOREN-woningen (bij het starter-appartement): die hebben WEL een route naar
				// beneden - hun bewoners mogen binnen spawnen en zelf naar buiten lopen. Daarvoor
				// tellen ook de hogere verdiepingen mee, niet alleen Apt 1xx.
				const FVector TowerXY = Starter->GetActorLocation();
				for (ACityDoor* A : Apt)
				{
					if (A == Starter || Ground.Contains(A)) { continue; }
					if (FVector::Dist2D(A->GetActorLocation(), TowerXY) < 4000.f) { Ground.Add(A); }
				}
				// Toren-woningen EERST: anders is de cap al vol met strip-woningen voordat er ook
				// maar een bewoner binnen in de toren gespawnd is.
				Ground.Sort([&TowerXY](const ACityDoor& A, const ACityDoor& B)
				{
					const bool bTa = FVector::Dist2D(A.GetActorLocation(), TowerXY) < 4000.f;
					const bool bTb = FVector::Dist2D(B.GetActorLocation(), TowerXY) < 4000.f;
					if (bTa != bTb) { return bTa; }
					return A.GetActorLocation().X < B.GetActorLocation().X;
				});
				int32 NQueued = 0;
				for (ACityDoor* A : Ground)
				{
					if (NLive + PendingResidents.Num() >= LiteCap) { break; }
					if (A->ActorHasTag(TEXT("ResidentNpc"))) { continue; }
					FPendingResident PR;
					PR.Door = A;
					// BINNEN (= in de HAL bij de eigen deur, nooit in een kamer) spawnen alleen als
					// er een speler-gemarkeerde ketting in de buurt van deze deur begint (zelfde
					// verdieping-band): die ketting is dan hun looppad naar buiten. Geen ketting =
					// op straat spawnen.
					PR.bInside = false;
					const FVector DLq = A->GetActorLocation();
					// Binnen spawnen als ENIG punt op een keten op dezelfde verdieping vlakbij deze deur ligt
					// (na de merge is Ch[0] de top - alleen dat checken zou alle lagere verdiepingen missen).
					for (const TArray<FVector>& Ch : NpcChains)
					{
						for (const FVector& CP : Ch)
						{
							if (FVector::Dist2D(CP, DLq) < 2000.f && FMath::Abs(CP.Z - DLq.Z) < 280.f)
							{
								PR.bInside = true;
								break;
							}
						}
						if (PR.bInside) { break; }
					}
					A->Tags.Add(TEXT("ResidentNpc"));
					PendingResidents.Add(PR);
					++NQueued;
				}
				if (NQueued > 0)
				{
					UE_LOG(LogWeedShop, Verbose, TEXT("Bewoners-lite: %d bewoners in de wachtrij (totaal %d/%d), verschijnen gespreid"), NQueued, NLive + PendingResidents.Num(), LiteCap);
				}
			}
			const FVector Top = Starter->GetActorLocation();
			const int32 WonSig = (Apt.Num() - 1) * 1000 + NBalcLocked; // log alleen bij echte deur/slot-verandering, niet elke bewoner-churn
			if (WonSig != LastWoningenSig)
			{
				LastWoningenSig = WonSig;
				UE_LOG(LogWeedShop, Display, TEXT("Woningen: %d voordeuren + %d schuifpuien op slot (bewoners) in %d gebouwen, starter-huis = Apt %d op (%.0f, %.0f, %.0f)"), Apt.Num() - 1, NBalcLocked, Buildings.Num(), Starter->GetAptNumber(), Top.X, Top.Y, Top.Z);
			}
		}
	}

	// BINNEN-LOOPPADEN (StairsPath.txt): door de speler gemarkeerde kettingen - elk opeenvolgend
	// paar markers wordt een smart-link. Zo tekent de speler letterlijk hoe NPC's door een
	// gebouw (trappenhuis) naar buiten lopen. PAS NA het toren-navmesh-anker laden (de navmesh
	// moet er echt zijn, anders zijn de al-beloopbaar-testen onzin) en op 5 min hertesten.
	if ((ScanPass == 60 || ScanPass == 150) && bTowerInvokerPlaced)
	{
		TArray<FString> SLines;
		FFileHelper::LoadFileToStringArray(SLines, *WeedData::File(TEXT("StairsPath.txt")));
		TArray<FVector> Chain;
		int32 NLinks = 0;
		UNavigationSystemV1* NavC = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
		auto FlushChain = [&]()
		{
			for (int32 ci = 0; ci + 1 < Chain.Num(); ++ci)
			{
				bool bNear = false;
				for (const FVector& PL : PlacedNavLinks)
				{
					if (FVector::Dist(PL, Chain[ci]) < 100.f) { bNear = true; break; }
				}
				if (bNear) { continue; }
				// Alleen een link leggen waar de navmesh het NIET al zelf kan: kan je er gewoon
				// heen lopen (volledig pad, niet absurd omlopen), dan is een link overbodig - en
				// een onnodige link laat NPC's in een rechte lijn (door muren) lopen.
				if (NavC)
				{
					UNavigationPath* PathC = NavC->FindPathToLocationSynchronously(W, Chain[ci] + FVector(0.f, 0.f, 50.f), Chain[ci + 1] + FVector(0.f, 0.f, 50.f), nullptr);
					const float Direct = FVector::Dist(Chain[ci], Chain[ci + 1]);
					if (PathC && PathC->IsValid() && !PathC->IsPartial() && PathC->GetPathLength() < FMath::Max(600.f, Direct * 2.5f))
					{
						continue; // navmesh kan dit stuk al zelf
					}
				}
				const FTransform LTM(Chain[ci]);
				if (ANavLinkProxy* Lnk = W->SpawnActorDeferred<ANavLinkProxy>(ANavLinkProxy::StaticClass(), LTM))
				{
					Lnk->PointLinks.Empty();
					Lnk->bSmartLinkIsRelevant = true;
					if (UNavLinkCustomComponent* Smart = Lnk->GetSmartLinkComp())
					{
						Smart->SetLinkData(FVector::ZeroVector, Chain[ci + 1] - Chain[ci], ENavLinkDirection::BothWays);
						Smart->SetEnabled(true);
					}
					Lnk->FinishSpawning(LTM);
					Lnk->SetSmartLinkEnabled(true);
					PlacedNavLinks.Add(Chain[ci]);
					++NLinks;
				}
			}
			Chain.Reset();
		};
		for (const FString& SL : SLines)
		{
			if (SL.TrimStartAndEnd().StartsWith(TEXT("---"))) { FlushChain(); continue; }
			TArray<FString> Pc;
			SL.ParseIntoArray(Pc, TEXT(","));
			if (Pc.Num() >= 3) { Chain.Add(FVector(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]))); }
		}
		FlushChain();
		if (NLinks > 0)
		{
			UE_LOG(LogWeedShop, Display, TEXT("Binnen-looppaden: %d smart-links gelegd uit StairsPath.txt"), NLinks);
		}
	}

	// NAVMESH IN DE TOREN: alle nav-invokers staan langs de route (kilometers verderop), dus de
	// toren zelf had geen navmesh - bewoners konden geen stap zetten en teleporteerden terug naar
	// huis. Een vast anker midden in de toren dekt alle verdiepingen plus het trappenhuis.
	if (!bTowerInvokerPlaced && StarterDoor.IsValid())
	{
		bTowerInvokerPlaced = true;
		const FVector TLoc = StarterDoor->GetActorLocation();
		const FVector AnchorLoc(TLoc.X, TLoc.Y, FMath::Max(200.f, TLoc.Z - 1000.f));
		if (AActor* Anchor = W->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), FTransform(AnchorLoc)))
		{
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
			{
				NavSys->RegisterNavigationInvoker(Anchor, 9000.f, 11000.f);
			}
			UE_LOG(LogWeedShop, Display, TEXT("Toren-navmesh anker op (%.0f, %.0f, %.0f)"), AnchorLoc.X, AnchorLoc.Y, AnchorLoc.Z);
		}
	}

	// "LIFT NEMEN": bewoners op een verdieping zonder loop-pad naar beneden (de toren heeft geen
	// doorlopende trap-navmesh; de lift kunnen NPC's niet bedienen) blijven anders eeuwig voor
	// hun deur staan. Wie daar ~10s stilstaat wordt - ALLEEN buiten zicht van de speler - naar
	// een ver route-punt beneden verplaatst en wandelt vandaar gewoon naar huis: alsof hij de
	// lift heeft genomen terwijl jij even niet keek.
	{
		TArray<FVector> StreetPts;
		// PERF: spawner-registry (O(spawners)) i.p.v. TActorIterator over alle actors - zelfde set.
		// De registry is per-proces, dus wél op wereld filteren (PIE/co-op-in-1-proces).
		for (const TWeakObjectPtr<ACustomerSpawner>& WSp : ACustomerSpawner::GetAll())
		{
			ACustomerSpawner* Sp = WSp.Get();
			if (IsValid(Sp) && Sp->GetWorld() == W) { StreetPts.Add(Sp->GetActorLocation()); }
		}
		if (StreetPts.Num() > 0)
		{
			const float Now = W->GetRealTimeSeconds();
			// Ver route-punt kiezen (zelfde regel als bewoner-spawns: ver van de speler) - gedeeld
			// door de lift-redding hieronder en de kamer-redding daarna.
			auto PickFarRoutePoint = [&](const FVector& FromL) -> FVector
			{
				FVector Dest = StreetPts[0];
				float BestScore = -TNumericLimits<float>::Max();
				for (const TArray<FVector>& Ring : NpcRings)
				{
					for (const FVector& RP2 : Ring)
					{
						float MinPlayer = 99999.f;
						for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
						{
							const APawn* Pp = PIt->Get() ? PIt->Get()->GetPawn() : nullptr;
							if (Pp) { MinPlayer = FMath::Min(MinPlayer, FVector::Dist2D(Pp->GetActorLocation(), RP2)); }
						}
						const float Score = FMath::Min(MinPlayer, 12000.f) - FVector::Dist2D(RP2, FromL) * 0.15f;
						if (Score > BestScore) { BestScore = Score; Dest = RP2; }
					}
				}
				return Dest;
			};
			// PERF: klant-registry (O(NPC's)) i.p.v. TActorIterator over alle actors - zelfde set
			// (crowd-bodies zijn ook ACustomerBase). Per-proces registry -> op wereld filteren (PIE).
			for (const TWeakObjectPtr<ACustomerBase>& WCb : ACustomerBase::GetAll())
			{
				ACustomerBase* Cb = WCb.Get();
				if (Cb && Cb->GetWorld() != W) { continue; }
				// Op NpcId checken (IsResidentWalker): met IsResident() was deze redding dood (bResident wordt
				// sinds de vereenvoudiging nooit gezet) -> boven-vastgelopen bewoners namen nooit de lift en
				// bleven in de hal/het trappenhuis staan tot de despawn. Afspraak-NPC's (bewust wachtend op
				// hun plek, ook met Resident_-id) NOOIT wegteleporteren.
				if (!IsValid(Cb) || !Cb->IsResidentWalker() || Cb->IsHidden() || Cb->HasActiveAppointment()) { continue; }
				const FVector L = Cb->GetActorLocation();
				// Straat-niveau hier = dichtstbijzijnd spawn-punt; ruim erboven = op een verdieping.
				float NearZ = L.Z;
				float BestD = TNumericLimits<float>::Max();
				for (const FVector& SP2 : StreetPts)
				{
					const float Dd = FVector::DistSquared2D(SP2, L);
					if (Dd < BestD) { BestD = Dd; NearZ = SP2.Z; }
				}
				const bool bUpstairs = L.Z > NearZ + 300.f;
				const bool bStill = Cb->GetVelocity().SizeSquared2D() < 25.f;
				if (!bUpstairs || !bStill)
				{
					ResidentStuckSince.Remove(Cb);
					continue;
				}
				float& Since = ResidentStuckSince.FindOrAdd(Cb, Now);
				if (Now - Since < 10.f) { continue; }
				// Buiten zicht? Geen speler dichtbij, en niet in iemands kijkrichting.
				bool bUnseen = true;
				for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
				{
					const APlayerController* PC = PIt->Get();
					const APawn* Pp = PC ? PC->GetPawn() : nullptr;
					if (!Pp) { continue; }
					const FVector To = L - Pp->GetActorLocation();
					if (To.Size() < 1500.f) { bUnseen = false; break; }
					if (To.Size() < 7000.f && FVector::DotProduct(PC->GetControlRotation().Vector(), To.GetSafeNormal()) > 0.05f) { bUnseen = false; break; }
				}
				if (!bUnseen) { continue; }
				// Ver route-punt kiezen (zelfde regel als bewoner-spawns: ver van de speler).
				const FVector Dest = PickFarRoutePoint(L);
				Cb->SetActorLocation(Dest + FVector(0.f, 0.f, 110.f), false, nullptr, ETeleportType::TeleportPhysics);
				// De patrouille-staat bij z'n spawner mee-resetten: anders liep hij vanaf de straat z'n oude
				// entry-ketting (boven in het gebouw) weer terug op i.p.v. gewoon verder te patrouilleren.
				// PERF: spawner-registry (O(spawners)) i.p.v. TActorIterator over alle actors - zelfde set.
				// De registry is per-proces, dus wel op wereld filteren (PIE/co-op-in-1-proces).
				for (const TWeakObjectPtr<ACustomerSpawner>& WSp2 : ACustomerSpawner::GetAll())
				{
					ACustomerSpawner* Sp2 = WSp2.Get();
					if (IsValid(Sp2) && Sp2->GetWorld() == W) { Sp2->NotifyWalkerTeleported(Cb); }
				}
				ResidentStuckSince.Remove(Cb);
				UE_LOG(LogWeedShop, Verbose, TEXT("Bewoner %s nam de lift: van Z %.0f naar de straat (%.0f, %.0f)"), *Cb->NpcId.ToString(), L.Z, Dest.X, Dest.Y);
			}

			// KAMER-REDDING (werkt OOK terwijl de speler kijkt): een bewoner-wandelaar die BINNEN
			// een woon-kamer staat (oude data, nav-glitch, deur-doorglipper) hoort daar nooit - in
			// geen enkele kamer, ook niet "z'n eigen". Na ~5s binnen (geen vals alarm bij een
			// deur-passage) loopt hij met een DIRECT-WALK (zelfde waypoint-techniek als het
			// entry-pad in de spawner) naar de eigen deur en de deur-lijn over de hal in - lopen
			// is geen pop, dus dit mag gewoon in beeld. Staat hij na ~20s NOG binnen (walk
			// geblokkeerd of geen deur), dan vuurt de teleport naar de straat ONGEACHT of de
			// speler kijkt: een woon-kamer is nooit een legitieme NPC-plek. De unseen-teleport
			// hierboven blijft de gewone off-screen redding. Afspraak-NPC's (bewust wachtend, in
			// de HAL bij hun deur) blijven met rust - de hal ligt buiten de kamer-boxen.
			for (auto MapIt = InRoomSince.CreateIterator(); MapIt; ++MapIt)
			{
				if (!MapIt.Key().IsValid()) { MapIt.RemoveCurrent(); } // gedespawnde wandelaars opruimen
			}
			// PERF: klant-registry (O(NPC's)) i.p.v. TActorIterator over alle actors - zelfde set.
			// Per-proces registry -> op wereld filteren (PIE/co-op-in-1-proces).
			for (const TWeakObjectPtr<ACustomerBase>& WRb : ACustomerBase::GetAll())
			{
				ACustomerBase* Cb = WRb.Get();
				if (!IsValid(Cb) || Cb->GetWorld() != W || !Cb->IsResidentWalker() || Cb->IsHidden() || Cb->HasActiveAppointment()) { continue; }
				const FVector L = Cb->GetActorLocation();
				if (!IsInsideHomeRoom(L))
				{
					InRoomSince.Remove(Cb);
					RoomWalkIssued.Remove(Cb);
					continue;
				}
				const float Since = InRoomSince.FindOrAdd(Cb, Now);
				const float InFor = Now - Since;
				// SPELERS-woning (starter/gekocht/competitive) = nul-tolerantie: NIET 5s wachten en NIET
				// zichtbaar naar buiten lopen (speler-besluit) — DIRECT naar de straat teleporteren,
				// zicht of geen zicht. Valt door naar het teleport-blok onderaan.
				const bool bPlayerRoom = IsInsidePlayerHome(L);
				if (!bPlayerRoom && InFor < 5.f) { continue; } // korte deur-passage (niet-spelers-kamer): nog geen redding
				ACityDoor* OwnDoor = bPlayerRoom ? nullptr : FindNearestDoor(L);
				if (OwnDoor && InFor < 20.f)
				{
					if (!RoomWalkIssued.Contains(Cb))
					{
						// Route: naar de eigen deur (NPC-auto-open) en dan de deur-lijn over, de hal in.
						const FVector DLoc = OwnDoor->GetActorLocation();
						FVector OutDir = DLoc - L; OutDir.Z = 0.f; OutDir = OutDir.GetSafeNormal();
						if (OutDir.IsNearlyZero()) { OutDir = OwnDoor->GetActorForwardVector().GetSafeNormal2D(); }
						FVector Past = DLoc + OutDir * 260.f;
						FHitResult EscFloor;
						FCollisionQueryParams EQ(SCENE_QUERY_STAT(RoomEscape), false);
						if (W->LineTraceSingleByChannel(EscFloor, Past + FVector(0.f, 0.f, 150.f), Past - FVector(0.f, 0.f, 450.f), ECC_WorldStatic, EQ))
						{
							Past.Z = EscFloor.ImpactPoint.Z + 5.f;
						}
						if (!IsInsideHomeRoom(Past, 0.f)) // eindpunt moet echt BUITEN de kamer liggen
						{
							TArray<FVector> Escape;
							Escape.Add(DLoc);
							Escape.Add(Past);
							// Bij de eigen spawner het entry-pad vervangen; kent geen spawner 'm, dan
							// adopteert de dichtstbijzijnde 'm (zonder ReturnPath - geen kringloop terug de kamer in).
							bool bIssued = false;
							ACustomerSpawner* NearSp = nullptr; float BestSd = TNumericLimits<float>::Max();
							for (TActorIterator<ACustomerSpawner> SpIt3(W); SpIt3; ++SpIt3)
							{
								if (!IsValid(*SpIt3)) { continue; }
								if (!bIssued && SpIt3->ForceEntryPath(Cb, Escape)) { bIssued = true; }
								const float Dd = FVector::DistSquared2D(SpIt3->GetActorLocation(), L);
								if (Dd < BestSd) { BestSd = Dd; NearSp = *SpIt3; }
							}
							if (!bIssued && NearSp) { bIssued = NearSp->ForceEntryPath(Cb, Escape, true); }
							if (bIssued)
							{
								RoomWalkIssued.Add(Cb);
								UE_LOG(LogWeedShop, Verbose, TEXT("Kamer-redding: %s loopt de kamer uit via de deur op (%.0f, %.0f, %.0f)"), *Cb->NpcId.ToString(), DLoc.X, DLoc.Y, DLoc.Z);
							}
						}
					}
					continue; // de walk de tijd geven (tot de 20s-teleport hieronder)
				}
				// Walk-route faalde (of geen deur binnen bereik): teleport naar een ver straat-punt,
				// ook in zicht - liever een enkele pop dan een blijvende indringer in de kamer.
				const FVector Dest = PickFarRoutePoint(L);
				Cb->SetActorLocation(Dest + FVector(0.f, 0.f, 110.f), false, nullptr, ETeleportType::TeleportPhysics);
				for (TActorIterator<ACustomerSpawner> SpIt4(W); SpIt4; ++SpIt4)
				{
					if (IsValid(*SpIt4)) { SpIt4->NotifyWalkerTeleported(Cb); }
				}
				InRoomSince.Remove(Cb);
				RoomWalkIssued.Remove(Cb);
				ResidentStuckSince.Remove(Cb);
				UE_LOG(LogWeedShop, Log, TEXT("Kamer-redding: %s uit een woon-kamer (Z %.0f) naar de straat (%.0f, %.0f) geteleporteerd"), *Cb->NpcId.ToString(), L.Z, Dest.X, Dest.Y);
			}
		}
	}

	// DIAGNOSE TRAP: bestaat er een trap in de toren, ligt er navmesh op, en is er een pad van
	// een verdiepingsgang naar de straat? Eenmalig zodra de toren-navmesh even heeft kunnen bouwen.
	if ((ScanPass == 30 || ScanPass == 90 || ScanPass == 180) && StarterDoor.IsValid())
	{
		const FVector TLoc = StarterDoor->GetActorLocation();
		FString Out;
		// 1) Alle trap-achtige meshes binnen 3500 van de toren (alle hoogtes).
		for (TObjectIterator<UStaticMeshComponent> SIt; SIt; ++SIt)
		{
			UStaticMeshComponent* C = *SIt;
			if (!C || C->GetWorld() != W || !C->GetStaticMesh()) { continue; }
			const FString Nm = C->GetStaticMesh()->GetName();
			if (!(Nm.Contains(TEXT("Stair")) || Nm.Contains(TEXT("Step")) || Nm.Contains(TEXT("Ladder")) || Nm.Contains(TEXT("Ramp")))) { continue; }
			const FVector O = C->Bounds.Origin;
			if (FVector::Dist2D(O, TLoc) > 3500.f) { continue; }
			Out += FString::Printf(TEXT("TRAP %s|%.0f,%.0f,%.0f|ext=%.0f,%.0f,%.0f|vis=%d|col=%d\n"),
				*Nm, O.X, O.Y, O.Z, C->Bounds.BoxExtent.X, C->Bounds.BoxExtent.Y, C->Bounds.BoxExtent.Z,
				(C->IsVisible() && C->GetOwner() && !C->GetOwner()->IsHidden()) ? 1 : 0,
				C->GetCollisionEnabled() != ECollisionEnabled::NoCollision ? 1 : 0);
		}
		// 2) Navmesh-dekking: projectie op een raster rond de toren op meerdere verdiepings-hoogtes.
		if (UNavigationSystemV1* NavD = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
		{
			// VASTE reparatie-links (uit de meting van deze toren):
			// (A) verdieping 1 -> trap-voet: de onderste trap-vlucht zat niet in de navmesh,
			//     waardoor NPC's een verdieping te hoog naar buiten gingen. De link loopt over
			//     de trap heen, dus visueel lopen ze 'm gewoon af.
			// (B) stoep-rand -> route: het pad vanaf de trap-voet strandde op een opstapje van
			//     1,5m vlak voor het route-punt.
			auto PlaceLink = [&](const FVector& From, const FVector& To)
			{
				for (const FVector& PL : PlacedNavLinks)
				{
					if (FVector::Dist(PL, From) < 300.f) { return; }
				}
				// SMART LINK: simpele PointLinks registreren bij runtime-spawn niet (hermeting
				// bleef PARTIEEL); de smart-link component is de runtime-betrouwbare variant.
				const FTransform LinkTM(From);
				if (ANavLinkProxy* Lnk = W->SpawnActorDeferred<ANavLinkProxy>(ANavLinkProxy::StaticClass(), LinkTM))
				{
					Lnk->PointLinks.Empty();
					Lnk->bSmartLinkIsRelevant = true;
					if (UNavLinkCustomComponent* Smart = Lnk->GetSmartLinkComp())
					{
						Smart->SetLinkData(FVector::ZeroVector, To - From, ENavLinkDirection::BothWays);
						Smart->SetEnabled(true);
					}
					Lnk->FinishSpawning(LinkTM);
					Lnk->SetSmartLinkEnabled(true);
					PlacedNavLinks.Add(From);
					Out += FString::Printf(TEXT("NAVLINK smart: (%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f)\n"), From.X, From.Y, From.Z, To.X, To.Y, To.Z);
				}
			};
			PlaceLink(FVector(-3111.f, -2086.f, 490.f), FVector(-3014.f, -1976.f, 80.f)); // (A) verdieping 1 -> trap-voet
			PlaceLink(FVector(-988.f, -3135.f, 15.f), FVector(-1075.f, -3138.f, 165.f));  // (B) stoep-rand -> route
			const float Heights[6] = { 540.f, 920.f, 1270.f, 1620.f, 1970.f, 2580.f };
			for (float Hz : Heights)
			{
				int32 NProj = 0;
				for (int32 gx = -3; gx <= 3; ++gx)
				{
					for (int32 gy = -3; gy <= 3; ++gy)
					{
						FNavLocation NL;
						if (NavD->ProjectPointToNavigation(FVector(TLoc.X + gx * 400.f, TLoc.Y + gy * 400.f, Hz), NL, FVector(200.f, 200.f, 150.f))) { ++NProj; }
					}
				}
				Out += FString::Printf(TEXT("NAVMESH Z=%.0f: %d/49 raster-punten geprojecteerd\n"), Hz, NProj);
			}
			// 3) Pad-test: van het gang-punt voor de starter-deur naar het dichtstbijzijnde straat-punt.
			FVector StreetP = FVector::ZeroVector;
			float BestD2 = TNumericLimits<float>::Max();
			for (TActorIterator<ACustomerSpawner> SpIt(W); SpIt; ++SpIt)
			{
				if (!IsValid(*SpIt)) { continue; }
				const float Dd = FVector::DistSquared2D(SpIt->GetActorLocation(), TLoc);
				if (Dd < BestD2) { BestD2 = Dd; StreetP = SpIt->GetActorLocation(); }
			}
			if (BestD2 < TNumericLimits<float>::Max())
			{
				UNavigationPath* Path = NavD->FindPathToLocationSynchronously(W, TLoc + FVector(0.f, 0.f, 50.f), StreetP, nullptr);
				Out += FString::Printf(TEXT("PADTEST gang(Z=%.0f) -> straat(%.0f, %.0f, %.0f): %s (%d punten, %.0f lang)\n"),
					TLoc.Z, StreetP.X, StreetP.Y, StreetP.Z,
					Path ? (Path->IsValid() ? (Path->IsPartial() ? TEXT("PARTIEEL") : TEXT("VOLLEDIG")) : TEXT("GEEN")) : TEXT("NULL"),
					Path ? Path->PathPoints.Num() : 0,
					Path ? Path->GetPathLength() : 0.f);
				if (Path && Path->PathPoints.Num() > 0)
				{
					const FVector End = Path->PathPoints.Last();
					Out += FString::Printf(TEXT("PADTEST eindigt op (%.0f, %.0f, %.0f)\n"), End.X, End.Y, End.Z);
					// (De automatische dek-link is vervangen door de vaste trap-links hierboven:
					// via het dek naar buiten was precies de verkeerde verdieping.)
				}
			}
		}
		// 4) TRAP-VOET: wat staat er onderaan het trappenhuis (de trap loopt fysiek door tot de
		// begane grond, maar het pad neemt de dek-uitgang een verdieping te hoog)? Dump alles
		// rond de trap-voet plus een pad-test vanaf daar naar de straat.
		{
			const FVector StairBase(-3014.f, -1976.f, 80.f);
			for (TObjectIterator<UStaticMeshComponent> BIt; BIt; ++BIt)
			{
				UStaticMeshComponent* C = *BIt;
				if (!C || C->GetWorld() != W || !C->GetStaticMesh()) { continue; }
				const FVector O = C->Bounds.Origin;
				if (FVector::Dist2D(O, StairBase) > 700.f || O.Z < -100.f || O.Z > 600.f) { continue; }
				const FString Nm = C->GetStaticMesh()->GetName();
				if (!(Nm.Contains(TEXT("Door")) || Nm.Contains(TEXT("Wall")) || Nm.Contains(TEXT("Stair")) || Nm.Contains(TEXT("Floor")) || Nm.Contains(TEXT("Glass")))) { continue; }
				AActor* Own = C->GetOwner();
				Out += FString::Printf(TEXT("VOET %s|owner=%s|%.0f,%.0f,%.0f|vis=%d|pawn=%d\n"),
					*Nm, Own ? *Own->GetClass()->GetName() : TEXT("?"), O.X, O.Y, O.Z,
					(C->IsVisible() && Own && !Own->IsHidden()) ? 1 : 0,
					(C->GetCollisionEnabled() != ECollisionEnabled::NoCollision && C->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) ? 1 : 0);
			}
			if (UNavigationSystemV1* NavB = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
			{
				FVector StreetB = FVector::ZeroVector;
				float BestB = TNumericLimits<float>::Max();
				for (TActorIterator<ACustomerSpawner> SpIt(W); SpIt; ++SpIt)
				{
					if (!IsValid(*SpIt)) { continue; }
					const float Dd = FVector::DistSquared2D(SpIt->GetActorLocation(), StairBase);
					if (Dd < BestB) { BestB = Dd; StreetB = SpIt->GetActorLocation(); }
				}
				if (BestB < TNumericLimits<float>::Max())
				{
					UNavigationPath* PB = NavB->FindPathToLocationSynchronously(W, StairBase, StreetB, nullptr);
					Out += FString::Printf(TEXT("VOET-PADTEST (%.0f, %.0f, %.0f) -> straat: %s (%.0f lang)\n"),
						StairBase.X, StairBase.Y, StairBase.Z,
						PB ? (PB->IsValid() ? (PB->IsPartial() ? TEXT("PARTIEEL") : TEXT("VOLLEDIG")) : TEXT("GEEN")) : TEXT("NULL"),
						PB ? PB->GetPathLength() : 0.f);
					if (PB && PB->PathPoints.Num() > 0)
					{
						const FVector EndB = PB->PathPoints.Last();
						Out += FString::Printf(TEXT("VOET-PADTEST eindigt op (%.0f, %.0f, %.0f)\n"), EndB.X, EndB.Y, EndB.Z);
					}
					// En de verbinding trap-voet OMHOOG naar de eerste verdieping (Z ~480):
					UNavigationPath* PU = NavB->FindPathToLocationSynchronously(W, StairBase, FVector(-3111.f, -2086.f, 500.f), nullptr);
					Out += FString::Printf(TEXT("VOET-OMHOOG naar Z500: %s\n"),
						PU ? (PU->IsValid() ? (PU->IsPartial() ? TEXT("PARTIEEL") : TEXT("VOLLEDIG")) : TEXT("GEEN")) : TEXT("NULL"));
				}
			}
		}
		FFileHelper::SaveStringToFile(Out, *(FPaths::ProjectSavedDir() / TEXT("StairDump.txt")));
		UE_LOG(LogWeedShop, Verbose, TEXT("StairDump geschreven (%d tekens)"), Out.Len()); // Verbose: dev-dump naar txt
	}

	// HERRIJZENIS: deuren met een ResidentNpc-tag waarvan de NPC niet meer bestaat (thuisgekomen
	// en gedespawned, of opgeruimd) krijgen hun tag terug vrij - de woningen-pass zet er dan een
	// verse bewoner neer. Zo blijft er continu volk in en uit de gebouwen lopen.
	if (ScanPass % 10 == 3)
	{
		TSet<FString> LiveIds;
		for (TActorIterator<ACustomerBase> CIt(W); CIt; ++CIt)
		{
			if (IsValid(*CIt)) { LiveIds.Add(CIt->NpcId.ToString()); }
		}
		int32 NFreed = 0;
		for (TActorIterator<ACityDoor> DIt(W); DIt; ++DIt)
		{
			ACityDoor* Dd = *DIt;
			if (!IsValid(Dd) || !Dd->ActorHasTag(TEXT("ResidentNpc"))) { continue; }
			const FVector DLoc = Dd->GetActorLocation();
			const int32 NIdx = FMath::Abs(FMath::RoundToInt(DLoc.X * 0.13f) + FMath::RoundToInt(DLoc.Y * 0.31f) + FMath::RoundToInt(DLoc.Z * 0.77f));
			if (LiveIds.Contains(FString::Printf(TEXT("Resident_%d"), NIdx))) { continue; }
			bool bPending = false;
			for (const FPendingResident& PRq : PendingResidents)
			{
				if (PRq.Door.Get() == Dd) { bPending = true; break; }
			}
			if (bPending) { continue; }
			Dd->Tags.Remove(TEXT("ResidentNpc"));
			++NFreed;
		}
		if (NFreed > 0)
		{
			LastAptDoorCount = -1; // woningen-pass opnieuw -> verse bewoners in de wachtrij
		}
	}

	// BEWONERS-WACHTRIJ: een bewoner per ~10 seconden laten verschijnen (niet allemaal tegelijk).
	// Toren-woningen (bij de starter) spawnen BINNEN - in de HAL bij hun eigen deur, nooit in een
	// kamer - en lopen zelf naar buiten; de rest spawnt op het dichtstbijzijnde route-punt voor
	// hun gebouw.
	// CO-OP: bewoner-bodies zijn replicerende ACustomerBase-actors -> ALLEEN de server materialiseert ze
	// (de joiner krijgt ze via replicatie). De client verwerkt de wachtrij niet: anders zou 'ie de rij
	// eindeloos rond-pompen (pop -> geen spawn -> requeue) of dubbele NPC's maken.
	if (!IsNetMode(NM_Client) && PendingResidents.Num() > 0 && (ScanPass % 5 == 0))
	{
		FPendingResident PR = PendingResidents[0];
		PendingResidents.RemoveAt(0);
		ACityDoor* A = PR.Door.Get();
		if (A)
		{
			const FVector DL = A->GetActorLocation();
			const int32 NameIdx = FMath::Abs(FMath::RoundToInt(DL.X * 0.13f) + FMath::RoundToInt(DL.Y * 0.31f) + FMath::RoundToInt(DL.Z * 0.77f));
			FVector SpawnAt = FVector::ZeroVector;
			FVector Front = FVector::ZeroVector;
			FVector Inside = FVector::ZeroVector;
			bool bOk = false;
			// Dichtstbijzijnde STRAAT-punt (route of spawner): voor toren-bewoners de hoogte-
			// referentie van hun loopdoelen (de trap af, de straat op), voor de rest de thuisplek.
			FVector StreetRef = FVector::ZeroVector;
			{
				float BestRd = TNumericLimits<float>::Max();
				for (const TArray<FVector>& Ring : NpcRings)
				{
					for (const FVector& RP2 : Ring)
					{
						const float Dd = FVector::DistSquared2D(RP2, DL);
						if (Dd < BestRd) { BestRd = Dd; StreetRef = RP2; }
					}
				}
				for (TActorIterator<ACustomerSpawner> SpIt(W); SpIt; ++SpIt)
				{
					if (!IsValid(*SpIt)) { continue; }
					const float Dd = FVector::DistSquared2D(SpIt->GetActorLocation(), DL);
					if (Dd < BestRd) { BestRd = Dd; StreetRef = SpIt->GetActorLocation(); }
				}
				if (BestRd == TNumericLimits<float>::Max()) { StreetRef = FVector::ZeroVector; }
			}
			if (PR.bInside)
			{
				// HAL-SPAWN (v/h kamer-spawn): een bewoner die "van huis vertrekt" verschijnt in de
				// HAL bij z'n eigen deur - NOOIT meer binnen in een kamer. In een woon-kamer hoort
				// nooit een random NPC (ook niet in "z'n eigen" kamer); hal/trappenhuis zijn de
				// legitieme binnen-plekken. De oude nav-projectie (extent 350x350x220) kon bovendien
				// door de muur naar de navmesh van de speler-kamer snappen.
				// COVER-FASE-GUARD: zolang de loading-cover op beeld staat of de stad nog niet
				// omgebouwd is, is de geometrie rond de deur mogelijk half gestreamd (de waaier +
				// vloer-trace zien dan gaten) -> bOk blijft false en de tail requeue't deze bewoner.
				if (!IsCoverPhase() && WeedShop_IsCityConverted())
				{
					// HAL-kant-detectie: rondom-waaier van 12 stralen per kant - de LANGSTE straal
					// verraadt de gang (die schiet de lange gang in), in een kamer kaatst alles
					// dichtbij. Twijfel (beide ruimtes klein/groot)? Dan beslist de zwaairichting
					// van de deur (apt-deuren zwaaien doorgaans de kamer IN -> hal = andersom).
					const FVector Fw = A->GetActorForwardVector();
					auto MaxRay = [&](const FVector& P) -> float
					{
						float MaxD = 0.f;
						const FVector S = P + FVector(0.f, 0.f, 130.f);
						for (int32 di = 0; di < 12; ++di)
						{
							const float Ang = di * 30.f;
							const FVector Dir = FRotator(0.f, Ang, 0.f).Vector();
							FHitResult H;
							const float Dd = W->LineTraceSingleByChannel(H, S, S + Dir * 3000.f, ECC_Visibility) ? H.Distance : 3000.f;
							MaxD = FMath::Max(MaxD, Dd);
						}
						return MaxD;
					};
					const FVector CandA = DL + Fw * 240.f;
					const FVector CandB = DL - Fw * 240.f;
					const float RayA = MaxRay(CandA);
					const float RayB = MaxRay(CandB);
					FVector Hall;
					if (FMath::Abs(RayA - RayB) > 300.f)
					{
						Hall = (RayA > RayB) ? CandA : CandB; // grootste max-straal = de hal/gang
					}
					else
					{
						const float SwingSide = (A->GetOpenSwing() <= 0.f) ? 1.f : -1.f;
						Hall = DL - Fw * 240.f * SwingSide; // kamer-kant gespiegeld = hal-kant
					}
					// Vangnet: koos de waaier tóch een kamer-box, dan de andere kant van de deur.
					if (IsInsideHomeRoom(Hall)) { Hall = DL * 2.f - Hall; }
					// Vloer-trace op de hal-kant (zelfde verdieping als de deur).
					FHitResult HallFloor;
					FCollisionQueryParams HQ(SCENE_QUERY_STAT(ResidentHallSpawn), false);
					if (!IsInsideHomeRoom(Hall)
						&& W->LineTraceSingleByChannel(HallFloor, Hall + FVector(0.f, 0.f, 150.f), Hall - FVector(0.f, 0.f, 450.f), ECC_WorldStatic, HQ)
						&& FMath::Abs(HallFloor.ImpactPoint.Z - DL.Z) < 280.f)
					{
						Hall.Z = HallFloor.ImpactPoint.Z + 5.f;
						// GEEN pop voor iemands neus: staat een speler in deze hal te kijken, dan
						// gewoon een latere poging (tail-requeue) - zelfde regel als de straat-spawns.
						bool bSeen = false;
						for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
						{
							const APlayerController* PCv = PIt->Get();
							const APawn* Pv = PCv ? PCv->GetPawn() : nullptr;
							if (!Pv) { continue; }
							const FVector To = Hall - Pv->GetActorLocation();
							if (To.Size() < 1200.f) { bSeen = true; break; }
							if (To.Size() < 5000.f && FVector::DotProduct(PCv->GetControlRotation().Vector(), To.GetSafeNormal()) > 0.05f) { bSeen = true; break; }
						}
						if (!bSeen)
						{
							Inside = Hall; // (historische naam; nu = de hal-plek bij de eigen deur)
							Front = DL * 2.f - Hall;
							SpawnAt = Hall;
							bOk = true;
						}
					}
					else
					{
						// Geen bruikbare hal-plek (geometrie/kamer-boxen): definitief terugvallen op
						// de STRAAT-tak - zelfde bewoner, volgende poging buiten (geen kamer-risico).
						PR.bInside = false;
					}
				}
				// bOk false -> achteraan de wachtrij (cover-fase / speler kijkt / geen hal-plek)
			}
			else if (!StreetRef.IsNearlyZero())
			{
				// VER van de speler spawnen (buiten zicht) en naar huis laten WANDELEN: kies het
				// route-punt met de beste mix van speler-afstand en niet-onnodig-ver-van-huis.
				// De thuisplek blijft het punt bij hun eigen gebouw - daar slenteren ze heen.
				FVector SpawnFar = StreetRef;
				float BestScore = -TNumericLimits<float>::Max();
				for (const TArray<FVector>& Ring : NpcRings)
				{
					for (const FVector& RP2 : Ring)
					{
						float MinPlayer = 99999.f;
						for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
						{
							const APawn* Pp = PIt->Get() ? PIt->Get()->GetPawn() : nullptr;
							if (Pp) { MinPlayer = FMath::Min(MinPlayer, FVector::Dist2D(Pp->GetActorLocation(), RP2)); }
						}
						const float Score = FMath::Min(MinPlayer, 12000.f) - FVector::Dist2D(RP2, StreetRef) * 0.25f;
						if (Score > BestScore) { BestScore = Score; SpawnFar = RP2; }
					}
				}
				SpawnAt = SpawnFar;
				Front = StreetRef;
				Inside = StreetRef;
				bOk = true;
			}
			// Navmesh-check op de spawn-plek (straat-variant): lukt de projectie op de juiste
			// hoogte niet, dan is de plek nog niet klaar (streaming) - rustig achteraan de rij,
			// in plaats van eindeloos over de map te blijven her-spawnen.
			bool bRequeued = false;
			if (bOk && !PR.bInside)
			{
				UNavigationSystemV1* NavR = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
				FNavLocation NavLoc;
				if (NavR && NavR->ProjectPointToNavigation(SpawnAt, NavLoc, FVector(300.f, 300.f, 60.f))
					&& FMath::Abs(NavLoc.Location.Z - SpawnAt.Z) <= 60.f)
				{
					SpawnAt = NavLoc.Location;
				}
				else
				{
					PendingResidents.Add(PR); // tag blijft staan: zelfde woning, latere poging
					bRequeued = true;
					bOk = false;
				}
			}
			if (bOk) // (server-only: het hele bewoner-materialisatie-blok draait achter HasAuthority, zie boven)
			{
				FActorSpawnParameters RP;
				RP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ACustomerBase* Cb = W->SpawnActor<ACustomerBase>(ACustomerBase::StaticClass(), FTransform(SpawnAt + FVector(0.f, 0.f, 110.f)), RP);
				if (Cb)
				{
					Cb->NpcId = FName(*FString::Printf(TEXT("Resident_%d"), NameIdx));
					// GEEN bewoners-logica (thuis/emergence/herstel vocht met alles): bewoner =
					// gewone wandelaar met een naam. De dichtstbijzijnde route-spawner adopteert
					// hem: zelfde patrouille over de route als iedereen, niks anders.
					ACustomerSpawner* OwnerSp = nullptr;
					float BestOd = TNumericLimits<float>::Max();
					for (TActorIterator<ACustomerSpawner> SpIt2(W); SpIt2; ++SpIt2)
					{
						if (!IsValid(*SpIt2)) { continue; }
						const float Dd = FVector::DistSquared2D(SpIt2->GetActorLocation(), SpawnAt);
						if (Dd < BestOd) { BestOd = Dd; OwnerSp = *SpIt2; }
					}
					// Binnen gespawnd: loop vanaf het punt op de (geknoopte) keten dat het DICHTST bij deze
					// deur ligt - op de eigen verdieping - AF tot het laatste punt op straat. Zo daalt een
					// bovenverdieping-bewoner het volledige resterende pad af i.p.v. op een bordes te stranden.
					TArray<FVector> EntrySuffix;
					if (PR.bInside)
					{
						float BestPd = TNumericLimits<float>::Max();
						const TArray<FVector>* BestCh = nullptr;
						int32 BestPi = 0;
						for (const TArray<FVector>& Ch : NpcChains)
						{
							for (int32 pi = 0; pi < Ch.Num(); ++pi)
							{
								const float Dd = FVector::DistSquared(Ch[pi], DL); // 3D: kiest het juiste verdieping-punt
								if (Dd < BestPd) { BestPd = Dd; BestCh = &Ch; BestPi = pi; }
							}
						}
						if (BestCh)
						{
							for (int32 pi = BestPi; pi < BestCh->Num(); ++pi) { EntrySuffix.Add((*BestCh)[pi]); }
						}
					}
					if (OwnerSp) { OwnerSp->AdoptWalker(Cb, EntrySuffix.Num() >= 2 ? &EntrySuffix : nullptr); }
					UE_LOG(LogWeedShop, Verbose, TEXT("Bewoner-wandelaar verschenen: Apt %d op route (%.0f, %.0f)"), A->GetAptNumber(), SpawnAt.X, SpawnAt.Y); // Verbose: terugkerend (~elke 10s bewoner-churn)
				}
			}
			else if (!bRequeued)
			{
				// Geen plek bekend (geen route/spawners hier): ook achteraan de rij, latere poging.
				PendingResidents.Add(PR);
			}
		}
	}

	// VROEGE thuis-teleport: de HomeSpawn-locatie is statisch (baked data), dus we kunnen METEEN naar huis
	// zonder te wachten op de hele stad/deuren. Zo sta je nooit eerst op straat. De settle-check (TickVirtualMove)
	// houdt je op de plek tot de penthouse-vloer is ingestreamd. Gebeurt op de eerste scan (~binnen het laadscherm).
	if (HomeAnchor.IsNearlyZero())
	{
		FString HomeTxt;
		if (FFileHelper::LoadFileToString(HomeTxt, *WeedData::File(TEXT("HomeSpawn.txt"))))
		{
			TArray<FString> Pc;
			HomeTxt.TrimStartAndEnd().ParseIntoArray(Pc, TEXT(","));
			if (Pc.Num() >= 3)
			{
				const FVector Inside(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]));
				HomeAnchor = Inside + FVector(0.f, 0.f, 110.f);
			}
		}
	}
	// COMPETITIVE: de host woont in z'n MARKER-kamer, niet in 703. Override de thuis-plek VROEG (voordat de
	// homing + settle-pin 'm gebruiken), zodat de normale machinerie de host in de marker-kamer neerzet i.p.v.
	// in 703 (geen teleport-gevecht, geen zweven). De originele 703-plek bewaren als meubel-kopie-referentie.
	if (Comp703Anchor.IsNearlyZero())
	{
		AWeedShopGameState* GScomp = W->GetGameState<AWeedShopGameState>();
		if (GScomp && GScomp->IsCompetitive())
		{
			TArray<FVector> Mk; GetCompetitiveMarkers(Mk);
			if (Mk.Num() >= 2)
			{
				Comp703Anchor = HomeAnchor.IsNearlyZero() ? (Mk[0] + FVector(0.f, 0.f, 110.f)) : HomeAnchor;
				// Per machine: de joiner (client) settelt in de TWEEDE marker-kamer (602), de host in de eerste
				// (603). Zo spawnt elke speler METEEN in z'n eigen kamer - geen spawn-in-603-dan-teleport meer.
				const FVector MyMark = (W->GetNetMode() == NM_Client) ? Mk[1] : Mk[0];
				HomeAnchor = MyMark + FVector(0.f, 0.f, 110.f);
				UE_LOG(LogWeedShop, Log, TEXT("Competitive: thuis-plek -> eigen marker (%.0f,%.0f,%.0f) [client=%d]; 703-ref=(%.0f,%.0f,%.0f)"), HomeAnchor.X, HomeAnchor.Y, HomeAnchor.Z, W->GetNetMode() == NM_Client ? 1 : 0, Comp703Anchor.X, Comp703Anchor.Y, Comp703Anchor.Z);
			}
		}
	}
	if (!HomeAnchor.IsNearlyZero())
	{
		if (!bBeachHomesBuilt) { RebuildBeachHomes(); } // registry vullen zodra de thuis-plek bekend is
		// Elke nog-niet-thuisgezette speler DIRECT thuiszetten via de gedeelde helper (kiest per rol: normaal =
		// HomeAnchor; de remote competitive-joiner op de host = de joiner-kamer Mk[1]=602; freeze-of-plaats naar
		// gelang de vloer al gestreamd is). Zelfde logica die de GameMode meteen na RestartPlayer aanroept, hier
		// als vangnet voor spelers die al aanwezig waren voordat de thuis-plek bekend was.
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			HomePawnNow(It->Get());
		}
	}

	// STARTER-HUIS: je begint IN je eigen appartement, en er loopt HUUR: EUR 500 per 31 dagen.
	// Genoeg cash op de vervaldag = automatisch geind; te weinig = deur op slot tot je aan de
	// deur betaalt (F). Dagen-resterend overleeft sessies via Saved/RentState.txt.
	if (StarterDoor.IsValid())
	{
		APlayerController* PCr = W->GetFirstPlayerController();
		APawn* Pr = PCr ? PCr->GetPawn() : nullptr;
		// Thuis-plek 1x bepalen (speler-gekozen Save home spawn wint, anders de kamer-kant van
		// de starter-deur via de zwaairichting). Geldt voor ALLE spelers (ook de co-op partner).
		if (HomeAnchor.IsNearlyZero() && bWalkersSpawned)
		{
			FVector Inside;
			FString HomeTxt;
			bool bCustom = false;
			if (FFileHelper::LoadFileToString(HomeTxt, *WeedData::File(TEXT("HomeSpawn.txt"))))
			{
				TArray<FString> Pc;
				HomeTxt.TrimStartAndEnd().ParseIntoArray(Pc, TEXT(","));
				if (Pc.Num() >= 3)
				{
					Inside = FVector(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]));
					bCustom = true;
				}
			}
			if (!bCustom)
			{
				const FVector DL = StarterDoor->GetActorLocation();
				const FVector Fw = StarterDoor->GetActorForwardVector();
				const float SwingSide = (StarterDoor->GetOpenSwing() <= 0.f) ? 1.f : -1.f;
				Inside = DL + Fw * 240.f * SwingSide;
			}
			HomeAnchor = Inside + FVector(0.f, 0.f, 110.f);
		}
		// HUIS-BOX meten (wand-traces): zodra de wanden geladen zijn, de kamer rond de thuis-plek
		// opmeten zodat de build-tool alleen BINNEN je eigen huis laat plaatsen. Lock zodra alle
		// vier de richtingen een wand binnen bereik raken.
		if (!HomeAnchor.IsNearlyZero() && !bHomeBoxReady)
		{
			// Traces NEGEREN spelers en geplaatste objecten (anders meet een trace tegen jezelf of
			// een meubel een veel te kleine kamer). Vanaf 2 hoogtes en per as de VERSTE hit nemen,
			// zodat nissen/lage objecten de box niet onnodig inkrimpen.
			FCollisionQueryParams BQ(SCENE_QUERY_STAT(HomeBox), false);
			for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
			{
				if (APawn* Pp = PIt->Get() ? PIt->Get()->GetPawn() : nullptr) { BQ.AddIgnoredActor(Pp); }
			}
			// DICHTSTBIJZIJNDE wand op het WorldStatic-kanaal (muren = static; meubels zijn dynamic
			// en tellen dus niet mee), op twee hoogtes -> de kleinste geldige hit is de echte wand.
			auto Wall = [&](const FVector& Dir) -> float
			{
				float Near = -1.f;
				for (float Hz : { 40.f, 150.f })
				{
					const FVector S = HomeAnchor + FVector(0.f, 0.f, Hz);
					FHitResult H;
					if (W->LineTraceSingleByChannel(H, S, S + Dir * 2500.f, ECC_WorldStatic, BQ))
					{
						Near = (Near < 0.f) ? H.Distance : FMath::Min(Near, H.Distance);
					}
				}
				return Near;
			};
			const float Xp = Wall(FVector(1, 0, 0)), Xn = Wall(FVector(-1, 0, 0));
			const float Yp = Wall(FVector(0, 1, 0)), Yn = Wall(FVector(0, -1, 0));
			if (Xp > 150.f && Xn > 150.f && Yp > 150.f && Yn > 150.f)
			{
				// Box tot de echte wand + 90 marge (zo kun je tot tegen de muur plaatsen), met een
				// kleine ondergrens. Geen gang-bleed meer (dichtstbijzijnde wand i.p.v. de verste).
				auto Half = [](float WallDist) { return FMath::Max(350.f, WallDist + 90.f); };
				HomeBoxMin = FVector(HomeAnchor.X - Half(Xn), HomeAnchor.Y - Half(Yn), HomeAnchor.Z - 220.f);
				HomeBoxMax = FVector(HomeAnchor.X + Half(Xp), HomeAnchor.Y + Half(Yp), HomeAnchor.Z + 520.f);
				bHomeBoxReady = true;
				RebuildBeachHomes(); // starter-bounds zijn nu accuraat -> registry verversen
				UE_LOG(LogWeedShop, Display, TEXT("Huis-box gemeten: X %.0f..%.0f Y %.0f..%.0f"), HomeBoxMin.X, HomeBoxMax.X, HomeBoxMin.Y, HomeBoxMax.Y);
			}
		}
		// Elke speler die nog niet thuisgezet is naar de thuis-plek (kleine spreiding zodat co-op-spelers niet in
		// elkaar spawnen), via de gedeelde helper - werkt ook voor een later-joinende partner. Toast alleen als de
		// helper de pawn ECHT thuiszette (return true).
		if (!HomeAnchor.IsNearlyZero())
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PCit = It->Get();
			APawn* Pw = PCit ? PCit->GetPawn() : nullptr;
			if (HomePawnNow(PCit) && Pw)
			{
				if (UPhoneClientComponent* Phw = Pw->FindComponentByClass<UPhoneClientComponent>())
				{
					Phw->Toast(FString::Printf(TEXT("Welcome home - Apt %d. Rent: EUR 500 due every 31 days."), StarterDoor->GetAptNumber()), FColor::Cyan, 6.f);
				}
			}
		}
		// Huur-administratie op de dag-teller.
		AWeedShopGameState* GSr = W->GetGameState<AWeedShopGameState>();
		UDayCycleComponent* DCr = GSr ? GSr->GetDayCycle() : nullptr;
		if (DCr)
		{
			const int32 Day = DCr->GetDayNumber();
			// Rent-teller PER SLOT: was 1 globaal RentState.txt -> lekte tussen slots en overleefde New Game niet
			// (verse New Game kon dag-1 overdue zijn). Slot uit het save-subsysteem; New Game wist dit bestand.
			int32 RentSlot = 0;
			if (UGameInstance* GI2 = GetWorld() ? GetWorld()->GetGameInstance() : nullptr) { if (USaveGameSubsystem* Sv2 = GI2->GetSubsystem<USaveGameSubsystem>()) { RentSlot = Sv2->GetSlot(); } }
			const FString RentFile = FPaths::ProjectSavedDir() / FString::Printf(TEXT("RentState_%d.txt"), RentSlot);
			if (RentDueDay < 0)
			{
				int32 DaysLeft = 31;
				FString RS;
				if (FFileHelper::LoadFileToString(RS, *RentFile))
				{
					DaysLeft = FMath::Clamp(FCString::Atoi(*RS), 0, 31);
				}
				RentDueDay = Day + DaysLeft;
			}
			const int32 Left = RentDueDay - Day;
			if (Left != LastRentSeenLeft)
			{
				LastRentSeenLeft = Left;
				FFileHelper::SaveStringToFile(FString::FromInt(FMath::Max(0, Left)), *RentFile);
				if (Left > 0 && Left <= 7 && Pr)
				{
					if (UPhoneClientComponent* Ph7 = Pr->FindComponentByClass<UPhoneClientComponent>())
					{
						Ph7->Toast(FString::Printf(TEXT("Rent: EUR 500 due in %d day%s"), Left, Left == 1 ? TEXT("") : TEXT("s")), FColor::Yellow, 4.f);
					}
				}
			}
			// CO-OP: de huur is GEDEELDE wereld-staat (1 huis). De inning + de OVERDUE-lock zijn server-
			// authoritative (een client mag de gedeelde economy niet muteren); de lock-vlag staat op de
			// GameState en repliceert. DoorRetrofitter is per-proces -> server = !IsNetMode(NM_Client).
			// COMPETITIVE: ieder woont in z'n EIGEN comp-kamer (603/602), niet in het starter-penthouse ->
			// ieder betaalt uit z'n EIGEN portemonnee en wie niet kan betalen krijgt de lock op z'n EIGEN
			// comp-deur (per-deur-id op de GameState, gerepliceerd). 1 gedeelde cyclus (RentDueDay) voor iedereen.
			if (Left <= 0 && GSr && !IsNetMode(NM_Client) && GSr->IsCompetitive())
			{
				// Wachten tot de comp-kamers staan (anders is de deur-id niet te bepalen). RentDueDay schuift
				// pas door als de inning echt draait, dus dit retryt vanzelf volgende scan.
				if (bCompHomesReady)
				{
					// Host-pawn = GetFirstPlayerController op de server (zelfde patroon als TickCompetitiveRooms).
					APawn* HostPawn = Pr;
					for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
					{
						APawn* Pw = It->Get() ? It->Get()->GetPawn() : nullptr;
						if (!Pw) { continue; }
						const uint32 DoorId = GetCompRentDoorId(Pw != HostPawn);
						// Nog steeds overdue van een vorige cyclus? Geen dubbele inning (zelfde gate als het
						// enkele-vlag-pad); betalen aan de deur is de enige uitweg, schuld stapelt niet.
						if (DoorId != 0 && GSr->IsRentOverdueDoor(DoorId)) { continue; }
						UEconomyComponent* Ec = Pw->FindComponentByClass<UEconomyComponent>();
						UPhoneClientComponent* Ph = Pw->FindComponentByClass<UPhoneClientComponent>();
						if (Ec && Ec->RemoveMoney(50000))
						{
							if (Ph) { Ph->Toast(TEXT("Rent paid: EUR 500. Next rent in 31 days."), FColor::Green, 5.f); }
						}
						else
						{
							// Gerepliceerd -> BEIDE processen locken hun lokale deur-kopie (ApplyCompRentDoorLocks).
							if (DoorId != 0) { GSr->AddRentOverdueDoor(DoorId); }
							else { UE_LOG(LogWeedShop, Warning, TEXT("Huur: comp-deur-id niet gevonden voor %s - lock overgeslagen"), *GetNameSafe(Pw)); }
							if (Ph) { Ph->Toast(TEXT("RENT OVERDUE - EUR 500. Your door is locked: pay at the door (F)."), FColor::Red, 7.f); }
						}
					}
					// Gedeelde cyclus ALTIJD doorschuiven: de wanbetaler blijft gelockt tot 'ie aan de deur
					// betaalt (dat schuift de cyclus NIET nogmaals door - zie de consume-tak hieronder).
					RentDueDay = Day + 31;
				}
			}
			else if (Left <= 0 && GSr && !GSr->IsStarterRentOverdue() && !IsNetMode(NM_Client))
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
					GSr->SetStarterRentOverdue(true, 50000); // gerepliceerd -> de apply-vlag hieronder lockt BEIDE deuren
					if (Phr) { Phr->Toast(TEXT("RENT OVERDUE - EUR 500. Your door is locked: pay at the door (F)."), FColor::Red, 7.f); }
				}
			}
			// Aan de deur betaald (host lokaal OF joiner via ServerPayRent)? Alleen de SERVER-deur krijgt
			// bRentJustPaid (de betaling loopt server-authoritative) -> wis de gedeelde vlag + hernieuw de maand.
			if (!IsNetMode(NM_Client) && GSr && GSr->IsCompetitive())
			{
				// COMPETITIVE: check de COMP-deuren op zojuist-betaald en geef ALLEEN dat deur-id vrij.
				// De gedeelde cyclus (RentDueDay) is bij de inning al doorgeschoven - hier niet nogmaals.
				for (const TWeakObjectPtr<ACityDoor>& WPd : ACityDoor::GetAll())
				{
					ACityDoor* Dp = WPd.Get();
					if (!IsValid(Dp) || Dp->GetWorld() != W) { continue; } // registry is per-PROCES -> wereld-filter
					if (!Dp->ConsumeRentJustPaid()) { continue; }
					GSr->ClearRentOverdueDoor(Dp->GetWorldSyncDoorId());
					// Melding bij de KAMER-EIGENAAR (host-deur -> host-pawn, anders de joiner-pawn; de
					// betaler is vrijwel altijd de eigenaar zelf - alleen die deur toont de pay-prompt zinvol).
					APawn* OwnPw = (Dp->GetWorldSyncDoorId() == GetCompRentDoorId(false)) ? Pr : nullptr;
					if (!OwnPw)
					{
						for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
						{ APawn* P3 = It->Get() ? It->Get()->GetPawn() : nullptr; if (P3 && P3 != Pr) { OwnPw = P3; break; } }
					}
					if (OwnPw)
					{
						if (UPhoneClientComponent* Php = OwnPw->FindComponentByClass<UPhoneClientComponent>())
						{
							Php->Toast(TEXT("Rent paid: EUR 500. Your door is unlocked."), FColor::Green, 5.f);
						}
					}
				}
			}
			else if (!IsNetMode(NM_Client) && StarterDoor->ConsumeRentJustPaid())
			{
				RentDueDay = Day + 31;
				if (GSr) { GSr->SetStarterRentOverdue(false, 0); }
				FFileHelper::SaveStringToFile(FString::FromInt(31), *RentFile);
				if (Pr)
				{
					if (UPhoneClientComponent* Php = Pr->FindComponentByClass<UPhoneClientComponent>())
					{
						Php->Toast(TEXT("Rent paid: EUR 500. Next rent in 31 days."), FColor::Green, 5.f);
					}
				}
			}
			// BEIDE processen: pas de gedeelde overdue-vlag toe op de LOKALE starter-deur (bReplicates=false)
			// zodat host EN joiner dezelfde vergrendelde deur + "pay at the door"-prompt zien.
			if (GSr)
			{
				if (GSr->IsStarterRentOverdue())
				{
					if (!StarterDoor->IsRentOverdue()) { StarterDoor->SetRentOverdue(GSr->GetStarterRentCents()); }
				}
				else if (StarterDoor->IsRentOverdue())
				{
					StarterDoor->ClearRentOverdue();
				}
			}
		}
	}

	// HANDMATIG VERGRENDELDE DEUREN (Lock door in crosshair): op slot zoals een bewoner-deur maar
	// zonder naam, NA de woningen-pass zodat een handmatig slot altijd wint. ALLEEN hertoepassen als het
	// deur-aantal wijzigt (nieuwe deuren ingestreamd) i.p.v. elke pass - die O(regels x alle deuren)-lus
	// elke 2s was de lag-spike-bron. Eenmaal gelockt blijft 'ie gelockt.
	if (!bLockedDoorsLoaded)
	{
		bLockedDoorsLoaded = true;
		FFileHelper::LoadFileToStringArray(LockedDoorLines, *(WeedData::File(TEXT("LockedDoors.txt"))));
	}
	if (LockedDoorLines.Num() > 0 && LastAptDoorCount != LastLockApplyCount)
	{
		LastLockApplyCount = LastAptDoorCount;
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

	// DEUR-SNAPS (DoorSnaps.txt): deuren die naast hun kozijn geconverteerd worden naar het juiste deurvak
	// zetten. Alleen hertoepassen bij deur-aantal-wijziging (net als de sloten) - geen elke-pass-lus.
	if (!bDoorSnapsLoaded)
	{
		bDoorSnapsLoaded = true;
		FFileHelper::LoadFileToStringArray(DoorSnapLines, *(WeedData::File(TEXT("DoorSnaps.txt"))));
	}
	if (DoorSnapLines.Num() > 0)
	{
		for (const FString& SL : DoorSnapLines)
		{
			// Elke regel = een markeer-positie (X,Y,Z) van een scheve deur. Pak de dichtstbijzijnde deur en
			// snap 'm EEN KEER netjes in z'n kozijn - en ALLEEN als 'ie dicht staat (anders meet de snap het
			// open-zwaaiende blad verkeerd en gaat 'ie heen-en-weer schuiven). Eenmaal gesnapt = met rust.
			TArray<FString> P;
			SL.ParseIntoArray(P, TEXT(","));
			if (P.Num() < 3) { continue; }
			const FVector Mark(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
			ACityDoor* Best = nullptr; float BestD = 150.f;
			for (TActorIterator<ACityDoor> It(W); It; ++It)
			{
				if (!IsValid(*It)) { continue; }
				const FVector DL = It->GetActorLocation();
				if (FMath::Abs(DL.Z - Mark.Z) > 300.f) { continue; }
				const float D2 = FVector::Dist2D(DL, Mark);
				if (D2 < BestD) { BestD = D2; Best = *It; }
			}
			if (Best && !SnappedDoors.Contains(Best) && Best->IsPanelClosedNow())
			{
				ACityDoor::SnapToNearestFrame(W, Best);
				SnappedDoors.Add(Best);
			}
		}
	}

	// STARTER-MEUBELS (StarterFurniture.txt): op een VERSE game de opgeslagen inrichting
	// neerzetten (bij een geladen game herstelt de save-subsystem de props zelf).
	if (!bFurniturePlaced)
	{
		bFurniturePlaced = true;
		bool bFresh = true;
		if (UGameInstance* GI = W->GetGameInstance())
		{
			if (USaveGameSubsystem* Sv = GI->GetSubsystem<USaveGameSubsystem>()) { bFresh = Sv->IsFreshGame(); }
		}
		// COMPETITIVE: het 703-penthouse (originele solo-kamer) blijft LEEG - de inrichting wordt naar 603/602
		// gekopieerd waar de spelers zitten. Alleen in Competitive.
		AWeedShopGameState* GSstarter = W->GetGameState<AWeedShopGameState>();
		const bool bCompStarter = GSstarter && GSstarter->IsCompetitive();
		// CO-OP: de replicerende starter-meubels (AWaterSink/AStorageShelf/APlaceableProp, bReplicates=true) alleen op de
		// ECHTE server spawnen; de joiner krijgt ze via replicatie. Op de joiner is bFresh ALTIJD true (Loaded==nullptr),
		// dus zonder gate zou de joiner z'n eigen dubbele set spawnen. DoorRetrofitter is per-proces + niet-gerepliceerd,
		// dus HasAuthority() is OOK true op de joiner -> gate op IsNetMode(NM_Client). UITZONDERING: de lichtschakelaar
		// (APackLightSwitch, bReplicates=false) is deterministisch-lokaal en MOET op BEIDE processen (zoals deuren),
		// anders ziet de joiner geen schakelaars -> die tak spawnt wel op de client (zie de skip hieronder).
		if (bFresh && !bCompStarter)
		{
			const bool bClient = IsNetMode(NM_Client);
			TArray<FString> FL;
			FFileHelper::LoadFileToStringArray(FL, *WeedData::File(TEXT("StarterFurniture.txt")));
			int32 NF = 0;
			for (const FString& L : FL)
			{
				TArray<FString> Pc;
				L.ParseIntoArray(Pc, TEXT(","));
				if (Pc.Num() < 5) { continue; }
				const FName ItemId(*Pc[0]);
				const FVector Loc(FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]), FCString::Atof(*Pc[3]));
				const FRotator Rot(0.f, FCString::Atof(*Pc[4]), 0.f);
				FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				FPlaceableDef Def;
				const bool bShelf = GetPlaceableDef(ItemId, Def) && Def.bIsShelf; // Fridge/Shelf/Chest = eigen class
				// De replicerende meubels (Sink/Shelf/Prop) op de client overslaan; alleen de niet-replicerende
				// lichtschakelaar spawnt ook lokaal op de joiner (bReplicates=false = deterministisch-lokaal).
				if (bClient && Pc[0] != TEXT("LightSwitch")) { continue; }
				if (Pc[0] == TEXT("Sink"))
				{
					if (AWaterSink* Sk = W->SpawnActor<AWaterSink>(AWaterSink::StaticClass(), FTransform(Rot, Loc), SP))
					{
						Sk->Tags.Add(FName(TEXT("AutoFixture")));
						++NF;
					}
				}
				else if (Pc[0] == TEXT("LightSwitch"))
					{
						// Lichtschakelaar = eigen APackLightSwitch (geen prop). Stabiele sleutel uit de positie
						// (zelfde als bij plaatsen) zodat aan/uit + dim per plek onthouden blijft.
						if (APackLightSwitch* Sw = W->SpawnActorDeferred<APackLightSwitch>(APackLightSwitch::StaticClass(), FTransform(Rot, Loc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
						{
							Sw->Setup(FString::Printf(TEXT("sw_%d_%d_%d"), FMath::RoundToInt(Loc.X / 10.f), FMath::RoundToInt(Loc.Y / 10.f), FMath::RoundToInt(Loc.Z / 10.f)), 800.f);
							Sw->FinishSpawning(FTransform(Rot, Loc));
							Sw->Tags.Add(FName(TEXT("AutoFixture")));
							++NF;
						}
					}
					else if (bShelf)
				{
					// Opslag-meubel (Fridge/Shelf/Chest): als functionele AStorageShelf terugzetten, niet als dode prop.
					if (AStorageShelf* Sh = W->SpawnActorDeferred<AStorageShelf>(AStorageShelf::StaticClass(), FTransform(Rot, Loc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
					{
						Sh->ShelfTier = ItemId;
						Sh->FinishSpawning(FTransform(Rot, Loc));
						Sh->Tags.Add(FName(TEXT("AutoFixture")));
						++NF;
					}
				}
				else if (APlaceableProp* Pr = W->SpawnActorDeferred<APlaceableProp>(APlaceableProp::StaticClass(), FTransform(Rot, Loc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
				{
					Pr->ItemId = ItemId;
					Pr->FinishSpawning(FTransform(Rot, Loc));
					Pr->Tags.Add(FName(TEXT("AutoFixture")));
					++NF;
				}
			}
			if (NF > 0) { UE_LOG(LogWeedShop, Display, TEXT("Starter-meubels geplaatst: %d"), NF); }
		}
	}

	// SCHAKELAAR-BESTAAN-SYNC (co-op): speler-geplaatste lichtschakelaars (APackLightSwitch, bReplicates=false)
	// spawnen op de server (plaatsen/save-load) en bestaan dus niet vanzelf op de joiner - en de joiner z'n verse
	// StarterFurniture-default-set kan juist STALE zijn t.o.v. een geladen save (verplaatst/verwijderd op de host).
	// De SERVER spiegelt daarom elke scan de WorldSync-arrays 1-op-1 aan z'n echte sw_*-actoren (dit is meteen de
	// robuuste vangnet-route voor "WorldSync was nog null" bij een vroege load/plaats: volgende scan herpubliceert);
	// de CLIENT spawnt ontbrekende schakelaars lokaal na en ruimt stale sw_*-schakelaars op. De aan/uit/dim-STAAT
	// synct al apart (LampIds in WorldSync) - dit gaat puur om het BESTAAN. Goedkoop: een handvol actors op scan-cadans.
	// Gate op bFurniturePlaced (en in Competitive op de comp-passes) zodat het fresh-/comp-pad niet NA deze pass
	// alsnog duplicaten op dezelfde posities spawnt.
	if (bFurniturePlaced)
	{
		AWeedShopGameState* GSsw = W->GetGameState<AWeedShopGameState>();
		UWorldSyncComponent* WSsw = GSsw ? GSsw->GetWorldSync() : nullptr;
		const bool bCompSw = GSsw && GSsw->IsCompetitive();
		if (WSsw && !IsNetMode(NM_Client) && (!bCompSw || bCompMirrorDone))
		{
			// SERVER: reconcile-publish. Add dedupet zelf op 10cm; entries zonder levende server-actor binnen
			// 50cm gaan eruit (oppakken, load-vervanging - elke destroy-route wordt zo vanzelf meegenomen).
			TArray<FVector> SrvPos;
			for (TActorIterator<APackLightSwitch> It(W); It; ++It)
			{
				if (!IsValid(*It) || !It->GetPersistKey().StartsWith(TEXT("sw_"))) { continue; }
				SrvPos.Add(It->GetActorLocation());
				WSsw->ServerAddSwitch(It->GetActorLocation(), It->GetActorRotation().Yaw);
			}
			for (int32 i = WSsw->GetSwitchCount() - 1; i >= 0; --i)
			{
				FVector SwP; float SwYaw;
				if (!WSsw->GetSwitch(i, SwP, SwYaw)) { continue; }
				bool bAlive = false;
				for (const FVector& SP2 : SrvPos) { if (FVector::DistSquared(SP2, SwP) <= 50.f * 50.f) { bAlive = true; break; } }
				if (!bAlive) { WSsw->ServerRemoveSwitch(SwP); }
			}
			WSsw->ServerMarkSwitchesPublished();
		}
		else if (WSsw && IsNetMode(NM_Client) && WSsw->AreSwitchesPublished() && (!bCompSw || bCompSwitchesDone))
		{
			// CLIENT: de lokale set aan de gepubliceerde set spiegelen. AreSwitchesPublished-gate: een nog niet
			// gepubliceerde (lege) array mag de verse default-set nooit wissen.
			for (int32 i = 0; i < WSsw->GetSwitchCount(); ++i)
			{
				FVector SwP; float SwYaw;
				if (!WSsw->GetSwitch(i, SwP, SwYaw)) { continue; }
				bool bHave = false;
				for (TActorIterator<APackLightSwitch> It(W); It; ++It)
				{
					if (IsValid(*It) && FVector::DistSquared(It->GetActorLocation(), SwP) <= 50.f * 50.f) { bHave = true; break; }
				}
				if (bHave) { continue; }
				// Zelfde spawn-route + PersistKey-formule als het fresh-pad hierboven, zodat Load() (stand/links)
				// en de lamp-staat-sync (LampSyncId uit positie+yaw) op host en joiner dezelfde sleutels gebruiken.
				const FRotator SwRot(0.f, SwYaw, 0.f);
				if (APackLightSwitch* Sw = W->SpawnActorDeferred<APackLightSwitch>(APackLightSwitch::StaticClass(), FTransform(SwRot, SwP), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
				{
					Sw->Setup(FString::Printf(TEXT("sw_%d_%d_%d"), FMath::RoundToInt(SwP.X / 10.f), FMath::RoundToInt(SwP.Y / 10.f), FMath::RoundToInt(SwP.Z / 10.f)), 800.f);
					Sw->FinishSpawning(FTransform(SwRot, SwP));
				}
			}
			// Stale lokale sw_*-schakelaars (geen server-entry binnen 50cm) opruimen - bv. de default-set van het
			// fresh-pad terwijl de host een geladen game draait waarin die schakelaar verplaatst/verwijderd is.
			TArray<APackLightSwitch*> Stale;
			for (TActorIterator<APackLightSwitch> It(W); It; ++It)
			{
				if (!IsValid(*It) || !It->GetPersistKey().StartsWith(TEXT("sw_"))) { continue; }
				if (!WSsw->HasSwitchNear(It->GetActorLocation(), 50.f)) { Stale.Add(*It); }
			}
			for (APackLightSwitch* SwOld : Stale) { SwOld->Destroy(); }
		}
	}

	// COMPETITIVE co-op: spiegel de solo-kamer naar Apt 603 (kopie) + 602 (echte spiegel) en zet beide
	// spelers in hun eigen kamer. Self-gated op IsCompetitive -> in normale co-op gebeurt hier niets.
	TickCompetitiveRooms();

	// COMPETITIVE huur-lock NA de kamer-claim toepassen: de claim-pass hierboven (SetCompPlayerHome)
	// zet bLocked elke scan weer uit, dus de per-deur overdue-staat moet er telkens NA opnieuw op.
	// Self-gated op IsCompetitive; draait op BEIDE processen (de deuren zijn bReplicates=false).
	ApplyCompRentDoorLocks();

	// WINKELS (ShopSpots.txt): op elke speler-markeerde plek een toonbank + ATM + verkoper.
	if (!bShopsPlaced)
	{
		bShopsPlaced = true;
		TArray<FString> ShopLines;
		FFileHelper::LoadFileToStringArray(ShopLines, *WeedData::File(TEXT("ShopSpots.txt")));
		int32 NShops = 0;
		for (const FString& SL : ShopLines)
		{
			TArray<FString> Pc;
			SL.ParseIntoArray(Pc, TEXT(","));
			if (Pc.Num() < 5) { continue; }
			FVector Pos(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]));
			// VLOER zoeken: de F9-marker zit op capsule-hoogte (~90cm boven de vloer). Trace omlaag
			// zodat balie/ATM op de echte vloer komen i.p.v. te zweven.
			{
				FHitResult FH;
				if (W->LineTraceSingleByChannel(FH, Pos + FVector(0.f, 0.f, 150.f), Pos - FVector(0.f, 0.f, 400.f), ECC_Visibility))
				{
					Pos.Z = FH.ImpactPoint.Z;
				}
			}
			// 180 gedraaid: de balie-voorkant (klant-kant) kijkt nu naar waar JIJ keek bij het zetten.
			const float Yaw = FCString::Atof(*Pc[3]) + 180.f;
			const int32 KindI = FCString::Atoi(*Pc[4]);
			EShopKind Kind = (KindI == 0) ? EShopKind::Grow : (KindI == 1) ? EShopKind::Supplies : EShopKind::Furniture;
			// Toonbank-kleur uit de gedeelde tabel (AStoreCounter::KindColor) - EEN bron van waarheid,
			// zodat de balie-tint en het bordje niet uit elkaar lopen. Kind mapt 1-op-1 op EShopKind.
			const FLinearColor Sign = AStoreCounter::KindColor(Kind);
			const FRotator Rot(0.f, Yaw, 0.f);
			FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStoreCounter* Counter = W->SpawnActor<AStoreCounter>(AStoreCounter::StaticClass(), FTransform(Rot, Pos + FVector(0.f, 0.f, 2.f)), SP);
			if (!Counter) { continue; }
			Counter->Kind = Kind;
			Counter->SetupVisual(Sign);
			const FVector Fwd = Counter->GetActorForwardVector();
			const FVector Tang(-Fwd.Y, Fwd.X, 0.f);
			// CO-OP: AAtm heeft bReplicates=true -> ALLEEN op de server spawnen; de joiner krijgt 'm via
			// replicatie. Zonder deze gate spawnt de joiner z'n eigen ATM BOVENOP de gerepliceerde = 2
			// overlappende ATM's per winkel (zelfde klasse als de gefixte meubel-dubbelspawn).
			if (!IsNetMode(NM_Client)) { W->SpawnActor<AAtm>(AAtm::StaticClass(), FTransform(Rot, Pos + Tang * 210.f + FVector(0.f, 0.f, 2.f)), SP); }
			// Verkoper achter de balie (de klant-kant is +Fwd, dus de keeper staat op -Fwd).
			float HalfH = 88.f;
			if (const ACustomerBase* CDO = ACustomerBase::StaticClass()->GetDefaultObject<ACustomerBase>())
			{
				if (const UCapsuleComponent* Cap = CDO->GetCapsuleComponent()) { HalfH = Cap->GetScaledCapsuleHalfHeight(); }
			}
			FVector KPos = Pos - Fwd * 80.f; KPos.Z = Pos.Z + HalfH + 2.f;
			// CO-OP: de verkoper is een replicerende ACustomerBase -> alleen de server spawnt 'm (joiner via
			// replicatie). De toonbank (StoreCounter, bReplicates=false) is deterministisch-lokaal op beide.
			if (ACustomerBase* Keeper = !IsNetMode(NM_Client) ? W->SpawnActorDeferred<ACustomerBase>(ACustomerBase::StaticClass(), FTransform(Rot, KPos), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr)
			{
				Keeper->bShopkeeper = true;
				Keeper->FinishSpawning(FTransform(Rot, KPos));
				if (!Keeper->GetController()) { Keeper->SpawnDefaultController(); }
			}
			++NShops;
		}
		if (NShops > 0) { UE_LOG(LogWeedShop, Display, TEXT("Winkels geplaatst: %d (toonbank + ATM + verkoper)"), NShops); }
	}

	// (VIRTUELE CROWD materialiseren/opruimen draait nu op de 10Hz-tick TickVirtualMove i.p.v. hier - met een
	//  spawn-cap per call, zodat NPC's vloeiend druppelen i.p.v. elke 2s in een burst = periodieke hang.)

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
			// Voordeur-lookup voor bewoners (NameIdx -> deur), om gezakte bewoners VERS te
			// kunnen respawnen: NPC weg, tag van de deur af, de woningen-pass zet 'm opnieuw
			// binnen in z'n huis neer.
			TMap<int32, ACityDoor*> ResidentDoors;
			for (TActorIterator<ACityDoor> DIt(W); DIt; ++DIt)
			{
				if (!IsValid(*DIt) || !DIt->ActorHasTag(TEXT("ResidentNpc"))) { continue; }
				const FVector DLoc = DIt->GetActorLocation();
				const int32 NIdx = FMath::Abs(FMath::RoundToInt(DLoc.X * 0.13f) + FMath::RoundToInt(DLoc.Y * 0.31f) + FMath::RoundToInt(DLoc.Z * 0.77f));
				ResidentDoors.Add(NIdx, *DIt);
			}
			int32 NRescued = 0;
			for (TActorIterator<ACustomerBase> CIt(W); CIt; ++CIt)
			{
				ACustomerBase* Cb = *CIt;
				if (!IsValid(Cb)) { continue; }
				const FVector L = Cb->GetActorLocation();
				if (Cb->NpcId.ToString().StartsWith(TEXT("Resident_")))
				{
					// Bewoner (op NpcId - IsResident is sinds de vereenvoudiging altijd false):
					// vergelijk met het STRAAT-niveau (dichtstbijzijnd spawn-punt). Alleen wie
					// ECHT onder de straat zakt wordt gerespawned; boven (trap/verdieping) is ok.
					FVector BestR = SpawnerLocs[0];
					float BestRD = TNumericLimits<float>::Max();
					for (const FVector& SL : SpawnerLocs)
					{
						const float Dd = FVector::DistSquared2D(SL, L);
						if (Dd < BestRD) { BestRD = Dd; BestR = SL; }
					}
					// ECHT door de map gezakt? Down-trace: is er grond vlak onder 'm, dan staat 'ie gewoon op
					// een lager stuk (strand/lager straatdeel) en laten we 'm met rust. Geen grond = gevallen.
					{
						FHitResult GH; FCollisionQueryParams GQ(SCENE_QUERY_STAT(NpcFloorR), false, Cb);
						if (W->LineTraceSingleByChannel(GH, L + FVector(0,0,50.f), L - FVector(0,0,700.f), ECC_WorldStatic, GQ)) { continue; }
					}
					const FString IdS = Cb->NpcId.ToString();
					const int32 NIdx = IdS.StartsWith(TEXT("Resident_")) ? FCString::Atoi(*IdS.RightChop(9)) : -1;
					if (ACityDoor* const* Dp = ResidentDoors.Find(NIdx))
					{
						(*Dp)->Tags.Remove(TEXT("ResidentNpc"));
						LastAptDoorCount = -1; // woningen-pass opnieuw -> verse bewoner
					}
					Cb->Destroy();
					++NRescued;
					continue;
				}
				// Klant: vergelijk met het dichtstbijzijnde spawn-punt; opruimen = respawn.
				FVector Best = SpawnerLocs[0];
				float BestD = TNumericLimits<float>::Max();
				for (const FVector& SL : SpawnerLocs)
				{
					const float Dd = FVector::DistSquared2D(SL, L);
					if (Dd < BestD) { BestD = Dd; Best = SL; }
				}
				// ECHT door de map gezakt? Down-trace i.p.v. hoogte-vergelijking (de map heeft lagere stukken;
				// een Z-check alleen ruimde NPC's op die gewoon op lager terrein liepen = constant despawnen).
				{
					FHitResult GH; FCollisionQueryParams GQ(SCENE_QUERY_STAT(NpcFloorC), false, Cb);
					if (W->LineTraceSingleByChannel(GH, L + FVector(0,0,50.f), L - FVector(0,0,700.f), ECC_WorldStatic, GQ)) { continue; }
				}
				Cb->Destroy();
				++NRescued;
			}
			if (NRescued > 0)
			{
				UE_LOG(LogWeedShop, Warning, TEXT("NPC-vangnet: %d gezakte NPC's gerespawned"), NRescued);
			}
			// (NPC-positie-diagnose verwijderd -> geen per-NPC log-spam meer.)
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
	// "INBAKKEN": de zware ombouw-sweeps (deuren/glas/raam/lampen/liften) lopen elk over ALLE actors.
	// Die hoeven ALLEEN te draaien als er nieuwe geometrie is ingestreamd (bWorldDirty via de level-
	// delegates), tijdens de eerste init, of als zeldzame backstop. Anders volledig overslaan -> geen
	// periodieke volle-actor-iteratie = geen periodieke freeze. Eenmaal omgebouwd gebied blijft omgebouwd.
	// Vangnet: flink verplaatst sinds de vorige zware sweep? Dan zijn er waarschijnlijk nieuwe cellen
	// gestreamd (ook als de delegate ze miste) -> opnieuw zwaar scannen. Stilstaan = niet dirty = geen freeze.
	for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
	{
		const APawn* Pp = PIt->Get() ? PIt->Get()->GetPawn() : nullptr;
		if (Pp && FVector::DistSquared(Pp->GetActorLocation(), LastScanPlayerPos) > 6000.f * 6000.f) { bWorldDirty = true; break; }
	}
	// De periodieke %20-backstop BLIJFT: hij vangt LAAT-ingestreamde geometrie op (interieur-lampen/deuren die
	// de dirty-delegate net te vroeg ving - de meshes laden async pas ná de eerste sweep). Eenmaal geclassificeerd
	// (Converted/ConvScanRejected/LampScanSeen) is de sweep goedkoop (alleen Contains-checks). NIET weghalen:
	// anders missen late interieur-lampen hun Movable-stap en blijven kamers 's nachts donker.
	const bool bRunHeavy = bWorldDirty || (ScanPass < 30) || (ScanPass % 20 == 0);
	bWorldDirty = false;
	if (bRunHeavy)
	{
		// onthoud waar we voor het laatst zwaar scanden (voor het verplaatsings-vangnet)
		for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
		{
			const APawn* Pp = PIt->Get() ? PIt->Get()->GetPawn() : nullptr;
			if (Pp) { LastScanPlayerPos = Pp->GetActorLocation(); break; }
		}
	for (TActorIterator<AStaticMeshActor> It(W); It; ++It)
	{
		AStaticMeshActor* SMA = *It;
		if (!IsValid(SMA) || Converted.Contains(SMA) || ConvScanRejected.Contains(SMA)) { continue; }
		UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent();
		const FLeafDef* Leaf = Comp ? MatchLeaf(Comp->GetStaticMesh()) : nullptr;
		// GEEN deur-blad: 1x vaststellen en daarna NOOIT meer onderzoeken (mesh van een actor wijzigt
		// niet). Anders MatchLeaf + geneste volle-actor-frame-scan elke 2s over duizenden props = ~90ms hang.
		if (!Leaf) { ConvScanRejected.Add(SMA); continue; }

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
		if (!IsValid(SMA) || Converted.Contains(SMA) || GlassScanSeen.Contains(SMA)) { continue; }
		UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent();
		if (!Comp || !IsLeafGlass(Comp->GetStaticMesh())) { GlassScanSeen.Add(SMA); continue; } // geen deur-glas: nooit meer checken

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
		if (!IsValid(A) || A->IsA(ACityDoor::StaticClass()) || Converted.Contains(A) || A->IsHidden() || WinFixSeen.Contains(A)) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		WinFixSeen.Add(A); // 1x onderzocht: glas-comps zijn nu gefixt/op-ignore, of er is geen glas - nooit meer scannen
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
			if (!IsValid(AL) || LampScanSeen.Contains(AL)) { continue; }
			TInlineComponentArray<ULocalLightComponent*> Lights(AL);
			LampScanSeen.Add(AL); // 1x onderzocht: lampen staan nu op Movable, of er zijn er geen - nooit meer scannen
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
			if (!IsValid(A) || A->IsA(APackElevator::StaticClass()) || ElevScanSeen.Contains(A)) { continue; }
			TInlineComponentArray<UStaticMeshComponent*> Comps(A);
			bool bElevActor = false;
			for (UStaticMeshComponent* Comp : Comps)
			{
				if (!Comp || !Comp->GetStaticMesh()) { continue; }
				const FString MeshName = Comp->GetStaticMesh()->GetName();
				const FVector L = Comp->GetComponentLocation();
				if (MeshName == TEXT("SM_ElevatorDoorFrame01"))
				{
					bElevActor = true;
					const FIntPoint Key(FMath::RoundToInt(L.X / 100.f), FMath::RoundToInt(L.Y / 100.f));
					FShaft& Sh = Shafts.FindOrAdd(Key);
					Sh.Ref = L;
					Sh.FrameYaw = Comp->GetComponentRotation().Yaw;
					Sh.FloorZ.AddUnique(L.Z);
				}
				else if (MeshName == TEXT("SM_ElevatorDoor"))
				{
					bElevActor = true;
					// Valideer de COMPONENT zelf (niet alleen de actor): tijdens streaming kan 'ie pending-kill
					// zijn terwijl de actor nog IsValid was -> later schrijven (SetMobility) = heap-corruptie.
					if (IsValid(Comp)) { AllPanels.Add(TPair<FVector, UStaticMeshComponent*>(L, Comp)); }
				}
			}
			// Geen lift-mesh? Nooit meer scannen. Wel lift-frames: blijven checken tot de schacht staat (ElevBuilt).
			if (!bElevActor) { ElevScanSeen.Add(A); }
		}
		for (TPair<FIntPoint, FShaft>& KV : Shafts)
		{
			if (ElevBuilt.Contains(KV.Key)) { continue; }
			// CRASH-GUARD: pas een lift bouwen als de wereld STABIEL is (kamer ingestreamd). Tijdens de vroege
			// map-init/world-partition-streaming is de physics/render-scene nog in opbouw; een lift spawnen +
			// componenten registreren raakte die dan -> EXCEPTION_ACCESS_VIOLATION in PackElevator::Setup.
			// Volgende scan-pass proberen we opnieuw zodra de wereld klaar is.
			if (!WeedShop_IsRoomReady()) { continue; }
			// EN: pas bouwen als de LIFT-MESHES echt klaar zijn (niet meer async aan het compileren/laden).
			// "Waiting on static mesh SM_ElevatorNumber_X before playing" -> een mesh/TextRender erop zetten in
			// Setup terwijl de render-data nog niet bestaat = de access violation. Wacht tot ze klaar zijn.
			{
				static const TCHAR* ElevMeshPaths[] = {
					TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorCabin.SM_ElevatorCabin"),
					TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorDoor.SM_ElevatorDoor"),
					TEXT("/Game/CityBeachStrip/Meshes/Architecture/Interiors/Elevator/SM_ElevatorNumber_0.SM_ElevatorNumber_0"),
				};
				bool bMeshesReady = true;
				for (const TCHAR* MP : ElevMeshPaths)
				{
					UStaticMesh* EM = LoadObject<UStaticMesh>(nullptr, MP);
					if (!EM || EM->IsCompiling() || EM->GetRenderData() == nullptr) { bMeshesReady = false; break; }
				}
				if (!bMeshesReady) { continue; } // volgende scan-pass opnieuw proberen
			}
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
			if (RawPanels.Num() > 0 && IsValid(RawPanels[0].Value))
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
					if (RP.Key == Fi && IsValid(RP.Value)) { FloorPanels.Add(RP.Value); }
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
			{ // TEMP [ELEVDIAG]
				FString DiagS;
				for (const FElevPanelInit& Pn : Panels) { DiagS += FString::Printf(TEXT("[F%d closed=(%.0f,%.0f,%.0f) slide=%.0f] "), Pn.FloorIdx, (float)Pn.ClosedPos.X, (float)Pn.ClosedPos.Y, (float)Pn.ClosedPos.Z, Pn.SlideDist); }
				UE_LOG(LogTemp, Verbose, TEXT("[ELEVDIAG] key=(%d,%d) floors=%d rawPanels=%d panels=%d span=(%.2f,%.2f) ref=(%.0f,%.0f) | %s"), KV.Key.X, KV.Key.Y, Floors.Num(), RawPanels.Num(), Panels.Num(), (float)SpanDir.X, (float)SpanDir.Y, (float)KV.Value.Ref.X, (float)KV.Value.Ref.Y, *DiagS); // Verbose: TEMP dev-diag
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
				// Per verdieping: call-knop naast de deur + digit-bordje boven de deur.
				// De KNOP zit op het frame-vlak (16cm diep -> 17cm uit het centrum: dat klopt op
				// deurhoogte). Het BORDJE zit hoger (+255) waar geen frame meer is maar de gang-MUUR:
				// die staat verder naar buiten en varieert per gebouw, dus de 17cm-aanname liet het
				// bordje vrij voor de muur zweven. Fix: per verdieping een line-trace op bord-hoogte
				// naar het echte muurvlak i.p.v. de vaste 17cm.
				// IGNORE-lijst 1x: de lift zelf (cabine/cabine-deuren/knoppen) EN alle hal-schuifpanelen,
				// zodat de trace op de MUUR raakt en niet op een lift-component die er (half) voor staat.
				FCollisionQueryParams SignQP(SCENE_QUERY_STAT(ElevSignWall), false);
				SignQP.AddIgnoredActor(Elev);
				for (const FElevPanelInit& Pn : Panels)
				{
					if (UStaticMeshComponent* PC = Pn.Comp) { SignQP.AddIgnoredComponent(PC); }
				}
				for (int32 Fi = 0; Fi < Floors.Num(); ++Fi)
				{
					// Knop EN bord flush tegen het ECHTE gang-muurvlak (trace op hoogte Z, lateraal naast de deur): de vaste
					// 12/17cm-offset liet ze voor de muur zweven omdat de muur-diepte per gebouw verschilt. Raak -> 0.5cm voor
					// het vlak (flush); mis/onzin -> FallbackCm uit het opening-centrum (altijd zichtbaar).
					auto FlushWallAt = [&](float ZWorld, float LatCm, float FallbackCm) -> FVector
					{
						const FVector Lat = FVector(OpeningCenter.X, OpeningCenter.Y, 0.f) + SlideDir * LatCm;
						FVector Out = Lat - ShaftSide * FallbackCm + FVector(0.f, 0.f, ZWorld);
						FHitResult WH;
						if (W->LineTraceSingleByChannel(WH, Lat - ShaftSide * 150.f + FVector(0.f, 0.f, ZWorld), Lat + ShaftSide * 40.f + FVector(0.f, 0.f, ZWorld), ECC_WorldStatic, SignQP))
						{
							const float OutDist = FVector::DotProduct(WH.ImpactPoint - FVector(OpeningCenter.X, OpeningCenter.Y, 0.f), -ShaftSide);
							if (OutDist > 4.f && OutDist < 90.f) { Out = WH.ImpactPoint + WH.Normal * 0.5f; }
						}
						return Out;
					};
					const FRotator BtnRot = (-ShaftSide).Rotation();
					const FVector BtnLoc = FlushWallAt(Floors[Fi] + 115.f, 110.f, 12.f); // knop NAAST de deur // flush tegen de muur (trace) i.p.v. vaste 12cm
					if (APackElevatorButton* Btn = W->SpawnActor<APackElevatorButton>(APackElevatorButton::StaticClass(), FTransform(BtnRot, BtnLoc), SP))
					{
						Btn->Setup(Elev, Fi);
						// Bordje op dezelfde manier flush tegen het gang-muurvlak (zelfde helper als de knop, bord-hoogte).
						const FVector SignLoc = FlushWallAt(Floors[Fi] + 255.f, 0.f, 12.f); // bord GECENTREERD boven de deur
						Btn->SetupSign(SignLoc, BtnRot);
						Elev->RegisterButton(Btn);
					}
				}
				UE_LOG(LogWeedShop, Display, TEXT("PackElevator gebouwd: %d verdiepingen @ (%.0f, %.0f)"), Floors.Num(), CabCenter.X, CabCenter.Y);
			}
		}
	}
	} // einde bRunHeavy: zware ombouw-sweeps overgeslagen als er niks instreamt (geen periodieke freeze)

	// CloneRooms(); // UIT op verzoek: kamer-klonen gaf muren/vloeren op verkeerde plekken. Code blijft staan voor een latere, betere aanpak (kamer op maat van het slot).
	// BuildMarkedRooms(); // UIT: marker-kopie werkte niet lekker - vervangen door de dev building-tool (muren/vloeren tekenen via het bouw-systeem)
	VerticalReplicate();
	ApplySavedStamps();

	if (NewThisPass > 0)
	{
		UE_LOG(LogWeedShop, Log, TEXT("DoorRetrofitter: %d nieuwe deuren werkend gemaakt (totaal %d)"), NewThisPass, TotalConverted);
	}

	// ADAPTIEVE CADANS: heeft deze pass iets nieuws verwerkt (set gegroeid of deur gebouwd)?
	const int32 _scanWork1 = Converted.Num() + ConvScanRejected.Num() + GlassScanSeen.Num()
		+ WinFixSeen.Num() + LampScanSeen.Num() + ElevScanSeen.Num() + ElevBuilt.Num();
	const bool bWorked = (_scanWork1 != _scanWork0) || (NewThisPass > 0) || (PendingResidents.Num() > 0);
	if (bWorked) { ScanIdleStreak = 0; } else { ++ScanIdleStreak; }
	// 3 passes niks nieuws -> wereld is uitgestreamd/stabiel: scan 4x trager (8s i.p.v. 2s) zodat de
	// onvermijdelijke volle-actor-sweep-hitch veel minder vaak gebeurt. Streamt er weer iets in (set
	// groeit), dan meteen terug naar 2s zodat nieuwe deuren/liften snel werkend worden.
	// Zolang de loading-cover op beeld staat blijven we juist VERSNELD (0.4s) scannen: de hitches
	// zijn onzichtbaar en de stad staat er sneller (de cover wacht op CityConverted hieronder).
	bScanSlow = (ScanIdleStreak >= 3);
	const float WantRate = IsCoverPhase() ? 0.4f : (bScanSlow ? 8.0f : 2.0f);
	if (!FMath::IsNearlyEqual(WantRate, ScanRate))
	{
		ScanRate = WantRate;
		GetWorldTimerManager().SetTimer(ScanTimer, this, &ADoorRetrofitter::ScanAndConvert, ScanRate, true);
	}

	// VINDBARE JOINTS: één keer scatteren zodra de beach-geometrie (prullenbakken/bankjes) is ingestreamd.
	// ScatterJoints() zet bJointsScattered alleen op true als het echt spots vond + joints plaatste; lukte
	// dat (nog) niet, dan blijft de vlag false en proberen we het de volgende pass opnieuw.
	if (!bJointsScattered && ScanPass >= 2) { ScatterJoints(); }

	// LOADING-COVER-SIGNAAL "stad omgebouwd": de gebakken kamers (BakedRooms-overlay) zijn geladen +
	// zichtbaar, DEZE sweep vond geen nieuwe geometrie meer om om te bouwen, en de joints zijn
	// gescatterd (of het ScanPass>=4-vangnet: op co-op clients scattert de server-only ScatterJoints
	// nooit, en als de spots-geometrie uitblijft willen we de cover ook niet eeuwig vasthouden).
	// LET OP: bewust NIET op ScanIdleStreak gaten - die blijft 0 zolang PendingResidents leegdruppelt
	// (1 bewoner per 5 passes, tot 12 in de rij = minuten) en dat is gespreide gameplay, geen pop-in.
	// De BootCoverWidget wacht hierop; reset gebeurt in WeedShop_RequestGameLoadingScreen.
	if (!WeedShop_IsCityConverted())
	{
		const bool bSweepIdle = (_scanWork1 == _scanWork0) && (NewThisPass == 0);
		const bool bOverlayReady = !BakedOverlay.IsValid() || (BakedOverlay->IsLevelLoaded() && BakedOverlay->IsLevelVisible());
		if (bOverlayReady && bSweepIdle && (bJointsScattered || ScanPass >= 4))
		{
			WeedShop_SetCityConverted(true);
			UE_LOG(LogWeedShop, Display, TEXT("Stad omgebouwd (pass %d, %d deuren, joints=%d) -> loading-cover mag verder"), ScanPass, TotalConverted, bJointsScattered ? 1 : 0);
		}
	}
}

// ============================ VINDBARE JOINTS (scatter rond prullenbak/bankje/asbak/kliko) ============================
// Server-only. Spawnt vindbare joint-pickups rond de prullenbakken, bankjes, sigaretten-asbakken en
// kliko's/vuilcontainers van de beach-strip. Elke joint
// = random strain + papier-afgeleid gewicht + THC% + kwaliteit%, gewogen zodat GOEDE joints (hoge kwaliteit,
// hoge-THC strain, grote backwoods) ZELDZAAM zijn. De speler loopt erheen en pakt 'm op (AWorldItemPickup-flow).

// 1 random joint spawnen op Loc. Geeft de pickup terug (of nullptr als de store ontbreekt / spawn faalt).
AWorldItemPickup* ADoorRetrofitter::MintJointAt(const FVector& Loc)
{
	UWorld* World = GetWorld();
	if (!World) { return nullptr; }
	AWeedShopGameState* GS = World->GetGameState<AWeedShopGameState>();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return nullptr; }

	// Strains gesorteerd op BaseThc OPLOPEND (index 0 = laagste THC).
	TArray<FName> Strains = Store->GetSeedCatalog();
	if (Strains.Num() == 0) { return nullptr; }
	Strains.Sort([Store](const FName& A, const FName& B)
	{
		float ThcA = 0.f, ThcB = 0.f, Y = 0.f, Gr = 0.f;
		Store->GetStrainStats(A, ThcA, Y, Gr);
		Store->GetStrainStats(B, ThcB, Y, Gr);
		return ThcA < ThcB;
	});

	// Speler-level -> tier-fractie (0 op lvl 1, 1 op lvl 50).
	// CO-OP/COMPETITIVE: seed-scatter is BEWUST GEDEELD (gedeelde wereld, geen speler-eigenaar) -> nullptr = crew-waarde.
	int32 Level = 1;
	if (GS->GetLeveling()) { Level = GS->GetLeveling()->GetLevelFor(nullptr); }
	const float LevelFrac = FMath::Clamp((Level - 1) / 49.f, 0.f, 1.f);

	// Strain schaalt mee met je tier: de index volgt je level (hogere strains naarmate je levelt). (1 - FRand^1.6)
	// weegt NAAR je tier maar houdt een staart naar beneden -> meestal rond je tier, soms een mindere. Cap op
	// je level +1 (zelden één tier boven je level = een leuke vondst), nooit ver erboven.
	const int32 N = Strains.Num();
	const int32 LevelIdx = FMath::Clamp(FMath::RoundToInt(LevelFrac * (N - 1)), 0, N - 1);
	const int32 MaxIdx = FMath::Clamp(LevelIdx + 1, 0, N - 1);
	const int32 Idx = FMath::Clamp(FMath::FloorToInt((MaxIdx + 1) * (1.f - FMath::Pow(FMath::FRand(), 1.6f))), 0, MaxIdx);
	const FName Strain = Strains[Idx];
	float BaseThc = 0.f, YieldG = 0.f, GrowM = 0.f;
	Store->GetStrainStats(Strain, BaseThc, YieldG, GrowM);

	// Papier-gewicht, gewogen naar klein (kleine joint gewoon, 10g backwoods zeldzaam).
	const float R = FMath::FRand();
	const int32 Grams = (R < 0.55f) ? 2 : (R < 0.85f) ? 5 : (R < 0.96f) ? 7 : 10;

	// THC% dicht bij de strain-basis, nooit erboven.
	const float ThcPercent = BaseThc * FMath::FRandRange(0.85f, 1.0f);
	// Kwaliteit% schaalt mee met level: de power-exponent zakt van ~2.8 (lvl 1, zwaar bottom-loaded) naar ~0.8
	// (lvl 50, vaker hoog), maar houdt altijd een staart naar laag -> soms nog een slechte.
	const float QExp = FMath::Clamp(2.8f - 2.0f * LevelFrac, 0.8f, 2.8f);
	const float QualityPct = 100.f * FMath::Pow(FMath::FRand(), QExp);

	const FName JointId = UInventoryComponent::MakeJointId(Strain, Grams);

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Loc is al op de vloer getraced (PlaceJointAtSpot) -> maar 4cm laten vallen zodat 'ie op z'n plek settelt
	// i.p.v. van hoog te vallen en weg te rollen/tunnelen.
	AWorldItemPickup* P = World->SpawnActor<AWorldItemPickup>(
		AWorldItemPickup::StaticClass(), FTransform(FRotator::ZeroRotator, Loc + FVector(0.f, 0.f, 4.f)), SP);
	if (P) { P->Setup(JointId, 1, ThcPercent, QualityPct); }
	return P;
}

// 1 joint met zijwaartse offset (~40-70cm) rond Spot plaatsen. Héél af en toe (DoubleJointChance, ~5%)
// ligt er een TWEEDE joint naast - zeldzame dubbel-vondst, "moet afentoe blijven". De 2e telt gewoon mee
// in ScatteredJoints en alleen als JointTarget dat nog toelaat, dus de cap blijft gerespecteerd.
// Geeft false terug als de EERSTE spawn faalde (store ontbreekt) zodat de caller kan stoppen.
bool ADoorRetrofitter::PlaceJointAtSpot(const FVector& Spot)
{
	UWorld* W = GetWorld();
	// Willekeurige offset rond de spot, dan naar de ECHTE vloer eronder tracen. Spots zoals asbak-bakken kunnen
	// BOVEN de grond zweven; zonder deze trace spawnt de joint in de lucht en valt/tunnelt hij weg (het echte
	// "geen joints"-probleem). Geen vloer onder het punt (rand/water) -> die offset afwijzen.
	auto GroundedOffset = [&](FVector& Out) -> bool
	{
		const float Ang = FMath::FRandRange(0.f, 2.f * PI);
		const float Rad = FMath::FRandRange(40.f, 70.f);
		const FVector XY = Spot + FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, 0.f);
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(JointGround), false);
		if (W && W->LineTraceSingleByChannel(Hit, XY + FVector(0, 0, 130.f), XY - FVector(0, 0, 500.f), ECC_WorldStatic, Q))
		{
			Out = Hit.ImpactPoint; // op de vloer (MintJointAt zet 'm 4cm hoger voor een korte settle)
			return true;
		}
		return false;
	};

	FVector Loc;
	if (!GroundedOffset(Loc) && !GroundedOffset(Loc)) { return false; } // 2x geen vloer -> spot overslaan (topup pakt 'm later)
	AWorldItemPickup* P = MintJointAt(Loc);
	if (!P) { return false; }
	ScatteredJoints.Add(P);
	if (ScatteredJoints.Num() < JointTarget && FMath::FRand() < DoubleJointChance)
	{
		FVector Loc2;
		if (GroundedOffset(Loc2)) { if (AWorldItemPickup* P2 = MintJointAt(Loc2)) { ScatteredJoints.Add(P2); } }
	}
	return true;
}

// Doel-aantal joints op de map o.b.v. het HUIDIGE speler-level: ~12% van de spots op lvl 1, +5% per 5 levels,
// cap ~60% (en de bovengrens). Zo vind je vroeg WEINIG en groeit het mee terwijl je levelt.
int32 ADoorRetrofitter::LevelJointTarget() const
{
	if (JointSpots.Num() == 0) { return 0; }
	// CO-OP/COMPETITIVE: joint-scatter is BEWUST GEDEELD (gedeelde wereld, geen speler-eigenaar) -> nullptr = crew-waarde.
	int32 Level = 1;
	if (UWorld* W = GetWorld()) { if (AWeedShopGameState* GS = W->GetGameState<AWeedShopGameState>()) { if (GS->GetLeveling()) { Level = GS->GetLeveling()->GetLevelFor(nullptr); } } }
	// Meer joints over de map (was 12%->60% = ~23 vroeg; speler kwam er zo nooit een tegen). Nu 30%->70%.
	const float Frac = FMath::Clamp(0.30f + (Level / 5) * 0.05f, 0.30f, 0.70f);
	return FMath::Clamp(FMath::RoundToInt(JointSpots.Num() * Frac), 8, FMath::Min(MaxScatteredJoints, JointSpots.Num()));
}

// One-time: spot-locaties (prullenbak/bankje/asbak/kliko) verzamelen en tot de cap vullen. Vond het geen spots, dan blijft
// bJointsScattered false zodat de volgende scan-pass het opnieuw probeert (geometrie nog niet ingestreamd).
void ADoorRetrofitter::ScatterJoints()
{
	if (IsNetMode(NM_Client)) { return; } // CO-OP: crowd/joints alleen op de ECHTE server; DoorRetrofitter is per-proces + niet-gerepliceerd, dus HasAuthority() is OOK true op de joiner
	UWorld* W = GetWorld();
	if (!W) { return; }

	// Spots verzamelen: static-mesh-actors met een prullenbak/bankje-mesh (basis, telt altijd) of een
	// asbak/kliko/container-mesh (nieuw spot-type, telt met LAGERE kans mee zodat "een rondje joints
	// zoeken" niet opeens overal raak is).
	JointSpots.Reset();
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) { continue; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Comp : Comps)
		{
			if (!Comp || !Comp->GetStaticMesh()) { continue; }
			const FString MeshName = Comp->GetStaticMesh()->GetName();
			// Basis-spots (bankjes + straat-prullenbakken): volle dichtheid, zoals altijd.
			const bool bBaseSpot = MeshName.Contains(TEXT("SM_Bench")) || MeshName.Contains(TEXT("StreetTrashBin"));
			// Nieuwe spot-types: sigaretten-prullenbakken en grote kliko's/containers (o.a. SM_AshtrayBin,
			// SM_Dumpster, MetalTrashCan). "Trash"/"Garbage" dekken de meeste can/bin/container-varianten;
			// bewust GEEN kaal "Bin"/"Container" (zou Cabinet/zeecontainers e.d. matchen).
			const bool bExtraSpot = !bBaseSpot && (
				MeshName.Contains(TEXT("Ashtray")) || MeshName.Contains(TEXT("Dumpster")) ||
				MeshName.Contains(TEXT("Trash")) || MeshName.Contains(TEXT("Garbage")) ||
				MeshName.Contains(TEXT("Wheelie")) || MeshName.Contains(TEXT("Recycl")));
			// Nieuw type telt maar met NewJointSpotAcceptChance mee -> per-object-kans op een joint blijft
			// LAGER dan bij de basis-spots (zelfde fractie over minder geaccepteerde spots).
			if (bBaseSpot || (bExtraSpot && FMath::FRand() < NewJointSpotAcceptChance))
			{
				const FVector L = Comp->GetComponentLocation();
				if (L.ContainsNaN()) { continue; } // implausibele spot overslaan
				JointSpots.Add(L);
			}
		}
	}
	if (JointSpots.Num() == 0) { return; } // niets gevonden -> volgende pass opnieuw proberen

	ScatteredJoints.Reset();
	// Aantal schaalt met je level (LevelJointTarget): vroeg WEINIG, later meer. Schud de spots en plaats 1 joint
	// per gekozen spot (+ zijwaartse offset ~40-70cm) zodat ze netjes over de hele map verspreid liggen.
	for (int32 s = JointSpots.Num() - 1; s > 0; --s) { JointSpots.Swap(s, FMath::RandRange(0, s)); }
	JointTarget = LevelJointTarget();
	for (int32 si = 0; si < JointSpots.Num() && ScatteredJoints.Num() < JointTarget; ++si)
	{
		PlaceJointAtSpot(JointSpots[si]); // spawn-fail = store nog niet klaar; volgende spots proberen kan geen kwaad
	}

	// KRITISCH: alleen als klaar markeren als er ECHT joints geplaatst zijn. Vond hij wel spots maar faalde
	// ELKE spawn (store/seed-catalogus nog niet klaar), dan NIET als klaar markeren -> anders bleef de map de
	// hele sessie leeg. In de editor is de store snel genoeg klaar (leek te werken); in de PACKAGED build laadt
	// dat later, dus daar spawnde er niks. Fix: bij 0 geplaatst kort opnieuw proberen tot de store er wel is.
	if (ScatteredJoints.Num() == 0)
	{
		UE_LOG(LogWeedShop, Log, TEXT("Joint-scatter: %d spots gevonden maar 0 geplaatst (store/catalogus nog niet klaar) -> retry over 3s"), JointSpots.Num());
		GetWorldTimerManager().SetTimer(JointRespawnTimer, this, &ADoorRetrofitter::ScatterJoints, 3.f, false); // los van de scan-tick (die idle kan gaan)
		return; // bJointsScattered blijft false
	}

	bJointsScattered = true;
	UE_LOG(LogWeedShop, Log, TEXT("Joints gescatterd: %d pickups rond %d spots (prullenbak/bankje/asbak/kliko)"),
		ScatteredJoints.Num(), JointSpots.Num());

	// Heel LANGZAAM bijvullen (~10 min): een leeggeraapte plek blijft lang leeg, zodat zelf joints draaien
	// nut blijft houden. Refill gaat tot JointTarget.
	GetWorldTimerManager().SetTimer(JointRespawnTimer, this, &ADoorRetrofitter::TopUpJoints, 600.f, true);
}

// Respawn-tick: dode/ongeldige entries prunen en de map weer bijvullen tot de cap.
void ADoorRetrofitter::TopUpJoints()
{
	if (IsNetMode(NM_Client)) { return; } // CO-OP: crowd/joints alleen op de ECHTE server; DoorRetrofitter is per-proces + niet-gerepliceerd, dus HasAuthority() is OOK true op de joiner
	UWorld* W = GetWorld();
	if (!W) { return; }

	ScatteredJoints.RemoveAll([](const TWeakObjectPtr<AWorldItemPickup>& P) { return !P.IsValid(); });

	// Doel opnieuw o.b.v. je HUIDIGE level -> het aantal joints op de map groeit mee terwijl je levelt.
	JointTarget = LevelJointTarget();

	// Refill verschijnt ERGENS ANDERS: alleen op spots die nu VRIJ zijn (geen levende joint binnen ~120cm),
	// geschud voor spreiding. Een opgeraapte joint komt na ~10 min dus terug op een andere lege plek -
	// niet pal op dezelfde plek en niet bovenop een volle.
	auto SpotOccupied = [this](const FVector& Spot) -> bool
	{
		for (const TWeakObjectPtr<AWorldItemPickup>& J : ScatteredJoints)
		{
			if (J.IsValid() && FVector::DistSquared2D(J->GetActorLocation(), Spot) < FMath::Square(120.f)) { return true; }
		}
		return false;
	};
	TArray<int32> Free;
	for (int32 i = 0; i < JointSpots.Num(); ++i) { if (!SpotOccupied(JointSpots[i])) { Free.Add(i); } }
	for (int32 s = Free.Num() - 1; s > 0; --s) { Free.Swap(s, FMath::RandRange(0, s)); }
	for (int32 fi = 0; fi < Free.Num() && ScatteredJoints.Num() < JointTarget; ++fi)
	{
		if (!PlaceJointAtSpot(JointSpots[Free[fi]])) { break; } // store/spawn faalt -> niet eindeloos doorlopen
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
	// ALLEEN puien NABIJ een speler proberen. Op de world-partition beach-map is de gevel-geometrie
	// van verre puien NIET ingestreamd -> hun traces lossen nooit op -> ze blijven onafgehandeld en
	// werden zo ELKE 2s opnieuw ge-traced (24 puien x ~35 line-traces = ~90ms hang, nonstop). Vlakbij
	// een speler IS de gevel er wel: daar lost de pui in 1-2 passes op, krijgt 'PuiFixed' en valt weg.
	TArray<FVector> Players;
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APawn* Pp = It->Get() ? It->Get()->GetPawn() : nullptr) { Players.Add(Pp->GetActorLocation()); }
	}
	if (Players.Num() == 0) { return; }
	const float NearSq = 9000.f * 9000.f; // ~90m: binnen streaming-bereik

	// Onbehandelde pui-deuren verzamelen; traces negeren ALLE werkende deuren (anders meet je
	// je eigen blad in plaats van de gevel).
	TArray<ACityDoor*> Puis;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(PuiHole), false);
	for (TActorIterator<ACityDoor> It(W); It; ++It)
	{
		if (!IsValid(*It)) { continue; }
		Q.AddIgnoredActor(*It);
		if (It->ActorHasTag(TEXT("BalcDoor")) && !It->ActorHasTag(TEXT("PuiFixed")))
		{
			const FVector PL = It->GetActorLocation();
			for (const FVector& PP : Players)
			{
				if (FVector::DistSquared(PP, PL) < NearSq) { Puis.Add(*It); break; }
			}
		}
	}
	if (Puis.Num() == 0) { return; }

	TSet<ACityDoor*> Done;
	int32 Processed = 0;
	const int32 MaxPerCall = 6; // harde cap: ook in een dichte cluster nooit een spike
	for (ACityDoor* D : Puis)
	{
		if (Done.Contains(D)) { continue; }
		if (Processed >= MaxPerCall) { break; }
		++Processed;
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
		UE_LOG(LogWeedShop, Verbose, TEXT("PuiHole: gat %.0f breed op s=%.0f bij (%.0f, %.0f, %.0f) - %s"), // Verbose: schuifpui-gat-carving setup-detail
			HoleW, HoleC, C0.X, C0.Y, C0.Z, Mate ? TEXT("2 bladen verdeeld") : TEXT("1 blad gecentreerd"));
	}
}

bool ADoorRetrofitter::HomePawnNow(APlayerController* PC)
{
	// EEN speler DIRECT thuiszetten (server-authoritative). Aangeroepen door de GameMode meteen na RestartPlayer
	// (joiner-spawn) en als vangnet door de scan-pass. Zelfde plaats-of-bevries-logica als de normale home-blokken
	// hierboven, maar dan gericht op precies deze pawn zodat de gerepliceerde positie al "thuis" is voordat de
	// joiner possesst -> geen zichtbare beach-tussenstaat op de host.
	UWorld* W = GetWorld();
	if (!W || !PC) { return false; }
	// SERVER-ONLY: op een echte client teleporteren we NIET zelf (dat zou op 2 PC's kunnen afwijken -> rubber-band).
	// De server verplaatst de pawn en repliceert de positie naar de joiner; de client houdt alleen z'n eigen
	// vloer-pin (TickVirtualMove) aan. DoorRetrofitter is per-proces + niet-gerepliceerd, dus filteren we expliciet
	// op NM_Client (HasAuthority is op de joiner OOK true).
	if (W->IsNetMode(NM_Client)) { return false; }

	APawn* Pw = PC->GetPawn();
	if (!Pw || HomedPawns.Contains(Pw)) { return false; }

	// Thuis-plek nog niet bekend? -> nu niet forceren; de scan-pass-homing pakt 'm alsnog zodra HomeAnchor er is.
	if (HomeAnchor.IsNearlyZero()) { return false; }

	// COMPETITIVE: op de HOST staat HomeAnchor op de host-kamer (603). Een NIET-lokale pawn (= de remote joiner)
	// hoort in de joiner-kamer (Mk[1]=602) -> die meteen daar neerzetten i.p.v. in 603 (geen 603-flits).
	FVector PawnHome = HomeAnchor;
	if (!Pw->IsLocallyControlled())
	{
		AWeedShopGameState* GSc = W->GetGameState<AWeedShopGameState>();
		if (GSc && GSc->IsCompetitive())
		{
			TArray<FVector> MkL; GetCompetitiveMarkers(MkL);
			if (MkL.Num() >= 2) { PawnHome = MkL[1] + FVector(0.f, 0.f, 110.f); }
		}
	}

	const int32 Slot = HomedPawns.Num();
	HomedPawns.Add(Pw);
	const FVector Off(((Slot % 2) ? 130.f : -130.f) * (Slot > 0 ? 1.f : 0.f), 0.f, 0.f);
	UCharacterMovementComponent* CMv = Pw->FindComponentByClass<UCharacterMovementComponent>();

	if (bRoomFloorReady)
	{
		// Vloer is AL ingestreamd (partner joint ná de load) -> settle-loop draait niet meer, dus DIRECT op de
		// vloer + lopend (een zweef-bevriezing zou hier nooit ontdooien). Zelfde korte CENTER-trace als de
		// host-settle; zij-offset alleen op de PLAATSING (anders trace je naast de vloer op een dak/terras).
		FHitResult FH; FCollisionQueryParams LQ(SCENE_QUERY_STAT(HomeLandNow), false);
		for (FConstPlayerControllerIterator It2 = W->GetPlayerControllerIterator(); It2; ++It2)
		{ if (APawn* P2 = It2->Get() ? It2->Get()->GetPawn() : nullptr) { LQ.AddIgnoredActor(P2); } }
		const FVector TS = PawnHome + FVector(0.f, 0.f, 30.f);
		FVector Dest = PawnHome + Off;
		if (W->LineTraceSingleByChannel(FH, TS, TS - FVector(0.f, 0.f, 260.f), ECC_WorldStatic, LQ))
		{ Dest = FH.Location + FVector(Off.X, Off.Y, 96.f); }
		Pw->SetActorLocation(Dest, false, nullptr, ETeleportType::TeleportPhysics);
		if (CMv) { CMv->StopMovementImmediately(); CMv->SetMovementMode(MOVE_Walking); }
		// Lokale speler direct op de (al klare) vloer geplaatst -> op z'n eindpositie, de join-cover mag weg.
		if (Pw->IsLocallyControlled()) { WeedShop_SetLocalPawnPlaced(true); }
	}
	else
	{
		// Vloer nog niet klaar -> METEEN BEVRIEZEN (vliegen, geen zwaartekracht) zodat je niet door de nog-ladende
		// world-partition vloer valt; de floor-pin in TickVirtualMove zet je op MOVE_Walking en op de ECHTE vloer
		// zodra die is ingestreamd. Settle-venster open zetten.
		Pw->SetActorLocation(PawnHome + Off, false, nullptr, ETeleportType::TeleportPhysics);
		if (CMv) { CMv->StopMovementImmediately(); CMv->SetMovementMode(MOVE_Flying); }
		HomeSettleUntil = W->GetRealTimeSeconds() + 45.f;
	}

	// Beach-map: ken de starter-woning toe in de phone-registry (idempotent via bPropertyInit).
	if (UPhoneClientComponent* Phw = Pw->FindComponentByClass<UPhoneClientComponent>()) { Phw->PropertyTick(); }
	UE_LOG(LogWeedShop, Log, TEXT("HomePawnNow: pawn direct thuisgezet op (%.0f,%.0f,%.0f) [local=%d, floorReady=%d]"),
		PawnHome.X, PawnHome.Y, PawnHome.Z, Pw->IsLocallyControlled() ? 1 : 0, bRoomFloorReady ? 1 : 0);
	return true;
}

void ADoorRetrofitter::TickVirtualMove()
{
	// THUIS-SETTLE (10Hz): val je vlak na het thuis-teleporteren door de nog-niet-geladen
	// penthouse-vloer, dan zet ik je terug op de thuis-plek tot de vloer-collision er is. Stopt
	// zodra het venster om is of je bewust wegloopt (XY > 6m van de thuis-plek).
	if (UWorld* WS = GetWorld())
	{
		// VLOER-PIN: de world-partition penthouse-vloer is vlak na het thuis-teleporteren vaak nog niet
		// ingestreamd. We HOUDEN de speler op de thuis-plek (zodat 'ie NIET door het gebouw valt) en checken
		// elke tik of de vloer er al is (down-trace, WorldStatic). Zodra de vloer er is: speler er netjes
		// bovenop + loslaten. Een absolute cap (HomeSettleUntil) voorkomt eindeloos pinnen als er iets misgaat.
		// De thuis-plek (HomeAnchor) is in competitive vroeg overschreven naar de host-marker, dus deze pin
		// zet de speler gewoon in z'n eigen kamer neer (normale settle, geen apart competitive-gevecht meer).
		if (!bRoomFloorReady && !HomeAnchor.IsNearlyZero())
		{
			FCollisionQueryParams FQ(SCENE_QUERY_STAT(RoomFloor), false);
			for (FConstPlayerControllerIterator It = WS->GetPlayerControllerIterator(); It; ++It)
			{
				if (APawn* P0 = It->Get() ? It->Get()->GetPawn() : nullptr) { FQ.AddIgnoredActor(P0); }
			}
			const FVector TS = HomeAnchor + FVector(0.f, 0.f, 30.f);
			// ALLEEN de EIGEN vloer (260cm). Een verdieping lager zit ~410cm+; die pakken we NOOIT, anders land je
			// op een LAGERE verdieping = een ander appartement (precies de "ik spawn opeens ergens anders"-bug).
			// Eigen vloer nog niet ingestreamd? -> bevroren blijven (zie hieronder) tot 'ie er is; NIET omlaag vallen.
			FHitResult FloorHit;
			const bool bPlace = WS->LineTraceSingleByChannel(FloorHit, TS, TS - FVector(0.f, 0.f, 260.f), ECC_WorldStatic, FQ);
			const FVector PlaceLoc = bPlace ? FloorHit.Location : FVector::ZeroVector;

			for (FConstPlayerControllerIterator It = WS->GetPlayerControllerIterator(); It; ++It)
			{
				APawn* Pp = It->Get() ? It->Get()->GetPawn() : nullptr;
				if (!Pp || !HomedPawns.Contains(Pp)) { continue; }
				if (FVector::Dist2D(Pp->GetActorLocation(), HomeAnchor) > 900.f) { continue; } // bewust weggelopen
				UCharacterMovementComponent* CMv = Pp->FindComponentByClass<UCharacterMovementComponent>();
				if (bPlace)
				{
					// Vloer (eigen, of - laatste redmiddel - eronder) gevonden -> netjes erbovenop + lopen.
					Pp->SetActorLocation(PlaceLoc + FVector(0.f, 0.f, 96.f), false, nullptr, ETeleportType::TeleportPhysics);
					if (CMv) { CMv->StopMovementImmediately(); CMv->SetMovementMode(MOVE_Walking); }
					// De LOKALE speler staat nu op z'n eindpositie (op de echte vloer) -> de join-cover mag weg.
					if (Pp->IsLocallyControlled()) { WeedShop_SetLocalPawnPlaced(true); }
				}
				else
				{
					// Nog GEEN vloer (ook niet diep) -> vasthouden op de thuis-plek en BEVROREN (vliegen,
					// geen zwaartekracht). NOOIT ontdooien zonder vloer, ook niet na de cap -> je valt dus
					// nooit door de wereld; hooguit zweef je heel even langer tot de vloer instreamt.
					Pp->SetActorLocation(HomeAnchor, false, nullptr, ETeleportType::TeleportPhysics);
					if (CMv) { CMv->StopMovementImmediately(); CMv->SetMovementMode(MOVE_Flying); }
				}
			}
			// Pas 'klaar' melden (laadscherm mag weg) als je ECHT op een vloer staat. Geen vloer = bevroren
			// blijven en de pin blijft draaien tot de vloer er is; de cover heeft z'n eigen tijd-cap.
			if (bPlace) { bRoomFloorReady = true; WeedShop_SetRoomReady(true); }
		}
		// JOIN-COVER (per-proces, ook op de JOINER): zodra de LOKALE speler-pawn bestaat, op de vloer staat
		// (MOVE_Walking, niet meer bevroren) en de kamer-vloer klaar is, is 'ie op z'n eindpositie -> de join-cover
		// mag weg. Vangt de joiner die z'n eindpositie via REPLICATIE kreeg (floor-pin-blok hierboven draait dan
		// niet, want bRoomFloorReady stond al true). Idempotent (de setter is een simpele bool).
		if (bRoomFloorReady && !WeedShop_IsLocalPawnPlaced())
		{
			for (FConstPlayerControllerIterator It = WS->GetPlayerControllerIterator(); It; ++It)
			{
				APawn* Lp = It->Get() ? It->Get()->GetPawn() : nullptr;
				if (!Lp || !Lp->IsLocallyControlled()) { continue; }
				const UCharacterMovementComponent* CMv = Lp->FindComponentByClass<UCharacterMovementComponent>();
				if (!CMv || CMv->MovementMode != MOVE_Flying) { WeedShop_SetLocalPawnPlaced(true); }
			}
		}
	}
	// CO-OP: de crowd-SIMULATIE + materialisatie is server-only (de host bezit de bodies; de client
	// krijgt ze via replicatie). De THUIS-SETTLE hierboven draait bewust op BEIDE processen (elk pint
	// z'n eigen lokale speler). Op de client is Crowd sowieso leeg (seed is server-gegate), maar de
	// expliciete gate maakt zeker dat de client geen bodies materialiseert (geen dubbele NPC's).
	if (IsNetMode(NM_Client)) { return; } // CO-OP: crowd/joints alleen op de ECHTE server; DoorRetrofitter is per-proces + niet-gerepliceerd, dus HasAuthority() is OOK true op de joiner
	if (GraphNodes.Num() < 2 || Crowd.Num() == 0) { return; }
	const float Step = 16.5f; // 165 cm/s wandeltred op een 0,1s-tik: vloeiend op de kaart
	for (FVirtualWalker& V : Crowd)
	{
		if (V.Body.IsValid()) { continue; } // lichaam beweegt zichzelf
		const FVector Tgt = GraphNodes[V.NextIdx];
		const FVector To2 = Tgt - V.Pos;
		const float D2 = To2.Size2D();
		if (D2 > Step)
		{
			V.Pos += To2.GetSafeNormal2D() * Step;
			continue;
		}
		V.Pos = Tgt;
		// Volgende knoop: rechtdoor-voorkeur, geen U-bocht, met strip-voorkeur.
		const TArray<int32>& Nb = GraphAdj[V.NextIdx];
		if (Nb.Num() == 0) { continue; }
		int32 Pick = Nb[FMath::RandRange(0, Nb.Num() - 1)];
		FVector InDir = FVector::ZeroVector;
		if (GraphNodes.IsValidIndex(V.PrevIdx)) { InDir = (GraphNodes[V.NextIdx] - GraphNodes[V.PrevIdx]).GetSafeNormal2D(); }
		int32 Straight = -1;
		float BestDot = -2.f;
		for (int32 NbIdx : Nb)
		{
			if (NbIdx == V.PrevIdx) { continue; }
			const float Dot = InDir.IsNearlyZero() ? 0.f : FVector::DotProduct(InDir, (GraphNodes[NbIdx] - GraphNodes[V.NextIdx]).GetSafeNormal2D());
			if (Dot > BestDot) { BestDot = Dot; Straight = NbIdx; }
		}
		if (Straight >= 0 && BestDot > 0.5f && FMath::FRand() < 0.75f) { Pick = Straight; }
		else if (Pick == V.PrevIdx && Nb.Num() > 1)
		{
			for (int32 t = 0; t < 4 && Pick == V.PrevIdx; ++t) { Pick = Nb[FMath::RandRange(0, Nb.Num() - 1)]; }
		}
		if (V.bStripLover && Nb.Num() > 1)
		{
			const bool bCurIn = GraphNodes[V.NextIdx].X > -1500.f;
			const bool bPickIn = GraphNodes[Pick].X > -1500.f;
			if (bCurIn && !bPickIn && FMath::FRand() < 0.85f)
			{
				for (int32 NbIdx : Nb)
				{
					if (NbIdx != V.PrevIdx && GraphNodes[NbIdx].X > -1500.f) { Pick = NbIdx; break; }
				}
			}
			else if (!bCurIn && FMath::FRand() < 0.5f)
			{
				int32 BestNb = Pick;
				float BestX = -TNumericLimits<float>::Max();
				for (int32 NbIdx : Nb)
				{
					if (NbIdx == V.PrevIdx) { continue; }
					if (GraphNodes[NbIdx].X > BestX) { BestX = GraphNodes[NbIdx].X; BestNb = NbIdx; }
				}
				Pick = BestNb;
			}
		}
		V.PrevIdx = V.NextIdx;
		V.NextIdx = Pick;
	}

	// Lichamen materialiseren/opruimen: NIET elke 10Hz-tick (dat gaf een spawn-cascade: een ~30ms modulaire
	// NPC-build maakt de frame > 0.1s -> de timer haalt in -> nog een spawn -> erger en erger). Maar 1x per
	// 5 ticks (~0.5s) zodat de spawns netjes gespreid binnendruppelen zonder de frame te laten hangen.
	if (++CrowdSubTick >= 5) { CrowdSubTick = 0; TickVirtualCrowd(); }
}

void ADoorRetrofitter::GetVirtualWalkerPositions(TArray<FVector>& Out) const
{
	for (const FVirtualWalker& V : Crowd)
	{
		if (!V.Body.IsValid()) { Out.Add(V.Pos); }
	}
}

void ADoorRetrofitter::TickVirtualCrowd()
{
	UWorld* W = GetWorld();
	// CO-OP: alleen de server materialiseert fysieke NPC-bodies (SpawnActorDeferred<ACustomerBase> hieronder);
	// de client krijgt ze via replicatie. Defensieve gate zodat geen enkele (toekomstige) caller op de client
	// bodies spawnt -> geen dubbele/spook-NPC's op de joiner.
	if (!W || IsNetMode(NM_Client) || GraphNodes.Num() < 2 || Crowd.Num() == 0) { return; }
	CrowdBodies.RemoveAll([](const TObjectPtr<ACustomerBase>& X){ return !IsValid(X); }); // dode refs opruimen
	// Speler-posities en kijkrichtingen cachen.
	TArray<FVector> PlayerPos;
	TArray<FVector> PlayerView;
	for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
	{
		const APlayerController* PC = PIt->Get();
		const APawn* Pp = PC ? PC->GetPawn() : nullptr;
		if (!Pp) { continue; }
		PlayerPos.Add(Pp->GetActorLocation());
		PlayerView.Add(PC->GetControlRotation().Vector());
	}
	auto MinPlayerDist = [&](const FVector& P) -> float
	{
		float M = TNumericLimits<float>::Max();
		for (const FVector& PP : PlayerPos) { M = FMath::Min(M, FVector::Dist2D(PP, P)); }
		return M;
	};
	// Voor SPAWN-verstoppen: alleen dichtbij relevant (verder weg valt verschijnen niet op).
	auto InAnyView = [&](const FVector& P) -> bool
	{
		for (int32 pi = 0; pi < PlayerPos.Num(); ++pi)
		{
			const FVector To = P - PlayerPos[pi];
			if (To.Size2D() < 6000.f && FVector::DotProduct(PlayerView[pi], To.GetSafeNormal()) > 0.1f) { return true; }
		}
		return false;
	};
	// Voor DESPAWN: op ELKE afstand - iemand die 300m verderop recht voor je loopt mag nooit
	// voor je ogen oplossen.
	auto InAnyViewFar = [&](const FVector& P) -> bool
	{
		for (int32 pi = 0; pi < PlayerPos.Num(); ++pi)
		{
			const FVector To = P - PlayerPos[pi];
			if (FVector::DotProduct(PlayerView[pi], To.GetSafeNormal()) > 0.05f) { return true; }
		}
		return false;
	};
	auto StreetAt = [&](const FVector& P, FVector& OutGround) -> bool
	{
		FHitResult H;
		if (!W->LineTraceSingleByChannel(H, P + FVector(0.f, 0.f, 300.f), P - FVector(0.f, 0.f, 300.f), ECC_Visibility)) { return false; }
		const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(H.GetComponent());
		if (!SMC || !SMC->GetStaticMesh()) { return false; }
		const FString Nm = SMC->GetStaticMesh()->GetName();
		if (!(Nm.Contains(TEXT("Street")) || Nm.Contains(TEXT("Sidewalk")) || Nm.Contains(TEXT("Road"))
			|| Nm.Contains(TEXT("ConcretePath")) || Nm.Contains(TEXT("Pavement")) || Nm.Contains(TEXT("Boardwalk"))
			|| Nm.Contains(TEXT("Crosswalk")) || Nm.Contains(TEXT("Floor")))) { return false; }
		OutGround = H.ImpactPoint;
		return true;
	};
	// LICHAAM-PLAFOND: max ~55 echte lichamen tegelijk (de performance-grens); de rest blijft
	// data. Lichamen gaan naar de virtuelen die het dichtst bij een speler lopen.
	// DAG/NACHT: overdag de volle crowd (70); 's nachts een KLEINERE crowd van vooral verslaafden -
	// de niet-verslaafden gaan (off-screen) naar bed = minder mensen, meer junkies/kopers buiten.
	bool bNight = false;
	if (AWeedShopGameState* GSnt = W->GetGameState<AWeedShopGameState>())
	{
		if (auto* DCnt = GSnt->GetDayCycle()) { bNight = DCnt->IsNight(); }
	}
	// ALTIJD de volle crowd (dag EN nacht). De speler wil dat NPC's er gewoon altijd zijn; de nacht-
	// uitdunning despawnte lopende NPC's en dat botst daarmee. (BodyCap=70 -> de despawn hieronder triggert
	// nooit meer want NBodies komt nooit boven 70.)
	const int32 BodyCap = 70;
	int32 NBodies = 0;
	// SPAWN-SPREIDING: max enkele echte NPC's PER CALL materialiseren. Elke spawn doet een synchrone
	// modulaire build (mesh-loads + components); een hele rij in 1 frame = de periodieke hang. Met deze cap
	// + de 10Hz-cadans (zie de call in TickVirtualMove) druppelen ze vloeiend binnen i.p.v. in bursts.
	// Tijdens het laden (de cover staat over beeld -> de spawn-hitch is verborgen) materialiseren we de crowd
	// VERSNELD (8/call: de cover wacht nu op CrowdWarm, dus vol gas achter het scherm) zodat de wereld vol
	// staat VOORDAT de cover wegfadet. De cover zet CrowdSpawned zodra 'ie fadet -> daarna terug naar
	// 1-per-keer voor smooth gameplay. (CrowdSpawned betekent hier: "de loading-cover is weg".)
	const int32 MaxSpawnPerTick = IsCoverPhase() ? 8 : (WeedShop_IsCrowdSpawned() ? 1 : 4);
	int32 Spawned = 0;
	int32 PendingSpawnable = 0; // spawnbare walkers (in bereik, straat geladen) die deze call NOG geen lichaam kregen -> CrowdWarm-vlag
	// DAGELIJKSE CROWD-ROTATIE: bij een nieuwe dag krijgt elke body (off-screen) één verse identiteit uit de
	// ~250-pool -> elke dag andere gezichten zonder dat je 't ziet gebeuren. AssignNpc schuift de cursor door.
	int32 CrowdDay = 1;
	if (AWeedShopGameState* GSday = W->GetGameState<AWeedShopGameState>()) { if (auto* DCday = GSday->GetDayCycle()) { CrowdDay = DCday->GetDayNumber(); } }
	if (LastCrowdDay < 0) { LastCrowdDay = CrowdDay; }                 // eerste run: niet roteren (net gespawnd)
	else if (CrowdDay != LastCrowdDay) { LastCrowdDay = CrowdDay; ++CrowdRerollGen; }
	int32 RerollBudget = 2; // max 2 re-skins per call -> geen hitch (gespreid, off-screen)
	UNpcRegistryComponent* CrowdReg = nullptr;
	if (AWeedShopGameState* GSreg = W->GetGameState<AWeedShopGameState>()) { CrowdReg = GSreg->GetNpcRegistry(); }
	for (FVirtualWalker& V : Crowd)
	{
		if (ACustomerBase* B = V.Body.Get())
		{
			++NBodies;
			V.Pos = B->GetActorLocation(); // data volgt het lichaam (voor de map-marker)
			// Dagelijkse rotatie: nog niet ge-reroll'd deze dag + ver + off-screen -> verse identiteit (nieuwe skin/stats).
			if (V.RerolledGen < CrowdRerollGen && RerollBudget > 0 && CrowdReg && MinPlayerDist(V.Pos) > 8000.f && !InAnyView(V.Pos))
			{
				B->ReassignCrowdIdentity(CrowdReg->AssignNpc());
				V.RerolledGen = CrowdRerollGen;
				--RerollBudget;
			}
			// NACHT-UITDUNNING: te veel volk 's nachts -> de NIET-verslaafden (< drempel) gaan naar bed.
			// Alleen OFF-SCREEN opruimen zodat je 't nooit ziet gebeuren = geen zichtbare churn; de junkies
			// (>= drempel) blijven gewoon rondlopen. Overdag materialiseren de slapers vanzelf weer.
			if (bNight && NBodies > BodyCap && B->GetAddiction() < NightAddictThreshold && MinPlayerDist(V.Pos) > 9000.f && !InAnyViewFar(V.Pos))
			{
				B->Destroy();
				V.Body = nullptr;
				--NBodies;
				continue;
			}
			// Overdag (en de junkies 's nachts): BLIJFT bestaan en doorlopen. Ver weg verdwijnt alleen z'n
			// model + animatie (SetCullDistance + OnlyTickPoseWhenRendered); het lichaam loopt door, marker klopt.
			continue;
		}
		// (Beweging gebeurt in TickVirtualMove op 10x/s - hier alleen het zware werk.)
		// MATERIALISEREN: speler binnen bereik, niet pal in beeld, echte straat onder de voeten
		// en onder het lichaam-plafond.
		const float Pd = MinPlayerDist(V.Pos);
		if (NBodies < BodyCap && Pd < 18000.f && Pd > 2500.f && !InAnyView(V.Pos))
		{
			FVector Ground;
			if (!StreetAt(V.Pos, Ground)) { continue; } // wereld hier (nog) niet geladen
			// BOTSVRIJ neerzetten: staat er al iemand, dan een stukje opzij - voorkomt in elkaar
			// gespawnde tweetallen die elkaar eeuwig klem duwen.
			FVector SpawnP = Ground + FVector(0.f, 0.f, 100.f);
			for (int32 t = 0; t < 4; ++t)
			{
				if (!W->OverlapBlockingTestByChannel(SpawnP, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(45.f, 90.f))) { break; }
				FVector Alt = Ground + FVector(FMath::FRandRange(-240.f, 240.f), FMath::FRandRange(-240.f, 240.f), 0.f);
				FVector AltGround;
				if (StreetAt(Alt, AltGround)) { SpawnP = AltGround + FVector(0.f, 0.f, 100.f); }
				else { SpawnP = Ground + FVector(FMath::FRandRange(-120.f, 120.f), FMath::FRandRange(-120.f, 120.f), 100.f); }
			}
			ACustomerBase* B = nullptr;
			// 1) HERGEBRUIK uit de pool (gratis - geen modulaire rebuild). Altijd toegestaan (geen cap).
			while (CrowdPool.Num() > 0 && !B)
			{
				ACustomerBase* P = CrowdPool.Pop();
				if (IsValid(P)) { B = P; }
			}
			if (B)
			{
				B->SetActorLocation(SpawnP);
				B->SetActorHiddenInGame(false);
				B->SetActorEnableCollision(true);
				B->SetActorTickEnabled(true);
				if (UCharacterMovementComponent* CM = B->FindComponentByClass<UCharacterMovementComponent>()) { CM->SetMovementMode(MOVE_Walking); }
			}
			else
			{
				// 2) Pool leeg -> ECHT bouwen (modulaire variatie zoals overal). Dit is de dure stap (~40ms),
				//    dus maar MaxSpawnPerTick per call -> de pool warmt op over een paar seconden, daarna alleen
				//    nog gratis hergebruik = glad (net als de dev-city die z'n NPC's maar 1x bouwt).
				if (Spawned >= MaxSpawnPerTick) { ++PendingSpawnable; continue; } // budget op: volgende call weer (telt als nog-niet-warm)
				++Spawned;
				// DEFERRED zodat bCrowdNpc geZET is VOOR BeginPlay->BuildAppearance: ambient walker krijgt de
				// goedkope 1-mesh-build i.p.v. de modulaire 6-component-build (~40ms hitch/spawn = het stotteren).
				B = W->SpawnActorDeferred<ACustomerBase>(ACustomerBase::StaticClass(), FTransform(SpawnP), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (B) { B->bCrowdNpc = true; B->FinishSpawning(FTransform(SpawnP)); }
			}
			if (!B) { ++PendingSpawnable; continue; }
			B->bVirtualCrowdBody = true; // beheerd door de DoorRetrofitter (blijvend) - de spawner-cull laat 'm met rust
			++NBodies;
			V.Body = B;
				CrowdBodies.AddUnique(B); // sterke ref -> geen GC (anti-churn)
			// Dichtstbijzijnde spawner adopteert (patrouille-aansturing).
			ACustomerSpawner* Near = nullptr;
			float BD = TNumericLimits<float>::Max();
			// PERF: spawner-registry (O(spawners)) i.p.v. TActorIterator over alle actors - zelfde set.
			// De registry is per-proces, dus wel op wereld filteren (PIE/co-op-in-1-proces).
			for (const TWeakObjectPtr<ACustomerSpawner>& WSp : ACustomerSpawner::GetAll())
			{
				ACustomerSpawner* Sp = WSp.Get();
				if (!IsValid(Sp) || Sp->GetWorld() != W) { continue; }
				const float Dd = FVector::DistSquared2D(Sp->GetActorLocation(), Ground);
				if (Dd < BD) { BD = Dd; Near = Sp; }
			}
			if (Near) { Near->AdoptWalker(B); }
		}
	}

	// LOADING-COVER-SIGNAAL "crowd warm": alle spawnbare walkers binnen bereik (18000cm, straat geladen,
	// niet pal in beeld) hebben een lichaam, of het lichaam-plafond staat. NBodies > 0 als bewijs dat de
	// spawn-pipeline echt gedraaid heeft (anders zou een te vroege call - straten nog niet gestreamd,
	// dus 0 spawnbaar - de vlag direct zetten). De BootCoverWidget wacht hierop (host-side; een co-op
	// client slaat deze gate over). Reset gebeurt in WeedShop_RequestGameLoadingScreen.
	if (!WeedShop_IsCrowdWarm() && (NBodies >= BodyCap || (PendingSpawnable == 0 && NBodies > 0)))
	{
		WeedShop_SetCrowdWarm(true);
		UE_LOG(LogWeedShop, Display, TEXT("Crowd warm: %d lichamen van %d wandelaars -> loading-cover mag verder"), NBodies, Crowd.Num());
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
		UE_LOG(LogWeedShop, Log, TEXT("InstantGlass: %d raam-slots direct helder gemaakt (%d kolommen)"), NSwapped, GlassRects.Num());
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
		UE_LOG(LogWeedShop, Verbose, TEXT("VertClone[%s]: bron-slice %d meshes (streak %d) - wacht"), *JobId, Slice.Num(), JStreak);
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
			UE_LOG(LogWeedShop, Verbose, TEXT("VertClone: verdieping %+d (Z %.0f) overgeslagen - kamer past hier niet (%d/%d stukken)"), N, TgtZ, FitPass, FitEligible);
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
		UE_LOG(LogWeedShop, Verbose, TEXT("VertClone: verdieping %+d (Z %.0f): %d meshes aangevuld (%d bestonden al, %d nep-glas verborgen)"), N, TgtZ, Placed, ExistCount, GlassHidden);
	}
	if (BakeOut.Len() > 0)
	{
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("JOB|%s"), *JobId) + LINE_TERMINATOR + BakeOut,
			*(FPaths::ProjectSavedDir() / TEXT("RoomBake.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
	UE_LOG(LogWeedShop, Verbose, TEXT("VertClone: KLAAR - %d meshes totaal aangevuld vanaf slice Z %.0f (%d bron-meshes)"), TotalPlaced, SrcZ, Slice.Num());
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
		UE_LOG(LogWeedShop, Log, TEXT("RoomStamper: sessie-herbouw '%s' op (%.0f, %.0f) - %d stukken"), *P[0], AL.X, AL.Y, Placed); // Log: one-time per kamer
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
		UE_LOG(LogWeedShop, Verbose, TEXT("RoomStamper: area-dump %d comps -> StampAreaDump.txt"), DumpCount); // Verbose: dev-DIAGNOSE-dump
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
			// LICHTLEK-FIX: gebakken interieur-muren zijn vaak enkelzijdig/dun -> vanaf de zonkant cast de
			// backface geen schaduw -> de zon lekt door massieve muren + liftdeuren (raam-patroon op de
			// andere kant). Dubbelzijdig laten casten dicht dat lek. Geldt voor ALLE kamer-meshes.
			if (!Comp->bCastShadowAsTwoSided) { Comp->bCastShadowAsTwoSided = true; Comp->MarkRenderStateDirty(); }
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
	UE_LOG(LogWeedShop, Display, TEXT("BakedRooms: %d nep-glas slots omgezet naar echt doorzichtig glas"), Swapped);
}

// ===========================================================================================
//  BEACH-MAP WONING-REGISTRY (ROADMAP 4.1)
//  Index 0 = starter (gratis); 1.. = via de marker-toets geregistreerde koopbare kamers
//  (opgeslagen in Saved/BeachHomes.txt: "InteriorX,Y,Z, HalfX,HalfY, PriceCents, Naam").
// ===========================================================================================
bool ADoorRetrofitter::MeasureRoomHalf(const FVector& Center, FVector& OutHalf) const
{
	UWorld* W = GetWorld();
	if (!W) { return false; }
	FCollisionQueryParams Q(SCENE_QUERY_STAT(RoomMeasure), false);
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		if (APawn* Pp = It->Get() ? It->Get()->GetPawn() : nullptr) { Q.AddIgnoredActor(Pp); }
	}
	// Dichtstbijzijnde wand per as op het WorldStatic-kanaal (muren = static; meubels zijn dynamic),
	// op twee hoogtes -> de kleinste geldige hit is de echte wand.
	auto Wall = [&](const FVector& Dir) -> float
	{
		float Near = -1.f;
		for (float Hz : { 40.f, 150.f })
		{
			const FVector S = Center + FVector(0.f, 0.f, Hz);
			FHitResult H;
			if (W->LineTraceSingleByChannel(H, S, S + Dir * 3000.f, ECC_WorldStatic, Q))
			{
				Near = (Near < 0.f) ? H.Distance : FMath::Min(Near, H.Distance);
			}
		}
		return Near;
	};
	const float Xp = Wall(FVector(1, 0, 0)), Xn = Wall(FVector(-1, 0, 0));
	const float Yp = Wall(FVector(0, 1, 0)), Yn = Wall(FVector(0, -1, 0));
	if (Xp < 0.f || Xn < 0.f || Yp < 0.f || Yn < 0.f) { return false; }
	// Halve-afmeting tot de verste wand per as + kleine marge (zo dekt de box de hele kamer ook als je
	// niet exact in het midden stond), met een ondergrens.
	OutHalf = FVector(FMath::Max(350.f, FMath::Max(Xp, Xn) + 60.f),
	                  FMath::Max(350.f, FMath::Max(Yp, Yn) + 60.f), 320.f);
	return true;
}
bool ADoorRetrofitter::MeasureRoomCenter(const FVector& Near, FVector& OutCenter) const
{
	UWorld* W = GetWorld();
	if (!W) { return false; }
	FCollisionQueryParams Q(SCENE_QUERY_STAT(RoomCenter), false);
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{ if (APawn* Pp = It->Get() ? It->Get()->GetPawn() : nullptr) { Q.AddIgnoredActor(Pp); } }
	auto Wall = [&](const FVector& Dir) -> float
	{
		float Nr = -1.f;
		for (float Hz : { 40.f, 150.f })
		{
			const FVector S = Near + FVector(0.f, 0.f, Hz);
			FHitResult H;
			if (W->LineTraceSingleByChannel(H, S, S + Dir * 3000.f, ECC_WorldStatic, Q))
			{ Nr = (Nr < 0.f) ? H.Distance : FMath::Min(Nr, H.Distance); }
		}
		return Nr;
	};
	const float Xp = Wall(FVector(1, 0, 0)), Xn = Wall(FVector(-1, 0, 0));
	const float Yp = Wall(FVector(0, 1, 0)), Yn = Wall(FVector(0, -1, 0));
	if (Xp < 0.f || Xn < 0.f || Yp < 0.f || Yn < 0.f) { return false; }
	OutCenter = Near + FVector((Xp - Xn) * 0.5f, (Yp - Yn) * 0.5f, 0.f);
	return true;
}

bool ADoorRetrofitter::IsInsideHomeRoom(const FVector& P, float Margin) const
{
	// Alle geregistreerde woon-/kamer-boxes: in een woon-KAMER hoort nooit zomaar een NPC te
	// staan of te spawnen - hal, trappenhuis, lift en straat zijn de legitieme plekken. Bewust
	// over ALLE registry-woningen (ook niet-gekochte units), niet alleen speler-bezit.
	for (const FApartmentHome& H : BeachHomes)
	{
		const FVector& C = H.InteriorPos; // op vloer-hoogte
		const FVector& Half = H.RoomHalf; // X/Y = halve afmeting, Z = kamerhoogte omhoog
		if (FMath::Abs(P.X - C.X) <= Half.X + Margin
			&& FMath::Abs(P.Y - C.Y) <= Half.Y + Margin
			&& P.Z >= C.Z - 150.f && P.Z <= C.Z + Half.Z + Margin)
		{
			return true;
		}
	}
	// De gemeten starter-kamer-box (wand-traces): nauwkeuriger dan de registry-fallback zolang
	// die nog op de ruime default staat.
	if (bHomeBoxReady
		&& P.X >= HomeBoxMin.X - Margin && P.X <= HomeBoxMax.X + Margin
		&& P.Y >= HomeBoxMin.Y - Margin && P.Y <= HomeBoxMax.Y + Margin
		&& P.Z >= HomeBoxMin.Z - Margin && P.Z <= HomeBoxMax.Z + Margin)
	{
		return true;
	}
	// Competitive spiegel-kamers (host 603 + joiner 602).
	if (bCompHomesReady)
	{
		const FBox Boxes[2] = { CompHomeBoxHost, CompHomeBoxJoiner };
		for (const FBox& B : Boxes)
		{
			if (B.IsValid && B.ExpandBy(FVector(Margin, Margin, Margin + 150.f)).IsInsideOrOn(P))
			{
				return true;
			}
		}
	}
	return false;
}

bool ADoorRetrofitter::IsInsidePlayerHome(const FVector& P, float Margin) const
{
	// SPELERS-woningen = nul-tolerantie (speler-besluit): een NPC hier wordt DIRECT verwijderd,
	// zonder 5s-gratie of zichtbare naar-buiten-wandeling. = de gemeten starter-kamer + de
	// competitive spiegel-kamers + alle GEKOCHTE woningen van alle spelers.
	if (bHomeBoxReady
		&& P.X >= HomeBoxMin.X - Margin && P.X <= HomeBoxMax.X + Margin
		&& P.Y >= HomeBoxMin.Y - Margin && P.Y <= HomeBoxMax.Y + Margin
		&& P.Z >= HomeBoxMin.Z - Margin && P.Z <= HomeBoxMax.Z + Margin)
	{
		return true;
	}
	if (bCompHomesReady)
	{
		const FBox Boxes[2] = { CompHomeBoxHost, CompHomeBoxJoiner };
		for (const FBox& B : Boxes)
		{
			if (B.IsValid && B.ExpandBy(FVector(Margin, Margin, Margin + 150.f)).IsInsideOrOn(P)) { return true; }
		}
	}
	// Gekochte woningen (OwnedHomes = indices in de unified homes-lijst = BeachHomes op deze map).
	if (UWorld* W = GetWorld())
	{
		for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
		{
			const APlayerController* PC = PIt->Get();
			const APawn* Pn = PC ? PC->GetPawn() : nullptr;
			const UPhoneClientComponent* Ph = Pn ? Pn->FindComponentByClass<UPhoneClientComponent>() : nullptr;
			if (!Ph) { continue; }
			for (int32 Idx : Ph->GetOwnedHomes())
			{
				if (!BeachHomes.IsValidIndex(Idx)) { continue; }
				const FApartmentHome& H = BeachHomes[Idx];
				if (FMath::Abs(P.X - H.InteriorPos.X) <= H.RoomHalf.X + Margin
					&& FMath::Abs(P.Y - H.InteriorPos.Y) <= H.RoomHalf.Y + Margin
					&& P.Z >= H.InteriorPos.Z - 150.f && P.Z <= H.InteriorPos.Z + H.RoomHalf.Z + Margin)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void ADoorRetrofitter::RebuildBeachHomes()
{
	BeachHomes.Reset();
	BeachHomePrices.Reset();

	// Index 0 = de starter-woning (gratis, al van jou). Interieur op vloerhoogte (HomeAnchor - 110),
	// bounds uit de gemeten huis-box als die er is, anders een ruime default.
	if (!HomeAnchor.IsNearlyZero())
	{
		FApartmentHome H;
		H.InteriorPos = FVector(HomeAnchor.X, HomeAnchor.Y, HomeAnchor.Z - 110.f);
		H.DoorPos = H.InteriorPos;
		H.HallPos = H.InteriorPos;
		H.Number = TEXT("Home");
		H.bApartment = true;
		H.Floor = 1;
		H.RoomHalf = bHomeBoxReady
			? FVector((HomeBoxMax.X - HomeBoxMin.X) * 0.5f, (HomeBoxMax.Y - HomeBoxMin.Y) * 0.5f, 320.f)
			: FVector(750.f, 750.f, 320.f);
		BeachHomes.Add(H);
		BeachHomePrices.Add(0);
	}

	// Index 1.. = koopbare woningen uit BeachHomes.txt (door de speler geregistreerd met de marker-toets).
	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArray(Lines, *WeedData::File(TEXT("BeachHomes.txt"))))
	{
		for (const FString& Raw : Lines)
		{
			const FString Line = Raw.TrimStartAndEnd();
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) { continue; }
			TArray<FString> P;
			Line.ParseIntoArray(P, TEXT(","));
			if (P.Num() < 6) { continue; }
			FApartmentHome H;
			H.InteriorPos = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
			H.RoomHalf = FVector(FMath::Max(150.f, FCString::Atof(*P[3])),
			                     FMath::Max(150.f, FCString::Atof(*P[4])), 320.f);
			H.DoorPos = H.InteriorPos;
			H.HallPos = H.InteriorPos;
			H.bApartment = true;
			H.Number = (P.Num() >= 7) ? P[6].TrimStartAndEnd() : FString::Printf(TEXT("Unit %d"), BeachHomes.Num());
			BeachHomes.Add(H);
			BeachHomePrices.Add((int64)FCString::Atoi64(*P[5]));
		}
	}
	bBeachHomesBuilt = true;
}

// ===========================================================================================
//  COMPETITIVE AUTO-SPIEGEL (alleen in Competitive co-op)
//  De solo-kamer (StarterFurniture.txt) staat in Apt <starter> (bv. 703, het penthouse). In
//  Competitive krijgen de twee spelers ELK een eigen kamer: host -> Apt <starter-100> (603, exact
//  één etage lager = identieke kamervorm), joiner -> Apt <starter-101> (602, de buurkamer). 603 krijgt
//  een 1-op-1 KOPIE van de inrichting (pure verschuiving), 602 een ECHTE SPIEGEL over de gedeelde muur.
//  No-build-zones + de build-zone (home-area) van de solo-kamer worden meeverschoven. In co-op (niet
//  competitive) wordt geen van deze code uitgevoerd, dus daar verandert er niets.
// ===========================================================================================
ACityDoor* ADoorRetrofitter::FindAptDoor(int32 Number, const FVector& NearXY) const
{
	UWorld* W = GetWorld();
	if (!W || Number <= 0) { return nullptr; }
	ACityDoor* Best = nullptr; float BestD = TNumericLimits<float>::Max();
	for (TActorIterator<ACityDoor> It(W); It; ++It)
	{
		if (!IsValid(*It) || It->GetAptNumber() != Number) { continue; }
		const float D = FVector::Dist2D(It->GetActorLocation(), NearXY);
		if (D < BestD) { BestD = D; Best = *It; }
	}
	return Best;
}

ACityDoor* ADoorRetrofitter::FindNearestDoor(const FVector& P) const
{
	UWorld* W = GetWorld();
	if (!W) { return nullptr; }
	ACityDoor* Best = nullptr; float BestD = 1400.f * 1400.f; // binnen ~14m (anders geen kamer-deur dichtbij)
	for (TActorIterator<ACityDoor> It(W); It; ++It)
	{
		if (!IsValid(*It)) { continue; }
		const float D = FVector::DistSquared(It->GetActorLocation(), P);
		if (D < BestD) { BestD = D; Best = *It; }
	}
	return Best;
}

void ADoorRetrofitter::GetHomeNoBuildZones(const FBox& HomeBox, TArray<FBox>& Out) const
{
	Out.Reset();
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Zelfde bron + parse als UBuildComponent::RefreshNoBuildZones (NoBuildZones.txt, paren markers).
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *(FPaths::ProjectSavedDir() / TEXT("NoBuildZones.txt")))) { return; }
	const FString MapName = W->GetOutermost()->GetName();
	TArray<FVector> Pts;
	TArray<FString> Lines; Content.ParseIntoArrayLines(Lines);
	for (const FString& Ln : Lines)
	{
		if (!Ln.Contains(MapName)) { continue; }
		int32 PosStart = Ln.Find(TEXT("pos=("));
		if (PosStart == INDEX_NONE) { continue; }
		PosStart += 5;
		const int32 PosEnd = Ln.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, PosStart);
		if (PosEnd == INDEX_NONE) { continue; }
		TArray<FString> N;
		Ln.Mid(PosStart, PosEnd - PosStart).ParseIntoArray(N, TEXT(","), true);
		if (N.Num() < 3) { continue; }
		Pts.Add(FVector(FCString::Atof(*N[0].TrimStartAndEnd()), FCString::Atof(*N[1].TrimStartAndEnd()), FCString::Atof(*N[2].TrimStartAndEnd())));
	}
	for (int32 i = 0; i + 1 < Pts.Num(); i += 2)
	{
		const FVector A = Pts[i], B = Pts[i + 1];
		const float MidZ = (A.Z + B.Z) * 0.5f;
		const FBox Zone(FVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), MidZ - 200.f),
		                FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), MidZ + 360.f));
		if (HomeBox.IsInsideOrOn(Zone.GetCenter())) { Out.Add(Zone); }
	}
}

AActor* ADoorRetrofitter::SpawnHomeItem(UWorld* W, FName ItemId, const FVector& Loc, float Yaw)
{
	if (!W || ItemId.IsNone()) { return nullptr; }
	// Dispatch IDENTIEK aan de StarterFurniture-loader, zodat een competitive-kamer 1-op-1 dezelfde
	// meubel-types/oppakbaarheid krijgt als de solo-kamer. AutoFixture-tag zoals de starter-inrichting.
	const FRotator Rot(0.f, Yaw, 0.f);
	const FTransform TM(Rot, Loc);
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Spawned = nullptr;
	FPlaceableDef Def;
	const bool bShelf = GetPlaceableDef(ItemId, Def) && Def.bIsShelf; // Fridge/Shelf/Chest = eigen class
	if (ItemId == FName(TEXT("Sink")))
	{
		Spawned = W->SpawnActor<AWaterSink>(AWaterSink::StaticClass(), TM, SP);
	}
	else if (ItemId == FName(TEXT("LightSwitch")))
	{
		if (APackLightSwitch* Sw = W->SpawnActorDeferred<APackLightSwitch>(APackLightSwitch::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Sw->Setup(FString::Printf(TEXT("sw_%d_%d_%d"), FMath::RoundToInt(Loc.X / 10.f), FMath::RoundToInt(Loc.Y / 10.f), FMath::RoundToInt(Loc.Z / 10.f)), 800.f);
			Sw->FinishSpawning(TM);
			Spawned = Sw;
		}
	}
	else if (bShelf)
	{
		if (AStorageShelf* Sh = W->SpawnActorDeferred<AStorageShelf>(AStorageShelf::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Sh->ShelfTier = ItemId;
			Sh->FinishSpawning(TM);
			Spawned = Sh;
		}
	}
	else if (APlaceableProp* Pr = W->SpawnActorDeferred<APlaceableProp>(APlaceableProp::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Pr->ItemId = ItemId;
		Pr->FinishSpawning(TM);
		Spawned = Pr;
	}
	if (Spawned) { Spawned->Tags.Add(FName(TEXT("AutoFixture"))); }
	return Spawned;
}

void ADoorRetrofitter::GetCompetitiveHomeBoxes(bool bJoiner, TArray<FBox>& Out) const
{
	Out.Reset();
	if (!bCompHomesReady) { return; }
	// PER-SPELER-EIGENDOM: elke speler mag ALLEEN in z'n eigen kamer bouwen. De joiner hoort bij 602, de
	// host bij 603. bJoiner is EXPLICIET meegegeven (afgeleid uit de owner-pawn), zodat dit ook klopt in een
	// Server-RPC waar de netmode NM_ListenServer is. Dus geef alleen de EIGEN box terug.
	const FBox& MyBox = bJoiner ? CompHomeBoxJoiner : CompHomeBoxHost;
	if (MyBox.IsValid) { Out.Add(MyBox); }
}

void ADoorRetrofitter::GetCompetitiveHomeBoxes(TArray<FBox>& Out) const
{
	// Client-side (preview): leid bJoiner af uit de netmode - de client is de joiner (602), de rest host (603).
	UWorld* W = GetWorld();
	const bool bJoiner = W && W->GetNetMode() == NM_Client;
	GetCompetitiveHomeBoxes(bJoiner, Out);
}

void ADoorRetrofitter::GetCompetitiveMarkers(TArray<FVector>& Out) const
{
	Out.Reset();
	UWorld* W = GetWorld();
	if (!W) { return; }
	// 1) Expliciet vastgelegd (CompSpawns.txt: X,Y,Z per regel).
	TArray<FString> CS;
	FFileHelper::LoadFileToStringArray(CS, *(FPaths::ProjectSavedDir() / TEXT("CompSpawns.txt")));
	for (const FString& L : CS)
	{
		TArray<FString> P; L.TrimStartAndEnd().ParseIntoArray(P, TEXT(","), true);
		if (P.Num() >= 3) { Out.Add(FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]))); }
	}
	// 2) Fallback: de LAATSTE 2 F9-markers (MarkedSpots.txt) van deze map.
	if (Out.Num() < 2)
	{
		TArray<FString> MS;
		FFileHelper::LoadFileToStringArray(MS, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
		const FString MapName = W->GetOutermost()->GetName();
		TArray<FVector> All;
		for (const FString& L : MS)
		{
			if (!L.Contains(MapName)) { continue; }
			int32 s = L.Find(TEXT("pos=(")); if (s == INDEX_NONE) { continue; }
			s += 5;
			const int32 e = L.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, s);
			if (e == INDEX_NONE) { continue; }
			TArray<FString> N; L.Mid(s, e - s).ParseIntoArray(N, TEXT(","), true);
			if (N.Num() >= 3) { All.Add(FVector(FCString::Atof(*N[0].TrimStartAndEnd()), FCString::Atof(*N[1].TrimStartAndEnd()), FCString::Atof(*N[2].TrimStartAndEnd()))); }
		}
		if (All.Num() >= 2) { Out.Reset(); Out.Add(All[0]); Out.Add(All[1]); }
	}
}

void ADoorRetrofitter::TickCompetitiveRooms()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	AWeedShopGameState* GS = W->GetGameState<AWeedShopGameState>();
	const bool bComp = GS && GS->IsCompetitive();
	// TIJDELIJKE DIAGNOSE: waarom vuurt competitive niet? Log de gate-states de eerste ~40 scans.
	// ALLEEN in competitive -> geen [CompDiag]-spam in single-player.
	if (bComp && !bCompHomesReady && CompDiagCount < 40)
	{
		++CompDiagCount;
		const int32 SrcN = StarterDoor.IsValid() ? StarterDoor->GetAptNumber() : -1;
		int32 nHost = 0, nJoin = 0;
		if (SrcN > 0) { for (TActorIterator<ACityDoor> It(W); It; ++It) { if (!IsValid(*It)) { continue; } const int32 A = It->GetAptNumber(); if (A == SrcN - 100) { ++nHost; } else if (A == SrcN - 101) { ++nJoin; } } }
		UE_LOG(LogWeedShop, Verbose, TEXT("[CompDiag] GS=%d comp=%d boxReady=%d anchor=%d starter=Apt%d  deuren603=%d 602=%d netmode=%d"),
			GS ? 1 : 0, bComp ? 1 : 0, bHomeBoxReady ? 1 : 0, HomeAnchor.IsNearlyZero() ? 0 : 1, SrcN, nHost, nJoin, (int32)W->GetNetMode());
	}
	if (!bComp) { return; } // ALLEEN in Competitive co-op

	// PER-MACHINE, ELKE SCAN: precies de 2 deuren van JOUW kamer -> sticky speler-huis (open); de 2 deuren van
	// de partner-kamer -> sticky "Co-op partner" (op slot). Alleen de 2 DICHTSTBIJZIJNDE per kamer (zelfde
	// verdieping) - buur-appartementen blijven NPC-deuren. Deuren die niet meer gekozen worden -> loslaten.
	if (bCompHomesReady)
	{
		const bool bClient = W->GetNetMode() == NM_Client;
		const FVector MyC = bClient ? CompAnchorJoiner : CompAnchorHost;
		const FVector OtherC = bClient ? CompAnchorHost : CompAnchorJoiner;
		// EIGEN kamer = AUTO (de lokale speler staat erin -> betrouwbaar; dit werkte al goed voor player 2).
		// PARTNER-kamer (ver weg) = jouw F9-deur-markers als die er zijn (auto pakt daar soms een buurdeur),
		// anders auto.
		auto PickDoors = [&](const FVector& C, TArray<ACityDoor*>& Out)
		{
			FCollisionQueryParams LoSQ(SCENE_QUERY_STAT(CompDoorLoS), false);
			for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
			{ if (APawn* Pp = PIt->Get() ? PIt->Get()->GetPawn() : nullptr) { LoSQ.AddIgnoredActor(Pp); } }
			TArray<TPair<float, ACityDoor*>> Cand;
			for (TActorIterator<ACityDoor> It(W); It; ++It)
			{
				if (!IsValid(*It)) { continue; }
				const FVector DL = It->GetActorLocation();
				if (FMath::Abs(DL.Z - C.Z) > 300.f) { continue; }
				const float D2 = FVector::Dist2D(DL, C);
				if (D2 > 1200.f) { continue; }
				const FVector A(C.X, C.Y, DL.Z + 60.f);
				const FVector B = DL + FVector(0.f, 0.f, 60.f);
				FHitResult LoS;
				if (W->LineTraceSingleByChannel(LoS, A, B, ECC_WorldStatic, LoSQ) && LoS.Distance < FVector::Dist(A, B) - 110.f) { continue; }
				Cand.Add(TPair<float, ACityDoor*>(D2, *It));
			}
			Cand.Sort([](const TPair<float, ACityDoor*>& X, const TPair<float, ACityDoor*>& Y) { return X.Key < Y.Key; });
			for (const TPair<float, ACityDoor*>& P : Cand) { if (P.Value->GetAptNumber() > 0) { Out.Add(P.Value); break; } }
			for (const TPair<float, ACityDoor*>& P : Cand) { if (Out.Num() >= 2) { break; } if (!Out.Contains(P.Value)) { Out.Add(P.Value); } }
		};
		TArray<ACityDoor*> Mine, Partner;
		PickDoors(MyC, Mine); // EIGEN kamer: auto - niet aankomen (player 2 werkte hiermee)
		{
			// PARTNER-deuren via F9-markers IN de partner-kamer (dichter bij OtherC dan MyC): de speler zet een
			// marker OP de deur. Zo wijst de host de juiste deuren in de verre kamer aan.
			TArray<FString> MS;
			FFileHelper::LoadFileToStringArray(MS, *(FPaths::ProjectSavedDir() / TEXT("CompDoors.txt"))); // GEBAKKEN deur-markers (permanent)
			{ TArray<FString> MSlive; FFileHelper::LoadFileToStringArray(MSlive, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt"))); MS.Append(MSlive); } // + live F9-markers (aanpassingen)
			const FString MapName = W->GetOutermost()->GetName();
			for (const FString& Lm : MS)
			{
				if (!Lm.Contains(MapName)) { continue; }
				int32 sPos = Lm.Find(TEXT("pos=(")); if (sPos == INDEX_NONE) { continue; }
				sPos += 5;
				const int32 ePos = Lm.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, sPos);
				if (ePos == INDEX_NONE) { continue; }
				TArray<FString> Nn; Lm.Mid(sPos, ePos - sPos).ParseIntoArray(Nn, TEXT(","), true);
				if (Nn.Num() < 3) { continue; }
				const FVector Mk(FCString::Atof(*Nn[0].TrimStartAndEnd()), FCString::Atof(*Nn[1].TrimStartAndEnd()), FCString::Atof(*Nn[2].TrimStartAndEnd()));
				if (FVector::Dist2D(Mk, OtherC) >= FVector::Dist2D(Mk, MyC)) { continue; } // alleen partner-kamer
				ACityDoor* Best = nullptr; float BestD2 = 250.f * 250.f;
				for (TActorIterator<ACityDoor> It(W); It; ++It)
				{
					if (!IsValid(*It)) { continue; }
					if (FMath::Abs(It->GetActorLocation().Z - Mk.Z) > 300.f) { continue; }
					const float Dq = FVector::DistSquared2D(It->GetActorLocation(), Mk);
					if (Dq < BestD2) { BestD2 = Dq; Best = *It; }
				}
				if (Best && !Partner.Contains(Best)) { Partner.Add(Best); }
			}
		}
		if (Partner.Num() == 0) { PickDoors(OtherC, Partner); } // geen partner-markers -> auto
		Partner.RemoveAll([&](ACityDoor* D) { return Mine.Contains(D); });
		TSet<TWeakObjectPtr<ACityDoor>> NewClaim;
		for (ACityDoor* D : Mine) { NewClaim.Add(D); }
		for (ACityDoor* D : Partner) { NewClaim.Add(D); }
		for (const TWeakObjectPtr<ACityDoor>& Old : CompClaimedDoors)
		{ if (Old.IsValid() && !NewClaim.Contains(Old)) { Old.Get()->SetCompRelease(); } }
		for (ACityDoor* D : Mine) { D->SetCompPlayerHome(); }
		for (ACityDoor* D : Partner) { D->SetCompPartner(); }
		CompClaimedDoors = NewClaim;
		if (CompDoorWait < 14) { ++CompDoorWait; FString Ms, Ps; for (ACityDoor* D : Mine) { Ms += FString::Printf(TEXT("#%d@(%.0f,%.0f) "), D->GetAptNumber(), D->GetActorLocation().X, D->GetActorLocation().Y); } for (ACityDoor* D : Partner) { Ps += FString::Printf(TEXT("#%d@(%.0f,%.0f) "), D->GetAptNumber(), D->GetActorLocation().X, D->GetActorLocation().Y); } UE_LOG(LogWeedShop, Verbose, TEXT("[CompDoor] client=%d MyC=(%.0f,%.0f) OtherC=(%.0f,%.0f) eigen=[%s] partner=[%s]"), bClient ? 1 : 0, MyC.X, MyC.Y, OtherC.X, OtherC.Y, *Ms, *Ps); }
	}

	// COMPETITIVE: het lege 703-penthouse donker houden - plafondlampen 1x uit (de DNC zet ze anders 's avonds
	// aan terwijl er niemand zit). SetSwitchControlledLight zodat de DNC ze niet meer terug aanzet.
	if (bCompHomesReady && !bComp703LightsOff && !Comp703Anchor.IsNearlyZero())
	{
		if (ADayNightController* DNC703 = ADayNightController::GetLocal(W))
		{
			TArray<UPointLightComponent*> L703;
			DNC703->CollectCeilingLightsNear(Comp703Anchor, 750.f, L703);
			for (UPointLightComponent* PL : L703)
			{
				if (!PL) { continue; }
				DNC703->SetSwitchControlledLight(PL, true);
				PL->SetIntensity(0.f);
			}
			if (L703.Num() > 0) { bComp703LightsOff = true; UE_LOG(LogWeedShop, Log, TEXT("Competitive: 703-penthouse lampen uit (%d)"), L703.Num()); }
		}
	}

	// 1) GEOMETRIE (op ELKE machine, 1x): zoek de deuren 603/602, bereken build-boxes + anchors +
	//    no-build. Deterministisch (zelfde deuren + gemeten box op host en client), dus de lokale
	//    build-checks (BuildComponent) kloppen overal.
	if (!bCompHomesReady && !HomeAnchor.IsNearlyZero())
	{
		// De 2 SPELER-MARKERS (CompSpawns.txt) zijn ground truth: de deur-nummer-geometrie klopt op deze
		// pack-map niet (603/602 lagen verkeerd). Marker 1 = host-kamer, marker 2 = joiner-kamer (vloer-posities).
		TArray<FString> CS;
		FFileHelper::LoadFileToStringArray(CS, *(FPaths::ProjectSavedDir() / TEXT("CompSpawns.txt")));
		TArray<FVector> Marks;
		for (const FString& L : CS)
		{
			TArray<FString> P; L.TrimStartAndEnd().ParseIntoArray(P, TEXT(","), true);
			if (P.Num() >= 3) { Marks.Add(FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]))); }
		}
		// Fallback: geen CompSpawns.txt? Pak de LAATSTE 2 F9-markers (MarkedSpots.txt) van deze map -
		// dan hoeft de speler alleen 2x F9 te zetten (geen console-commando nodig). Marker 1 = host, 2 = joiner.
		if (Marks.Num() < 2)
		{
			TArray<FString> MS;
			FFileHelper::LoadFileToStringArray(MS, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
			const FString MapName = W->GetOutermost()->GetName();
			TArray<FVector> All;
			for (const FString& L : MS)
			{
				if (!L.Contains(MapName)) { continue; }
				int32 s = L.Find(TEXT("pos=(")); if (s == INDEX_NONE) { continue; }
				s += 5;
				const int32 e = L.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, s);
				if (e == INDEX_NONE) { continue; }
				TArray<FString> N; L.Mid(s, e - s).ParseIntoArray(N, TEXT(","), true);
				if (N.Num() >= 3) { All.Add(FVector(FCString::Atof(*N[0].TrimStartAndEnd()), FCString::Atof(*N[1].TrimStartAndEnd()), FCString::Atof(*N[2].TrimStartAndEnd()))); }
			}
			if (All.Num() >= 2) { Marks.Reset(); Marks.Add(All[0]); Marks.Add(All[1]); }
		}
		if (Marks.Num() >= 2)
		{
			const FVector MA = Marks[0]; // host-kamer (vloer)
			const FVector MB = Marks[1]; // joiner-kamer (vloer)
			const FVector C703 = Comp703Anchor.IsNearlyZero() ? FVector(HomeAnchor.X, HomeAnchor.Y, HomeAnchor.Z - 110.f) : FVector(Comp703Anchor.X, Comp703Anchor.Y, Comp703Anchor.Z - 110.f); // ECHTE 703-referentie (HomeAnchor is naar de marker overschreven)
			FVector Half;
			if (bHomeBoxReady) { Half = FVector((HomeBoxMax.X - HomeBoxMin.X) * 0.5f, (HomeBoxMax.Y - HomeBoxMin.Y) * 0.5f, 320.f); }
			else if (!MeasureRoomHalf(HomeAnchor, Half)) { Half = FVector(600.f, 600.f, 320.f); }
			const FVector Off603(0.f, 0.f, MA.Z - C703.Z); // RECHT OMLAAG (zelfde XY als 703) - consistent met de meubel-kopie
				CompV603 = Off603;
				CompAnchorHost   = MA + FVector(0.f, 0.f, 110.f);
				CompAnchorJoiner = MB + FVector(0.f, 0.f, 110.f);
				// Spiegel-centrum = midden tussen de 603-KOPIE (op 703's XY = C703) en het mik-punt in 602.
				// Tgt602 = MB + halve (C703->MA)-translatie, met -40 X (iets naar ZUID) zoals getuned door de speler.
				const FVector Tgt602(MB.X + (C703.X - MA.X) * 0.5f - 40.f, MB.Y + (C703.Y - MA.Y) * 0.5f, MB.Z);
				CompMirrorM = FVector((C703.X + Tgt602.X) * 0.5f, (C703.Y + Tgt602.Y) * 0.5f, MB.Z);
				CompMirrorN = FVector(Tgt602.X - C703.X, Tgt602.Y - C703.Y, 0.f).GetSafeNormal();
				auto MirrorBoxXY = [&](const FBox& B) -> FBox {
					const FVector C = B.GetCenter();
					const float dd = (C.X - CompMirrorM.X) * CompMirrorN.X + (C.Y - CompMirrorM.Y) * CompMirrorN.Y;
					const FVector Cm(C.X - 2.f * dd * CompMirrorN.X, C.Y - 2.f * dd * CompMirrorN.Y, C.Z);
					const FVector E = B.GetExtent();
					return FBox(Cm - E, Cm + E);
				};
				// BOUWZONE (home-area): jouw 703-build-area (BuildArea.txt) recht omlaag naar 603 + gespiegeld naar 602.
				FBox Area703(ForceInit);
				{
					FString BA;
					if (FFileHelper::LoadFileToString(BA, *(FPaths::ProjectSavedDir() / TEXT("BuildArea.txt")))) {
						TArray<FString> BLines; BA.ParseIntoArrayLines(BLines);
						const FString MapPath = W->GetOutermost()->GetName();
						for (const FString& Ln : BLines) {
							TArray<FString> Pp; Ln.ParseIntoArray(Pp, TEXT("|"), true);
							if (Pp.Num() < 7 || Pp[0] != MapPath) { continue; }
							const FVector B1(FCString::Atof(*Pp[1]), FCString::Atof(*Pp[2]), FCString::Atof(*Pp[3]));
							const FVector B2(FCString::Atof(*Pp[4]), FCString::Atof(*Pp[5]), FCString::Atof(*Pp[6]));
							const float FZ = FMath::Min(B1.Z, B2.Z);
							Area703 = FBox(FVector(FMath::Min(B1.X, B2.X), FMath::Min(B1.Y, B2.Y), FZ - 70.f), FVector(FMath::Max(B1.X, B2.X), FMath::Max(B1.Y, B2.Y), FZ + 380.f));
						}
					}
				}
				if (Area703.IsValid) {
					CompHomeBoxHost   = Area703.ShiftBy(Off603);
					CompHomeBoxJoiner = MirrorBoxXY(CompHomeBoxHost);
				} else {
					CompHomeBoxHost   = FBox(FVector(MA.X - Half.X, MA.Y - Half.Y, MA.Z - 120.f), FVector(MA.X + Half.X, MA.Y + Half.Y, MA.Z + 520.f));
					CompHomeBoxJoiner = FBox(FVector(MB.X - Half.X, MB.Y - Half.Y, MB.Z - 120.f), FVector(MB.X + Half.X, MB.Y + Half.Y, MB.Z + 520.f));
				}
				// NO-BUILD-zones: jouw 703-zones recht omlaag naar 603 + gespiegeld naar 602.
				const FBox Box703(FVector(C703.X - (Half.X + 350.f), C703.Y - (Half.Y + 350.f), C703.Z - 120.f), FVector(C703.X + (Half.X + 350.f), C703.Y + (Half.Y + 350.f), C703.Z + 520.f));
				TArray<FBox> SrcZones; GetHomeNoBuildZones(Box703, SrcZones);
				CompNoBuildZones.Reset();
				for (const FBox& Z : SrcZones) { const FBox Z603 = Z.ShiftBy(Off603); CompNoBuildZones.Add(Z603); CompNoBuildZones.Add(MirrorBoxXY(Z603)); }
			// Dichtstbijzijnde deur bij elke marker -> speler-kamer (van het bewoner-slot af).
			if (ACityDoor* DH = CompDoorHost.Get()) { DH->SetPlayerHome(); }
			if (ACityDoor* DJ = CompDoorJoiner.Get()) { DJ->SetPlayerHome(); }
			bCompHomesReady = true;
			UE_LOG(LogWeedShop, Log, TEXT("Competitive: kamers klaar via markers - host=(%.0f,%.0f,%.0f) joiner=(%.0f,%.0f,%.0f) no-build %d"), MA.X, MA.Y, MA.Z, MB.X, MB.Y, MB.Z, CompNoBuildZones.Num());
		}
		else if (CompDiagCount < 40)
		{
			UE_LOG(LogWeedShop, Warning, TEXT("Competitive: WACHT op spawn-markers - zet 2x F9 (1 per appartement) + console WeedSaveCompSpawns (nu %d gevonden)"), Marks.Num());
		}
	}

	// 2) MEUBEL-SPIEGEL (alleen host/standalone, verse game, 1x): kopie 703->603, echte spiegel ->602.
	//    Alleen de host spawnt (meubels repliceren naar de client); op een geladen game herstelt de save
	//    de meubels zelf, dus dan niet opnieuw spawnen.
	if (bCompHomesReady && !bCompMirrorDone && W->GetNetMode() != NM_Client)
	{
		bool bFresh = true;
		if (UGameInstance* GI = W->GetGameInstance())
		{ if (USaveGameSubsystem* Sv = GI->GetSubsystem<USaveGameSubsystem>()) { bFresh = Sv->IsFreshGame(); } }
		if (bFresh && !CompMirrorN.IsNearlyZero())
		{
						TArray<FString> FL;
			FFileHelper::LoadFileToStringArray(FL, *WeedData::File(TEXT("StarterFurniture.txt")));
			struct FFI { FName Id; FVector Loc; float Yaw; };
				TArray<FFI> Items; FVector Cf(0.f, 0.f, 0.f);
				for (const FString& L : FL)
				{
					TArray<FString> Pc; L.ParseIntoArray(Pc, TEXT(","));
					if (Pc.Num() < 5) { continue; }
					FFI It; It.Id = FName(*Pc[0]);
					It.Loc = FVector(FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]), FCString::Atof(*Pc[3]));
					It.Yaw = FCString::Atof(*Pc[4]);
					Items.Add(It); Cf += It.Loc;
				}
				if (Items.Num() > 0) { Cf /= (float)Items.Num(); }
				const FVector CompMA = CompAnchorHost - FVector(0.f, 0.f, 110.f);
				const FVector Ref703 = Comp703Anchor.IsNearlyZero() ? HomeAnchor : Comp703Anchor;
				const FVector CopyOff(0.f, 0.f, CompAnchorHost.Z - Ref703.Z); // 603 ligt recht ONDER 703 -> zelfde XY, alleen omlaag (matcht de kamer erboven exact)
				int32 NMir = 0;
				for (const FFI& It : Items)
				{
					const FVector L603 = It.Loc + CopyOff;
					SpawnHomeItem(W, It.Id, L603, It.Yaw);
					const float d = (L603.X - CompMirrorM.X) * CompMirrorN.X + (L603.Y - CompMirrorM.Y) * CompMirrorN.Y;
					const FVector L602(L603.X - 2.f * d * CompMirrorN.X, L603.Y - 2.f * d * CompMirrorN.Y, L603.Z);
					float Yaw602 = It.Yaw;
					// De spiegel klapt 1 as om: meubels die LANGS de spiegel-as kijken (loodrecht op de andere as)
					// moeten 180 om om weer de goede kant op te staan; meubels langs de andere as blijven gelijk.
					// Wand-montage (lichtschakelaar) orienteert zichzelf op de muur -> die laten we met rust.
					const float FacingDotN = FMath::Cos(FMath::DegreesToRadians(It.Yaw)) * CompMirrorN.X + FMath::Sin(FMath::DegreesToRadians(It.Yaw)) * CompMirrorN.Y;
					if (It.Id != FName(TEXT("LightSwitch")) && FMath::Abs(FacingDotN) < 0.5f) { Yaw602 = It.Yaw + 180.f; }
					SpawnHomeItem(W, It.Id, L602, Yaw602);
					NMir += 2;
				}
				UE_LOG(LogWeedShop, Log, TEXT("Competitive: %d meubels gecentreerd; zwaartepunt 703=(%.0f,%.0f) host=(%.0f,%.0f)"), NMir, Cf.X, Cf.Y, CompMA.X, CompMA.Y);
		}
		bCompMirrorDone = true;
	}

	// 2b) LICHTSCHAKELAARS op de CLIENT lokaal spawnen: APackLightSwitch repliceert niet (bReplicates=false),
	//     dus zonder dit ziet de joiner geen schakelaars in 602. De host heeft ze al via stap 2; de client
	//     berekent dezelfde posities (CopyOff + spiegel uit stap 1 = machine-onafhankelijk).
	if (bCompHomesReady && !bCompSwitchesDone && !CompMirrorN.IsNearlyZero() && W->GetNetMode() == NM_Client)
	{
		const FVector Ref703sw = Comp703Anchor.IsNearlyZero() ? HomeAnchor : Comp703Anchor;
		const FVector CopyOffSw(0.f, 0.f, CompAnchorHost.Z - Ref703sw.Z);
		TArray<FString> FLsw;
		FFileHelper::LoadFileToStringArray(FLsw, *WeedData::File(TEXT("StarterFurniture.txt")));
		for (const FString& Lsw : FLsw)
		{
			TArray<FString> Pc; Lsw.ParseIntoArray(Pc, TEXT(","));
			if (Pc.Num() < 5 || Pc[0] != TEXT("LightSwitch")) { continue; }
			const FVector Loc0(FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]), FCString::Atof(*Pc[3]));
			const float Yaw0 = FCString::Atof(*Pc[4]);
			const FVector L603 = Loc0 + CopyOffSw;
			SpawnHomeItem(W, FName(TEXT("LightSwitch")), L603, Yaw0);
			const float dsw = (L603.X - CompMirrorM.X) * CompMirrorN.X + (L603.Y - CompMirrorM.Y) * CompMirrorN.Y;
			const FVector L602(L603.X - 2.f * dsw * CompMirrorN.X, L603.Y - 2.f * dsw * CompMirrorN.Y, L603.Z);
			SpawnHomeItem(W, FName(TEXT("LightSwitch")), L602, Yaw0);
		}
		bCompSwitchesDone = true;
		UE_LOG(LogWeedShop, Log, TEXT("Competitive: lichtschakelaars lokaal op client gespawnd"));
	}


	// 3) SPELERS naar hun eigen kamer (host/standalone): host -> 603, ieder ander -> 602. Eén keer per
	//    speler; een speler die AL in z'n kamer staat (geladen game) wordt niet verplaatst. Eigen mini-pin:
	//    vloer er nog niet? -> even bevriezen, volgende scan opnieuw (de bevroren speler triggert het streamen).
	if (bCompHomesReady && bCompMirrorDone && W->GetNetMode() != NM_Client)
	{
		// Deuren van 603/602 speler-eigen houden (de woningen-pass kan ze anders her-locken als bewoner).
		if (ACityDoor* DH = CompDoorHost.Get()) { DH->SetPlayerHome(); }
		if (ACityDoor* DJ = CompDoorJoiner.Get()) { DJ->SetPlayerHome(); }
		APlayerController* HostPC = W->GetFirstPlayerController();
		APawn* HostPawn = HostPC ? HostPC->GetPawn() : nullptr;
		if (CompDiag3Count < 16)
		{
			++CompDiag3Count;
			UE_LOG(LogWeedShop, Verbose, TEXT("[CompDiag3] hostPawn=(%.0f,%.0f,%.0f) anchorH=(%.0f,%.0f,%.0f) homed=%d"),
				HostPawn ? HostPawn->GetActorLocation().X : 0.f, HostPawn ? HostPawn->GetActorLocation().Y : 0.f, HostPawn ? HostPawn->GetActorLocation().Z : 0.f, CompAnchorHost.X, CompAnchorHost.Y, CompAnchorHost.Z, CompHomedPawns.Num());
		}
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* Pw = It->Get() ? It->Get()->GetPawn() : nullptr;
			if (!Pw || CompHomedPawns.Contains(Pw)) { continue; }
			const bool bHost = (Pw == HostPawn);
			const FVector PLoc = Pw->GetActorLocation();
			// Staat 'ie al in z'n EIGEN kamer? -> klaar. PER ROL: host hoort in 603, joiner in 602. Anders
				// markeert de joiner zich "thuis" in 603 (waar 'ie initieel via de gedeelde HomeAnchor spawnt) en
				// gaat 'ie nooit naar 602.
				const FBox& MyBox = bHost ? CompHomeBoxHost : CompHomeBoxJoiner;
				if (MyBox.IsValid && MyBox.IsInsideOrOn(PLoc)) { CompHomedPawns.Add(Pw); continue; }
			const FVector Anchor = bHost ? CompAnchorHost : CompAnchorJoiner;
			UCharacterMovementComponent* CMv = Pw->FindComponentByClass<UCharacterMovementComponent>();
			FHitResult FH; FCollisionQueryParams LQ(SCENE_QUERY_STAT(CompHomeLand), false);
			for (FConstPlayerControllerIterator It2 = W->GetPlayerControllerIterator(); It2; ++It2)
			{ if (APawn* P2 = It2->Get() ? It2->Get()->GetPawn() : nullptr) { LQ.AddIgnoredActor(P2); } }
			const FVector TS = Anchor + FVector(0.f, 0.f, 30.f);
			if (W->LineTraceSingleByChannel(FH, TS, TS - FVector(0.f, 0.f, 320.f), ECC_WorldStatic, LQ))
			{
				// Vloer gevonden -> netjes erbovenop + lopend.
				Pw->SetActorLocation(FH.Location + FVector(0.f, 0.f, 96.f), false, nullptr, ETeleportType::TeleportPhysics);
				if (CMv) { CMv->StopMovementImmediately(); CMv->SetMovementMode(MOVE_Walking); }
				CompHomedPawns.Add(Pw);
				if (UPhoneClientComponent* Ph = Pw->FindComponentByClass<UPhoneClientComponent>())
				{
					// kamer-nummer is met markers niet meer relevant
					Ph->Toast(bHost ? TEXT("Competitive: this is your apartment.") : TEXT("Competitive: this is your apartment (mirror)."), FColor::Cyan, 6.f);
				}
			}
			else
			{
				// Vloer nog niet ingestreamd -> op de plek bevriezen (vliegen, geen zwaartekracht) tot 'ie er is.
				Pw->SetActorLocation(Anchor, false, nullptr, ETeleportType::TeleportPhysics);
				if (CMv) { CMv->StopMovementImmediately(); CMv->SetMovementMode(MOVE_Flying); }
			}
		}
	}
}

bool ADoorRetrofitter::GetCompDeliverySpot(bool bJoiner, FVector& Out) const
{
	if (!bCompHomesReady) { return false; }
	const FVector Anchor = bJoiner ? CompAnchorJoiner : CompAnchorHost;
	UWorld* W = GetWorld();
	// Dichtstbijzijnde GELDIGE deur van de eigen kamer - exact hetzelfde filter als PickDoors in
	// TickCompetitiveRooms (zelfde verdieping + binnen bereik), maar dan de allerdichtste.
	ACityDoor* Best = nullptr; float BestD2 = TNumericLimits<float>::Max();
	if (W)
	{
		for (TActorIterator<ACityDoor> It(W); It; ++It)
		{
			if (!IsValid(*It)) { continue; }
			const FVector DL = It->GetActorLocation();
			if (FMath::Abs(DL.Z - Anchor.Z) > 300.f) { continue; }
			const float D2 = FVector::Dist2D(DL, Anchor);
			if (D2 > 1200.f) { continue; }
			if (D2 < BestD2) { BestD2 = D2; Best = *It; }
		}
	}
	if (Best)
	{
		// Op de stoep, net buiten de deur (zelfde formule als de huis-deur-tak in FindDeliveryPoint):
		// deur + (deur - anchor, in 2D genormaliseerd) * 150.
		const FVector DoorLoc = Best->GetActorLocation();
		FVector Outward = DoorLoc - Anchor; Outward.Z = 0.f; Outward = Outward.GetSafeNormal();
		if (Outward.IsNearlyZero()) { Outward = Best->GetActorForwardVector(); Outward.Z = 0.f; Outward = Outward.GetSafeNormal(); }
		Out = DoorLoc + Outward * 150.f;
	}
	else
	{
		Out = Anchor; // geen deur gevonden -> val terug op de anchor
	}
	return true;
}

uint32 ADoorRetrofitter::GetCompRentDoorId(bool bJoiner) const
{
	// COMPETITIVE huur: de VOORDEUR van de comp-kamer (host/joiner) -> WorldSync-id. Zelfde
	// dichtstbijzijnde-deur-filter als GetCompDeliverySpot (zelfde verdieping, binnen bereik), met
	// voorkeur voor de apt-genummerde deur (de echte voordeur) zoals PickDoors in TickCompetitiveRooms.
	// NB: CompDoorHost/CompDoorJoiner (members) worden nergens gezet -> bewust via de anchors afleiden.
	if (!bCompHomesReady) { return 0; }
	UWorld* W = GetWorld();
	if (!W) { return 0; }
	const FVector Anchor = bJoiner ? CompAnchorJoiner : CompAnchorHost;
	ACityDoor* Best = nullptr; float BestD2 = TNumericLimits<float>::Max();
	ACityDoor* BestApt = nullptr; float BestAptD2 = TNumericLimits<float>::Max();
	// PERF: deur-registry i.p.v. TActorIterator; registry is per-PROCES -> wereld-filter.
	for (const TWeakObjectPtr<ACityDoor>& WD : ACityDoor::GetAll())
	{
		ACityDoor* D = WD.Get();
		if (!IsValid(D) || D->GetWorld() != W) { continue; }
		const FVector DL = D->GetActorLocation();
		if (FMath::Abs(DL.Z - Anchor.Z) > 300.f) { continue; }
		const float D2 = FVector::Dist2D(DL, Anchor);
		if (D2 > 1200.f) { continue; }
		if (D2 < BestD2) { BestD2 = D2; Best = D; }
		if (D->GetAptNumber() > 0 && D2 < BestAptD2) { BestAptD2 = D2; BestApt = D; }
	}
	ACityDoor* Pick = BestApt ? BestApt : Best;
	return Pick ? Pick->GetWorldSyncDoorId() : 0;
}

void ADoorRetrofitter::ApplyCompRentDoorLocks()
{
	// COMPETITIVE huur: pas de gerepliceerde per-deur overdue-lijst (GameState) toe op de LOKALE
	// deur-kopieen, op BEIDE processen. Moet NA de claim-pass draaien (aanroep direct na
	// TickCompetitiveRooms in ScanAndConvert): SetCompPlayerHome zet bLocked daar elke scan weer uit.
	UWorld* W = GetWorld();
	AWeedShopGameState* GS = W ? W->GetGameState<AWeedShopGameState>() : nullptr;
	if (!GS || !GS->IsCompetitive()) { return; }
	const TArray<uint32>& Ids = GS->GetRentOverdueDoorIds();
	for (const TWeakObjectPtr<ACityDoor>& WD : ACityDoor::GetAll())
	{
		ACityDoor* D = WD.Get();
		if (!IsValid(D) || D->GetWorld() != W) { continue; } // registry is per-PROCES -> wereld-filter
		const uint32 Id = D->GetWorldSyncDoorId();
		if (Id != 0 && Ids.Contains(Id))
		{
			// ONVOORWAARDELIJK her-zetten (niet gaten op IsRentOverdue): de claim-pass laat bRentDue
			// staan maar zet bLocked uit -> alleen-bij-transitie zetten zou de deur open laten.
			// Bedrag = het vaste starter-huurbedrag (EUR 500, zelfde 50000 als de inning hierboven).
			D->SetRentOverdue(50000);
		}
		else if (D->IsRentOverdue())
		{
			// Niet (meer) in de lijst -> lock eraf. Was dit op DEZE machine de PARTNER-deur (comp-geclaimd
			// maar niet je eigen huis)? Direct weer als partner-deur op slot, anders staat 'ie een
			// scan-rondje (~2s) open totdat de claim-pass 'm herclaimt.
			D->ClearRentOverdue();
			if (D->IsCompForced() && !D->IsPlayerHome()) { D->SetCompPartner(); }
		}
	}
}

void ADoorRetrofitter::GetOutdoorWaitSpots(TArray<FVector>& Out) const
{
	UWorld* W = GetWorld();
	if (!W) { Out.Reset(); return; }

	// Levende deuren tellen via de per-proces registry MET GetWorld()-filter (PIE/co-op-in-1-proces).
	const TArray<TWeakObjectPtr<ACityDoor>>& AllDoors = ACityDoor::GetAll();
	int32 LiveCount = 0;
	for (const TWeakObjectPtr<ACityDoor>& WD : AllDoors)
	{
		const ACityDoor* D = WD.Get();
		if (IsValid(D) && D->GetWorld() == W) { ++LiveCount; }
	}

	// Cache: herbouw alleen als 't deur-aantal wijzigt (laat-gestreamde gebouwen brengen deuren bij).
	if (bWaitSpotsBuilt && LiveCount == WaitSpotsDoorCount)
	{
		Out = WaitSpots;
		return;
	}

	WaitSpots.Reset();
	for (const TWeakObjectPtr<ACityDoor>& WD : AllDoors)
	{
		const ACityDoor* D = WD.Get();
		if (!IsValid(D) || D->GetWorld() != W) { continue; }

		// Alleen commerciele/niet-woning-deuren: winkel/garage/lobby/steeg. Woning-deuren hebben een
		// appartement-nummer (verdieping*100+unit) of zijn een speler-huis; die vallen af.
		// (Koopbare panden hebben ook een AptNumber != 0, dus die worden hierdoor eveneens uitgesloten.)
		if (D->GetAptNumber() != 0 || D->IsPlayerHome()) { continue; }

		// Stoep-punt net BUITEN de deur: zelfde 150-cm-formule als GetCompDeliverySpot/FindDeliveryPoint.
		// Zonder interieur-anker gebruiken we de deur-voorwaartse richting; kies de kant die NIET binnen
		// een woon-kamer valt (zodat we nooit binnen een kamer belanden).
		const FVector DoorLoc = D->GetActorLocation();
		FVector Fwd = D->GetActorForwardVector(); Fwd.Z = 0.f; Fwd = Fwd.GetSafeNormal();
		if (Fwd.IsNearlyZero()) { Fwd = FVector(1.f, 0.f, 0.f); }
		FVector Spot = DoorLoc + Fwd * 150.f;
		if (IsInsideHomeRoom(Spot, 0.f)) { Spot = DoorLoc - Fwd * 150.f; } // andere kant proberen
		if (IsInsideHomeRoom(Spot, 0.f)) { continue; } // beide kanten binnen -> deze deur overslaan

		WaitSpots.Add(Spot);
	}

	WaitSpotsDoorCount = LiveCount;
	bWaitSpotsBuilt = true;
	Out = WaitSpots;
}

void ADoorRetrofitter::GetBeachPropertyOffers(TArray<FCityPropertyOffer>& Out) const
{
	Out.Reset();
	for (int32 i = 0; i < BeachHomes.Num(); ++i)
	{
		FCityPropertyOffer O;
		O.HomeIndex = i;
		O.Homes.Add(i);
		O.bStarter = (i == 0);
		O.PriceCents = O.bStarter ? 0 : (BeachHomePrices.IsValidIndex(i) ? BeachHomePrices[i] : 0);
		O.Title = O.bStarter ? TEXT("Your apartment") : BeachHomes[i].Number;
		O.Sub = FString::Printf(TEXT("~%.0f x %.0f m"),
			BeachHomes[i].RoomHalf.X * 2.f / 100.f, BeachHomes[i].RoomHalf.Y * 2.f / 100.f);
		Out.Add(O);
	}
}

void ADoorRetrofitter::RegisterHomeAtPlayer(APawn* Player)
{
	UWorld* W = GetWorld();
	if (!W || !Player) { return; }
	const FVector Loc = Player->GetActorLocation();

	// Kamer rond de speler meten (wanden). Lukt dat niet -> nette default.
	FVector Half;
	if (!MeasureRoomHalf(Loc, Half)) { Half = FVector(450.f, 450.f, 320.f); }

	// Interieur op vloerhoogte: trace omlaag naar het vloerslab.
	FVector Inside = Loc;
	{
		FHitResult Fl;
		FCollisionQueryParams FQ(SCENE_QUERY_STAT(HomeRegFloor), false, Player);
		if (W->LineTraceSingleByChannel(Fl, Loc + FVector(0.f, 0.f, 40.f), Loc - FVector(0.f, 0.f, 500.f), ECC_WorldStatic, FQ))
		{
			Inside.Z = Fl.Location.Z;
		}
	}

	// Prijs schatten uit de oppervlakte (~EUR 200 / m2, oplopend met grootte). Tunebaar per regel achteraf.
	const float AreaM2 = (Half.X * 2.f / 100.f) * (Half.Y * 2.f / 100.f);
	const int64 PriceCents = (int64)FMath::RoundToInt(AreaM2 * 200.0) * 100;

	const FString Name = FString::Printf(TEXT("Unit %d"), BeachHomes.Num());
	const FString Row = FString::Printf(TEXT("%.0f,%.0f,%.0f,%.0f,%.0f,%lld,%s\n"),
		Inside.X, Inside.Y, Inside.Z, Half.X, Half.Y, (long long)PriceCents, *Name);
	FFileHelper::SaveStringToFile(Row, *WeedData::File(TEXT("BeachHomes.txt")),
		FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);

	RebuildBeachHomes();

	if (UPhoneClientComponent* Ph = Player->FindComponentByClass<UPhoneClientComponent>())
	{
		Ph->Toast(FString::Printf(TEXT("Home registered: %s  (~%.0f x %.0f m, EUR %lld)"),
			*Name, Half.X * 2.f / 100.f, Half.Y * 2.f / 100.f, (long long)(WeedRoundEuros((int64)PriceCents) / 100)), FColor::Green, 6.f);
	}
}
