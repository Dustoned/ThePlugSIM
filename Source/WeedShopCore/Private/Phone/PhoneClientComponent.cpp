#include "Phone/PhoneClientComponent.h"
#include "DrawDebugHelpers.h"

#include "WeedShopCore.h"
#include "UI/BootCoverWidget.h"
#include "Game/WeedShopGameState.h"
#include "Npc/NpcRegistryComponent.h" // GetNpcRegistry()->IsOnRefusalCooldown() = volledige type nodig
#include "World/DayCycleComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Progression/LevelComponent.h"
#include "Progression/GoalsComponent.h"
#include "Phone/ContactsComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Economy/EconomyComponent.h"
#include "Customer/CustomerBase.h"
#include "Customer/CustomerSpawner.h"
#include "World/DeliveryDrone.h"
#include "World/CityTypes.h" // FApartmentHome / FCityPropertyOffer (verhuisd uit CityGenerator.h)
#include "World/DoorRetrofitter.h"
#include "World/CityDoor.h"
#include "EngineUtils.h"
#include "Cultivation/PotTypes.h"
#include "Cultivation/GrowPlant.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameViewportClient.h"
#include "InputCoreTypes.h"
#include "UI/PhoneWidget.h"
#include "UI/DevMenuWidget.h"
#include "UI/DealWidget.h"
#include "UI/StatusHudWidget.h"
#include "UI/PlantInfoWidget.h"
#include "UI/HotbarWidget.h"
#include "UI/InventoryWidget.h"
#include "UI/RollWidget.h"
#include "UI/CompassWidget.h"
#include "UI/HotkeyHintWidget.h"
#include "UI/AtmWidget.h"
#include "UI/WardrobeWidget.h"
#include "UI/SpotInfoWidget.h"
#include "World/RoomStamper.h"
#include "World/MapBorder.h"
#include "HAL/FileManager.h"
#include "UI/PackWidget.h"
#include "UI/ShelfWidget.h"
#include "UI/DryingRackWidget.h"
#include "UI/StoreWidget.h"
#include "World/StoreCounter.h"
#include "Placement/PlaceableProp.h"
#include "World/WaterSink.h"
#include "Progression/LevelComponent.h"
#include "World/HeatComponent.h" // huur-schuld -> heat
#include "Save/AssetKeepAliveSubsystem.h" // WBP-pauzemenu-klasse rooten over map-loads (laadtijd-fix)
#include "World/Atm.h"           // kluis-capaciteit scannen
#include "UI/HandInfoWidget.h"
#include "UI/WeedToast.h"
#include "Cultivation/DryingRack.h"
#include "UI/PauseMenuWidget.h"
#include "UI/MainMenuWidget.h"
#include "UI/SaveIndicatorWidget.h"
#include "UI/LevelUpWidget.h"
#include "UI/CrosshairWidget.h"
#include "UI/SettingsWidget.h"
#include "UI/LightDimmerWidget.h"
#include "World/PackLightSwitch.h"
#include "World/DayNightController.h"
#include "World/LampLinkMarker.h"
#include "Components/PointLightComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Camera/CameraActor.h"
#include "UI/MapWidget.h"
#include "Interaction/PlayerNpcActions.h"
#include "Customer/CustomerBase.h"
#include "World/StorageShelf.h"
#include "Save/SaveGameSubsystem.h"
#include "Engine/GameInstance.h"
#include "Camera/CameraComponent.h"
#include "Misc/ConfigCacheIni.h"
#include "Blueprint/UserWidget.h"
#include "Net/UnrealNetwork.h"

UPhoneClientComponent::UPhoneClientComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPhoneClientComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPhoneClientComponent, bBankAppUnlocked);
	DOREPLIFETIME(UPhoneClientComponent, OwnedHomes);
	DOREPLIFETIME(UPhoneClientComponent, ActiveHome);
	DOREPLIFETIME(UPhoneClientComponent, RentDueDay);
	DOREPLIFETIME(UPhoneClientComponent, bPhoneOpenRep);
	// Bezorg-lijsten alleen naar de eigenaar-client (privacy in co-op/competitive: je pakketten zijn van jou).
	DOREPLIFETIME_CONDITION(UPhoneClientComponent, PendingDeliveries, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UPhoneClientComponent, DeliveredHistory, COND_OwnerOnly);
}

void UPhoneClientComponent::OnRep_Deliveries()
{
	// De bezorg-lijsten zijn zojuist naar deze (eigenaar-)client gerepliceerd. De Packages-app in de
	// PhoneWidget difft zelf elke tick op PackagesSignature() en herbouwt de kaarten wanneer de set
	// wijzigt -> hier is geen expliciete rebuild nodig (en we raken PhoneWidget bewust niet aan). De hook
	// bestaat zodat de replicatie-flow een OnRep heeft; het aanmaken van de widget blijft lui bij UI-gebruik.
}

void UPhoneClientComponent::ServerSetPhoneOpen_Implementation(bool bInOpen)
{
	bPhoneOpenRep = bInOpen; // server-authoritative -> repliceert naar alle proxies (texting-anim)
}

int32 UPhoneClientComponent::GetRentDueCents() const
{
	int64 Total = 0;
	for (int32 H : OwnedHomes)
	{
		Total += 40000;                                  // basis-huur per appartement (EUR 400)
		Total += (int64)GetHomeSellValueCents(H) * 12 / 100; // duurder pand -> meer huur
	}
	return static_cast<int32>(WeedRoundEuros(Total));
}

void UPhoneClientComponent::ProcessRentForDay(int32 Day)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }

	// Intro-melding (1x): vertel het huur-doel zodra de eerste dag voorbij is.
	if (!bShownRentIntro)
	{
		bShownRentIntro = true;
		const int32 Rent0 = GetRentDueCents();
		if (Rent0 > 0 && GEngine)
		{
			UWeedToast::NotifyPawn(GetOwner(), -1, 7.f, FColor(255, 210, 120),
				FString::Printf(TEXT("Make money for rent! EUR %d due by day %d (in 30 days) - it comes off your bank."), (int32)(WeedRoundEuros((int64)Rent0) / 100), RentDueDay));
		}
	}

	if (Day < RentDueDay || OwnedHomes.Num() == 0) { return; }

	const int32 Rent = GetRentDueCents();
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (Rent > 0 && Econ)
	{
		Econ->ChargeBank(Rent); // mag in de min (schuld)
		const bool bDebt = Econ->IsBankInDebt();
		if (bDebt)
		{
			if (AWeedShopGameState* GS = GetGS())
			{
				if (UHeatComponent* Heat = GS->GetHeat()) { Heat->AddHeatFor(Cast<APawn>(GetOwner()), 20.f); } // schuld trekt aandacht (gematigd) - per-speler in competitive
			}
		}
		if (GEngine)
		{
			UWeedToast::NotifyPawn(GetOwner(), -1, 6.f, bDebt ? FColor::Red : FColor(150, 220, 160), bDebt
				? FString::Printf(TEXT("Rent EUR %d charged - bank is in the RED (EUR %lld). Pay it off, heat rising!"), (int32)(WeedRoundEuros((int64)Rent) / 100), (long long)(WeedRoundEuros((int64)Econ->GetBankCents()) / 100))
				: FString::Printf(TEXT("Rent paid: EUR %d. Bank: EUR %lld"), (int32)(WeedRoundEuros((int64)Rent) / 100), (long long)(WeedRoundEuros((int64)Econ->GetBankCents()) / 100)));
		}
	}
	RentDueDay = Day + 30; // volgende termijn over 30 dagen
}

void UPhoneClientComponent::BeginPlay()
{
	Super::BeginPlay();
	// Game-instellingen (FOV/sensitivity) inladen + FOV toepassen voor de lokale speler.
	LoadGameSettings();
	// Woning-systeem: periodiek de starter toekennen (server) + mijn eigen deuren ontgrendelen (lokaal).
	GetWorld()->GetTimerManager().SetTimer(PropertyTimer, this, &UPhoneClientComponent::PropertyTick, 1.0f, true, 1.0f);
}

void UPhoneClientComponent::LoadGameSettings()
{
	float Fov = 90.f, Sens = 1.f;
	GConfig->GetFloat(TEXT("ThePlugSIM.Game"), TEXT("FOV"), Fov, GGameUserSettingsIni);
	GConfig->GetFloat(TEXT("ThePlugSIM.Game"), TEXT("MouseSensitivity"), Sens, GGameUserSettingsIni);
	LookSensitivity = FMath::Clamp(Sens, 0.1f, 4.f);
	ApplyFov(Fov);
	bool bBob = true; GConfig->GetBool(TEXT("ThePlugSIM.Game"), TEXT("HeadBob"), bBob, GGameUserSettingsIni); bHeadBob = bBob;
}

void UPhoneClientComponent::ApplyFov(float NewFov)
{
	FovValue = FMath::Clamp(NewFov, 60.f, 120.f);
	if (GetOwner())
	{
		if (UCameraComponent* Cam = GetOwner()->FindComponentByClass<UCameraComponent>())
		{
			Cam->SetFieldOfView(FovValue);
		}
	}
	GConfig->SetFloat(TEXT("ThePlugSIM.Game"), TEXT("FOV"), FovValue, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UPhoneClientComponent::SetLookSensitivity(float S)
{
	LookSensitivity = FMath::Clamp(S, 0.1f, 4.f);
	GConfig->SetFloat(TEXT("ThePlugSIM.Game"), TEXT("MouseSensitivity"), LookSensitivity, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UPhoneClientComponent::SetHeadBob(bool bOn)
{
	bHeadBob = bOn;
	GConfig->SetBool(TEXT("ThePlugSIM.Game"), TEXT("HeadBob"), bHeadBob, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UPhoneClientComponent::OpenSettings()
{
	EnsureWidget();
	bSettingsOpen = true;
	UpdateCursor();
}

void UPhoneClientComponent::CloseSettings()
{
	bSettingsOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::OpenLightDimmer(APackLightSwitch* Sw)
{
	if (!Sw) { return; }
	EnsureWidget();
	DimmerSwitch = Sw;
	Sw->SetOn(true); // dimmer openen = licht aan (zodat je het effect van de slider ziet)
	bLightDimmerOpen = true;
	UpdateCursor();
}

void UPhoneClientComponent::CloseLightDimmer()
{
	bLightDimmerOpen = false;
	DimmerSwitch = nullptr;
	UpdateCursor();
}

APackLightSwitch* UPhoneClientComponent::GetDimmerSwitch() const
{
	return DimmerSwitch.Get();
}

void UPhoneClientComponent::EnterLinkMode()
{
	if (bLinkModeActive) { return; }
	APackLightSwitch* Sw = DimmerSwitch.Get();
	UWorld* W = GetWorld();
	ADayNightController* DNC = W ? ADayNightController::GetLocal(W) : nullptr;
	if (!Sw || !W || !DNC) { return; }

	bLinkModeActive = true;
	LinkSwitch = Sw;
	Sw->SetLinkPreview(true); // alle lampen in de buurt even aan + de gelinkte BLAUW kleuren

	// Enumereer de lampen via DEZELFDE bron als ClaimLamps (CollectCeilingLightsNear) zodat de marker-key
	// gegarandeerd identiek is aan de claim-key. Dedupe op key (CeilLamp + CeilGlow delen 1 positie).
	// Enumereer de DIFFUSERS (lightboxes) via CollectCeilingEmisNear -> markers + blauwe glow vallen precies op
	// de lamp-box. De sleutel (MakeLampKey, grof op Z) is identiek aan die in ClaimLamps voor dezelfde lamp.
	TArray<UMaterialInstanceDynamic*> Mids; TArray<float> Bright; TArray<FVector> Pos;
	DNC->CollectCeilingEmisNear(Sw->GetActorLocation(), Sw->GetControlRadius(), Mids, Bright, Pos);
	TSet<FString> Seen;
	for (const FVector& LampPos : Pos)
	{
		const FString Key = APackLightSwitch::MakeLampKey(LampPos);
		if (Seen.Contains(Key)) { continue; }
		Seen.Add(Key);

		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Klik-doel + blauwe glow MIDDEN op de lightbox (diffuser-positie).
		if (ALampLinkMarker* M = W->SpawnActor<ALampLinkMarker>(ALampLinkMarker::StaticClass(), FTransform(LampPos), SP))
		{
			M->Init(Sw, this, Key); // Init -> RefreshLink: blauw als al gelinkt
			LinkMarkers.Add(M);
		}
	}

	// Dimmer dicht; link-modus draait verder als losse game-input-state (los van de 450cm dimmer-auto-close).
	CloseLightDimmer();
	// Sluit vanzelf na 60s zonder klik (elke marker-klik reset 'm via NotifyLinkActivity).
	if (W) { W->GetTimerManager().SetTimer(LinkIdleTimer, this, &UPhoneClientComponent::ExitLinkMode, 60.f, false); }
	UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Cyan,
		TEXT("Link-modus: kijk een lamp aan + interact om te koppelen (blauw = gelinkt). Esc of dim-menu = klaar."));
}

void UPhoneClientComponent::ExitLinkMode()
{
	if (!bLinkModeActive) { return; }
	bLinkModeActive = false;
	if (APackLightSwitch* Sw = LinkSwitch.Get()) { Sw->SetLinkPreview(false); } // lampen terug naar normaal
	LinkSwitch = nullptr;
	for (ALampLinkMarker* M : LinkMarkers) { if (M) { M->Destroy(); } }
	LinkMarkers.Reset();
	if (UWorld* W = GetWorld()) { W->GetTimerManager().ClearTimer(LinkIdleTimer); }
	// Links zijn al per-klik opgeslagen (ToggleLampLink->SaveLinks); niets extra te bewaren.
}

void UPhoneClientComponent::NotifyLinkActivity()
{
	if (!bLinkModeActive) { return; }
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().SetTimer(LinkIdleTimer, this, &UPhoneClientComponent::ExitLinkMode, 60.f, false);
	}
}

void UPhoneClientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ExitLinkMode(); // markers opruimen bij pawn-death / level-transitie
	Super::EndPlay(EndPlayReason);
}

AWeedShopGameState* UPhoneClientComponent::GetGS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
}

void UPhoneClientComponent::MarkChatSeen(FName ContactId)
{
	if (ContactId.IsNone()) { return; }
	// Server-authoritative: zet bSeen op de berichten (repliceert) zodat de badge bij BEIDE spelers verdwijnt.
	ServerMarkThreadSeen(ContactId);
}

void UPhoneClientComponent::ServerMarkThreadSeen_Implementation(FName ContactId)
{
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS && GS->GetContacts())
	{
		// Competitive: server leidt de acterende speler af uit de owner-pawn (GetOwner = de speler-pawn
		// waarop deze component draait). Buiten competitive blijft CallerId leeg = gedeeld/co-op.
		const APawn* Me = Cast<APawn>(GetOwner());
		const FString CallerId = (GS->IsCompetitive() && Me) ? USaveGameSubsystem::StablePlayerId(Me) : FString();
		GS->GetContacts()->MarkThreadSeen(ContactId, CallerId);
	}
}

bool UPhoneClientComponent::HasUnreadFrom(FName ContactId) const
{
	const AWeedShopGameState* GS = GetGS();
	const UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return false; }
	for (const FPhoneMessage& M : Con->GetMessages())
	{
		if (!M.bFromMe && M.FromContactId == ContactId && !M.bSeen) { return true; }
	}
	return false;
}

int32 UPhoneClientComponent::GetUnreadMessageCount() const
{
	const AWeedShopGameState* GS = GetGS();
	const UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return 0; }
	// Competitive: tel alleen JOUW berichten (eigen telefoon); co-op telt alles. Ongelezen = !bSeen (gerepliceerd).
	const APawn* Me = Cast<APawn>(GetOwner());
	const bool bComp = GS->IsCompetitive();
	const FString MyId = (bComp && Me) ? USaveGameSubsystem::StablePlayerId(Me) : FString();
	int32 Unread = 0;
	for (const FPhoneMessage& M : Con->GetMessages())
	{
		if (bComp && !M.ForPlayerId.IsEmpty() && M.ForPlayerId != MyId) { continue; }
		if (!M.bFromMe && !M.bSeen) { ++Unread; }
	}
	return Unread;
}

// Aantal ongelezen berichten van EEN specifiek contact (voor de teller-badge in de berichtenlijst).
int32 UPhoneClientComponent::GetUnreadCountFrom(FName ContactId) const
{
	const AWeedShopGameState* GS = GetGS();
	const UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return 0; }
	const APawn* Me = Cast<APawn>(GetOwner());
	const bool bComp = GS->IsCompetitive();
	const FString MyId = (bComp && Me) ? USaveGameSubsystem::StablePlayerId(Me) : FString();
	int32 Unread = 0;
	for (const FPhoneMessage& M : Con->GetMessages())
	{
		if (M.FromContactId != ContactId) { continue; }
		if (bComp && !M.ForPlayerId.IsEmpty() && M.ForPlayerId != MyId) { continue; }
		if (!M.bFromMe && !M.bSeen) { ++Unread; }
	}
	return Unread;
}

APlayerController* UPhoneClientComponent::GetPC() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(Pawn->GetController());
	}
	return nullptr;
}

void UPhoneClientComponent::SetWaypoint(const FVector& World)
{
	WaypointWorld = World;
	bHasWaypoint = true;
	if (CompassWidget) { CompassWidget->SetWaypoint(World, true); }
}

void UPhoneClientComponent::ClearWaypoint()
{
	bHasWaypoint = false;
	if (CompassWidget) { CompassWidget->SetWaypoint(WaypointWorld, false); }
}

void UPhoneClientComponent::Toast(const FString& Msg, FColor Color, float Time)
{
	APawn* P = Cast<APawn>(GetOwner());
	// Lokaal getoond als dit al de juiste client/host is; anders (server met remote eigenaar) via RPC.
	if (P && P->IsLocallyControlled()) { UWeedToast::Notify(-1, Time, Color, Msg); }
	else if (GetOwnerRole() == ROLE_Authority) { ClientToast(Msg, Color, Time); }
	else { UWeedToast::Notify(-1, Time, Color, Msg); }
}

void UPhoneClientComponent::ClientToast_Implementation(const FString& Msg, FColor Color, float Time)
{
	UWeedToast::Notify(-1, Time, Color, Msg);
}

void UPhoneClientComponent::CloseMapOverlay()
{
	if (!MapOverlay) { return; }
	MapOverlay->RemoveFromParent();
	MapOverlay = nullptr;
	if (APlayerController* PC = GetPC())
	{
		if (!IsAnyGameUIOpen() && !bMainMenuOpen)
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->bShowMouseCursor = false;
		}
	}
}

void UPhoneClientComponent::ToggleMapOverlay()
{
	APlayerController* PC = GetPC();
	// Al open -> sluiten + game-input terug.
	if (MapOverlay) { CloseMapOverlay(); return; }
	if (!PC || !PC->IsLocalController()) { return; }
	// Maar 1 UI tegelijk: sluit alles anders voordat de kaart opent.
	if (IsAnyGameUIOpen()) { CloseAllUI(); }
	if (bOpen) { Toggle(); }
	MapOverlay = CreateWidget<UMapWidget>(PC, UMapWidget::StaticClass());
	if (MapOverlay)
	{
		MapOverlay->SetFullscreen(true);
		MapOverlay->AddToViewport(50);
		// GameAndUI: muis-klikken op de kaart werken (waypoint), maar M (game-input) sluit 'm nog steeds.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
	}
}

UInventoryComponent* UPhoneClientComponent::GetOwnerInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
}

// --- Woningen (3 koopbare panden) ---

ADoorRetrofitter* UPhoneClientComponent::FindRetro() const
{
	// Gecached (weak): de TActorIterator-scan liep bij ELKE caller opnieuw; alleen scannen zolang de
	// cache invalid is (zelfde patroon als UMapWidget::NativeTick). Helpt alle FindRetro-callers.
	if (RetroCache.IsValid()) { return RetroCache.Get(); }
	if (!GetWorld()) { return nullptr; }
	for (TActorIterator<ADoorRetrofitter> It(GetWorld()); It; ++It) { RetroCache = *It; return *It; }
	return nullptr;
}

void UPhoneClientComponent::GetHomesUnified(TArray<FApartmentHome>& Out) const
{
	Out.Reset();
	if (ADoorRetrofitter* Retro = FindRetro()) { Out = Retro->GetBeachHomes(); }
}

void UPhoneClientComponent::GetOffersUnified(TArray<FCityPropertyOffer>& Out) const
{
	Out.Reset();
	if (ADoorRetrofitter* Retro = FindRetro()) { Retro->GetBeachPropertyOffers(Out); }
	// Alle pand-prijzen op hele euro's zodat tonen, kopen en verkopen consistent zijn.
	for (FCityPropertyOffer& O : Out) { if (O.PriceCents > 0) { O.PriceCents = FMath::Max<int64>(100, WeedRoundEuros(O.PriceCents)); } }
}

void UPhoneClientComponent::GetPropertyOffers(TArray<FCityPropertyOffer>& Out) const
{
	GetOffersUnified(Out);
}

void UPhoneClientComponent::ApplyLocalDoors()
{
	// Deuren zijn lokaal/niet-gerepliceerd: ELKE client zet z'n eigen deuren in de juiste staat.
	// (Voorheen alleen de eigen owned-homes -> bij de 2e speler bleven bewoner/te-koop-deuren leeg,
	//  geen "LOCKED - <naam>"-popup, en door speler 1 gekochte woningen bleven op slot.)
	APawn* P = Cast<APawn>(GetOwner());
	if (!P || !P->IsLocallyControlled()) { return; }
	UWorld* W = GetWorld();
	if (!W) { return; }
	// BEACH-MAP (geen City-door-lijst): open ALLE deuren BINNEN een gekochte woning (de hele kamer
	// wordt van jou). Andere deuren laat de DoorRetrofitter op resident/locked staan.
	TArray<FApartmentHome> BHomes; GetHomesUnified(BHomes);
	if (BHomes.Num() == 0) { return; }
	TSet<int32> OwnedAny; // gedeeld co-op: een woning van EENDER WELKE speler telt als "ons"
	for (TActorIterator<APawn> It(W); It; ++It)
	{
		if (const UPhoneClientComponent* Ph = It->FindComponentByClass<UPhoneClientComponent>())
		{ for (int32 Idx : Ph->OwnedHomes) { OwnedAny.Add(Idx); } }
	}
	// Per eigen woning ALLEEN de dichtstbijzijnde voordeur (+ balkonpui) als player-home markeren -
	// niet elke deur binnen de box, want dan pakt 'ie de buur-deur (bv. 701 naast je 703) erbij.
	TSet<ACityDoor*> HomeDoors;
	for (int32 Idx : OwnedAny)
	{
		if (!BHomes.IsValidIndex(Idx)) { continue; }
		const FApartmentHome& H = BHomes[Idx];
		ACityDoor* BestFront = nullptr; float BestFD = 0.f;
		ACityDoor* BestBalc = nullptr; float BestBD = 0.f;
		for (TActorIterator<ACityDoor> It(W); It; ++It)
		{
			ACityDoor* Dr = *It;
			if (!Dr) { continue; }
			const bool bBalc = Dr->ActorHasTag(TEXT("BalcDoor"));
			const bool bApt  = Dr->ActorHasTag(TEXT("AptDoor"));
			if (!bBalc && !bApt) { continue; }
			const FVector DL = Dr->GetActorLocation();
			if (FMath::Abs(DL.X - H.InteriorPos.X) > H.RoomHalf.X + 160.f) { continue; }
			if (FMath::Abs(DL.Y - H.InteriorPos.Y) > H.RoomHalf.Y + 160.f) { continue; }
			if (DL.Z < H.InteriorPos.Z - 220.f || DL.Z > H.InteriorPos.Z + 420.f) { continue; }
			const float D = FVector::DistSquared(DL, H.InteriorPos);
			if (bBalc) { if (!BestBalc || D < BestBD) { BestBD = D; BestBalc = Dr; } }
			else       { if (!BestFront || D < BestFD) { BestFD = D; BestFront = Dr; } }
		}
		if (BestFront) { HomeDoors.Add(BestFront); }
		if (BestBalc)  { HomeDoors.Add(BestBalc); }
	}
	// Markeer je eigen deur(en); deuren die ten onrechte 'your home' staan terugzetten naar bewoner.
	for (TActorIterator<ACityDoor> It(W); It; ++It)
	{
		ACityDoor* Dr = *It;
		if (!Dr) { continue; }
		if (HomeDoors.Contains(Dr)) { Dr->SetPlayerHome(); }
		else if (Dr->IsPlayerHome())
		{
			const FVector L = Dr->GetActorLocation();
			const int32 NameIdx = FMath::Abs(FMath::RoundToInt(L.X * 0.13f) + FMath::RoundToInt(L.Y * 0.31f) + FMath::RoundToInt(L.Z * 0.77f));
			Dr->SetResident(ACityDoor::ResidentNameForDoor(W, NameIdx)); // gender-correcte naam (voorspelde skin)
		}
	}
}

void UPhoneClientComponent::OnRep_Property()
{
	ApplyLocalDoors();
}

void UPhoneClientComponent::SpawnLightSwitches()
{
	APawn* P = Cast<APawn>(GetOwner());
	if (!P || !P->IsLocallyControlled()) { return; }
	UWorld* W = GetWorld();
	if (!W) { return; }
	ADayNightController* DNC = ADayNightController::GetLocal(W);
	if (!DNC) { return; } // plafondlampen nog niet verzameld -> volgende tick opnieuw

	TArray<FApartmentHome> Homes; GetHomesUnified(Homes);
	if (Homes.Num() == 0) { return; }

	// Gedeeld co-op: woningen van eender welke speler tellen als "ons".
	TSet<int32> OwnedAny;
	for (TActorIterator<APawn> It(W); It; ++It)
	{
		if (const UPhoneClientComponent* Ph = It->FindComponentByClass<UPhoneClientComponent>())
		{ for (int32 Idx : Ph->OwnedHomes) { OwnedAny.Add(Idx); } }
	}
	if (OwnedAny.Num() == 0) { return; }

	TArray<FVector> LampPos; DNC->GetCeilingLampPositions(LampPos);

	// Marker-override: F9-speler-markers met 'switch' in het label op deze map.
	const FString MapPath = W->GetOutermost()->GetName();
	TArray<FString> MarkLines;
	FFileHelper::LoadFileToStringArray(MarkLines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));

	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Idx : OwnedAny)
	{
		if (LightSwitchHomesDone.Contains(Idx) || !Homes.IsValidIndex(Idx)) { continue; }
		const FApartmentHome& H = Homes[Idx];

		auto InBounds = [&H](const FVector& L)
		{
			return FMath::Abs(L.X - H.InteriorPos.X) <= H.RoomHalf.X + 120.f
				&& FMath::Abs(L.Y - H.InteriorPos.Y) <= H.RoomHalf.Y + 120.f
				&& L.Z >= H.InteriorPos.Z - H.RoomHalf.Z - 60.f
				&& L.Z <= H.InteriorPos.Z + H.RoomHalf.Z + 120.f;
		};

		// Voordeur van deze woning (dichtstbijzijnde AptDoor binnen de box) - bepaalt hoogte + richting.
		FVector DoorPos = H.DoorPos; ACityDoor* BestFront = nullptr; float BestFD = 0.f;
		for (TActorIterator<ACityDoor> It(W); It; ++It)
		{
			ACityDoor* Dr = *It;
			if (!Dr || !Dr->ActorHasTag(TEXT("AptDoor"))) { continue; }
			const FVector DL = Dr->GetActorLocation();
			if (!InBounds(DL)) { continue; }
			const float D = FVector::DistSquared(DL, H.InteriorPos);
			if (!BestFront || D < BestFD) { BestFD = D; BestFront = Dr; }
		}
		if (BestFront) { DoorPos = BestFront->GetActorLocation(); }
		const float SwitchZ = DoorPos.Z + 110.f; // handhoogte boven de vloer (deur staat op de vloer)

		auto SpawnSwitch = [&](const FVector& Pos, const FString& Role, float Radius)
		{
			FVector Dir = H.InteriorPos - Pos; Dir.Z = 0.f;
			const FRotator Rot = Dir.IsNearlyZero() ? FRotator::ZeroRotator : Dir.Rotation();
			if (APackLightSwitch* Sw = W->SpawnActor<APackLightSwitch>(APackLightSwitch::StaticClass(), Pos, Rot, SP))
			{
				Sw->Setup(FString::Printf(TEXT("apt%d_%s"), Idx, *Role), Radius);
			}
		};

		// 1) Heb je zelf plekken gemarkeerd (F9) binnen deze woning? -> die bepalen ALLES; elke marker wordt
		//    een schakelaar. We filteren NIET op label (F9 logt als 'F9'); de woning-grens is genoeg scoping.
		struct FMarkSpot { FVector P; float Yaw; };
		TArray<FMarkSpot> MarkSpots;
		for (const FString& Line : MarkLines)
		{
			if (!Line.Contains(MapPath)) { continue; }
			const int32 PIdx = Line.Find(TEXT("pos=("));
			if (PIdx == INDEX_NONE) { continue; }
			FString PosStr = Line.Mid(PIdx + 5);
			int32 ClosePar = INDEX_NONE; if (PosStr.FindChar(TEXT(')'), ClosePar)) { PosStr = PosStr.Left(ClosePar); }
			TArray<FString> Parts; PosStr.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() < 3) { continue; }
			const FVector M(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
			if (!InBounds(M)) { continue; }
			float Yaw = 0.f;
			const int32 YIdx = Line.Find(TEXT("yaw="));
			if (YIdx != INDEX_NONE) { Yaw = FCString::Atof(*Line.Mid(YIdx + 4)); }
			MarkSpots.Add({ M, Yaw });
		}
		// F9 plakt nieuwe markers erachter: gebruik alleen de LAATSTE 2 (meest recente) zodat opnieuw
		// markeren de schakelaars VERPLAATST i.p.v. er dubbele bij te zetten.
		while (MarkSpots.Num() > 2) { MarkSpots.RemoveAt(0); }
		if (MarkSpots.Num() > 0)
		{
			for (int32 m = 0; m < MarkSpots.Num(); ++m)
			{
				// F9 logt waar je STOND, niet waar je KEEK. Trace vanaf de marker in je kijkrichting (yaw)
				// tot de muur en plak het plaatje daar plat tegenaan, met de voorkant de kamer in.
				// WorldStatic eerst (muren blokkeren dat betrouwbaar), Visibility als reserve.
				const FVector Start = MarkSpots[m].P;
				const FVector Dir = FRotator(0.f, MarkSpots[m].Yaw, 0.f).Vector();
				FVector Pos = Start;
				FRotator Rot(0.f, MarkSpots[m].Yaw + 180.f, 0.f);
				FHitResult Hit;
				FCollisionQueryParams Q(FName(TEXT("LightSwitchWall")), /*bTraceComplex=*/false, P);
				bool bHit = W->LineTraceSingleByChannel(Hit, Start, Start + Dir * 700.f, ECC_WorldStatic, Q);
				if (!bHit) { bHit = W->LineTraceSingleByChannel(Hit, Start, Start + Dir * 700.f, ECC_Visibility, Q); }
				if (bHit)
				{
					Pos = Hit.Location - Dir * 4.f; // net vóór de muur (geen z-fighting)
					if (!Hit.Normal.IsNearlyZero()) { Rot = Hit.Normal.Rotation(); } // voorkant = uit de muur de kamer in
				}
				if (APackLightSwitch* Sw = W->SpawnActor<APackLightSwitch>(APackLightSwitch::StaticClass(), Pos, Rot, SP))
				{
					Sw->Setup(FString::Printf(TEXT("apt%d_mark%d"), Idx, m), 600.f);
				}
			}
			LightSwitchHomesDone.Add(Idx);
			continue;
		}

		// 2) Auto. Lampen van deze woning verzamelen; nog geen lampen -> volgende tick opnieuw.
		TArray<FVector> Apt;
		for (const FVector& L : LampPos) { if (InBounds(L)) { Apt.Add(L); } }
		if (Apt.Num() == 0) { continue; }

		// Hoofd-schakelaar: net binnen de voordeur, opzij tegen de muur, op handhoogte.
		FVector InDir = H.InteriorPos - DoorPos; InDir.Z = 0.f;
		if (!InDir.Normalize()) { InDir = FVector(1.f, 0.f, 0.f); }
		const FVector RightDir = FVector::CrossProduct(FVector::UpVector, InDir).GetSafeNormal();
		const FVector MainPos = FVector(DoorPos.X, DoorPos.Y, SwitchZ) + InDir * 35.f + RightDir * 55.f;
		SpawnSwitch(MainPos, TEXT("main"), 520.f);

		// Badkamer-schakelaar: bij de lamp die het verst van de deur ligt (beste gok zonder kamer-geometrie).
		if (Apt.Num() >= 2)
		{
			int32 FarI = INDEX_NONE; float FarD = -1.f;
			for (int32 li = 0; li < Apt.Num(); ++li)
			{
				const float D = FVector::DistSquared2D(Apt[li], DoorPos);
				if (D > FarD) { FarD = D; FarI = li; }
			}
			if (FarI != INDEX_NONE)
			{
				FVector BDir = DoorPos - Apt[FarI]; BDir.Z = 0.f;
				if (!BDir.Normalize()) { BDir = -InDir; }
				const FVector BathPos = FVector(Apt[FarI].X, Apt[FarI].Y, SwitchZ) + BDir * 80.f;
				SpawnSwitch(BathPos, TEXT("bath"), 360.f);
			}
		}

		LightSwitchHomesDone.Add(Idx);
	}
}

void UPhoneClientComponent::PropertyTick()
{
	// Server: ken eenmalig het starter-flatje toe bij een VERSE start (load zet de staat zelf). Werkt op
	// de procedurele stad EN de beach-map (offers via GetOffersUnified - City of DoorRetrofitter-registry).
	if (GetOwnerRole() == ROLE_Authority && !bPropertyInit)
	{
		TArray<FCityPropertyOffer> Offers; GetOffersUnified(Offers);
		if (Offers.Num() > 0)
		{
			bPropertyInit = true;
			if (OwnedHomes.Num() == 0 && ActiveHome < 0)
			{
				for (const FCityPropertyOffer& O : Offers)
				{
					if (O.bStarter)
					{
						OwnedHomes.AddUnique(O.HomeIndex);
						ActiveHome = O.HomeIndex;
						// Beach-map: GEEN MoveOwnerToHome - de DoorRetrofitter plaatst de speler al.
						break;
					}
				}
			}
		}
	}
	ApplyLocalDoors();
	// (Lichtschakelaars zijn nu een PLACEABLE item + met F8 in de furniture-template te bewaren,
	//  niet meer automatisch via markers.)

	// Intro-melding (1x, vroeg): zodra je je starter-flat hebt, vertel het huur-doel.
	if (GetOwnerRole() == ROLE_Authority && !bShownRentIntro && OwnedHomes.Num() > 0)
	{
		bShownRentIntro = true;
		const int32 Rent0 = GetRentDueCents();
		if (Rent0 > 0 && GEngine)
		{
			UWeedToast::NotifyPawn(GetOwner(), -1, 8.f, FColor(255, 210, 120),
				FString::Printf(TEXT("Make money for rent! EUR %d due by day %d (in 30 days) - it comes off your bank."), (int32)(WeedRoundEuros((int64)Rent0) / 100), RentDueDay));
		}
	}
}

void UPhoneClientComponent::RestoreProperty(const TArray<int32>& InOwned, int32 InActive)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	OwnedHomes = InOwned;
	ActiveHome = InActive;
	bPropertyInit = true; // de save bepaalt de staat; niet alsnog de starter forceren
	ApplyLocalDoors();
}

void UPhoneClientComponent::MoveOwnerToHome(int32 HomeIndex)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) { return; }
	TArray<FApartmentHome> Homes; GetHomesUnified(Homes);
	if (!Homes.IsValidIndex(HomeIndex)) { return; }
	const FVector Interior = Homes[HomeIndex].InteriorPos;
	// Zet de speler STEVIG op de vloer van de woning: trace omlaag naar het vloerslab en plaats de capsule
	// daar bovenop. Zo val/zweef je niet (wat de anti-stuck eerder liet slingeren tussen huis en park).
	FVector To = Interior + FVector(0.f, 0.f, 60.f);
	if (UWorld* W = GetWorld())
	{
		FHitResult Hit;
		const FVector S = Interior + FVector(0.f, 0.f, 250.f);
		const FVector E = Interior - FVector(0.f, 0.f, 600.f);
		FCollisionQueryParams Q(SCENE_QUERY_STAT(HomeFloor), false, Pawn);
		if (W->LineTraceSingleByChannel(Hit, S, E, ECC_WorldStatic, Q))
		{
			To = Hit.ImpactPoint + FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight() + 8.f);
		}
	}
	// Co-op: geef elke speler een eigen offset binnen de woning, anders landen ze op EXACT dezelfde plek
	// en duwen de capsules elkaar omhoog (zweven). Spreid ze op een klein rooster.
	int32 PlayerIdx = 0;
	if (const APlayerState* PS = Pawn->GetPlayerState())
	{
		if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
		{
			PlayerIdx = FMath::Max(0, GS->PlayerArray.IndexOfByKey(PS));
		}
	}
	if (PlayerIdx > 0)
	{
		To += FVector((float)(PlayerIdx % 2) * 110.f, (float)(PlayerIdx / 2 + (PlayerIdx % 2 == 0 ? 1 : 0)) * 110.f, 0.f);
	}
	Pawn->TeleportTo(To, Pawn->GetActorRotation(), false, true);
	// Laat de eigenaar-client zichzelf ook meteen daar neerzetten (server-teleport van een client-pawn
	// komt anders pas aan na een beweging-update -> client bleef in het park tot 'ie sprong).
	ClientLandAtHome(To);
}

void UPhoneClientComponent::ClientLandAtHome_Implementation(FVector To)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) { return; }
	Pawn->TeleportTo(To, Pawn->GetActorRotation(), false, true);
	if (ACharacter* C = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* Move = C->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->bForceNextFloorCheck = true;
		}
	}
}

void UPhoneClientComponent::ServerBuyProperty_Implementation(int32 HomeIndex)
{
	TArray<FCityPropertyOffer> Offers; GetOffersUnified(Offers);
	const FCityPropertyOffer* Off = Offers.FindByPredicate([HomeIndex](const FCityPropertyOffer& O) { return O.HomeIndex == HomeIndex; });
	if (!Off) { return; }                       // alleen de aangeboden panden
	if (OwnedHomes.Contains(HomeIndex)) { return; }
	if (Off->PriceCents > 0)
	{
		UEconomyComponent* Econ = GetOwnerEconomy();
		if (!Econ || !Econ->RemoveBank(Off->PriceCents))
		{
			if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Orange, TEXT("Not enough bank balance for this property.")); }
			return;
		}
	}
	for (int32 Idx : Off->Homes) { OwnedHomes.AddUnique(Idx); } // 1 woning, of 3 bij het rijtjesblok
	ActiveHome = Off->HomeIndex;
	MoveOwnerToHome(Off->HomeIndex);
	ApplyLocalDoors();
	if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 4.f, FColor::Green, FString::Printf(TEXT("Property bought: %s"), *Off->Title)); }
}

void UPhoneClientComponent::ServerSetActiveHome_Implementation(int32 HomeIndex)
{
	if (!OwnedHomes.Contains(HomeIndex)) { return; }
	ActiveHome = HomeIndex;
	MoveOwnerToHome(HomeIndex);
	ApplyLocalDoors();
}

void UPhoneClientComponent::SetActiveHomeFromLocation(const FVector& WorldLoc)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	TArray<FApartmentHome> Homes; GetHomesUnified(Homes);
	if (Homes.Num() == 0) { return; }
	// Zoek een GEKOCHTE woning waarvan de kamer-bounds deze locatie bevatten (bv. het bed staat erin).
	for (int32 Idx : OwnedHomes)
	{
		if (!Homes.IsValidIndex(Idx)) { continue; }
		const FApartmentHome& H = Homes[Idx];
		const FVector D = WorldLoc - H.InteriorPos;
		if (FMath::Abs(D.X) <= H.RoomHalf.X + 120.f && FMath::Abs(D.Y) <= H.RoomHalf.Y + 120.f)
		{
			if (ActiveHome != Idx)
			{
				ActiveHome = Idx; // GEEN teleport: je bent hier al. Alleen je woon-/spawn-plek verschuift.
				ApplyLocalDoors();
				if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Green, TEXT("You now live here.")); }
			}
			return;
		}
	}
}

int32 UPhoneClientComponent::GetHomeSellValueCents(int32 HomeIndex) const
{
	TArray<FCityPropertyOffer> Offers; GetOffersUnified(Offers);
	const FCityPropertyOffer* Off = Offers.FindByPredicate(
		[HomeIndex](const FCityPropertyOffer& O) { return O.HomeIndex == HomeIndex || O.Homes.Contains(HomeIndex); });
	if (!Off || Off->PriceCents <= 0) { return 0; }  // starter (gratis) is niet verkoopbaar
	return (int32)FMath::Max<int64>(100, WeedRoundEuros((int64)Off->PriceCents * 65 / 100)); // 65% terug
}

void UPhoneClientComponent::ServerSellProperty_Implementation(int32 HomeIndex)
{
	if (!OwnedHomes.Contains(HomeIndex)) { return; }
	TArray<FCityPropertyOffer> Offers; GetOffersUnified(Offers);
	const FCityPropertyOffer* Off = Offers.FindByPredicate(
		[HomeIndex](const FCityPropertyOffer& O) { return O.HomeIndex == HomeIndex || O.Homes.Contains(HomeIndex); });
	if (!Off) { return; }
	if (Off->PriceCents <= 0)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("You can't sell your starter home.")); }
		return;
	}

	const int64 Refund = WeedRoundEuros((int64)Off->PriceCents * 65 / 100);
	if (UEconomyComponent* Econ = GetOwnerEconomy()) { Econ->AddBank(Refund, false); }

	for (int32 Idx : Off->Homes) { OwnedHomes.Remove(Idx); }                       // hele aanbieding (1 of 3 panden)
	if (Off->Homes.Contains(ActiveHome) || ActiveHome == HomeIndex)
	{
		ActiveHome = OwnedHomes.Num() > 0 ? OwnedHomes[0] : -1;
		if (ActiveHome >= 0) { MoveOwnerToHome(ActiveHome); }
	}
	if (SelectedDeliveryHome == HomeIndex || Off->Homes.Contains(SelectedDeliveryHome)) { SelectedDeliveryHome = -1; }
	ApplyLocalDoors();
	if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Green, FString::Printf(TEXT("Property sold: +EUR %lld"), (long long)(Refund / 100))); }
}

bool UPhoneClientComponent::GetActiveHomeLocation(FVector& OutWorld) const
{
	if (ActiveHome < 0) { return false; }
	TArray<FApartmentHome> Homes; GetHomesUnified(Homes);
	if (!Homes.IsValidIndex(ActiveHome)) { return false; }
	OutWorld = Homes[ActiveHome].DoorPos; // plek vóór je voordeur (waar de home-marker heen wijst)
	return true;
}

int32 UPhoneClientComponent::GetHomePlayerIsInside() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) { return -1; }
	TArray<FApartmentHome> Homes; GetHomesUnified(Homes);
	if (Homes.Num() == 0) { return -1; }
	const FVector P = Pawn->GetActorLocation();
	for (int32 Idx : OwnedHomes)
	{
		if (!Homes.IsValidIndex(Idx)) { continue; }
		const FApartmentHome& H = Homes[Idx];
		const FVector C = H.InteriorPos;
		const FVector R = H.RoomHalf;
		if (FMath::Abs(P.X - C.X) <= R.X + 40.f
			&& FMath::Abs(P.Y - C.Y) <= R.Y + 40.f
			&& P.Z >= C.Z - 150.f && P.Z <= C.Z + FMath::Max(220.f, R.Z) + 150.f)
		{
			return Idx;
		}
	}
	return -1;
}

FString UPhoneClientComponent::GetHomeLabel(int32 HomeIndex) const
{
	TArray<FApartmentHome> Homes; GetHomesUnified(Homes);
	if (Homes.IsValidIndex(HomeIndex) && !Homes[HomeIndex].Number.IsEmpty())
	{
		return FString::Printf(TEXT("Nr %s"), *Homes[HomeIndex].Number);
	}
	return FString::Printf(TEXT("Home %d"), HomeIndex);
}

FString UPhoneClientComponent::GetHomeInfoLine(int32 HomeIndex) const
{
	TArray<FApartmentHome> Homes; GetHomesUnified(Homes);
	if (!Homes.IsValidIndex(HomeIndex)) { return FString(); }
	const FApartmentHome& H = Homes[HomeIndex];
	FString Line = H.bApartment ? TEXT("Appartement") : TEXT("Rijtjeshuis");
	if (!H.Number.IsEmpty()) { Line += FString::Printf(TEXT("  -  nr %s"), *H.Number); }
	if (H.bApartment && H.Floor > 0) { Line += FString::Printf(TEXT("  -  verd. %d"), H.Floor); }
	return Line;
}

int32 UPhoneClientComponent::ResolveDeliveryHome() const
{
	// Handmatige keuze wint; anders het huis waar je NU binnen bent; anders de actieve woning.
	if (SelectedDeliveryHome >= 0 && OwnedHomes.Contains(SelectedDeliveryHome)) { return SelectedDeliveryHome; }
	const int32 Inside = GetHomePlayerIsInside();
	if (Inside >= 0) { return Inside; }
	return ActiveHome;
}

UEconomyComponent* UPhoneClientComponent::GetOwnerEconomy() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UEconomyComponent>() : nullptr;
}

void UPhoneClientComponent::RefreshInputMode()
{
	UpdateCursor();
}

void UPhoneClientComponent::UpdateCursor()
{
	const bool bAnyUI = bOpen || bDevMenuOpen || bRollOpen || bDealOpen || bInventoryOpen || bPotUpgradeOpen || bMergeOpen || bAtmOpen || bPackOpen || bShelfOpen || bDryRackOpen || bStoreOpen || bPauseOpen || bMainMenuOpen || bSettingsOpen || bWardrobeOpen || bLightDimmerOpen;

	// Texting-anim: ALLEEN als de echte telefoon open is (bOpen = het toestel zelf, home of een app
	// erop). De andere flags (deal/inventory/atm/pack/shelf/dryrack/store/pot/merge/roll) zijn
	// WERELD-UIs - daar sta je bij een kassa/rek/tafel en hoort er geen telefoon-in-de-hand-pose bij.
	// Dit gerepliceerde signaal stuurt de texting-anim op alle proxies.
	const bool bPhoneVisual = bOpen;
	// Dedup tegen bPhoneOpenRep (op de host meteen gezet via de Server-RPC, op een client via replicatie) -
	// zo is geen extra member nodig en blijft dit een .cpp-only wijziging die Live Coding kan hot-patchen.
	if (bPhoneVisual != bPhoneOpenRep && GetOwner() && GetOwner()->GetLocalRole() != ROLE_SimulatedProxy)
	{
		ServerSetPhoneOpen(bPhoneVisual);
	}

	// Maar 1 UI tegelijk: opent er een ander scherm, dan gaat de fullscreen-kaart dicht.
	if (bAnyUI && MapOverlay)
	{
		MapOverlay->RemoveFromParent();
		MapOverlay = nullptr;
	}
	if (APlayerController* PC = GetPC())
	{
		PC->SetShowMouseCursor(bAnyUI);
		PC->bEnableClickEvents = bAnyUI;
		PC->bEnableMouseOverEvents = bAnyUI;
		if (bAnyUI)
		{
			// GameAndUI = standaard klik-/hover-routing van de HUD hit-boxes (dat werkte goed),
			// cursor blijft zichtbaar bij indrukken (HideCursorDuringCapture(false)). Muis NIET in het
			// venster opsluiten (DoNotLock): je kunt 'm vrij naar buiten/naar een ander scherm bewegen
			// en gewoon weer terugklikken op de game.
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);

			// Capture-mode op standaard (CaptureDuringMouseDown) laten staan: NoCapture brak
			// het klikken (2-3x nodig) en de hover-events. Alleen cursor-verbergen uitzetten.
			if (UGameViewportClient* VP = PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr)
			{
				VP->SetHideCursorDuringCapture(false);
			}
		}
		else
		{
			// Gameplay: muis ECHT vastzetten + verbergen, en PERMANENT capturen zodat je rond kunt
			// kijken zonder een knop in te houden. (De UI-modus zette HideCursorDuringCapture=false en
			// liet de capture op "only during mouse click"; dat moeten we hier terugzetten.)
			if (UGameViewportClient* VP = PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr)
			{
				VP->SetHideCursorDuringCapture(true);
				VP->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
			}
			FInputModeGameOnly Mode;
			Mode.SetConsumeCaptureMouseDown(true);
			PC->SetInputMode(Mode);
			PC->SetShowMouseCursor(false);
		}
	}
}

void UPhoneClientComponent::EnsureWidget()
{
	if (PhoneWidget) { return; }
	APlayerController* PC = GetPC();
	if (!PC || !PC->IsLocalController()) { return; }

	PhoneWidget = CreateWidget<UPhoneWidget>(PC, UPhoneWidget::StaticClass());
	if (PhoneWidget)
	{
		PhoneWidget->SetPhone(this);
		PhoneWidget->AddToViewport(20);
	}
	// Dev-menu (F10): één sidebar met alle dev-tools. Boven de telefoon (20), onder pauze (40).
	DevMenuWidget = CreateWidget<UDevMenuWidget>(PC, UDevMenuWidget::StaticClass());
	if (DevMenuWidget)
	{
		DevMenuWidget->SetPhone(this);
		DevMenuWidget->AddToViewport(21);
	}
	// IN-GAME COVER-SCHERM (de "2e" loadingscreen): een vol-scherm widget die NA de map-load over beeld
	// blijft tot je echt stil in de kamer staat (vloer onder je gevonden) en dan wegfade't. Ziet er
	// identiek uit aan het oude movie-scherm. Alleen tonen als we via een laad-transitie binnenkomen
	// (New Game / Load / Join zetten de gedeelde laad-timer); buiten een transitie is 'ie niet nodig.
	// NIET bij de BOOT (eerste map -> hoofdmenu): die zet ook de laad-timer (voor het boot-movie-scherm)
	// maar heeft geen wereld-opbouw - een cover zou daar het menu afdekken tot de harde cap.
	if (WeedShop_LoadElapsedSeconds() > 0.0 && !WeedShop_IsBootLoading())
	{
		if (UBootCoverWidget* Cover = CreateWidget<UBootCoverWidget>(PC, UBootCoverWidget::StaticClass()))
		{
			Cover->AddToViewport(100); // bovenop ALLE HUD (pauze/menu zitten op 40-44) tijdens het laden
			WeedShop_SetCoverUp(true);  // de movie mag nu overdragen (cover staat op beeld) -> geen laad-gat / witte flash
			UE_LOG(LogTemp, Verbose, TEXT("[COVER] created+viewport @E=%.1f"), WeedShop_LoadElapsedSeconds());
		}
	}
	// Status-HUD (altijd zichtbaar) + deal-scherm via dezelfde, bewezen route.
	StatusWidget = CreateWidget<UStatusHudWidget>(PC, UStatusHudWidget::StaticClass());
	if (StatusWidget) { StatusWidget->AddToViewport(0); }
	DealWidget = CreateWidget<UDealWidget>(PC, UDealWidget::StaticClass());
	if (DealWidget) { DealWidget->SetPhone(this); DealWidget->AddToViewport(30); }
	PlantWidget = CreateWidget<UPlantInfoWidget>(PC, UPlantInfoWidget::StaticClass());
	if (PlantWidget) { PlantWidget->AddToViewport(10); }
	HotbarWidget = CreateWidget<UHotbarWidget>(PC, UHotbarWidget::StaticClass());
	if (HotbarWidget) { HotbarWidget->AddToViewport(26); } // boven de inventory-blur/dim (25) -> hotbar blijft scherp + sleepbaar in inv-view
	CrosshairWidget = CreateWidget<UCrosshairWidget>(PC, UCrosshairWidget::StaticClass());
	if (CrosshairWidget) { CrosshairWidget->AddToViewport(1); }
	HandInfoWidget = CreateWidget<UHandInfoWidget>(PC, UHandInfoWidget::StaticClass());
	if (HandInfoWidget) { HandInfoWidget->AddToViewport(6); }
	InventoryWidget = CreateWidget<UInventoryWidget>(PC, UInventoryWidget::StaticClass());
	if (InventoryWidget) { InventoryWidget->SetPhone(this); InventoryWidget->AddToViewport(25); }
	RollWidget = CreateWidget<URollWidget>(PC, URollWidget::StaticClass());
	if (RollWidget) { RollWidget->SetPhone(this); RollWidget->AddToViewport(26); }
	CompassWidget = CreateWidget<UCompassWidget>(PC, UCompassWidget::StaticClass());
	if (CompassWidget) { CompassWidget->AddToViewport(3); }
	HotkeyWidget = CreateWidget<UHotkeyHintWidget>(PC, UHotkeyHintWidget::StaticClass());
	if (HotkeyWidget) { HotkeyWidget->AddToViewport(2); }
	AtmWidget = CreateWidget<UAtmWidget>(PC, UAtmWidget::StaticClass());
	if (AtmWidget) { AtmWidget->SetPhone(this); AtmWidget->AddToViewport(28); }
	WardrobeWidget = CreateWidget<UWardrobeWidget>(PC, UWardrobeWidget::StaticClass());
	if (WardrobeWidget) { WardrobeWidget->SetPhone(this); WardrobeWidget->AddToViewport(34); }
	SpotInfoWidget = CreateWidget<USpotInfoWidget>(PC, USpotInfoWidget::StaticClass());
	if (SpotInfoWidget) { SpotInfoWidget->AddToViewport(8); SpotInfoWidget->SetInfoVisibleSilent(false); } // default UIT (clean HUD; toggle-baar)
	PackWidget = CreateWidget<UPackWidget>(PC, UPackWidget::StaticClass());
	if (PackWidget) { PackWidget->SetPhone(this); PackWidget->AddToViewport(29); }
	ShelfWidget = CreateWidget<UShelfWidget>(PC, UShelfWidget::StaticClass());
	if (ShelfWidget) { ShelfWidget->SetPhone(this); ShelfWidget->AddToViewport(31); }
	DryRackWidget = CreateWidget<UDryingRackWidget>(PC, UDryingRackWidget::StaticClass());
	if (DryRackWidget) { DryRackWidget->SetPhone(this); DryRackWidget->AddToViewport(32); }
	StoreWidget = CreateWidget<UStoreWidget>(PC, UStoreWidget::StaticClass());
	if (StoreWidget) { StoreWidget->SetPhone(this); StoreWidget->AddToViewport(33); }
	{
		// Pauzemenu: gebruik de kit-gestylde WBP-subclass als die bestaat, anders de C++-fallback.
		UClass* PauseCls = LoadClass<UPauseMenuWidget>(nullptr, TEXT("/Game/UI/Screens/WBP_PauseMenu.WBP_PauseMenu_C"));
		UAssetKeepAliveSubsystem::Keep(this, PauseCls); // WBP-klasse rooten over map-loads (laadtijd-fix; null-veilig)
		if (!PauseCls) { PauseCls = UPauseMenuWidget::StaticClass(); }
		PauseWidget = CreateWidget<UPauseMenuWidget>(PC, PauseCls);
	}
	if (PauseWidget) { PauseWidget->SetPhone(this); PauseWidget->AddToViewport(40); }
	MainMenuWidget = CreateWidget<UMainMenuWidget>(PC, UMainMenuWidget::StaticClass());
	if (MainMenuWidget) { MainMenuWidget->SetPhone(this); MainMenuWidget->AddToViewport(42); }
	SettingsWidget = CreateWidget<USettingsWidget>(PC, USettingsWidget::StaticClass());
	if (SettingsWidget) { SettingsWidget->SetPhone(this); SettingsWidget->AddToViewport(44); }
	LightDimmerWidget = CreateWidget<ULightDimmerWidget>(PC, ULightDimmerWidget::StaticClass());
	if (LightDimmerWidget) { LightDimmerWidget->SetPhone(this); LightDimmerWidget->AddToViewport(27); }
	// Save-indicator bovenop alles (ook over pauze/menu heen) zodat je 'm altijd ziet.
	SaveIndicatorWidget = CreateWidget<USaveIndicatorWidget>(PC, USaveIndicatorWidget::StaticClass());
	if (SaveIndicatorWidget) { SaveIndicatorWidget->AddToViewport(50); }
	// Centrale toast-meldingen (vervangt de debug-tekst linksboven).
	LevelUpWidget = CreateWidget<ULevelUpWidget>(PC, ULevelUpWidget::StaticClass());
	if (LevelUpWidget) { LevelUpWidget->AddToViewport(46); }

	ToastWidget = CreateWidget<UWeedToast>(PC, UWeedToast::StaticClass());
	if (ToastWidget) { ToastWidget->AddToViewport(48); }
}

void UPhoneClientComponent::Toggle()
{
	EnsureWidget();
	bOpen = !bOpen;
	bBankViaAtm = false; // ATM-bank-bypass eindigt zodra je de telefoon zelf opent/sluit
	if (bOpen)
	{
		bRollOpen = false; // niet allebei tegelijk
		bDealOpen = false;
		bInventoryOpen = false;
		bPotUpgradeOpen = false;
		bAtmOpen = false; bPackOpen = false; bShelfOpen = false; bDryRackOpen = false; bStoreOpen = false;
		bDevMenuOpen = false; // dev-menu en telefoon niet tegelijk
		bHomeScreen = true; // open altijd op het home-scherm met de apps
	}
	// De texting-flag (telefoon-zichtbaar voor proxies) wordt nu gecentraliseerd in UpdateCursor gezet,
	// zodat de pose ook aanblijft in de telefoon-apps (inventory/deal/atm/...), niet alleen op home.
	UpdateCursor();
}

void UPhoneClientComponent::ToggleDevMenu()
{
	const AWeedShopGameState* GS = GetGS();
	if (!GS) { return; }
	// Chord-gate: Ctrl+Shift+F10 zet de dev-tools sessie-breed aan/uit (werkt ook in Shipping, waar
	// console/Exec niet bestaan). Gewone F10 doet zonder dev-tools NIKS (stil - clean playthrough).
	const APlayerController* PC = GetPC();
	const bool bChord = PC && PC->IsInputKeyDown(EKeys::LeftControl) && PC->IsInputKeyDown(EKeys::LeftShift);
	if (!GS->AreDevToolsEnabled())
	{
		if (!bChord) { return; } // stil in een normaal spel
		ServerSetDevTools(true);
		Toast(TEXT("Dev tools enabled"), FColor::Cyan, 3.f);
		EnsureWidget();
		bDevMenuOpen = true;
		bOpen = false; // telefoon en dev-menu niet tegelijk
		if (DevMenuWidget) { DevMenuWidget->MarkDirty(); }
		UpdateCursor();
		return;
	}
	if (bChord)
	{
		// Chord terwijl AAN -> dev-tools UIT + menu dicht.
		ServerSetDevTools(false);
		Toast(TEXT("Dev tools disabled"), FColor::Orange, 3.f);
		bDevMenuOpen = false;
		UpdateCursor();
		return;
	}
	// Dev-tools AAN + gewone F10 -> menu-toggle zoals voorheen.
	EnsureWidget();
	bDevMenuOpen = !bDevMenuOpen;
	if (bDevMenuOpen)
	{
		bOpen = false; // telefoon en dev-menu niet tegelijk
		if (DevMenuWidget) { DevMenuWidget->MarkDirty(); } // dynamische lijsten vers bij openen
	}
	UpdateCursor();
}

void UPhoneClientComponent::OpenApp(int32 AppIndex)
{
	Tab = FMath::Clamp(AppIndex, 0, AppCount - 1);
	bHomeScreen = false;
}

void UPhoneClientComponent::GoHome()
{
	bHomeScreen = true;
}

void UPhoneClientComponent::OpenAtm()
{
	// Een fysieke ATM opent z'n EIGEN fullscreen geldautomaat-scherm (UAtmWidget), NIET de telefoon-
	// Bank-app. UAtmWidget toont zichzelf zodra IsAtmOpen() true is; UpdateCursor() pakt de UI-input +
	// cursor (bAtmOpen zit in de bAnyUI-check). Andere schermen sluiten (wederzijds uitsluitend).
	EnsureWidget();
	bAtmOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false; bPotUpgradeOpen = false; bPackOpen = false; bShelfOpen = false; bDryRackOpen = false; bWardrobeOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::CloseAtm()
{
	bAtmOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::ToggleSpotInfo()
{
	// Dev-tool (F9 spot-markers): alleen met dev-tools AAN. In een normaal spel doet dit niks -> clean playthrough.
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (!GS || !GS->AreDevToolsEnabled()) { return; }
	EnsureWidget();
	if (SpotInfoWidget) { SpotInfoWidget->ToggleInfo(); }
}

bool UPhoneClientComponent::IsSpotInfoVisible() const
{
	return SpotInfoWidget && SpotInfoWidget->IsInfoVisible();
}

void UPhoneClientComponent::OpenWardrobe()
{
	EnsureWidget();
	bWardrobeOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false; bPotUpgradeOpen = false; bAtmOpen = false; bPackOpen = false; bShelfOpen = false; bDryRackOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::CloseWardrobe()
{
	bWardrobeOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::OpenPack(int32 Batch)
{
	EnsureWidget();
	PackBatchUI = FMath::Max(1, Batch);
	bPackOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false; bPotUpgradeOpen = false; bAtmOpen = false; bShelfOpen = false; bDryRackOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::ClosePack()
{
	bPackOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::TogglePause()
{
	if (bPauseOpen) { ClosePause(); }
	else { OpenPause(); }
}

void UPhoneClientComponent::OpenPause()
{
	EnsureWidget();
	bPauseOpen = true;
	// Sluit alle andere schermen zodat het pauze-menu schoon bovenop ligt.
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false;
	bPotUpgradeOpen = false; bAtmOpen = false; bPackOpen = false; bShelfOpen = false; bDryRackOpen = false;
	// In standalone (single-player) pauzeren we de wereld echt; in co-op blijft de wereld lopen.
	if (APlayerController* PC = GetPC())
	{
		if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone)
		{
			PC->SetPause(true);
		}
	}
	UpdateCursor();
}

void UPhoneClientComponent::ClosePause()
{
	bPauseOpen = false;
	if (APlayerController* PC = GetPC())
	{
		PC->SetPause(false);
	}
	UpdateCursor();
}

bool UPhoneClientComponent::IsAnyGameUIOpen() const
{
	return bOpen || bDevMenuOpen || bRollOpen || bDealOpen || bInventoryOpen || bPotUpgradeOpen || bMergeOpen
		|| bAtmOpen || bPackOpen || bShelfOpen || bDryRackOpen || bStoreOpen || bPauseOpen || bSettingsOpen || bWardrobeOpen;
}

void UPhoneClientComponent::CloseAllUI()
{
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false;
	bPotUpgradeOpen = false; bMergeOpen = false; bAtmOpen = false; bPackOpen = false;
	bShelfOpen = false; bDryRackOpen = false; bPauseOpen = false; bSettingsOpen = false; bWardrobeOpen = false;
	bDevMenuOpen = false;
	if (APlayerController* PC = GetPC())
	{
		if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) { PC->SetPause(false); }
	}
	UpdateCursor();
}

void UPhoneClientComponent::ShowMainMenu()
{
	// BOOT-LAADSCHERM stoppen: de movie die de eerste map-load (naar dit hoofdmenu) afdekte mag weg nu
	// het menu op beeld komt. No-op als er geen movie speelt (reset ook de boot-vlag, zie WeedShopCore).
	WeedShop_StopGameLoadingScreen();
	// Dev: met -AutoSoak slaan we het titelscherm over en tickt de wereld direct (geen pauze, geen save
	// aangeraakt) - voor headless NPC-soak-tests (bv. CENTERDIAG-runs zonder dat iemand hoeft te klikken).
	if (FParse::Param(FCommandLine::Get(), TEXT("AutoSoak")))
	{
		bMainMenuOpen = false;
		if (APlayerController* PC = GetPC()) { PC->SetPause(false); }
		UpdateCursor();
		return;
	}
	EnsureWidget();
	bMainMenuOpen = true;
	// Sluit alles anders zodat het titelscherm schoon bovenop ligt.
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false;
	bPotUpgradeOpen = false; bAtmOpen = false; bPackOpen = false; bShelfOpen = false; bDryRackOpen = false; bPauseOpen = false;
	// LIVE-achtergrond: is er een menu-camera voor deze map, zet de view daarop en NIET pauzeren (anders
	// bevriest de wereld = geen bewegende bomen). Geen cam -> oude gedrag (pauze + statische view).
	const bool bLiveCam = ApplyMenuCam();
	if (APlayerController* PC = GetPC())
	{
		if (!bLiveCam && GetWorld() && GetWorld()->GetNetMode() == NM_Standalone) { PC->SetPause(true); }
	}
	UpdateCursor();
}

bool UPhoneClientComponent::ApplyMenuCam()
{
	APlayerController* PC = GetPC();
	UWorld* W = GetWorld();
	if (!PC || !W) { return false; }
	FString Txt;
	if (!FFileHelper::LoadFileToString(Txt, *(FPaths::ProjectSavedDir() / TEXT("MenuCam.txt")))) { return false; }
	TArray<FString> F;
	Txt.TrimStartAndEnd().ParseIntoArray(F, TEXT("|"));
	if (F.Num() < 7) { return false; }
	if (F[0].TrimStartAndEnd() != W->GetOutermost()->GetName()) { return false; } // cam hoort bij een andere map
	const FVector Loc(FCString::Atof(*F[1]), FCString::Atof(*F[2]), FCString::Atof(*F[3]));
	const FRotator Rot(FCString::Atof(*F[4]), FCString::Atof(*F[5]), FCString::Atof(*F[6]));
	if (!MenuCamActor.IsValid())
	{
		FActorSpawnParameters SP; SP.ObjectFlags |= RF_Transient;
		MenuCamActor = W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(Rot, Loc), SP);
	}
	AActor* Cam = MenuCamActor.Get();
	if (!Cam) { return false; }
	Cam->SetActorLocationAndRotation(Loc, Rot);
	PC->SetViewTargetWithBlend(Cam, 0.f);
	PC->SetIgnoreMoveInput(true);   // speler kan niet bewegen tijdens het menu (wereld tikt wel door)
	PC->SetIgnoreLookInput(true);
	return true;
}

void UPhoneClientComponent::ClearMenuCam()
{
	APlayerController* PC = GetPC();
	if (PC)
	{
		if (APawn* P = PC->GetPawn()) { PC->SetViewTargetWithBlend(P, 0.25f); }
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
	}
	if (MenuCamActor.IsValid()) { MenuCamActor->Destroy(); }
	MenuCamActor = nullptr;
}

void UPhoneClientComponent::HideMainMenu()
{
	bMainMenuOpen = false;
	ClearMenuCam(); // view terug naar de speler + input weer vrij
	if (APlayerController* PC = GetPC()) { PC->SetPause(false); }
	UpdateCursor();
}

void UPhoneClientComponent::OpenMainMenuLoad()
{
	EnsureWidget();
	if (MainMenuWidget) { MainMenuWidget->RequestPicker(2); } // open meteen de Load-keuze
	ShowMainMenu();
}

void UPhoneClientComponent::OpenToApp(int32 AppIndex)
{
	EnsureWidget();
	ClosePause();
	bOpen = true;
	bHomeScreen = false;
	Tab = FMath::Clamp(AppIndex, 0, AppCount - 1);
	bRollOpen = false; bDealOpen = false; bInventoryOpen = false; bPotUpgradeOpen = false;
	bAtmOpen = false; bPackOpen = false; bShelfOpen = false; bDryRackOpen = false;
	bBankViaAtm = false; // standaard geen ATM-bypass (OpenAtm zet 'm daarna weer aan)
	UpdateCursor();
}

void UPhoneClientComponent::OpenShelf(AStorageShelf* Shelf)
{
	if (!Shelf) { return; }
	EnsureWidget();
	ShelfActor = Shelf;
	bShelfOpen = true;
	// Open OOK je echte inventory ernaast (net als de droogrek): zo sleep je rechtstreeks tussen je
	// inventory/hotbar en het schap, met overal hetzelfde inventory-systeem.
	bInventoryOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bPotUpgradeOpen = false; bAtmOpen = false; bPackOpen = false; bDryRackOpen = false; bStoreOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::CloseShelf()
{
	bShelfOpen = false;
	bInventoryOpen = false; // sluit de echte inventory die met het schap mee openging
	ShelfActor = nullptr;
	UpdateCursor();
}

void UPhoneClientComponent::OpenDryRack(ADryingRack* Rack)
{
	if (!Rack) { return; }
	EnsureWidget();
	DryRackActor = Rack;
	bDryRackOpen = true;
	// Open OOK je echte inventory eronder: zo hang je rechtstreeks vanuit je inventory/hotbar op.
	bInventoryOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bPotUpgradeOpen = false; bAtmOpen = false; bPackOpen = false; bShelfOpen = false; bStoreOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::CloseDryRack()
{
	bDryRackOpen = false;
	bInventoryOpen = false; // sluit de echte inventory die met het rek mee openging
	DryRackActor = nullptr;
	UpdateCursor();
}

ADryingRack* UPhoneClientComponent::GetDryRack() const
{
	return DryRackActor.Get();
}

void UPhoneClientComponent::RequestDryHang(int32 StackId) { ServerDryHang(DryRackActor.Get(), StackId); }
void UPhoneClientComponent::RequestDryCollect(int32 Index) { ServerDryCollect(DryRackActor.Get(), Index); }
void UPhoneClientComponent::RequestDryCollectAll() { ServerDryCollectAll(DryRackActor.Get()); }

void UPhoneClientComponent::ServerDryHang_Implementation(ADryingRack* Rack, int32 StackId)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Rack || !Inv || StackId == 0) { return; }
	if (GetOwner() && FVector::Dist(GetOwner()->GetActorLocation(), Rack->GetActorLocation()) > 400.f) { return; }

	// Hang precies DEZE stapel op met z'n eigen THC%/kwaliteit — niet alle wiet van die strain samen.
	const int32 Idx = Inv->FindStackById(StackId);
	const TArray<FInventoryStack>& St = Inv->GetStacks();
	if (!St.IsValidIndex(Idx)) { return; }
	const FName WetId = St[Idx].ItemId;
	if (!WetId.ToString().StartsWith(TEXT("WetBud_"))) { return; }
	const int32 Have = St[Idx].Quantity;
	const float Thc = St[Idx].Quality;
	const float Qual = St[Idx].QualityPct;
	if (Have <= 0) { return; }
	const int32 Hung = Rack->ServerHangWet(WetId, Have, Thc, Qual);
	if (Hung > 0) { Inv->RemoveFromStackById(StackId, Hung); }
	else if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("Drying rack is full.")); }
}

void UPhoneClientComponent::ServerDryCollect_Implementation(ADryingRack* Rack, int32 Index)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Rack || !Inv) { return; }
	if (GetOwner() && FVector::Dist(GetOwner()->GetActorLocation(), Rack->GetActorLocation()) > 400.f) { return; }

	FName OutId; int32 OutQty = 0; float OutThc = 0.f; float OutQual = 0.f;
	if (Rack->ServerCollectIndex(Index, OutId, OutQty, OutThc, OutQual))
	{
		Inv->AddItem(OutId, OutQty, OutThc, OutQual);
	}
}

void UPhoneClientComponent::ServerDryCollectAll_Implementation(ADryingRack* Rack)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Rack || !Inv) { return; }
	if (GetOwner() && FVector::Dist(GetOwner()->GetActorLocation(), Rack->GetActorLocation()) > 400.f) { return; }

	// Van achter naar voren zodat indices niet verschuiven.
	for (int32 i = Rack->GetEntries().Num() - 1; i >= 0; --i)
	{
		FName OutId; int32 OutQty = 0; float OutThc = 0.f; float OutQual = 0.f;
		if (Rack->ServerCollectIndex(i, OutId, OutQty, OutThc, OutQual))
		{
			Inv->AddItem(OutId, OutQty, OutThc, OutQual);
		}
	}
}

AStorageShelf* UPhoneClientComponent::GetShelf() const
{
	return ShelfActor.Get();
}

void UPhoneClientComponent::RequestShelfStore(FName ItemId, int32 Count)
{
	ServerShelfStore(ShelfActor.Get(), ItemId, Count);
}

void UPhoneClientComponent::RequestShelfTake(int32 SlotIndex, int32 Count)
{
	// De VERWACHTE item-id op dit slot meesturen (client-kant) zodat de server een verschoven index weigert.
	const FName ExpId = ShelfActor.IsValid() ? ShelfActor->GetSlotId(SlotIndex) : NAME_None;
	ServerShelfTake(ShelfActor.Get(), SlotIndex, Count, ExpId);
}

void UPhoneClientComponent::RequestShelfCook(int32 SlotIndex)
{
	const FName ExpId = ShelfActor.IsValid() ? ShelfActor->GetSlotId(SlotIndex) : NAME_None;
	ServerShelfCook(ShelfActor.Get(), SlotIndex, ExpId);
}

void UPhoneClientComponent::ServerShelfCook_Implementation(AStorageShelf* Shelf, int32 SlotIndex, FName ExpectedId)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Shelf || !Inv || !Shelf->IsFridge()) { return; }
	if (GetOwner() && FVector::Dist(GetOwner()->GetActorLocation(), Shelf->GetActorLocation()) > 400.f) { return; }
	if (!Shelf->Contents.IsValidIndex(SlotIndex)) { return; }
	// CO-OP anti-race: verschoven index (ander speler nam iets weg) -> weiger i.p.v. het verkeerde slot koken.
	if (!ExpectedId.IsNone() && Shelf->Contents[SlotIndex].ItemId != ExpectedId) { return; }

	const FShelfStack& S = Shelf->Contents[SlotIndex];
	const FString Id = S.ItemId.ToString();
	if (!Id.StartsWith(TEXT("ButterMix_")))
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("Only butter mix sets into edibles.")); }
		return;
	}
	if (Shelf->Cooking.Num() >= Shelf->FridgeCookCap())
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("Fridge is busy (max 4 batches setting).")); }
		return;
	}
	const FString Strain = Id.RightChop(FString(TEXT("ButterMix_")).Len());

	// Cookies/gummies als de speler de bak-ingredienten bij zich heeft (zoals de oude conversion-fridge deed).
	// Sugar is voor beide nodig; + flour -> cookies, + gelatin -> gummies (cookies krijgen voorrang).
	FString OutPre;
	bool bUseFlour = false, bUseGelatin = false, bUseSugar = false;
	const bool bHasSugar   = Inv->HasItem(FName(TEXT("Sugar")), 1);
	const bool bHasFlour   = Inv->HasItem(FName(TEXT("Flour")), 1);
	const bool bHasGelatin = Inv->HasItem(FName(TEXT("Gelatin")), 1);
	if (bHasSugar && bHasFlour)        { OutPre = TEXT("Cookie_"); bUseFlour = true; bUseSugar = true; }
	else if (bHasSugar && bHasGelatin) { OutPre = TEXT("Gummy_");  bUseGelatin = true; bUseSugar = true; }

	// Haal de hele ButterMix-stapel uit de koelkast en start de batch met diens THC/kwaliteit.
	FName Oid; float Ot = 0.f, Oq = 0.f;
	const int32 Taken = Shelf->ServerTake(SlotIndex, S.Quantity, ExpectedId, Oid, Ot, Oq);
	if (Taken <= 0) { return; }
	if (!Shelf->ServerStartEdible(Strain, Taken, Ot, Oq, OutPre))
	{
		Shelf->ServerStore(Oid, Taken, Ot, Oq); // mislukt -> ButterMix terug op het schap
		return;
	}
	if (bUseFlour)   { Inv->RemoveItem(FName(TEXT("Flour")), 1); }
	if (bUseGelatin) { Inv->RemoveItem(FName(TEXT("Gelatin")), 1); }
	if (bUseSugar)   { Inv->RemoveItem(FName(TEXT("Sugar")), 1); }
	if (GEngine)
	{
		const TCHAR* What = OutPre == TEXT("Cookie_") ? TEXT("Baking cookies") : (OutPre == TEXT("Gummy_") ? TEXT("Setting gummies") : TEXT("Setting edibles"));
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor(120, 200, 255), FString::Printf(TEXT("%s (%dg, ~3 min)..."), What, Taken));
	}
}

void UPhoneClientComponent::ServerShelfStore_Implementation(AStorageShelf* Shelf, FName ItemId, int32 Count)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Shelf || !Inv || ItemId.IsNone() || Count <= 0) { return; }
	// Afstand-check (anti-cheat/lag).
	if (GetOwner() && FVector::Dist(GetOwner()->GetActorLocation(), Shelf->GetActorLocation()) > 400.f) { return; }

	// Cash opslaan = ECHT geld van je saldo naar het schap/de safe/de koelkast (co-op: zo deel of bewaar je
	// briefgeld). De inventory-cash is een SPIEGEL van het saldo (SetCashDisplayEuros): dus NIET via
	// Inv->RemoveItem (de reconcile zou de stapel herscheppen = gratis geld), maar via Economy->RemoveMoney —
	// de spiegel-stapel volgt dan vanzelf. Quantity op het schap = hele euro's.
	if (ItemId == FName(TEXT("Cash")))
	{
		UEconomyComponent* Ec = GetOwner() ? GetOwner()->FindComponentByClass<UEconomyComponent>() : nullptr;
		if (!Ec) { return; }
		const int32 HaveEur = (int32)FMath::Clamp<int64>(Ec->GetBalanceCents() / 100, 0, (int64)MAX_int32);
		const int32 WantEur = FMath::Min(Count, HaveEur);
		if (WantEur <= 0) { return; }
		const int32 StoredEur = Shelf->ServerStore(ItemId, WantEur, 0.f, 0.f);
		if (StoredEur > 0) { Ec->RemoveMoney((int64)StoredEur * 100); }
		else if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("Shelf is full.")); }
		return;
	}

	const int32 Have = Inv->GetQuantity(ItemId);
	const int32 Want = FMath::Min(Count, Have);
	if (Want <= 0) { return; }
	const float Thc = Inv->GetItemQuality(ItemId);
	const float Qual = Inv->GetItemQualityPct(ItemId);
	const int32 Stored = Shelf->ServerStore(ItemId, Want, Thc, Qual);
	if (Stored > 0) { Inv->RemoveItem(ItemId, Stored); }
	else if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("Shelf is full.")); }
}

void UPhoneClientComponent::ServerShelfTake_Implementation(AStorageShelf* Shelf, int32 SlotIndex, int32 Count, FName ExpectedId)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Shelf || !Inv || Count <= 0) { return; }
	if (GetOwner() && FVector::Dist(GetOwner()->GetActorLocation(), Shelf->GetActorLocation()) > 400.f) { return; }

	FName OutId; float OutThc = 0.f; float OutQual = 0.f;
	const int32 Taken = Shelf->ServerTake(SlotIndex, Count, ExpectedId, OutId, OutThc, OutQual);
	if (Taken <= 0) { return; }
	if (OutId == FName(TEXT("Cash")))
	{
		// Cash terugpakken = terug op je saldo (Quantity = hele euro's); de inventory-spiegel volgt vanzelf.
		// GEEN Inv->AddItem: dat zou een tweede cash-stapel naast de spiegel zetten (dubbel geld tot reconcile).
		if (UEconomyComponent* Ec = GetOwner() ? GetOwner()->FindComponentByClass<UEconomyComponent>() : nullptr)
		{
			Ec->AddMoney((int64)Taken * 100);
		}
		return;
	}
	if (!Inv->AddItem(OutId, Taken, OutThc, OutQual))
	{
		// Geen ruimte in de inventory -> terug op het schap.
		Shelf->ServerStore(OutId, Taken, OutThc, OutQual);
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("No room in your inventory.")); }
	}
}

void UPhoneClientComponent::ClaimGoal(int32 Idx) { ServerClaimGoal(Idx); }

void UPhoneClientComponent::ServerClaimGoal_Implementation(int32 Idx)
{
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UGoalsComponent* Goals = GS ? GS->GetGoals() : nullptr;
	if (!Goals || !Goals->MarkClaimed(Idx)) { return; }     // niet klaar of al geclaimd
	const FGoalDef& Gd = UGoalsComponent::Goals()[Idx];

	// Geld-reward (cash) + item-reward naar deze speler. Hele euro's.
	const int64 RewardMoney = Gd.RewardMoneyCents > 0 ? FMath::Max<int64>(100, WeedRoundEuros((int64)Gd.RewardMoneyCents)) : 0;
	if (RewardMoney > 0)
	{
		if (UEconomyComponent* Ec = GetOwner() ? GetOwner()->FindComponentByClass<UEconomyComponent>() : nullptr)
		{
			Ec->AddMoney(RewardMoney);
		}
	}
	if (!Gd.RewardItem.IsNone() && Gd.RewardQty > 0)
	{
		if (UInventoryComponent* Inv = GetOwnerInventory()) { Inv->AddItem(Gd.RewardItem, Gd.RewardQty); }
	}

	// Nette popup (goud) bij het behalen.
	if (GEngine)
	{
		FString Reward;
		if (RewardMoney > 0) { Reward += FString::Printf(TEXT("EUR %lld"), (long long)(RewardMoney / 100)); }
		if (!Gd.RewardItem.IsNone() && Gd.RewardQty > 0) { Reward += FString::Printf(TEXT("%s%dx %s"), RewardMoney > 0 ? TEXT(" + ") : TEXT(""), Gd.RewardQty, *WeedUI::PrettyItemName(Gd.RewardItem)); }
		UWeedToast::NotifyPawn(GetOwner(), -1, 4.5f, FColor(255, 210, 70), FString::Printf(TEXT("GOAL COMPLETE: %s  ->  %s"), *Gd.Title, *Reward));
	}
}

int32 UPhoneClientComponent::ContainerCapacity(FName ContainerId)
{
	// MAX gram per container (de gram-slider gaat van 1 tot dit). Realistischer: jars tot ~50g.
	const FString S = ContainerId.ToString();
	if (S == TEXT("Cont_Bag2"))     { return 2; }
	if (S == TEXT("Cont_Bag5"))     { return 5; }
	if (S == TEXT("Cont_Jar10"))    { return 25; }  // small jar
	if (S == TEXT("Cont_Jar15"))    { return 50; }  // grote pot
	if (S == TEXT("Cont_Block100")) { return 100; }
	if (S == TEXT("Cont_Garbage500")) { return 500; }
	return 0;
}

void UPhoneClientComponent::RequestPack(FName BudId, FName ContainerId)
{
	ServerPack(BudId, ContainerId, PackBatchUI);
}

void UPhoneClientComponent::RequestPackGrams(FName BudId, FName ContainerId, int32 Grams)
{
	ServerPackGrams(BudId, ContainerId, Grams);
}

void UPhoneClientComponent::ServerPackGrams_Implementation(FName BudId, FName ContainerId, int32 Grams)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv) { return; }
	const FString BudStr = BudId.ToString();
	if (!BudStr.StartsWith(TEXT("Bud_"))) { return; }            // alleen GEDROOGDE buds verpakken
	const int32 Cap = ContainerCapacity(ContainerId);
	if (Cap <= 0 || !Inv->HasItem(ContainerId, 1)) { return; }

	// Verpak uit ÉÉN specifieke stapel van deze strain (de grootste), met z'n EIGEN THC%/kwaliteit, zodat
	// het zakje exact de wiet bevat die je gebruikt (geen mix van verschillende kwaliteiten).
	int32 BestSid = 0, BestQty = 0; float Thc = 0.f, Q = 0.f;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (S.ItemId == BudId && S.Quantity > BestQty) { BestSid = S.StackId; BestQty = S.Quantity; Thc = S.Quality; Q = S.QualityPct; }
	}
	if (BestSid == 0 || BestQty <= 0) { return; }
	// Klamp de gevraagde grammen op de container-capaciteit én de beschikbare wiet in DIE stapel.
	const int32 PackGrams = FMath::Clamp(Grams, 1, FMath::Min(Cap, BestQty));
	if (PackGrams <= 0) { return; }

	const FName Strain(*BudStr.RightChop(4));                            // Bud_X -> X
	const FName BagId = UInventoryComponent::MakeBagId(Strain, ContainerId, PackGrams); // -> Bag_X_<gram>

	Inv->RemoveFromStackById(BestSid, PackGrams);
	Inv->RemoveItem(ContainerId, 1);
	Inv->AddItem(BagId, 1, Thc, Q); // één zakje van PackGrams gram met de THC/kwaliteit van die stapel

	if (GEngine)
	{
		UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor(120, 220, 160),
			FString::Printf(TEXT("Packed a %dg bag of %s."), PackGrams, *Strain.ToString()));
	}
}

void UPhoneClientComponent::RequestUnpack(FName BagId, int32 Count) { ServerUnpack(BagId, Count); }

void UPhoneClientComponent::ServerUnpack_Implementation(FName BagId, int32 Count)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv) { return; }
	const FString BagStr = BagId.ToString();
	if (!BagStr.StartsWith(TEXT("Bag_"))) { return; } // alleen verpakte zakjes uitpakken
	const int32 Owned = Inv->GetQuantity(BagId);      // aantal zakjes in de stapel
	if (Owned <= 0) { return; }
	const int32 N = FMath::Clamp(Count, 1, Owned);    // ALLEEN het gevraagde aantal (max wat je hebt)

	const FName Strain = UInventoryComponent::BagStrain(BagId);
	const int32 PerBag = FMath::Max(1, UInventoryComponent::BagGrams(BagId));
	const int32 TotalGrams = N * PerBag;
	const FName BudId(*FString::Printf(TEXT("Bud_%s"), *Strain.ToString())); // Bag_X_g -> Bud_X
	const float Thc = Inv->GetItemQuality(BagId);
	const float Q = Inv->GetItemQualityPct(BagId);

	if (!Inv->RemoveItem(BagId, N)) { return; } // alleen N zakjes openen
	Inv->AddItem(BudId, TotalGrams, Thc, Q);     // wiet weer los terug

	// Lege container(s) teruggeven: 1 per uitgepakt zakje, zodat je ze kunt hergebruiken. De bag bewaart z'n
	// container-type niet (id = Bag_<strain>_<gram>), dus we geven de KLEINSTE container die deze gram kon bevatten.
	FName ContId = UInventoryComponent::BagContainer(BagId);   // exacte container waarin verpakt
	if (ContId.IsNone())                                       // oude 2-token bag -> kleinste-passende
	{
		if      (PerBag <= 2)   { ContId = FName(TEXT("Cont_Bag2")); }
		else if (PerBag <= 5)   { ContId = FName(TEXT("Cont_Bag5")); }
		else if (PerBag <= 25)  { ContId = FName(TEXT("Cont_Jar10")); }
		else if (PerBag <= 50)  { ContId = FName(TEXT("Cont_Jar15")); }
		else if (PerBag <= 100) { ContId = FName(TEXT("Cont_Block100")); }
		else                    { ContId = FName(TEXT("Cont_Garbage500")); }
	}
	Inv->AddItem(ContId, N);                     // N lege containers terug

	if (GEngine)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor(120, 220, 160),
			FString::Printf(TEXT("Unpacked %d bag%s -> %dg %s + %d empty container%s back."), N, N == 1 ? TEXT("") : TEXT("s"), TotalGrams, *Strain.ToString(), N, N == 1 ? TEXT("") : TEXT("s")));
	}
}

void UPhoneClientComponent::ServerPack_Implementation(FName BudId, FName ContainerId, int32 Batch)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv) { return; }
	const FString BudStr = BudId.ToString();
	if (!BudStr.StartsWith(TEXT("Bud_"))) { return; }            // alleen GEDROOGDE buds verpakken
	const int32 Cap = ContainerCapacity(ContainerId);
	if (Cap <= 0) { return; }

	const FName Strain(*BudStr.RightChop(4)); // Bud_X -> X
	const float Thc = Inv->GetItemQuality(BudId);
	const float Q = Inv->GetItemQualityPct(BudId);

	// Verwerk tot Batch zakjes (tier van de tafel) zolang je containers + wiet hebt.
	int32 BagsMade = 0, TotalGrams = 0;
	const int32 N = FMath::Max(1, Batch);
	for (int32 b = 0; b < N; ++b)
	{
		if (!Inv->HasItem(ContainerId, 1)) { break; }
		const int32 Have = Inv->GetQuantity(BudId);
		if (Have <= 0) { break; }
		const int32 Grams = FMath::Min(Cap, Have);
		if (!Inv->RemoveItem(BudId, Grams)) { break; }
		Inv->RemoveItem(ContainerId, 1);
		Inv->AddItem(UInventoryComponent::MakeBagId(Strain, ContainerId, Grams), 1, Thc, Q); // één gemaatd zakje
		++BagsMade; TotalGrams += Grams;
	}
	if (GEngine && BagsMade > 0)
	{
		UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor(120, 220, 160),
			FString::Printf(TEXT("Packed %d bag(s) (%dg total)."), BagsMade, TotalGrams));
	}
}

void UPhoneClientComponent::ToggleRollUI()
{
	// Maar 1 UI tegelijk: niet bovenop een ander scherm openen (sluit eerst het huidige).
	if (!bRollOpen && (IsAnyGameUIOpen() || bMainMenuOpen)) { return; }
	bRollOpen = !bRollOpen;
	if (bRollOpen)
	{
		RollGrams = FMath::Clamp(RollGrams, MinGrams, GetMaxJointGrams());
	}
	UpdateCursor();
}

void UPhoneClientComponent::ToggleInventory()
{
	// Een open wereld-paneel (rek/winkel/schap/verpak/ATM) sluit met deze toets het HELE paneel
	// (sommige openden de inventory mee). Zo sluit je 't droogrek ook gewoon met E.
	if (bDryRackOpen) { CloseDryRack(); return; }
	if (bStoreOpen)   { CloseStore();   return; }
	if (bShelfOpen)   { CloseShelf();   return; }
	if (bPackOpen)    { ClosePack();    return; }
	if (bAtmOpen)     { CloseAtm();     return; }
	// Maar 1 UI tegelijk: niet bovenop een ander scherm openen (sluit eerst het huidige).
	if (!bInventoryOpen && (IsAnyGameUIOpen() || bMainMenuOpen)) { return; }
	bInventoryOpen = !bInventoryOpen;
	UpdateCursor();
}

void UPhoneClientComponent::OpenMerge(FName ItemId)
{
	const UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv || Inv->CountStacksOf(ItemId) < 2)
	{
		return; // niets te mergen
	}
	MergeItemId = ItemId;
	bMergeOpen = true;
	UpdateCursor();
}

void UPhoneClientComponent::CloseMerge()
{
	bMergeOpen = false;
	MergeItemId = NAME_None;
	UpdateCursor();
}

void UPhoneClientComponent::ConfirmMerge()
{
	if (!MergeItemId.IsNone())
	{
		ServerMergeItem(MergeItemId);
	}
	bMergeOpen = false;
	MergeItemId = NAME_None;
	UpdateCursor();
}

void UPhoneClientComponent::ServerMergeItem_Implementation(FName ItemId)
{
	if (UInventoryComponent* Inv = GetOwnerInventory())
	{
		Inv->MergeItem(ItemId);
	}
}

void UPhoneClientComponent::SetRollGrams(int32 Grams)
{
	RollGrams = FMath::Clamp(Grams, MinGrams, GetMaxJointGrams());
}

void UPhoneClientComponent::ConfirmRoll()
{
	// Geen bruikbare wiet -> niet rollen (backstop voor de RMB-hold die via dit pad gaat).
	FName WeedId; float Thc = 0.f, Q = 0.f;
	if (!GetRollWeed(RollGrams, WeedId, Thc, Q))
	{
		SetRollLoadedUI(false, 0); // lading wissen zodat de RMB-roll stopt
		if (GEngine)
		{
			UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange,
				FString::Printf(TEXT("No weed to roll (%d g needed)."), RollGrams));
		}
		bRollOpen = false;
		UpdateCursor();
		return;
	}
	// GetRollWeed heeft RollStrain net op de gebruikte stapel gezet (of gelaten); geef 'm mee aan de server.
	ServerRollJoint(RollGrams, RollStrain);
	bRollOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::LoadRoll()
{
	// Geen bruikbare wiet -> niet laden, geef een duidelijke melding (geen lege joint kunnen rollen).
	FName WeedId; float Thc = 0.f, Q = 0.f;
	if (!GetRollWeed(RollGrams, WeedId, Thc, Q))
	{
		SetRollLoadedUI(false, 0);
		if (GEngine)
		{
			UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange,
				FString::Printf(TEXT("No weed to roll (%d g needed) - grow/buy buds first."), RollGrams));
		}
		return;
	}
	SetRollLoadedUI(true, RollGrams);
	// Onthoud welke wiet geladen is (voor de hand-preview links-onder).
	RollLoadDesc = FString::Printf(TEXT("%dg %s - %.0f%% THC, %.0f%% quality"),
		RollGrams, *WeedUI::PrettyItemName(WeedId), Thc, Q);
	bRollOpen = false; // menu sluit; rollen door rechtermuis in te houden
	UpdateCursor();
}

namespace
{
	// Paper-tiers oplopend in capaciteit (gram per joint).
	struct FPaperDef { const TCHAR* Id; int32 Capacity; };
	static const FPaperDef GPapers[] = {
		{ TEXT("Papers_Small"),     2 },
		{ TEXT("Papers_Big"),       5 },
		{ TEXT("Papers_Blunt"),     7 },
		{ TEXT("Papers_Backwoods"), 10 },
	};
}

int32 UPhoneClientComponent::PaperCapacity(FName PaperId)
{
	// Capaciteit van een specifiek papers-item (voor de quick-view); 0 = geen papers.
	for (const FPaperDef& P : GPapers)
	{
		if (PaperId == FName(P.Id)) { return P.Capacity; }
	}
	return 0;
}

int32 UPhoneClientComponent::GetMaxJointGrams() const
{
	// Hoogste capaciteit van de papers die je hebt; geen papers = niet kunnen rollen.
	int32 Max = 0;
	if (UInventoryComponent* Inv = GetOwnerInventory())
	{
		for (const FPaperDef& P : GPapers)
		{
			if (Inv->HasItem(FName(P.Id), 1))
			{
				Max = FMath::Max(Max, P.Capacity);
			}
		}
	}
	return Max;
}

float UPhoneClientComponent::JointIntensity(int32 Grams, float ThcPercent, float QualityPct)
{
	// Joint-sterkte = KWALITEIT (niet THC; THC% verandert toch niet per joint). Het aantal gram dat je
	// erin doet schaalt de EFFECTIEVE kwaliteit: 1g van 70%-wiet voelt zwakker dan een volle joint van
	// dezelfde wiet. Zo levert een dun/zwak jointje minder op en zijn niet-verslaafde of doorgewinterde
	// rokers er minder snel van onder de indruk. ~3g = volle kwaliteit; een dikke joint kan richting 100%.
	(void)ThcPercent;
	const float Q = FMath::Clamp(QualityPct / 100.f, 0.f, 1.f);
	const float GramsFactor = FMath::Max(0, Grams) / 3.f; // 1g=0.33, 2g=0.67, 3g=1.0, meer=boven
	return FMath::Clamp(Q * GramsFactor, 0.f, 1.f);
}

bool UPhoneClientComponent::GetRollWeed(int32 Grams, FName& OutItemId, float& OutThcPercent, float& OutQualityPct) const
{
	OutItemId = NAME_None; OutThcPercent = 0.f; OutQualityPct = 0.f;
	const UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv) { return false; }
	// 1) Probeer eerst de door de speler gekozen strain (RollStrain) als die genoeg gram op voorraad heeft.
	if (!RollStrain.IsNone())
	{
		for (const FInventoryStack& St : Inv->GetStacks())
		{
			if (St.ItemId == RollStrain && St.Quantity >= Grams)
			{
				OutItemId = St.ItemId;
				OutThcPercent = St.Quality;
				OutQualityPct = St.QualityPct;
				return true;
			}
		}
	}
	// 2) Fallback: de eerste bruikbare Bud_-stapel. Zet RollStrain gelijk aan die match zodat de UI klopt.
	for (const FInventoryStack& St : Inv->GetStacks())
	{
		if (St.ItemId.ToString().StartsWith(TEXT("Bud_")) && St.Quantity >= Grams)
		{
			OutItemId = St.ItemId;
			OutThcPercent = St.Quality;
			OutQualityPct = St.QualityPct;
			const_cast<UPhoneClientComponent*>(this)->RollStrain = St.ItemId;
			return true;
		}
	}
	return false;
}

bool UPhoneClientComponent::GetRollWeedInfo(int32 Grams, float& OutThcPercent, float& OutQualityPct) const
{
	FName Id;
	return GetRollWeed(Grams, Id, OutThcPercent, OutQualityPct);
}

void UPhoneClientComponent::ServerRollJoint_Implementation(int32 Grams, FName Strain)
{
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv)
	{
		return;
	}

	const int32 MaxG = GetMaxJointGrams();
	if (MaxG <= 0)
	{
		if (GEngine)
		{
			UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange, TEXT("No papers — buy some from the supplier (phone)."));
		}
		return;
	}
	Grams = FMath::Clamp(Grams, MinGrams, MaxG);

	// Kies de kleinste vloei die past (spaart je dure papers).
	FName Paper = NAME_None;
	for (const FPaperDef& P : GPapers)
	{
		if (P.Capacity >= Grams && Inv->HasItem(FName(P.Id), 1))
		{
			Paper = FName(P.Id);
			break;
		}
	}
	if (Paper.IsNone())
	{
		return;
	}

	// Zoek een bud-stapel met genoeg gram. Probeer eerst de gekozen strain; val anders rock-solid terug
	// op de eerste bruikbare Bud_-stapel (Strain None of niet-in-voorraad -> rollen mag nooit breken).
	FName BudItem = NAME_None;
	if (!Strain.IsNone())
	{
		for (const FInventoryStack& Stack : Inv->GetStacks())
		{
			if (Stack.ItemId == Strain && Stack.Quantity >= Grams)
			{
				BudItem = Stack.ItemId;
				break;
			}
		}
	}
	if (BudItem.IsNone())
	{
		for (const FInventoryStack& Stack : Inv->GetStacks())
		{
			if (Stack.ItemId.ToString().StartsWith(TEXT("Bud_")) && Stack.Quantity >= Grams)
			{
				BudItem = Stack.ItemId;
				break;
			}
		}
	}
	if (BudItem.IsNone())
	{
		if (GEngine)
		{
			UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Orange,
				FString::Printf(TEXT("Not enough weed (%d g needed)."), Grams));
		}
		return;
	}

	// THC% + Kwaliteit% van de gebruikte wiet -> komt mee in de joint.
	const float BudThc = Inv->GetItemQuality(BudItem);
	const float BudQ = Inv->GetItemQualityPct(BudItem);

	Inv->RemoveItem(BudItem, Grams);
	Inv->RemoveItem(Paper, 1);

	// Joint-id onthoudt nu ook de STRAIN: Joint_<Strain>_<G>g. Strain = bud-id zonder de "Bud_"-prefix.
	// THC% + Kwaliteit% bewaren we op de stapel (zoals voorheen).
	FString StrainStr = BudItem.ToString();
	StrainStr.RemoveFromStart(TEXT("Bud_"));
	const FName JointId = UInventoryComponent::MakeJointId(FName(*StrainStr), Grams);
	Inv->AddItem(JointId, 1, BudThc, BudQ);
	// Goal-teller: joints gerold.
	if (AWeedShopGameState* GSg = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (UGoalsComponent* Gl = GSg->GetGoals()) { Gl->NoteJointsRolled(1); }
	}
	if (GEngine)
	{
		const FString StrainName = BudItem.ToString().StartsWith(TEXT("Bud_"))
			? BudItem.ToString().RightChop(4) : BudItem.ToString();
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Green,
			FString::Printf(TEXT("Joint rolled: %dg weed (%s) + 1 paper"), Grams, *StrainName));
	}
}

// --- Deal ---

void UPhoneClientComponent::ServerSetCustomerTalking_Implementation(ACustomerBase* Customer, bool bTalking)
{
	if (!Customer) { return; }
	APawn* MyPawn = Cast<APawn>(GetOwner());
	// EXCLUSIVITEIT: niet overnemen als een ANDERE speler al met deze klant bezig is.
	if (bTalking && Customer->IsBusyWithOther(MyPawn)) { return; }
	Customer->SetTalkingToPlayer(bTalking, MyPawn);
}

void UPhoneClientComponent::OpenDeal(ACustomerBase* Customer)
{
	if (!Customer)
	{
		return;
	}
	// CO-OP: maar 1 speler tegelijk met een NPC. Is een ANDERE speler al bezig -> niet openen.
	if (Customer->IsBusyWithOther(Cast<APawn>(GetOwner())))
	{
		UWeedToast::Notify(-1, 2.5f, FColor::Orange, TEXT("Someone else is talking to this person."));
		return;
	}
	// Net geweigerd? Korte her-aanbied-cooldown: nog niet opnieuw openen (anti-spam, los van aankoop-cooldown).
	if (const AWeedShopGameState* GSrc = GetGS())
	{
		if (GSrc->GetNpcRegistry() && Customer && !Customer->NpcId.IsNone()
			&& GSrc->GetNpcRegistry()->IsOnRefusalCooldown(Customer->NpcId))
		{
			UWeedToast::Notify(-1, 2.5f, FColor::Orange, TEXT("They just turned you down - give it a minute."));
			return;
		}
	}
	// Praat-venster opent voor IEDEREEN (naam, stats, dialoog, joint geven). De deal-sectie zelf
	// (prijs/aanbod) tonen we in de UI alleen als 'ie ook echt wil kopen.
	DealCustomer = Customer;
	ServerSetCustomerTalking(Customer, true); // NPC stopt met lopen zolang je 'm aanspreekt
	bDealOpen = true;
	bOpen = false;
	bRollOpen = false;
	bInventoryOpen = false;
	bPotUpgradeOpen = false;
	DealAltProduct = NAME_None; // begin met het gevraagde product
	// Start op de marktprijs als vraagprijs (eerlijk bod).
	DealAskCents = Customer->GetMarketPriceCents();
	SetDealAskCents(DealAskCents);
	UpdateCursor();
}

FName UPhoneClientComponent::GetOfferedProduct() const
{
	if (!DealAltProduct.IsNone()) { return DealAltProduct; }
	const ACustomerBase* C = DealCustomer.Get();
	return C ? C->DesiredProductId : NAME_None;
}

bool UPhoneClientComponent::IsOfferingSubstitute() const
{
	const ACustomerBase* C = DealCustomer.Get();
	return C && !DealAltProduct.IsNone() && DealAltProduct != C->DesiredProductId;
}

int32 UPhoneClientComponent::GetOfferMarketCents() const
{
	const ACustomerBase* C = DealCustomer.Get();
	return C ? C->GetMarketPriceForProduct(GetOfferedProduct()) : 0;
}

void UPhoneClientComponent::SetOfferedProduct(FName ProductId)
{
	const ACustomerBase* C = DealCustomer.Get();
	// None of het gevraagde product -> terug naar "normaal".
	DealAltProduct = (C && ProductId == C->DesiredProductId) ? NAME_None : ProductId;
	// Reset de vraagprijs naar de markt van het nu aangeboden product (eerlijk bod).
	DealAskCents = GetOfferMarketCents();
	SetDealAskCents(DealAskCents);
}

void UPhoneClientComponent::SetDealAskCents(int32 Cents)
{
	const int32 Market = GetOfferMarketCents();
	if (Market <= 0)
	{
		DealAskCents = FMath::Max(0, (int32)WeedRoundEuros((int64)Cents));
		return;
	}
	// Band: 40%..200% van de markt van het aangeboden product. Hele euro's.
	const int32 Lo = (int32)FMath::Max<int64>(100, WeedRoundEuros((int64)FMath::RoundToInt(Market * 0.40f)));
	const int32 Hi = (int32)FMath::Max<int64>(100, WeedRoundEuros((int64)FMath::RoundToInt(Market * 2.00f)));
	DealAskCents = FMath::Clamp((int32)WeedRoundEuros((int64)Cents), Lo, Hi);
}

void UPhoneClientComponent::MarkUiClickConsumed()
{
	if (const UWorld* W = GetWorld()) { LastUiClickTime = W->GetTimeSeconds(); }
}

bool UPhoneClientComponent::DidUiConsumeClickRecently() const
{
	const UWorld* W = GetWorld();
	return W && (W->GetTimeSeconds() - LastUiClickTime) < 0.25;
}

void UPhoneClientComponent::CloseDeal()
{
	if (ACustomerBase* C = DealCustomer.Get()) { ServerSetCustomerTalking(C, false); } // NPC mag weer lopen
	bDealOpen = false;
	DealCustomer = nullptr;
	UpdateCursor();
}

void UPhoneClientComponent::ConfirmDeal()
{
	if (ACustomerBase* C = DealCustomer.Get())
	{
		ServerSubmitOffer(C, GetOfferedProduct(), DealAskCents);
		ServerSetCustomerTalking(C, false); // klaar -> NPC mag weer lopen/vertrekken
	}
	bDealOpen = false;
	DealCustomer = nullptr;
	DealAltProduct = NAME_None;
	UpdateCursor();
}

void UPhoneClientComponent::RequestGiveJoint(ACustomerBase* Customer)
{
	if (!Customer) { return; }
	// Routeer naar de speler-pawn (game-module implementeert de sample-flow via de interface).
	if (IPlayerNpcActions* Actions = Cast<IPlayerNpcActions>(GetOwner()))
	{
		Actions->GiveJointToCustomer(Customer);
	}
}

void UPhoneClientComponent::RequestGiveJointId(ACustomerBase* Customer, FName JointId)
{
	if (!Customer) { return; }
	// Routeer de gekozen joint naar de speler-pawn (game-module via de interface).
	if (IPlayerNpcActions* Actions = Cast<IPlayerNpcActions>(GetOwner()))
	{
		Actions->GiveJointToCustomerId(Customer, JointId);
	}
}

void UPhoneClientComponent::ServerSubmitOffer_Implementation(ACustomerBase* Customer, FName ProductId, int32 AskCents)
{
	if (!Customer)
	{
		return;
	}
	// EXCLUSIVITEIT (server-auth): is een ANDERE speler al met deze klant bezig -> weigeren (geen dubbele verkoop).
	if (Customer->IsBusyWithOther(Cast<APawn>(GetOwner())))
	{
		return;
	}
	UEconomyComponent* Econ = GetOwnerEconomy();
	UInventoryComponent* Stock = GetOwnerInventory();

	AskCents = AskCents > 0 ? (int32)FMath::Max<int64>(100, WeedRoundEuros((int64)AskCents)) : (int32)WeedRoundEuros((int64)AskCents);
	if (const AWeedShopGameState* GSs = GetGS())
	{
		if (GSs->GetNpcRegistry() && Customer && !Customer->NpcId.IsNone()
			&& GSs->GetNpcRegistry()->IsOnRefusalCooldown(Customer->NpcId))
		{
			return; // net geweigerd: bod negeren tot de cooldown om is
		}
	}
	const EDealResult Result = Customer->SubmitOfferProduct(ProductId, AskCents, Econ, Stock);

	// NPC reageert in het praat-venster.
	switch (Result)
	{
	case EDealResult::Accepted: Customer->Say(TEXT("Pleasure doing business. Catch you later!")); break;
	case EDealResult::Haggle:   Customer->Say(TEXT("Whoa, too pricey. Cut me a better deal?")); break;
	case EDealResult::NoStock:  Customer->Say(TEXT("You don't even have it? Come on...")); break;
	default:                    Customer->Say(TEXT("Nah, not for that. Forget it.")); break;
	}

	if (GEngine)
	{
		FColor Col = FColor::White;
		FString Msg;
		switch (Result)
		{
		case EDealResult::Accepted:
			Col = FColor::Green;
			Msg = FString::Printf(TEXT("Deal! Sold for EUR %d"), (int32)(WeedRoundEuros((int64)AskCents * Customer->DesiredQuantity) / 100));
			break;
		case EDealResult::Haggle:
			Col = FColor::Yellow;  Msg = TEXT("Too expensive — they want to haggle."); break;
		case EDealResult::NoStock:
			Col = FColor::Orange; Msg = TEXT("You don't have the stock for this order."); break;
		default:
			Col = FColor::Red;    Msg = TEXT("Customer refused the offer."); break;
		}
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, Col, Msg);
	}
}

void UPhoneClientComponent::ServerBuySupply_Implementation(FName SupplyId)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetStore())
		{
			GS->GetStore()->BuySupply(SupplyId, GetOwnerInventory());
		}
	}
}

void UPhoneClientComponent::OpenStore(AStoreCounter* Counter)
{
	if (!Counter || !Counter->HasShop()) { return; }
	StoreCounterRef = Counter;
	bStoreOpen = true;
	// Sluit andere UI's.
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false;
	bPotUpgradeOpen = false; bAtmOpen = false; bPackOpen = false; bShelfOpen = false; bDryRackOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::CloseStore()
{
	bStoreOpen = false;
	StoreCounterRef = nullptr;
	UpdateCursor();
}

void UPhoneClientComponent::ServerStoreBuy_Implementation(FName ItemId, bool bBank)
{
	AWeedShopGameState* GS = GetGS();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = GetOwnerInventory();
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (!Store || !Inv || !Econ) { return; }

	// Prijs + soort bepalen (seed vs supply).
	FText Name; int32 Price = 0; int32 Pack = 1;
	const bool bSeed = Store->GetSeedDisplay(ItemId, Name, Price);
	if (!bSeed && !Store->GetSupplyDisplay(ItemId, Name, Price, Pack)) { return; }
	if (Price <= 0) { return; }

	// Betalen met bank of cash; geen bezorgkosten.
	const bool bPaid = bBank ? Econ->RemoveBank(Price) : Econ->RemoveMoney(Price);
	if (!bPaid)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Red, bBank ? TEXT("Not enough in the bank.") : TEXT("Not enough cash.")); }
		return;
	}

	// Direct leveren in je inventory.
	if (bSeed) { Inv->AddItem(Store->SeedItemId(ItemId), 1); }
	else       { Inv->AddItem(ItemId, FMath::Max(1, Pack)); }

	if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.f, FColor(120, 220, 160), FString::Printf(TEXT("Bought: %s"), *Name.ToString())); }
}

void UPhoneClientComponent::ServerStoreCheckout_Implementation(const TArray<FName>& Ids, const TArray<int32>& Qtys, bool bBank)
{
	AWeedShopGameState* GS = GetGS();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = GetOwnerInventory();
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (!Store || !Inv || !Econ) { return; }
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const int32 PlayerLvl = (GS->GetLeveling()) ? GS->GetLeveling()->GetLevelFor(OwnerPawn) : 999;

	// 1) Totaal + geldige regels (level-cap + bestaande catalog-artikelen).
	struct FBuyLine { FName Id; int32 Qty; bool bSeed; int32 Pack; };
	TArray<FBuyLine> Lines; int64 Total = 0;
	for (int32 i = 0; i < Ids.Num(); ++i)
	{
		const int32 Qty = Qtys.IsValidIndex(i) ? Qtys[i] : 0;
		if (Qty <= 0) { continue; }
		if (Store->RequiredLevelFor(Ids[i]) > PlayerLvl) { continue; } // gelockt -> overslaan
		FText N; int32 Price = 0; int32 Pack = 1;
		const bool bSeed = Store->GetSeedDisplay(Ids[i], N, Price);
		if (!bSeed && !Store->GetSupplyDisplay(Ids[i], N, Price, Pack)) { continue; }
		if (Price <= 0) { continue; }
		Lines.Add({ Ids[i], Qty, bSeed, Pack });
		Total += static_cast<int64>(Price) * Qty;
	}
	if (Lines.Num() == 0 || Total <= 0) { return; }

	// 2) Betalen (cash of bank), geen bezorgkosten.
	const bool bPaid = bBank ? Econ->RemoveBank(static_cast<int32>(Total)) : Econ->RemoveMoney(static_cast<int32>(Total));
	if (!bPaid)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Red, bBank ? TEXT("Not enough in the bank.") : TEXT("Not enough cash.")); }
		return;
	}

	// 3) Alles direct leveren.
	for (const FBuyLine& L : Lines)
	{
		if (L.bSeed) { Inv->AddItem(Store->SeedItemId(L.Id), L.Qty); }
		else         { Inv->AddItem(L.Id, FMath::Max(1, L.Pack) * L.Qty); }
	}
	if (GEngine) { UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor(120, 220, 160), FString::Printf(TEXT("Bought for EUR %lld"), (long long)(WeedRoundEuros(Total) / 100))); }
}

void UPhoneClientComponent::SetTab(int32 NewTab)
{
	Tab = FMath::Clamp(NewTab, 0, AppCount - 1);
	bHomeScreen = false;
}

void UPhoneClientComponent::CycleTab()
{
	// Q = terug naar het home-scherm (of, als je al thuis bent, niets).
	if (bOpen)
	{
		bHomeScreen = true;
	}
}

void UPhoneClientComponent::SetSupplierCat(int32 Cat)
{
	// Categorie-id's lopen tot 12 (10 = Hash, 11 = Kitchen-machines, 12 = Ingredients). NIET op
	// SupplierCatCount klemmen, anders vielen de hoge cats terug op een verkeerde tab/categorie.
	SupplierCat = FMath::Clamp(Cat, 0, 12);
}

void UPhoneClientComponent::SellInventoryIndex(int32 StackIndex)
{
	if (const UInventoryComponent* Inv = GetOwnerInventory())
	{
		const TArray<FInventoryStack>& Stacks = Inv->GetStacks();
		if (Stacks.IsValidIndex(StackIndex))
		{
			ServerSell(Stacks[StackIndex].ItemId);
		}
	}
}

void UPhoneClientComponent::SellInventoryIndexAll(int32 StackIndex)
{
	if (const UInventoryComponent* Inv = GetOwnerInventory())
	{
		const TArray<FInventoryStack>& Stacks = Inv->GetStacks();
		if (Stacks.IsValidIndex(StackIndex))
		{
			ServerSellAll(Stacks[StackIndex].ItemId);
		}
	}
}

void UPhoneClientComponent::ServerSellAll_Implementation(FName ItemId)
{
	AWeedShopGameState* GS = GetGS();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Store || !Inv) { return; }
	// Verkoop tot er niets meer van dit item is (elke SellItem checkt zelf de voorraad).
	int32 Guard = Inv->GetQuantity(ItemId) + 4;
	while (Guard-- > 0 && Inv->HasItem(ItemId, 1))
	{
		if (!Store->SellItem(ItemId, Inv)) { break; }
	}
}

// --- Winkel: aantal-keuze + winkelwagen ---

int32 UPhoneClientComponent::GetPendingQty(FName ItemId) const
{
	const int32* P = PendingQty.Find(ItemId);
	return P ? *P : 1;
}

void UPhoneClientComponent::AdjustPendingQty(FName ItemId, int32 Delta)
{
	const int32 Cur = GetPendingQty(ItemId);
	PendingQty.Add(ItemId, FMath::Clamp(Cur + Delta, 1, 99));
}

int32 UPhoneClientComponent::GetPendingSellQty(FName ItemId) const
{
	const int32* P = PendingSellQty.Find(ItemId);
	return P ? *P : 1;
}

void UPhoneClientComponent::AdjustPendingSellQty(FName ItemId, int32 Delta)
{
	const int32 Cur = GetPendingSellQty(ItemId);
	PendingSellQty.Add(ItemId, FMath::Clamp(Cur + Delta, 1, 999));
}

void UPhoneClientComponent::AddToCart(FName ItemId)
{
	if (ItemId.IsNone()) { return; }
	const int32 Qty = GetPendingQty(ItemId);
	for (FCartLine& L : Cart)
	{
		if (L.ItemId == ItemId && !L.bSell) { L.Qty = FMath::Clamp(L.Qty + Qty, 1, 999); return; }
	}
	FCartLine NewLine; NewLine.ItemId = ItemId; NewLine.Qty = Qty; NewLine.bSell = false;
	Cart.Add(NewLine);
}

void UPhoneClientComponent::AddSellToCart(FName ItemId)
{
	if (ItemId.IsNone()) { return; }
	// Niet meer in de wagen zetten dan je hebt.
	const UInventoryComponent* Inv = GetOwnerInventory();
	const int32 Have = Inv ? Inv->GetQuantity(ItemId) : 0;
	if (Have <= 0) { return; }
	const int32 Want = GetPendingSellQty(ItemId);
	for (FCartLine& L : Cart)
	{
		if (L.ItemId == ItemId && L.bSell) { L.Qty = FMath::Clamp(L.Qty + Want, 1, Have); return; }
	}
	FCartLine NewLine; NewLine.ItemId = ItemId; NewLine.Qty = FMath::Min(Want, Have); NewLine.bSell = true;
	Cart.Add(NewLine);
}

bool UPhoneClientComponent::GetCartLine(int32 Index, FName& OutItemId, int32& OutQty, bool& bOutSell) const
{
	if (!Cart.IsValidIndex(Index)) { return false; }
	OutItemId = Cart[Index].ItemId;
	OutQty = Cart[Index].Qty;
	bOutSell = Cart[Index].bSell;
	return true;
}

void UPhoneClientComponent::AdjustCartLine(int32 Index, int32 Delta)
{
	if (!Cart.IsValidIndex(Index)) { return; }
	Cart[Index].Qty += Delta;
	if (Cart[Index].Qty <= 0) { Cart.RemoveAt(Index); }
}

void UPhoneClientComponent::ClearCart()
{
	Cart.Reset();
}

int32 UPhoneClientComponent::GetCartTotalCents() const
{
	return GetCartBuyCents();
}

int32 UPhoneClientComponent::GetCartBuyCents() const
{
	const AWeedShopGameState* GS = GetGS();
	const UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return 0; }
	int32 Total = 0;
	for (const FCartLine& L : Cart)
	{
		if (!L.bSell) { Total += Store->GetCatalogPriceCents(L.ItemId) * L.Qty; }
	}
	return Total;
}

int32 UPhoneClientComponent::GetCartSellCents() const
{
	const AWeedShopGameState* GS = GetGS();
	const UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return 0; }
	int32 Total = 0;
	for (const FCartLine& L : Cart)
	{
		if (L.bSell) { Total += Store->GetSellValueCents(L.ItemId) * L.Qty; }
	}
	return Total;
}

int32 UPhoneClientComponent::GetCartNetCents(int32 DeliveryOption) const
{
	const int32 Buy = GetCartBuyCents();
	const int32 Sell = GetCartSellCents();
	const int32 Fee = FMath::RoundToInt(Buy * DeliveryFeePct(DeliveryOption)); // bezorgkosten alleen op koopdeel
	return Buy + Fee - Sell; // negatief = je ontvangt geld
}

float UPhoneClientComponent::DeliveryFeePct(int32 Opt)
{
	switch (Opt) { case 2: return 0.25f; case 1: return 0.08f; default: return 0.01f; }
}
float UPhoneClientComponent::DeliveryDelaySeconds(int32 Opt)
{
	switch (Opt) { case 2: return 0.f; case 1: return 40.f; default: return 120.f; }
}
FString UPhoneClientComponent::DeliveryName(int32 Opt)
{
	switch (Opt) { case 2: return TEXT("Instant"); case 1: return TEXT("Express"); default: return TEXT("Standard"); }
}
FString UPhoneClientComponent::DeliveryTimeText(int32 Opt)
{
	switch (Opt) { case 2: return TEXT("now"); case 1: return TEXT("~40s"); default: return TEXT("~2 min"); }
}

void UPhoneClientComponent::Checkout(int32 DeliveryOption)
{
	if (Cart.Num() == 0) { return; }
	TArray<FName> BuyIds, SellIds; TArray<int32> BuyQ, SellQ;
	for (const FCartLine& L : Cart)
	{
		if (L.bSell) { SellIds.Add(L.ItemId); SellQ.Add(L.Qty); }
		else { BuyIds.Add(L.ItemId); BuyQ.Add(L.Qty); }
	}
	ServerBuyCart(BuyIds, BuyQ, SellIds, SellQ, DeliveryOption, ResolveDeliveryHome());
	Cart.Reset();
}

void UPhoneClientComponent::DeliverCart(int32 OrderId, const TArray<FName>& ItemIds, const TArray<int32>& Quantities)
{
	// Ruim de pending-regel + timer op (ook bij directe levering geen-op als OrderId 0).
	if (OrderId > 0)
	{
		PendingDeliveries.RemoveAll([OrderId](const FPendingDelivery& D) { return D.OrderId == OrderId; });
		DeliveryTimers.Remove(OrderId);
	}

	AWeedShopGameState* GS = GetGS();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Store || !Inv) { return; }

	int32 Bought = 0, Failed = 0;
	for (int32 i = 0; i < ItemIds.Num(); ++i)
	{
		const int32 Qty = Quantities.IsValidIndex(i) ? Quantities[i] : 0;
		for (int32 q = 0; q < Qty; ++q)
		{
			if (Store->BuyAny(ItemIds[i], Inv)) { ++Bought; }
			else { ++Failed; break; }
		}
	}
	if (GEngine)
	{
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, Failed > 0 ? FColor::Orange : FColor::Green,
			FString::Printf(TEXT("Delivery arrived: %d item(s)%s"), Bought, Failed > 0 ? TEXT(" (some failed - low cash/phase)") : TEXT("")));
	}
}

void UPhoneClientComponent::ServerBuyCart_Implementation(const TArray<FName>& BuyIds, const TArray<int32>& BuyQtys,
	const TArray<FName>& SellIds, const TArray<int32>& SellQtys, int32 DeliveryOption, int32 DeliveryHome)
{
	// Door de client gekozen/afgeleid bezorg-huis vastleggen (server-authoritative check zit in ResolveDeliveryHome).
	if (DeliveryHome >= 0 && OwnedHomes.Contains(DeliveryHome)) { SelectedDeliveryHome = DeliveryHome; }

	AWeedShopGameState* GS = GetGS();
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	UInventoryComponent* Inv = GetOwnerInventory();
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (!Store || !Inv || !Econ) { return; }

	// 0) Level-gate: hogere tiers (rekken/tafels/containers) vereisen een minimum level.
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const int32 PlayerLvl = GS->GetLeveling() ? GS->GetLeveling()->GetLevelFor(OwnerPawn) : 1;
	for (int32 i = 0; i < BuyIds.Num(); ++i)
	{
		const int32 Req = Store->RequiredLevelFor(BuyIds[i]);
		if (Req > PlayerLvl)
		{
			if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.5f, FColor::Orange, FString::Printf(TEXT("%s unlocks at level %d."), *Store->GetCatalogName(BuyIds[i]).ToString(), Req)); }
			return;
		}
	}

	// 1) Koop-subtotaal + bezorgkosten (alleen op het koopdeel).
	int64 BuySub = 0;
	for (int32 i = 0; i < BuyIds.Num(); ++i)
	{
		BuySub += (int64)Store->GetCatalogPriceCents(BuyIds[i]) * (BuyQtys.IsValidIndex(i) ? BuyQtys[i] : 0);
	}
	const int64 Fee = WeedRoundEuros((int64)FMath::RoundToInt(BuySub * DeliveryFeePct(DeliveryOption)));

	// 2) Verkoop-opbrengst op basis van wat de speler echt heeft (clamp; nog niet verwijderen).
	int64 SellProceeds = 0;
	TArray<int32> SellActual; SellActual.SetNum(SellIds.Num());
	for (int32 i = 0; i < SellIds.Num(); ++i)
	{
		const int32 Want = SellQtys.IsValidIndex(i) ? SellQtys[i] : 0;
		const int32 N = FMath::Min(Want, Inv->GetQuantity(SellIds[i]));
		SellActual[i] = N;
		SellProceeds += (int64)Store->GetSellValueCents(SellIds[i]) * N;
	}

	// 3) De telefoon-winkel is online/legaal -> het KOOPdeel betaal je met BANKGELD (wit). De verkoop
	//    van je waar levert CASH (zwart) op. Genoeg bankgeld voor de aankoop?
	const int64 Cost = BuySub + Fee;
	if (Cost > 0 && !Econ->CanAffordBank(Cost))
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.5f, FColor::Red, TEXT("Not enough BANK money - launder some cash first (Bank app).")); }
		return;
	}

	// 4) Verkoop uitvoeren (items weg, opbrengst als CASH), daarna het koopdeel van de BANK afschrijven.
	for (int32 i = 0; i < SellIds.Num(); ++i)
	{
		if (SellActual[i] > 0) { Inv->RemoveItem(SellIds[i], SellActual[i]); }
	}
	if (SellProceeds > 0) { Econ->AddMoney(SellProceeds); } // cash (zwart)
	if (Cost > 0) { Econ->RemoveBank(Cost); }               // bank (wit)

	// 5) Geen koop-items? Dan zijn we klaar (alleen verkocht).
	int32 BuyCount = 0;
	for (int32 i = 0; i < BuyIds.Num(); ++i) { BuyCount += (BuyQtys.IsValidIndex(i) ? BuyQtys[i] : 0); }
	if (BuyCount <= 0)
	{
		if (GEngine && SellProceeds > 0)
		{
			UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Green, FString::Printf(TEXT("Sold for EUR %lld"), (long long)(WeedRoundEuros(SellProceeds) / 100)));
		}
		return;
	}

	// 6) Koop-items: bezorgdrone naar de voordeur (items zijn al betaald -> gratis op pickup).
	const float Flight = FMath::Max(DeliveryDelaySeconds(DeliveryOption), 5.f);
	// Server-uniek order-id over spelers heen (H.9): de GEDEELDE ActiveDeliveries wordt op OrderId gekeyed,
	// dus host + joiner mogen niet hetzelfde id uitdelen. De GameState deelt uit (authority-gated); valt
	// terug op de per-component teller als er (onverwacht) geen GameState is.
	const int32 OrderId = GS ? GS->AllocDeliveryId() : NextOrderId++;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	FPendingDelivery PD;
	PD.OrderId = OrderId;
	PD.DeliveryOpt = DeliveryOption;
	PD.FeeCents = Fee;
	PD.PaidCents = Cost;       // koopprijs + fee, terug bij annuleren
	PD.PlacedTime = Now;
	PD.ArriveTime = Now + Flight;
	PD.Ids = BuyIds;
	PD.Qtys = BuyQtys;
	for (int32 i = 0; i < BuyIds.Num(); ++i)
	{
		const int32 Q = BuyQtys.IsValidIndex(i) ? BuyQtys[i] : 0;
		PD.ItemCount += Q;
		if (!PD.Summary.IsEmpty()) { PD.Summary += TEXT(", "); }
		PD.Summary += FString::Printf(TEXT("%dx %s"), Q, *WeedUI::PrettyItemName(BuyIds[i]));
	}

	if (UWorld* World = GetWorld())
	{
		const FVector Drop = FindDeliveryPoint();
		// Altijd vanuit het NOORDWESTEN aanvliegen (+X = noord, -Y = west) en hoog -> over de open zee/strand-
		// kant, zodat de drone nergens door gebouwen heen vliegt voor 'ie bij de voordeur indaalt.
		const FVector Start = Drop + FVector(1700.f, -1700.f, 1600.f);
		ADeliveryDrone* Drone = World->SpawnActor<ADeliveryDrone>(ADeliveryDrone::StaticClass(), FTransform(Start));
		if (Drone)
		{
			Drone->Setup(Start, Drop, Flight, OrderId, BuyIds, BuyQtys, this);
			PD.Drone = Drone;
		}
		// Bezorg-marker bij de voordeur (Drop), gedeeld via de GameState -> map + kompas tonen 'm bij ALLE
		// spelers tot het pakket opgehaald is. In COMPETITIVE geven we de stabiele id van de bestellende
		// speler mee zodat map/kompas alleen de EIGEN marker tonen (anders verklap je de kamer van de
		// tegenstander); in co-op blijft 'ie leeg -> gedeelde marker (gewenst).
		if (GS)
		{
			FString ForId;
			if (GS->IsCompetitive()) { if (const APawn* Me = Cast<APawn>(GetOwner())) { ForId = USaveGameSubsystem::StablePlayerId(Me); } }
			GS->AddDeliveryTarget(OrderId, Drop, ForId);
		}
	}
	PendingDeliveries.Add(PD);

	if (GEngine)
	{
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor(120, 200, 255),
			FString::Printf(TEXT("Order placed - %s delivery. A drone is bringing it to your door."), *DeliveryName(DeliveryOption)));
	}
}

FVector UPhoneClientComponent::FindDeliveryPoint() const
{
	const UWorld* World = GetWorld();
	FVector Point = FVector::ZeroVector;
	bool bFound = false;

	// 0) HOOGSTE prioriteit in Competitive: de eigen kamer-deur van DEZE speler (per-speler spiegel-kamers).
	//    Server-side: op de listen-server is de host-pawn locally controlled, de joiner niet -> zo weten we
	//    voor wie we het droppunt zoeken. De DoorRetrofitter kent de per-speler deur-spot.
	{
		AWeedShopGameState* GS = World ? World->GetGameState<AWeedShopGameState>() : nullptr;
		if (GS && GS->IsCompetitive())
		{
			if (ADoorRetrofitter* R = FindRetro())
			{
				const APawn* P = Cast<APawn>(GetOwner());
				const bool bJoiner = P && !P->IsLocallyControlled();
				if (R->GetCompDeliverySpot(bJoiner, Point)) { bFound = true; }
			}
		}
	}

	// 1) Voor de VOORDEUR van het GEKOZEN bezorg-huis (handmatig, het huis waar je nu binnen bent, of je
	//    actieve woning). DoorPos is op de pack-map het kamer-MIDDEN, dus zoeken we de echte deur-actor
	//    (dichtstbijzijnde eigen-woning-deur bij dit huis) en zetten het pakket NET BUITEN op de stoep -
	//    niet midden in de kamer en niet door het dak. Wint van de vaste DeliveryPoint.txt-marker: spelers
	//    met een eigen huis krijgen het pakket aan hun eigen deur.
	if (!bFound)
	{
		const int32 DH = ResolveDeliveryHome();
		if (DH >= 0)
		{
			TArray<FApartmentHome> Homes; GetHomesUnified(Homes);
			if (Homes.IsValidIndex(DH) && World)
			{
				const FVector Interior = Homes[DH].InteriorPos;
				// NOOIT Homes[DH].DoorPos als eindpunt laten staan: op de pack-map is DoorPos het
				// kamer-MIDDEN - zonder echte deur-actor stond het pakket (en de afspraak-NPC die
				// deze resolver ook gebruikt) dus midden in de kamer. Geen deur gevonden -> bFound
				// blijft false en we vallen netjes door naar de Shift+F7-marker-tak.
				ACityDoor* BestAny = nullptr;  float BestAnyD = TNumericLimits<float>::Max();
				ACityDoor* BestHome = nullptr; float BestHomeD = TNumericLimits<float>::Max();
				const float MaxR = FMath::Max(Homes[DH].RoomHalf.X, Homes[DH].RoomHalf.Y) + 400.f;
				for (TActorIterator<ACityDoor> It(const_cast<UWorld*>(World)); It; ++It)
				{
					if (!IsValid(*It)) { continue; }
					const float D2 = FVector::DistSquared2D(It->GetActorLocation(), Interior);
					if (D2 > MaxR * MaxR) { continue; }
					if (D2 < BestAnyD) { BestAnyD = D2; BestAny = *It; }
					if (It->IsPlayerHome() && D2 < BestHomeD) { BestHomeD = D2; BestHome = *It; }
				}
				if (ACityDoor* Door = BestHome ? BestHome : BestAny)
				{
					const FVector DoorLoc = Door->GetActorLocation();
					FVector Outward = DoorLoc - Interior; Outward.Z = 0.f; Outward = Outward.GetSafeNormal();
					if (Outward.IsNearlyZero()) { Outward = Door->GetActorForwardVector(); Outward.Z = 0.f; Outward = Outward.GetSafeNormal(); }
					Point = DoorLoc + Outward * 150.f; // op de stoep, net buiten de voordeur
					// De BestAny-fallback (dichtstbijzijnde WILLEKEURIGE deur) alleen accepteren als
					// het stoep-punt niet alsnog binnen een woon-kamer ligt (verkeerde deur gekozen
					// = het punt kan aan de kamer-kant van die deur uitkomen).
					ADoorRetrofitter* Retro = FindRetro();
					bFound = (BestHome != nullptr) || !(Retro && Retro->IsInsideHomeRoom(Point, 0.f));
				}
			}
		}
	}

	// 2) Fallback voor spelers ZONDER eigen huis: een door de speler vastgelegd bezorg-punt (Shift+F7) voor
	//    DEZE map - bv. helemaal beneden bij de hotel-hoofdingang. Alleen als de huis-deur-tak niks vond
	//    (spelers met een eigen huis krijgen het pakket aan hun eigen deur, niet bij de vaste marker).
	if (!bFound)
	{
		TArray<FString> DpLines;
		if (FFileHelper::LoadFileToStringArray(DpLines, *(FPaths::ProjectSavedDir() / TEXT("DeliveryPoint.txt"))))
		{
			const FString CurMap = World ? World->GetOutermost()->GetName() : FString(TEXT("?"));
			for (const FString& Raw : DpLines)
			{
				TArray<FString> P; Raw.TrimStartAndEnd().ParseIntoArray(P, TEXT("|"));
				if (P.Num() >= 4 && P[0] == CurMap)
				{
					Point = FVector(FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3]));
					bFound = true;
				}
			}
		}
	}

	if (World && !bFound)
	{
		// 3) Een door de level-designer getagde actor "DeliveryPoint".
		for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
		{
			if (It->ActorHasTag(FName(TEXT("DeliveryPoint")))) { Point = It->GetActorLocation(); bFound = true; break; }
		}
		// 4) Anders: de (eerste) plek waar klanten verschijnen = bij de voordeur.
		if (!bFound)
		{
			for (TActorIterator<ACustomerSpawner> It(const_cast<UWorld*>(World)); It; ++It)
			{
				Point = It->GetActorLocation(); bFound = true; break;
			}
		}
	}
	// 5) Fallback: voor de speler.
	if (!bFound)
	{
		if (const APawn* P = Cast<APawn>(GetOwner()))
		{
			Point = P->GetActorLocation() + P->GetActorForwardVector() * 300.f;
		}
	}

	// Op de grond plaatsen (recht naar beneden tracen).
	if (World)
	{
		FHitResult Hit;
		const FVector DStart = Point + FVector(0.f, 0.f, 300.f);
		const FVector DEnd = Point - FVector(0.f, 0.f, 600.f);
		FCollisionQueryParams Params;
		if (GetOwner()) { Params.AddIgnoredActor(GetOwner()); }
		if (World->LineTraceSingleByChannel(Hit, DStart, DEnd, ECC_Visibility, Params))
		{
			Point.Z = Hit.ImpactPoint.Z;
		}
	}
	return Point;
}

void UPhoneClientComponent::NotifyDroneArrived(int32 OrderId)
{
	for (FPendingDelivery& D : PendingDeliveries)
	{
		if (D.OrderId == OrderId) { D.bArrived = true; D.Drone = nullptr; break; }
	}
}

void UPhoneClientComponent::OnPackagePickedUp(int32 OrderId)
{
	// Historie: kopieer de opgehaalde bestelling naar DeliveredHistory (nieuwste voorop, gecapt op 20)
	// VOOR we 'm uit de pending-lijst gooien.
	for (const FPendingDelivery& D : PendingDeliveries)
	{
		if (D.OrderId != OrderId) { continue; }
		FDeliveredRecord Rec;
		Rec.OrderId = D.OrderId;
		Rec.Ids = D.Ids;
		Rec.Qtys = D.Qtys;
		Rec.PaidCents = D.PaidCents;
		Rec.FeeCents = D.FeeCents;
		DeliveredHistory.Insert(Rec, 0);
		while (DeliveredHistory.Num() > 20) { DeliveredHistory.RemoveAt(DeliveredHistory.Num() - 1); }
		break;
	}
	PendingDeliveries.RemoveAll([OrderId](const FPendingDelivery& D) { return D.OrderId == OrderId; });
	// Marker weghalen (map + kompas) zodra het pakket opgehaald is.
	if (UWorld* W = GetWorld()) { if (AWeedShopGameState* GS = W->GetGameState<AWeedShopGameState>()) { GS->RemoveDeliveryTarget(OrderId); } }
}

void UPhoneClientComponent::RequestDeposit(int64 CashAmount)
{
	ServerDeposit(CashAmount);
}

void UPhoneClientComponent::ServerDeposit_Implementation(int64 CashAmount)
{
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (!Econ) { return; }
	int64 Amt = CashAmount;
	if (Amt <= 0) { Amt = FMath::Min(Econ->GetCashCents(), Econ->GetDailyDepositRemainingCents()); } // max
	if (Amt > 0) { Econ->Deposit(Amt); }
}

int64 UPhoneClientComponent::GetSafeCapCents() const
{
	int64 MaxCap = 0;
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AAtm> It(W); It; ++It)
		{
			if (It->IsSafe()) { MaxCap = FMath::Max(MaxCap, It->GetSafeCapacityCents()); }
		}
	}
	return MaxCap;
}

void UPhoneClientComponent::RequestSafeMove(int64 Cents, bool bToSafe) { ServerSafeMove(Cents, bToSafe); }

void UPhoneClientComponent::ServerSafeMove_Implementation(int64 Cents, bool bToSafe)
{
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (!Econ) { return; }
	if (bToSafe)
	{
		// Klamp op de vrije kluis-ruimte: cap (grootste geplaatste safe) min wat er al in zit.
		const int64 Room = FMath::Max<int64>(0, GetSafeCapCents() - Econ->GetSafeCents());
		int64 Amt = (Cents <= 0) ? Econ->GetCashCents() : Cents;
		Amt = FMath::Min(Amt, Room);
		if (Amt > 0) { Econ->DepositToSafe(Amt); }
	}
	else
	{
		int64 Amt = (Cents <= 0) ? Econ->GetSafeCents() : Cents; // max = hele kluis
		if (Amt > 0) { Econ->WithdrawFromSafe(Amt); }
	}
}

void UPhoneClientComponent::RequestDevHeatEvent(bool bBust) { ServerDevHeatEvent(bBust); }

void UPhoneClientComponent::RequestGiveBuildKit() { ServerGiveBuildKit(); }
void UPhoneClientComponent::RequestGiveFurnitureKit() { ServerGiveFurnitureKit(); }

void UPhoneClientComponent::ServerGiveFurnitureKit_Implementation()
{
	APawn* P = Cast<APawn>(GetOwner());
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }
	// Woon-meubels (geen grow/processing-gear): genoeg om een kamer mee in te richten.
	static const TCHAR* Kit[] = {
		TEXT("Mattress"), TEXT("Table"), TEXT("Fridge"), TEXT("Shelf"),
		TEXT("Wardrobe"), TEXT("Sink"), TEXT("Lamp_Ceiling"), TEXT("Chest"),
		TEXT("Bench_Pack"), TEXT("Safe_Small") };
	for (const TCHAR* Id : Kit) { Inv->AddItem(FName(Id), 3); }
	UWeedToast::NotifyPawn(P, -1, 3.f, FColor::Green, TEXT("Furniture kit added - place them via the build mode"));
}

void UPhoneClientComponent::SaveStarterFurniture()
{
	UWorld* W = GetWorld();
	APawn* P = Cast<APawn>(GetOwner());
	if (!W || !P) { return; }
	// Alle meubels rond de speler (de kamer waar je in staat): radius 1500, Z +/-500.
	const FVector C = P->GetActorLocation();
	FString Out;
	int32 N = 0;
	for (TActorIterator<APlaceableProp> It(W); It; ++It)
	{
		APlaceableProp* Pr = *It;
		if (!IsValid(Pr) || Pr->ItemId.IsNone()) { continue; }
		const FVector L = Pr->GetActorLocation();
		if (FVector::Dist2D(L, C) > 1500.f || FMath::Abs(L.Z - C.Z) > 500.f) { continue; }
		Out += FString::Printf(TEXT("%s,%.1f,%.1f,%.1f,%.1f"), *Pr->ItemId.ToString(), L.X, L.Y, L.Z, Pr->GetActorRotation().Yaw) + LINE_TERMINATOR;
		++N;
	}
	for (TActorIterator<AWaterSink> It(W); It; ++It)
	{
		AWaterSink* Sk = *It;
		if (!IsValid(Sk)) { continue; }
		const FVector L = Sk->GetActorLocation();
		if (FVector::Dist2D(L, C) > 1500.f || FMath::Abs(L.Z - C.Z) > 500.f) { continue; }
		Out += FString::Printf(TEXT("Sink,%.1f,%.1f,%.1f,%.1f"), L.X, L.Y, L.Z, Sk->GetActorRotation().Yaw) + LINE_TERMINATOR;
		++N;
	}
	// Opslag-meubels (Fridge/Shelf/Chest) zijn een EIGEN class (AStorageShelf), geen APlaceableProp - net als
	// de sink. Zonder deze loop verdween jouw geplaatste fridge elke keer. We schrijven de ShelfTier als ItemId
	// zodat de load-kant 'm weer als AStorageShelf (functioneel) terugzet i.p.v. een dode prop.
	for (TActorIterator<AStorageShelf> It(W); It; ++It)
	{
		AStorageShelf* Sh = *It;
		if (!IsValid(Sh)) { continue; }
		const FVector L = Sh->GetActorLocation();
		if (FVector::Dist2D(L, C) > 1500.f || FMath::Abs(L.Z - C.Z) > 500.f) { continue; }
		const FName Tier = Sh->ShelfTier.IsNone() ? FName(TEXT("Shelf")) : Sh->ShelfTier;
		Out += FString::Printf(TEXT("%s,%.1f,%.1f,%.1f,%.1f"), *Tier.ToString(), L.X, L.Y, L.Z, Sh->GetActorRotation().Yaw) + LINE_TERMINATOR;
		++N;
	}
	// Lichtschakelaars (APackLightSwitch = eigen class, geen prop) -> zonder deze loop verdwenen jouw
	// geplaatste schakelaars elke save (net als sink/shelf hierboven).
	for (TActorIterator<APackLightSwitch> It(W); It; ++It)
	{
		APackLightSwitch* Sw = *It;
		if (!IsValid(Sw)) { continue; }
		const FVector L = Sw->GetActorLocation();
		if (FVector::Dist2D(L, C) > 1500.f || FMath::Abs(L.Z - C.Z) > 500.f) { continue; }
		Out += FString::Printf(TEXT("LightSwitch,%.1f,%.1f,%.1f,%.1f"), L.X, L.Y, L.Z, Sw->GetActorRotation().Yaw) + LINE_TERMINATOR;
		++N;
	}
	if (N == 0)
	{
		UWeedToast::NotifyPawn(P, -1, 3.f, FColor::Orange, TEXT("No furniture near you to save - place some first"));
		return;
	}
	FFileHelper::SaveStringToFile(Out, *(FPaths::ProjectSavedDir() / TEXT("StarterFurniture.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UWeedToast::NotifyPawn(P, -1, 4.f, FColor::Green, FString::Printf(TEXT("Starter layout saved (%d items)! New games start furnished"), N));
}

void UPhoneClientComponent::ClearStarterFurniture()
{
	WeedData::DeleteFile(TEXT("StarterFurniture.txt"));
	UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("Starter furniture cleared"));
}

void UPhoneClientComponent::SaveRoomTemplateNow()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const FString Name = FString::Printf(TEXT("Room_%d"), ARoomStamper::ListTemplates().Num() + 1);
	FString Err;
	if (ARoomStamper::SaveTemplateFromMarkers(W, Name, Err))
	{
		// Markers vrijmaken voor de volgende kamer.
		FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Green, FString::Printf(TEXT("Template '%s' saved! Markers cleared"), *Name));
	}
	else
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, Err);
	}
}

void UPhoneClientComponent::StartRoomStamp(const FString& TemplateName)
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	ARoomStamper* Stamper = nullptr;
	for (TActorIterator<ARoomStamper> It(W); It; ++It) { Stamper = *It; break; }
	if (!Stamper) { Stamper = W->SpawnActor<ARoomStamper>(ARoomStamper::StaticClass(), FTransform::Identity); }
	if (Stamper && Stamper->BeginStamp(TemplateName))
	{
		CloseAllUI(); // telefoon dicht zodat je vrij kunt mikken
	}
	else
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("Could not load that template"));
	}
}

void UPhoneClientComponent::SaveRoomJob()
{
	UWorld* W = GetWorld();
	const FString MapPath = W ? W->GetOutermost()->GetName() : FString();
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
	if (Marks.Num() != 3)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("Need exactly 3 markers to save a room build"));
		return;
	}
	const FString JobLine = FString::Printf(TEXT("%.0f,%.0f,%.0f|%.0f,%.0f,%.0f|%.0f,%.0f,%.0f"),
		Marks[0].X, Marks[0].Y, Marks[0].Z, Marks[1].X, Marks[1].Y, Marks[1].Z, Marks[2].X, Marks[2].Y, Marks[2].Z) + LINE_TERMINATOR;
	FFileHelper::SaveStringToFile(JobLine, *(FPaths::ProjectSavedDir() / TEXT("RoomJobs.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	// Markers vrijmaken voor het volgende gebouw.
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Green, TEXT("Room build saved! Markers cleared - on to the next building"));
}

void UPhoneClientComponent::ServerGiveBuildKit_Implementation()
{
	APawn* P = Cast<APawn>(GetOwner());
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }
	static const TCHAR* Kit[] = {
		TEXT("Struct_Wall4m"), TEXT("Struct_Wall2m"), TEXT("Struct_Wall1m"),
		TEXT("Struct_WallDoor4m"), TEXT("Struct_WallDoor3m"),
		TEXT("Struct_Floor4x4"), TEXT("Struct_Floor1x1"),
		TEXT("Struct_Ceil4x4"), TEXT("Struct_Ceil1x1"),
		TEXT("Struct_CeilLamp"), TEXT("Struct_Door") };
	for (const TCHAR* Id : Kit)
	{
		Inv->AddItem(FName(Id), 1);
	}
	UWeedToast::NotifyPawn(P, -1, 3.f, FColor::Green, TEXT("Building kit added (11 pieces, infinite use)"));
}

void UPhoneClientComponent::ServerDevHeatEvent_Implementation(bool bBust)
{
	AWeedShopGameState* GS = GetGS();
	UHeatComponent* Heat = GS ? GS->GetHeat() : nullptr;
	if (!Heat) { return; }
	if (bBust) { Heat->DevTriggerBust(); } else { Heat->DevTriggerRobbery(); }
}

void UPhoneClientComponent::RequestSetDayNight(bool bNight) { ServerSetDayNight(bNight); }

void UPhoneClientComponent::ServerSetDayNight_Implementation(bool bNight)
{
	AWeedShopGameState* GS = GetGS();
	UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr;
	if (!DC) { return; }
	// Midden-dag = halverwege de lichtfase; midden-nacht = halverwege de donkerfase.
	const float NewTime = bNight ? (DC->DayLengthSeconds + DC->NightLengthSeconds * 0.5f) : (DC->DayLengthSeconds * 0.5f);
	DC->SetTimeOfDaySeconds(NewTime);
}

void UPhoneClientComponent::ServerSetDevTools_Implementation(bool bOn)
{
	// Sessie-brede dev-tools-vlag (elke speler mag 'm zetten; gerepliceerd via de GameState).
	if (AWeedShopGameState* GS = GetGS()) { GS->SetDevTools(bOn); }
}

void UPhoneClientComponent::ServerDevGiveCash_Implementation(int64 Cents)
{
	const AWeedShopGameState* GS = GetGS();
	if (!GS || !GS->AreDevToolsEnabled() || Cents <= 0) { return; }
	if (UEconomyComponent* Econ = GetOwnerEconomy())
	{
		Econ->SetBalanceCents(Econ->GetBalanceCents() + Cents);
	}
}

void UPhoneClientComponent::ServerDevSandboxMoney_Implementation()
{
	const AWeedShopGameState* GS = GetGS();
	if (!GS || !GS->AreDevToolsEnabled()) { return; }
	// Zelfde bedragen als de Sandbox-startmode: EUR 1.000.000 cash + bank.
	if (UEconomyComponent* Econ = GetOwnerEconomy())
	{
		Econ->SetBalanceCents(100000000);
		Econ->SetBankCents(100000000);
	}
}

void UPhoneClientComponent::ServerDevSetLevel_Implementation(int32 NewLevel)
{
	AWeedShopGameState* GS = GetGS();
	ULevelComponent* Lv = GS ? GS->GetLeveling() : nullptr;
	if (!Lv || !GS->AreDevToolsEnabled()) { return; }
	Lv->GrantLevelFor(Cast<APawn>(GetOwner()), NewLevel); // per-speler in competitive (deze cheat zet alleen het eigen level)
	// GrantLevel zet de shop-licentie NIET (die zit alleen in het AddXP-levelup-pad - oude quirk);
	// vanaf level 50 hoort de licentie erbij, dus hier expliciet mee-zetten.
	if (NewLevel >= ULevelComponent::ShopLicenseLevel) { Lv->RestoreShopLicensed(true); }
}

void UPhoneClientComponent::ServerDevWarmNpcs_Implementation()
{
	AWeedShopGameState* GS = GetGS();
	if (!GS || !GS->AreDevToolsEnabled()) { return; }
	if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry())
	{
		Reg->WarmAllForTesting(GS->GetContacts());
		Toast(TEXT("All NPCs warmed - good stats, unlocked + contacts added"), FColor::Green, 3.f);
	}
}

void UPhoneClientComponent::ServerDevSetFreeBuild_Implementation(bool bOn)
{
	AWeedShopGameState* GS = GetGS();
	if (!GS || !GS->AreDevToolsEnabled()) { return; }
	GS->SetFreeBuild(bOn);
}

void UPhoneClientComponent::ServerDevGiveStarterKit_Implementation(bool bSandbox)
{
	// Item-lijsten 1-op-1 uit de Testing/Sandbox-startmodes (USaveGameSubsystem::ApplyStartMode),
	// zodat je ze ook midden in een sessie kunt pakken. Alleen items + zaden (geld/level/NPC's
	// hebben hun eigen cheat-knoppen).
	AWeedShopGameState* GS = GetGS();
	if (!GS || !GS->AreDevToolsEnabled()) { return; }
	APawn* P = Cast<APawn>(GetOwner());
	UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inv) { return; }
	auto Give = [Inv](const TCHAR* Id, int32 N) { Inv->AddItem(FName(Id), N); };
	if (bSandbox)
	{
		// Sandbox = NETTE set i.p.v. een volgepropte inventory. Eerst leeg (anders stapelt 'ie op wat
		// je al had), dan een standaard starter + precies de plaatsbare meubels voor het inrichten.
		Inv->ClearAll();
		Give(TEXT("Papers_Small"), 5);
		Give(TEXT("Soil_Basic"), 5);
		Give(TEXT("WaterBottle_Plastic"), 2);
		Give(TEXT("Pot_Clay"), 2);
		Give(TEXT("Cont_Bag2"), 10);
		// Plaatsbare meubels (authoring):
		Give(TEXT("Table"), 5);
		Give(TEXT("Fridge"), 5);
		Give(TEXT("Mattress"), 5);
		Give(TEXT("Sink"), 5); // sandbox-only: om de sink-positie voor de template in te richten
		Give(TEXT("DryRack_Std"), 3);
		Give(TEXT("Bench_Pack"), 3);
		Give(TEXT("Shelf"), 3);
		Give(TEXT("Chest"), 3);
		Give(TEXT("Lamp_Ceiling"), 3);
		Give(TEXT("Atm"), 2);
		// Eén soort zaad om mee te testen.
		if (GS->GetStore())
		{
			const TArray<FName> Seeds = GS->GetStore()->GetSeedCatalog();
			if (Seeds.Num() > 0) { Inv->AddItem(UStoreComponent::SeedItemId(Seeds[0]), 3); }
		}
	}
	else
	{
		// Testing: starter-budget + handige items (zoals voorheen).
		Give(TEXT("Soil_Basic"),          3);
		Give(TEXT("WaterBottle_Plastic"), 1);
		Give(TEXT("Papers_Small"),        10);
		Give(TEXT("Cont_Bag2"),           10);
		Give(TEXT("Pot_Clay"),            1);
		Give(TEXT("DryRack_Cheap"),       1);
		Give(TEXT("Bench_Pack"),          1);
		// (Geen Sink: vaste fixture.)
		if (GS->GetStore())
		{
			const TArray<FName> Seeds = GS->GetStore()->GetSeedCatalog();
			const int32 Count = FMath::Min(2, Seeds.Num());
			for (int32 i = 0; i < Count; ++i) { Inv->AddItem(UStoreComponent::SeedItemId(Seeds[i]), 3); }
		}
	}
	UWeedToast::NotifyPawn(P, -1, 3.f, FColor::Green, bSandbox
		? TEXT("Sandbox kit added (inventory REPLACED - authoring set)")
		: TEXT("Tester kit added (starter items + 2 seed types)"));
}

void UPhoneClientComponent::RequestTransfer(int64 AmountCents)
{
	ServerTransfer(AmountCents);
}

void UPhoneClientComponent::ServerTransfer_Implementation(int64 AmountCents)
{
	UEconomyComponent* Mine = GetOwnerEconomy();
	if (!Mine || AmountCents <= 0) { return; }

	// Co-op deelt EEN bank (BankOwner() = gedeelde GS-economy): een transfer is dan een no-op die alleen
	// de fee verbrandt. Alleen in Competitive heeft ieder z'n eigen bank. Buiten competitive vroeg terug.
	{
		AWeedShopGameState* GScoop = GetGS();
		if (!GScoop || !GScoop->IsCompetitive())
		{
			UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("No transfer needed - you share one bank."));
			return;
		}
	}

	// Zoek een co-op vriend (de portemonnee van een andere speler) om naar te sturen.
	UEconomyComponent* Friend = nullptr;
	if (UWorld* W = GetWorld())
	{
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			APawn* P = PC ? PC->GetPawn() : nullptr;
			if (!P || P == GetOwner()) { continue; }
			if (UEconomyComponent* E = P->FindComponentByClass<UEconomyComponent>()) { Friend = E; break; }
		}
	}
	if (!Friend)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Orange, TEXT("No co-op friend online to send money to.")); }
		return;
	}

	// Bedrag + fee verlaat MIJN bank; de vriend ontvangt het volle bedrag (belastingvrij).
	if (Mine->TransferBank(AmountCents))
	{
		Friend->AddBank(AmountCents, false);
	}
}

void UPhoneClientComponent::RequestBuyPhoneUpgrade()
{
	ServerBuyPhoneUpgrade();
}

void UPhoneClientComponent::RequestSaveGame()
{
	ServerRequestSave();
}

void UPhoneClientComponent::ServerRequestSave_Implementation()
{
	if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (USaveGameSubsystem* Sv = GI->GetSubsystem<USaveGameSubsystem>())
		{
			Sv->SaveGame();
		}
	}
}

void UPhoneClientComponent::ServerBuyPhoneUpgrade_Implementation()
{
	if (bBankAppUnlocked) { return; }
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (!Econ || !Econ->RemoveBank(PhoneUpgradeCostCents))
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Red, TEXT("Not enough BANK money for the phone upgrade (launder cash first).")); }
		return;
	}
	bBankAppUnlocked = true;
	if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 4.f, FColor::Green, TEXT("Phone upgraded - Bank app unlocked!")); }
}

float UPhoneClientComponent::GetDeliveryProgress(const FPendingDelivery& D) const
{
	const float Span = D.ArriveTime - D.PlacedTime;
	if (Span <= 0.f) { return 1.f; }
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : D.ArriveTime;
	return FMath::Clamp((Now - D.PlacedTime) / Span, 0.f, 1.f);
}

float UPhoneClientComponent::GetDeliverySecondsLeft(const FPendingDelivery& D) const
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : D.ArriveTime;
	return FMath::Max(0.f, D.ArriveTime - Now);
}

void UPhoneClientComponent::CancelDelivery(int32 OrderId)
{
	ServerCancelDelivery(OrderId);
}

void UPhoneClientComponent::ServerCancelDelivery_Implementation(int32 OrderId)
{
	const int32 Idx = PendingDeliveries.IndexOfByPredicate([OrderId](const FPendingDelivery& D) { return D.OrderId == OrderId; });
	if (Idx == INDEX_NONE) { return; }

	// Ligt het pakket al bij de deur? Dan niet annuleren - gewoon oppakken.
	if (PendingDeliveries[Idx].bArrived)
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Orange, TEXT("Already delivered - pick it up at the door.")); }
		return;
	}
	// Drone nog onderweg -> laat 'm verdwijnen.
	if (ADeliveryDrone* Drone = PendingDeliveries[Idx].Drone.Get())
	{
		Drone->Destroy();
	}
	// Het koopdeel (itemprijs + fee) was al bij checkout betaald -> volledig terugstorten.
	const int64 Refund = PendingDeliveries[Idx].PaidCents;
	if (Refund > 0)
	{
		if (UEconomyComponent* Econ = GetOwnerEconomy()) { Econ->AddBank(Refund, false); } // terug op de bank
	}
	if (GEngine)
	{
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Yellow,
			FString::Printf(TEXT("Order cancelled - EUR %lld refunded"), (long long)(WeedRoundEuros(Refund) / 100)));
	}
	PendingDeliveries.RemoveAt(Idx);
	// Gedeelde bezorg-marker (map + kompas) weghalen bij annuleren - anders blijft er een spook-marker bij
	// de deur staan die nooit meer opgehaald wordt (dezelfde opruiming als bij het oppakken van het pakket).
	if (UWorld* W = GetWorld()) { if (AWeedShopGameState* GS = W->GetGameState<AWeedShopGameState>()) { GS->RemoveDeliveryTarget(OrderId); } }
}

void UPhoneClientComponent::OpenPotUpgrade(AGrowPlant* Pot)
{
	if (!Pot)
	{
		return;
	}
	UpgPot = Pot;
	bPotUpgradeOpen = true;
	bOpen = false; bRollOpen = false; bDealOpen = false; bInventoryOpen = false;
	UpdateCursor();
}

void UPhoneClientComponent::ClosePotUpgrade()
{
	bPotUpgradeOpen = false;
	UpgPot = nullptr;
	UpdateCursor();
}

void UPhoneClientComponent::BuyPotUpgrade(int32 UpgIndex)
{
	if (AGrowPlant* Pot = UpgPot.Get())
	{
		ServerBuyPotUpgrade(Pot, UpgIndex);
	}
}

void UPhoneClientComponent::ServerBuyPotUpgrade_Implementation(AGrowPlant* Pot, int32 UpgIndex)
{
	if (!Pot || Pot->HasPotUpgrade(UpgIndex))
	{
		return;
	}
	// Sommige upgrades (auto-water/hogere tiers) kunnen pas op latere potten.
	if (!IsPotUpgradeAllowed(UpgIndex, Pot->GetPotTier()))
	{
		if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Orange, TEXT("That upgrade needs a better pot.")); }
		return;
	}
	const TArray<FPotUpgradeDef>& UpgDefs = GetPotUpgrades();
	if (UpgDefs.IsValidIndex(UpgIndex))
	{
		// Eerst de vorige tier nodig.
		if (UpgDefs[UpgIndex].PrereqIndex >= 0 && !Pot->HasPotUpgrade(UpgDefs[UpgIndex].PrereqIndex))
		{
			if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Orange, TEXT("Install the previous tier first.")); }
			return;
		}
		// Level-eis.
		const int32 PlayerLvl = (GetGS() && GetGS()->GetLeveling()) ? GetGS()->GetLeveling()->GetLevelFor(Cast<APawn>(GetOwner())) : 1;
		if (PlayerLvl < UpgDefs[UpgIndex].MinPlayerLevel)
		{
			if (GEngine) { UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Orange, FString::Printf(TEXT("That upgrade unlocks at level %d."), UpgDefs[UpgIndex].MinPlayerLevel)); }
			return;
		}
	}
	const int32 Cost = GetPotUpgradeCost(UpgIndex, Pot->GetPotTier());
	UEconomyComponent* Econ = GetOwnerEconomy();
	if (Cost <= 0 || !Econ || !Econ->RemoveBank(Cost)) // via telefoon -> bankgeld (wit)
	{
		if (GEngine)
		{
			UWeedToast::NotifyPawn(GetOwner(),-1, 2.5f, FColor::Red, TEXT("Not enough BANK money for that pot upgrade (launder cash first)."));
		}
		return;
	}
	Pot->ApplyPotUpgrade(UpgIndex);
	if (GEngine)
	{
		const TArray<FPotUpgradeDef>& Ups = GetPotUpgrades();
		const FString Name = Ups.IsValidIndex(UpgIndex) ? Ups[UpgIndex].DisplayName : TEXT("upgrade");
		UWeedToast::NotifyPawn(GetOwner(),-1, 3.f, FColor::Green, FString::Printf(TEXT("Pot upgrade installed: %s"), *Name));
	}
}

void UPhoneClientComponent::ServerSell_Implementation(FName ItemId)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetStore())
		{
			GS->GetStore()->SellItem(ItemId, GetOwnerInventory());
		}
	}
}

void UPhoneClientComponent::HandleNumberKey(FKey Key)
{
	int32 Index = -1;
	if (Key == EKeys::One)        Index = 0;
	else if (Key == EKeys::Two)   Index = 1;
	else if (Key == EKeys::Three) Index = 2;
	else if (Key == EKeys::Four)  Index = 3;
	else if (Key == EKeys::Five)  Index = 4;
	else if (Key == EKeys::Six)   Index = 5;
	if (Index >= 0)
	{
		DoAction(Index);
	}
}

void UPhoneClientComponent::DoAction(int32 Index)
{
	if (!bOpen)
	{
		return;
	}
	AWeedShopGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	if (Tab == 1) // Suppliers: items uit de huidige subcategorie
	{
		if (UStoreComponent* Store = GS->GetStore())
		{
			const TArray<FName> Items = Store->GetSupplierCategory(SupplierCat);
			if (Items.IsValidIndex(Index))
			{
				if (UStoreComponent::IsSeedCategory(SupplierCat))
				{
					ServerBuySeed(Items[Index]);
				}
				else
				{
					ServerBuySupply(Items[Index]);
				}
			}
		}
	}
	else if (Tab == 3) // Berichten: 0 = accepteren, 1 = weigeren
	{
		if (Index == 0)      { ServerRespond(true); }
		else if (Index == 1) { ServerRespond(false); }
	}
	else if (Tab == 0) // Upgrades
	{
		if (GS->GetUpgrades())
		{
			const TArray<FName> Ids = GS->GetUpgrades()->GetAllUpgradeIds();
			if (Ids.IsValidIndex(Index))
			{
				ServerBuyUpgrade(Ids[Index]);
			}
		}
	}
	// Tab == 2 (Contacten): geen actie
}

void UPhoneClientComponent::ServerBuyUpgrade_Implementation(FName UpgradeId)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetUpgrades())
		{
			GS->GetUpgrades()->BuyUpgrade(UpgradeId, GetOwnerEconomy());
		}
	}
}

void UPhoneClientComponent::ServerBuySeed_Implementation(FName StrainId)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetStore())
		{
			GS->GetStore()->BuySeed(StrainId, GetOwnerInventory());
		}
	}
}

void UPhoneClientComponent::ServerRespond_Implementation(bool bAccept)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetContacts())
		{
			// Competitive: server leidt de acterende speler af uit de owner-pawn; leeg buiten competitive.
			const APawn* Me = Cast<APawn>(GetOwner());
			const FString CallerId = (GS->IsCompetitive() && Me) ? USaveGameSubsystem::StablePlayerId(Me) : FString();
			GS->GetContacts()->RespondTopPending(bAccept, CallerId);
		}
	}
}

void UPhoneClientComponent::ServerRespondContact_Implementation(FName ContactId, bool bAccept)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetContacts())
		{
			// Competitive: server leidt de acterende speler af uit de owner-pawn; leeg buiten competitive.
			const APawn* Me = Cast<APawn>(GetOwner());
			const FString CallerId = (GS->IsCompetitive() && Me) ? USaveGameSubsystem::StablePlayerId(Me) : FString();
			GS->GetContacts()->RespondToContact(ContactId, bAccept, CallerId);
		}
	}
}

void UPhoneClientComponent::ServerProposeContactTime_Implementation(FName ContactId, int32 MinutesOfDay)
{
	if (AWeedShopGameState* GS = GetGS())
	{
		if (GS->GetContacts())
		{
			// Competitive: server leidt de acterende speler af uit de owner-pawn; leeg buiten competitive.
			const APawn* Me = Cast<APawn>(GetOwner());
			const FString CallerId = (GS->IsCompetitive() && Me) ? USaveGameSubsystem::StablePlayerId(Me) : FString();
			GS->GetContacts()->ProposeTimeToContact(ContactId, MinutesOfDay, CallerId);
		}
	}
}

void UPhoneClientComponent::ServerProposeContactStrain_Implementation(FName ContactId, FName Strain)
{
	AWeedShopGameState* GS = GetGS();
	if (!GS || !GS->GetContacts() || Strain.IsNone()) { return; }

	// Beste THC/kwaliteit van DEZE strain over je inventory + ALLE chests/shelves (Bud_ of Bag_, niet nat).
	float OffThc = -1.f, OffQual = -1.f;
	auto Consider = [&](FName Id, float Thc, float Ql)
	{
		const FString S = Id.ToString();
		if (S.StartsWith(TEXT("WetBud_"))) { return; }
		FName St;
		if (UInventoryComponent::IsBag(Id)) { St = UInventoryComponent::BagStrain(Id); }
		else if (S.StartsWith(TEXT("Bud_"))) { St = FName(*S.RightChop(4)); }
		else { return; }
		if (St != Strain) { return; }
		if (Thc > OffThc) { OffThc = Thc; OffQual = Ql; }
	};
	if (APawn* P = Cast<APawn>(GetOwner()))
	{
		if (UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			for (const FInventoryStack& St : Inv->GetStacks()) { Consider(St.ItemId, St.Quality, St.QualityPct); }
		}
	}
	if (UWorld* W = GetWorld())
	{
		// COMPETITIVE: beperk de shelf-scan tot de EIGEN gespiegelde kamer, anders lek/gebruik je de opslag
		// van de tegenstander. bJoiner EXPLICIET uit de owner-pawn (op de listen-server is de host-pawn
		// locally-controlled, de joiner-pawn niet) - net als BuildComponent::InCompHome. Buiten competitive
		// blijft de scan wereldwijd: gedeelde crew-voorraad is dan bedoeld.
		TArray<FBox> CompBoxes;
		if (GS->IsCompetitive())
		{
			const APawn* OwnerPawn = Cast<APawn>(GetOwner());
			const bool bJoiner = OwnerPawn && !OwnerPawn->IsLocallyControlled();
			if (ADoorRetrofitter* Retro = FindRetro()) { Retro->GetCompetitiveHomeBoxes(bJoiner, CompBoxes); }
		}
		const bool bRestrict = (CompBoxes.Num() > 0);
		for (TActorIterator<AStorageShelf> It(W); It; ++It)
		{
			if (bRestrict)
			{
				bool bInside = false;
				const FVector Loc = It->GetActorLocation();
				for (const FBox& B : CompBoxes) { if (B.IsInsideOrOn(Loc)) { bInside = true; break; } }
				if (!bInside) { continue; }
			}
			for (const FShelfStack& C : It->Contents) { Consider(C.ItemId, C.Thc, C.QualityPct); }
		}
	}
	// Competitive: server leidt de acterende speler af uit de owner-pawn; leeg buiten competitive.
	const APawn* Me = Cast<APawn>(GetOwner());
	const FString CallerId = (GS->IsCompetitive() && Me) ? USaveGameSubsystem::StablePlayerId(Me) : FString();
	GS->GetContacts()->ProposeAlternativeStrain(ContactId, Strain, OffThc, OffQual, CallerId);
}

void UPhoneClientComponent::SaveMapBorder()
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
	if (Marks.Num() < 2)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("Set at least 2 markers along the border (in order)"));
		return;
	}
	FString Out;
	for (const FVector& M : Marks) { Out += FString::Printf(TEXT("%.1f,%.1f,%.1f"), M.X, M.Y, M.Z) + LINE_TERMINATOR; }
	FFileHelper::SaveStringToFile(Out, *(FPaths::ProjectSavedDir() / TEXT("MapBorder.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	AMapBorder* Border = nullptr;
	for (TActorIterator<AMapBorder> It(W); It; ++It) { Border = *It; break; }
	if (!Border) { Border = W->SpawnActor<AMapBorder>(AMapBorder::StaticClass(), FTransform::Identity); }
	if (Border) { Border->Rebuild(); }
	UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Green, FString::Printf(TEXT("Map border saved (%d points)! Markers cleared"), Marks.Num()));
}

void UPhoneClientComponent::SaveNpcRoute()
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
	if (Marks.Num() < 2)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("Set at least 2 markers along the sidewalk (in order)"));
		return;
	}
	// TOEVOEGEN i.p.v. overschrijven: elke save is een eigen route-ring ("---" als scheiding),
	// zodat je meerdere loopgebieden kunt neerleggen. Eerste en laatste marker worden bij het
	// laden automatisch verbonden (gesloten ring, net als de map-border).
	FString Cur;
	FFileHelper::LoadFileToString(Cur, *WeedData::File(TEXT("NpcRoute.txt")));
	int32 NRoutes = 1;
	for (int32 ci = 0; ci < Cur.Len(); ++ci) { if (Cur[ci] == TEXT('-')) { ++NRoutes; ci += 2; } }
	if (!Cur.IsEmpty() && !Cur.EndsWith(LINE_TERMINATOR)) { Cur += LINE_TERMINATOR; }
	if (!Cur.IsEmpty()) { Cur += TEXT("---"); Cur += LINE_TERMINATOR; }
	for (const FVector& M : Marks) { Cur += FString::Printf(TEXT("%.1f,%.1f,%.1f"), M.X, M.Y, M.Z) + LINE_TERMINATOR; }
	FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("NpcRoute.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Green, FString::Printf(TEXT("NPC route %d saved (%d points, closed loop)! Markers cleared - restart applies"), NRoutes, Marks.Num()));
}

void UPhoneClientComponent::SaveNoBuildZone()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const FString MapPath = W->GetOutermost()->GetName();
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	// Hele marker-regels van deze map bewaren (BuildComponent leest er pos=(X,Y,Z) uit; paren = boxen).
	FString Add; int32 N = 0;
	for (const FString& Line : Lines)
	{
		if (Line.Contains(MapPath) && Line.Contains(TEXT("pos=("))) { Add += Line + LINE_TERMINATOR; ++N; }
	}
	if (N < 2)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 5.f, FColor::Orange, TEXT("Mark at least 2 corners (diagonal) of the wall/area with F9 first."));
		return;
	}
	// EIGEN bestand dat geen ander dev-tool leegmaakt; APPEND (in-memory) zodat meerdere muren/deuren stapelen.
	const FString File = FPaths::ProjectSavedDir() / TEXT("NoBuildZones.txt");
	FString Cur;
	FFileHelper::LoadFileToString(Cur, *File);
	if (!Cur.IsEmpty() && !Cur.EndsWith(LINE_TERMINATOR)) { Cur += LINE_TERMINATOR; }
	Cur += Add;
	FFileHelper::SaveStringToFile(Cur, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	// Markers wissen zodat het kladblok leeg blijft voor de volgende zone.
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Green,
		FString::Printf(TEXT("No-build zone saved (%d corners). Permanent - markers cleared."), N));
}

void UPhoneClientComponent::ClearNoBuildZone()
{
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("NoBuildZones.txt")));
	UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Cyan, TEXT("All no-build zones cleared."));
}

void UPhoneClientComponent::SaveStairsPath()
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
	if (Marks.Num() < 2)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("Set at least 2 markers along the indoor path (in order)"));
		return;
	}
	FString Cur;
	FFileHelper::LoadFileToString(Cur, *WeedData::File(TEXT("StairsPath.txt")));
	if (!Cur.IsEmpty() && !Cur.EndsWith(LINE_TERMINATOR)) { Cur += LINE_TERMINATOR; }
	if (!Cur.IsEmpty()) { Cur += TEXT("---"); Cur += LINE_TERMINATOR; }
	for (const FVector& M : Marks) { Cur += FString::Printf(TEXT("%.1f,%.1f,%.1f"), M.X, M.Y, M.Z) + LINE_TERMINATOR; }
	FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("StairsPath.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Green, FString::Printf(TEXT("Stairs path saved (%d points)! Restart applies the links"), Marks.Num()));
}

void UPhoneClientComponent::SaveChillSpots()
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
	if (Marks.Num() < 1)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("Set markers on the chill spots first"));
		return;
	}
	FString Cur;
	FFileHelper::LoadFileToString(Cur, *WeedData::File(TEXT("ChillSpots.txt")));
	if (!Cur.IsEmpty() && !Cur.EndsWith(LINE_TERMINATOR)) { Cur += LINE_TERMINATOR; }
	for (const FVector& M : Marks) { Cur += FString::Printf(TEXT("%.1f,%.1f,%.1f"), M.X, M.Y, M.Z) + LINE_TERMINATOR; }
	FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("ChillSpots.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Green, FString::Printf(TEXT("%d chill spots added! Restart applies"), Marks.Num()));
}

void UPhoneClientComponent::SaveShopSpots()
{
	UWorld* W = GetWorld();
	APawn* Pn = Cast<APawn>(GetOwner());
	if (!W || !Pn) { return; }
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
	if (Marks.Num() < 1)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("Stand where the counter goes (facing the customer side) and set a marker first"));
		return;
	}
	FString Cur;
	FFileHelper::LoadFileToString(Cur, *WeedData::File(TEXT("ShopSpots.txt")));
	const float Yaw = Pn->GetActorRotation().Yaw; // toonbank kijkt zoals jij keek (naar de klanten)
	if (!Cur.IsEmpty() && !Cur.EndsWith(LINE_TERMINATOR)) { Cur += LINE_TERMINATOR; }
	const int32 Kind = FMath::Clamp(SelectedShopKind, 0, 2); // de gekozen soort geldt voor deze hele save
	for (int32 i = 0; i < Marks.Num(); ++i)
	{
		Cur += FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f,%d"), Marks[i].X, Marks[i].Y, Marks[i].Z, Yaw, Kind) + LINE_TERMINATOR;
	}
	FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("ShopSpots.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(FString(), *(FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt")));
	static const TCHAR* KN[3] = { TEXT("Grow shop"), TEXT("Supplies"), TEXT("Furniture") };
	UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Green, FString::Printf(TEXT("%d %s shop(s) saved! Restart builds them"), Marks.Num(), KN[Kind]));
}

void UPhoneClientComponent::SetShopTypeInCrosshair()
{
	UWorld* W = GetWorld();
	APawn* Pn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pn ? Cast<APlayerController>(Pn->GetController()) : nullptr;
	if (!W || !PC) { return; }
	FVector VL; FRotator VR;
	PC->GetPlayerViewPoint(VL, VR);
	FHitResult Hit;
	FCollisionQueryParams Q; Q.AddIgnoredActor(Pn);
	const bool bHit = W->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 4000.f, ECC_Visibility, Q);
	const FVector Target = bHit ? Hit.ImpactPoint : VL + VR.Vector() * 1000.f;
	// Dichtstbijzijnde toonbank bij je kijk-punt.
	AStoreCounter* Best = nullptr; float BestD = 400.f;
	for (TActorIterator<AStoreCounter> It(W); It; ++It)
	{
		const float Dd = FVector::Dist(It->GetActorLocation(), Target);
		if (Dd < BestD) { BestD = Dd; Best = *It; }
	}
	if (!Best)
	{
		UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("No shop counter in crosshair"));
		return;
	}
	// Soort cyclen (Grow/Supplies/Furniture) + kleur + de prompt updaten.
	const int32 NewKind = (((int32)Best->Kind) + 1) % 3; // enum 0=Grow 1=Supplies 2=Furniture
	Best->Kind = (EShopKind)NewKind;
	const FLinearColor Sign = (NewKind == 0) ? FLinearColor(0.30f, 0.85f, 0.35f)
		: (NewKind == 1) ? FLinearColor(0.30f, 0.65f, 0.95f) : FLinearColor(0.65f, 0.45f, 0.85f);
	Best->SetupVisual(Sign);
	// In ShopSpots.txt de regel updaten waarvan de XY het dichtst bij deze toonbank ligt.
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *WeedData::File(TEXT("ShopSpots.txt")));
	int32 BestLine = -1; float BestLD = TNumericLimits<float>::Max();
	const FVector BL = Best->GetActorLocation();
	for (int32 li = 0; li < Lines.Num(); ++li)
	{
		TArray<FString> Pc; Lines[li].ParseIntoArray(Pc, TEXT(","));
		if (Pc.Num() < 5) { continue; }
		const float Dd = FVector::Dist2D(FVector(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), 0.f), FVector(BL.X, BL.Y, 0.f));
		if (Dd < BestLD) { BestLD = Dd; BestLine = li; }
	}
	if (BestLine >= 0)
	{
		TArray<FString> Pc; Lines[BestLine].ParseIntoArray(Pc, TEXT(","));
		Pc[4] = FString::FromInt(NewKind);
		Lines[BestLine] = FString::Join(Pc, TEXT(","));
		FString Out; for (const FString& L : Lines) { Out += L + LINE_TERMINATOR; }
		FFileHelper::SaveStringToFile(Out, *(FPaths::ProjectSavedDir() / TEXT("ShopSpots.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	static const TCHAR* KN[3] = { TEXT("Grow shop"), TEXT("Supplies"), TEXT("Furniture") };
	UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Cyan, FString::Printf(TEXT("Shop type -> %s"), KN[NewKind]));
}

void UPhoneClientComponent::ClearShopSpots()
{
	WeedData::DeleteFile(TEXT("ShopSpots.txt"));
	UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("Shop spots cleared (restart applies)"));
}

void UPhoneClientComponent::ClearChillSpots()
{
	WeedData::DeleteFile(TEXT("ChillSpots.txt"));
	UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("Chill spots cleared (restart applies)"));
}

void UPhoneClientComponent::ShowAllPaths()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	FlushPersistentDebugLines(W);
	auto ParseBlocks = [](const FString& FileName, TArray<TArray<FVector>>& Out)
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *WeedData::File(FileName));
		TArray<FVector> Cur;
		for (const FString& L : Lines)
		{
			if (L.TrimStartAndEnd().StartsWith(TEXT("---")))
			{
				if (Cur.Num() >= 2) { Out.Add(Cur); }
				Cur.Reset();
				continue;
			}
			TArray<FString> P;
			L.ParseIntoArray(P, TEXT(","));
			if (P.Num() >= 3) { Cur.Add(FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]))); }
		}
		if (Cur.Num() >= 2) { Out.Add(Cur); }
	};
	int32 NRoutes = 0, NChains = 0;
	// Loop-ringen: groen, gesloten (laatste -> eerste), iets boven de grond.
	{
		TArray<TArray<FVector>> Routes;
		ParseBlocks(TEXT("NpcRoute.txt"), Routes);
		for (const TArray<FVector>& R : Routes)
		{
			const int32 NSeg = (R.Num() >= 3) ? R.Num() : R.Num() - 1;
			for (int32 i = 0; i < NSeg; ++i)
			{
				const FVector A = R[i] + FVector(0.f, 0.f, 120.f);
				const FVector B = R[(i + 1) % R.Num()] + FVector(0.f, 0.f, 120.f);
				DrawDebugLine(W, A, B, FColor::Green, true, -1.f, 0, 14.f);
			}
			for (const FVector& P : R) { DrawDebugSphere(W, P + FVector(0.f, 0.f, 120.f), 45.f, 8, FColor::Green, true); }
			++NRoutes;
		}
	}
	// Gebouw-kettingen: oranje, open lijnen.
	{
		TArray<TArray<FVector>> Chains;
		ParseBlocks(TEXT("StairsPath.txt"), Chains);
		for (const TArray<FVector>& C : Chains)
		{
			for (int32 i = 0; i + 1 < C.Num(); ++i)
			{
				DrawDebugLine(W, C[i] + FVector(0.f, 0.f, 100.f), C[i + 1] + FVector(0.f, 0.f, 100.f), FColor::Orange, true, -1.f, 0, 14.f);
			}
			for (const FVector& P : C) { DrawDebugSphere(W, P + FVector(0.f, 0.f, 100.f), 40.f, 8, FColor::Orange, true); }
			++NChains;
		}
	}
	// Chill-plekken: cyaan bollen.
	int32 NChill = 0;
	{
		TArray<FString> CLines;
		FFileHelper::LoadFileToStringArray(CLines, *WeedData::File(TEXT("ChillSpots.txt")));
		for (const FString& CL : CLines)
		{
			TArray<FString> P;
			CL.ParseIntoArray(P, TEXT(","));
			if (P.Num() < 3) { continue; }
			DrawDebugSphere(W, FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])) + FVector(0.f, 0.f, 110.f), 55.f, 10, FColor::Cyan, true);
			++NChill;
		}
	}
	UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Cyan, FString::Printf(TEXT("Showing %d routes (green) + %d building paths (orange) + %d chill spots (cyan)"), NRoutes, NChains, NChill));
}

void UPhoneClientComponent::SaveHomeSpawn()
{
	UWorld* W = GetWorld();
	APawn* P = Cast<APawn>(GetOwner());
	if (!W || !P) { return; }
	// Geen marker nodig: JE STAAT er gewoon - huidige positie is de spawn-plek.
	const FVector L = P->GetActorLocation();
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("%.1f,%.1f,%.1f"), L.X, L.Y, L.Z),
		*(FPaths::ProjectSavedDir() / TEXT("HomeSpawn.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Green, TEXT("Home spawn saved - you'll start RIGHT HERE every session"));
}

void UPhoneClientComponent::DeletePathInCrosshair()
{
	UWorld* W = GetWorld();
	APawn* P = Cast<APawn>(GetOwner());
	APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!W || !PC) { return; }
	FVector VL;
	FRotator VR;
	PC->GetPlayerViewPoint(VL, VR);
	FHitResult Hit;
	FCollisionQueryParams Q;
	Q.AddIgnoredActor(P);
	const bool bHit = W->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 8000.f, ECC_Visibility, Q);
	const FVector Target = bHit ? Hit.ImpactPoint : VL + VR.Vector() * 1500.f;

	// Zoek over beide bestanden het pad-blok met het dichtstbijzijnde punt (max 6m van je kijk-punt).
	auto TryDelete = [&](const FString& FileName, const TCHAR* Label) -> bool
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *WeedData::File(FileName));
		TArray<TArray<FString>> Blocks;
		TArray<FString> Cur;
		for (const FString& L : Lines)
		{
			if (L.TrimStartAndEnd().StartsWith(TEXT("---")))
			{
				if (Cur.Num() > 0) { Blocks.Add(Cur); }
				Cur.Reset();
				continue;
			}
			if (!L.TrimStartAndEnd().IsEmpty()) { Cur.Add(L); }
		}
		if (Cur.Num() > 0) { Blocks.Add(Cur); }
		int32 BestBlock = -1;
		float BestD = 600.f;
		for (int32 bi = 0; bi < Blocks.Num(); ++bi)
		{
			for (const FString& L : Blocks[bi])
			{
				TArray<FString> Pc;
				L.ParseIntoArray(Pc, TEXT(","));
				if (Pc.Num() < 3) { continue; }
				const FVector Pt(FCString::Atof(*Pc[0]), FCString::Atof(*Pc[1]), FCString::Atof(*Pc[2]));
				const float Dd = FVector::Dist2D(Pt, Target);
				if (Dd < BestD) { BestD = Dd; BestBlock = bi; }
			}
		}
		if (BestBlock < 0) { return false; }
		const int32 NPts = Blocks[BestBlock].Num();
		Blocks.RemoveAt(BestBlock);
		FString Out;
		for (int32 bi = 0; bi < Blocks.Num(); ++bi)
		{
			if (bi > 0) { Out += TEXT("---"); Out += LINE_TERMINATOR; }
			for (const FString& L : Blocks[bi]) { Out += L; Out += LINE_TERMINATOR; }
		}
		FFileHelper::SaveStringToFile(Out, *(FPaths::ProjectSavedDir() / FileName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UWeedToast::NotifyPawn(GetOwner(), -1, 4.f, FColor::Green, FString::Printf(TEXT("%s deleted (%d points) - restart applies to walkers"), Label, NPts));
		ShowAllPaths(); // lijnen meteen hertekenen zodat je ziet dat 'ie weg is
		return true;
	};
	if (TryDelete(TEXT("NpcRoute.txt"), TEXT("Walk route"))) { return; }
	if (TryDelete(TEXT("StairsPath.txt"), TEXT("Stairs path"))) { return; }
	UWeedToast::NotifyPawn(GetOwner(), -1, 3.f, FColor::Orange, TEXT("No path point near crosshair (use Show all paths)"));
}

void UPhoneClientComponent::HideAllPaths()
{
	if (UWorld* W = GetWorld()) { FlushPersistentDebugLines(W); }
	UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::White, TEXT("Paths hidden"));
}

void UPhoneClientComponent::ClearStairsPath()
{
	WeedData::DeleteFile(TEXT("StairsPath.txt"));
	UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("Stairs paths cleared (restart applies)"));
}

void UPhoneClientComponent::ClearNpcRoute()
{
	WeedData::DeleteFile(TEXT("NpcRoute.txt"));
	UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("NPC route cleared (restart restores default points)"));
}

void UPhoneClientComponent::ClearMapBorder()
{
	WeedData::DeleteFile(TEXT("MapBorder.txt"));
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AMapBorder> It(W); It; ++It) { It->Rebuild(); }
	}
	UWeedToast::NotifyPawn(GetOwner(), -1, 2.5f, FColor::Orange, TEXT("Map border cleared"));
}
