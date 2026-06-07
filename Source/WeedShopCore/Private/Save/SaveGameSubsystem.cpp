#include "Save/SaveGameSubsystem.h"

#include "WeedShopCore.h"
#include "Save/WeedShopSaveGame.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/LevelComponent.h"
#include "Npc/NpcRegistryComponent.h"   // NPC-relaties opslaan/herstellen
#include "Phone/ContactsComponent.h"    // contacten + berichten opslaan/herstellen
#include "Progression/StoreComponent.h"
#include "UI/WeedToast.h"
#include "Cultivation/GrowPlant.h"
#include "Cultivation/DryingRack.h"
#include "World/StorageShelf.h"
#include "World/PackBench.h"
#include "World/Atm.h"
#include "Placement/PlaceableProp.h"
#include "World/CeilingLamp.h"             // plafondlampen opslaan/herstellen
#include "World/ProcessorMachine.h"        // hasj-machines opslaan/herstellen
#include "Cultivation/WaterCanComponent.h" // water in je fles
#include "World/HeatComponent.h"           // politie-heat
#include "EngineUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

void USaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	int32 Last = 0;
	GConfig->GetInt(TEXT("ThePlugSIM.Save"), TEXT("LastSlot"), Last, GGameIni);
	CurrentSlot = FMath::Clamp(Last, 0, NumSlots - 1);

	bAutosaveEnabled = true;
	GConfig->GetBool(TEXT("ThePlugSIM.Save"), TEXT("Autosave"), bAutosaveEnabled, GGameIni);

	// Speeltijd-meter starten (wordt overschreven bij Load/NewGame met de echte basis).
	PlaytimeBaseSeconds = 0.0;
	PlaytimeMark = FDateTime::UtcNow();
}

double USaveGameSubsystem::CurrentPlaytimeSeconds() const
{
	const FTimespan Elapsed = FDateTime::UtcNow() - PlaytimeMark;
	return PlaytimeBaseSeconds + FMath::Max(0.0, Elapsed.GetTotalSeconds());
}

void USaveGameSubsystem::SetAutosaveEnabled(bool bEnabled)
{
	bAutosaveEnabled = bEnabled;
	GConfig->SetBool(TEXT("ThePlugSIM.Save"), TEXT("Autosave"), bEnabled, GGameIni);
	GConfig->Flush(false, GGameIni);
}

FString USaveGameSubsystem::SlotNameFor(int32 Slot) const
{
	return FString::Printf(TEXT("%s_%d"), *SlotName, FMath::Clamp(Slot, 0, NumSlots - 1));
}

FString USaveGameSubsystem::AutoSlotNameFor(int32 Slot) const
{
	return FString::Printf(TEXT("%s_%d_auto"), *SlotName, FMath::Clamp(Slot, 0, NumSlots - 1));
}

FString USaveGameSubsystem::ResolveLoadName(bool bPreferNewest) const
{
	const FString Manual = SlotNameFor(CurrentSlot);
	const FString Auto = AutoSlotNameFor(CurrentSlot);
	const bool bHasManual = UGameplayStatics::DoesSaveGameExist(Manual, 0);
	const bool bHasAuto = UGameplayStatics::DoesSaveGameExist(Auto, 0);

	if (!bHasManual && !bHasAuto) { return FString(); }
	if (!bHasManual) { return Auto; }   // nog nooit handmatig opgeslagen -> de autosave
	if (!bHasAuto) { return Manual; }

	// Beide bestaan. Voor "Continue" pakken we het nieuwste; voor een echte Load altijd de
	// handmatige save (jouw echte save-punt), ook al is de autosave nieuwer.
	if (!bPreferNewest) { return Manual; }

	const UWeedShopSaveGame* M = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(Manual, 0));
	const UWeedShopSaveGame* A = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(Auto, 0));
	const FDateTime MT = M ? M->SavedAt : FDateTime(0);
	const FDateTime AT = A ? A->SavedAt : FDateTime(0);
	return (AT > MT) ? Auto : Manual;
}

void USaveGameSubsystem::SetSlot(int32 Slot)
{
	CurrentSlot = FMath::Clamp(Slot, 0, NumSlots - 1);
	GConfig->SetInt(TEXT("ThePlugSIM.Save"), TEXT("LastSlot"), CurrentSlot, GGameIni);
	GConfig->Flush(false, GGameIni);
}

bool USaveGameSubsystem::HasSaveInSlot(int32 Slot) const
{
	return UGameplayStatics::DoesSaveGameExist(SlotNameFor(Slot), 0)
		|| UGameplayStatics::DoesSaveGameExist(AutoSlotNameFor(Slot), 0);
}

bool USaveGameSubsystem::GetSlotInfo(int32 Slot, FString& OutSummary) const
{
	if (!HasSaveInSlot(Slot)) { return false; }
	// Toon de handmatige save als die er is, anders de autosave.
	const FString Name = UGameplayStatics::DoesSaveGameExist(SlotNameFor(Slot), 0) ? SlotNameFor(Slot) : AutoSlotNameFor(Slot);
	if (const UWeedShopSaveGame* S = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(Name, 0)))
	{
		OutSummary = FString::Printf(TEXT("Day %d  -  EUR %lld  -  %d player(s)"),
			S->DayNumber, (long long)((S->TotalEarnedCents) / 100), S->Players.Num());
		return true;
	}
	return false;
}

bool USaveGameSubsystem::HasManualSaveInSlot(int32 Slot) const
{
	return UGameplayStatics::DoesSaveGameExist(SlotNameFor(Slot), 0);
}

bool USaveGameSubsystem::HasAutoSaveInSlot(int32 Slot) const
{
	return UGameplayStatics::DoesSaveGameExist(AutoSlotNameFor(Slot), 0);
}

bool USaveGameSubsystem::FillSlotInfo(const FString& Name, FSaveSlotInfo& Out) const
{
	Out = FSaveSlotInfo();
	if (!UGameplayStatics::DoesSaveGameExist(Name, 0)) { return false; }
	const UWeedShopSaveGame* S = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(Name, 0));
	if (!S) { return false; }

	Out.bExists = true;
	Out.DayNumber = S->DayNumber;
	Out.CrewLevel = FMath::Max(1, S->CrewLevel);
	Out.PlaytimeSeconds = S->PlaytimeSeconds;
	Out.NumPlayers = S->Players.Num();
	Out.SavedAt = S->SavedAt;
	Out.bIsAutosave = S->bIsAutosave;

	// Totaal saldo = contant + bank, opgeteld over alle spelers (co-op).
	int64 Total = 0;
	for (const FPlayerSaveData& P : S->Players) { Total += P.CashCents + P.BankCents; }
	// Legacy v1: geen Players-array, maar host-velden.
	if (S->Players.Num() == 0) { Total += S->BalanceCents + S->BankCents; }
	Out.TotalCents = Total;
	return true;
}

bool USaveGameSubsystem::GetSlotDetails(int32 Slot, FSaveSlotInfo& Out) const
{
	// Toon de handmatige save als die er is, anders de autosave.
	const FString Name = HasManualSaveInSlot(Slot) ? SlotNameFor(Slot) : AutoSlotNameFor(Slot);
	return FillSlotInfo(Name, Out);
}

bool USaveGameSubsystem::GetSlotDetailsEx(int32 Slot, bool bAutosave, FSaveSlotInfo& Out) const
{
	return FillSlotInfo(bAutosave ? AutoSlotNameFor(Slot) : SlotNameFor(Slot), Out);
}

bool USaveGameSubsystem::GetMostRecentSaveTime(FDateTime& Out) const
{
	bool bAny = false;
	FDateTime Best(0);
	for (int32 s = 0; s < NumSlots; ++s)
	{
		for (const FString& Name : { SlotNameFor(s), AutoSlotNameFor(s) })
		{
			if (!UGameplayStatics::DoesSaveGameExist(Name, 0)) { continue; }
			if (const UWeedShopSaveGame* S = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(Name, 0)))
			{
				if (!bAny || S->SavedAt > Best) { Best = S->SavedAt; bAny = true; }
			}
		}
	}
	Out = Best;
	return bAny;
}

void USaveGameSubsystem::NewGameInSlot(int32 Slot)
{
	SetSlot(Slot);
	// Verse staat: wis dit slot (handmatig + autosave) zodat een Load straks niet de oude save pakt.
	if (UGameplayStatics::DoesSaveGameExist(SlotNameFor(Slot), 0)) { UGameplayStatics::DeleteGameInSlot(SlotNameFor(Slot), 0); }
	if (UGameplayStatics::DoesSaveGameExist(AutoSlotNameFor(Slot), 0)) { UGameplayStatics::DeleteGameInSlot(AutoSlotNameFor(Slot), 0); }
	Loaded = nullptr;
	RestoredPlayers.Reset();
	PlaytimeBaseSeconds = 0.0; // verse speeltijd
	PlaytimeMark = FDateTime::UtcNow();
}

bool USaveGameSubsystem::LoadSlot(int32 Slot)
{
	SetSlot(Slot);
	return LoadGame(false); // echte Load: altijd jouw handmatige save-punt
}

void USaveGameSubsystem::ReloadCurrentLevel(const FString& Options)
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	if (APlayerController* PC = W->GetFirstPlayerController()) { PC->SetPause(false); } // travel vanuit pauze
	const FString LevelName = UGameplayStatics::GetCurrentLevelName(W, true);
	UE_LOG(LogWeedShop, Log, TEXT("Level herladen voor save-actie: %s (opts='%s')"), *LevelName, *Options);
	WeedShop_RequestGameLoadingScreen(); // toon het laadscherm voor deze in-game transitie
	UGameplayStatics::OpenLevel(W, FName(*LevelName), true, Options);
}

void USaveGameSubsystem::HostNewGameLan(int32 Slot, EGameStartMode Mode)
{
	// Zelfde verse start als New Game, maar herlaad het level ALS LISTEN-SERVER (?listen). Zo kan een
	// vriend met JoinLan() direct binnenkomen. De Pending-staat overleeft de reload (GameInstance-subsystem).
	SetSlot(Slot);
	if (UGameplayStatics::DoesSaveGameExist(SlotNameFor(Slot), 0)) { UGameplayStatics::DeleteGameInSlot(SlotNameFor(Slot), 0); }
	if (UGameplayStatics::DoesSaveGameExist(AutoSlotNameFor(Slot), 0)) { UGameplayStatics::DeleteGameInSlot(AutoSlotNameFor(Slot), 0); }
	Loaded = nullptr;
	RestoredPlayers.Reset();
	PlaytimeBaseSeconds = 0.0;
	PlaytimeMark = FDateTime::UtcNow();
	Pending = EPending::Fresh;
	PendingStartMode = Mode;
	PendingLoadName.Reset();
	ReloadCurrentLevel(TEXT("listen"));
}

void USaveGameSubsystem::JoinLan(const FString& IpPort)
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	FString Addr = IpPort.TrimStartAndEnd();
	if (Addr.IsEmpty()) { return; }
	if (!Addr.Contains(TEXT(":"))) { Addr += TEXT(":7777"); } // standaard UE-poort
	if (APlayerController* PC = W->GetFirstPlayerController()) { PC->SetPause(false); }
	WeedShop_RequestGameLoadingScreen(); // laadscherm tijdens het verbinden
	UE_LOG(LogWeedShop, Log, TEXT("LAN join -> %s"), *Addr);
	if (APlayerController* PC = W->GetFirstPlayerController())
	{
		PC->ClientTravel(Addr, ETravelType::TRAVEL_Absolute);
	}
}

void USaveGameSubsystem::RequestNewGame(int32 Slot, EGameStartMode Mode)
{
	SetSlot(Slot);
	if (UGameplayStatics::DoesSaveGameExist(SlotNameFor(Slot), 0)) { UGameplayStatics::DeleteGameInSlot(SlotNameFor(Slot), 0); }
	if (UGameplayStatics::DoesSaveGameExist(AutoSlotNameFor(Slot), 0)) { UGameplayStatics::DeleteGameInSlot(AutoSlotNameFor(Slot), 0); }
	Loaded = nullptr;
	RestoredPlayers.Reset();
	PlaytimeBaseSeconds = 0.0;
	PlaytimeMark = FDateTime::UtcNow();
	Pending = EPending::Fresh;
	PendingStartMode = Mode;
	PendingLoadName.Reset();
	ReloadCurrentLevel();
}

bool USaveGameSubsystem::RequestLoad(int32 Slot, bool bAutosave)
{
	FString Name = bAutosave ? AutoSlotNameFor(Slot) : SlotNameFor(Slot);
	// Bestaat de gevraagde variant niet (bv. slot heeft alleen een autosave en geen handmatige
	// save)? Val dan terug op de andere variant, zodat de hoofd-knop altijd laadt wat er is.
	if (!UGameplayStatics::DoesSaveGameExist(Name, 0))
	{
		const FString Other = bAutosave ? SlotNameFor(Slot) : AutoSlotNameFor(Slot);
		if (UGameplayStatics::DoesSaveGameExist(Other, 0)) { Name = Other; }
		else { return false; }
	}
	SetSlot(Slot);
	Pending = EPending::Load;
	PendingLoadName = Name;
	ReloadCurrentLevel();
	return true;
}

bool USaveGameSubsystem::RequestContinue()
{
	int32 Target = HasSaveInSlot(CurrentSlot) ? CurrentSlot : INDEX_NONE;
	if (Target == INDEX_NONE) { for (int32 i = 0; i < NumSlots; ++i) { if (HasSaveInSlot(i)) { Target = i; break; } } }
	if (Target == INDEX_NONE) { return false; }
	SetSlot(Target);
	const FString Name = ResolveLoadName(true); // nieuwste van handmatig/autosave
	if (Name.IsEmpty()) { return false; }
	Pending = EPending::Load;
	PendingLoadName = Name;
	ReloadCurrentLevel();
	return true;
}

bool USaveGameSubsystem::RunPendingOnWorldReady()
{
	if (Pending == EPending::Fresh)
	{
		Pending = EPending::None;
		ApplyStartMode(PendingStartMode); // geld + items van de gekozen modus
		PendingStartMode = EGameStartMode::Normal;
		return true; // verder is een verse wereld = map-default
	}
	if (Pending == EPending::Load)
	{
		Pending = EPending::None;
		const FString Name = PendingLoadName;
		PendingLoadName.Reset();
		LoadGameFromName(Name);
		return true;
	}
	return false;
}

void USaveGameSubsystem::ApplyStartMode(EGameStartMode Mode)
{
	AWeedShopGameState* GS = GetWeedGameState();
	UWorld* World = GS ? GS->GetWorld() : nullptr;
	APawn* P = (World && World->GetFirstPlayerController()) ? World->GetFirstPlayerController()->GetPawn() : nullptr;

	// Normale start: een kleine startkit + EUR 420 (geen free-build, geen max level).
	if (Mode == EGameStartMode::Normal)
	{
		if (!P) { return; }
		if (UEconomyComponent* Econ = P->FindComponentByClass<UEconomyComponent>())
		{
			Econ->SetBalanceCents(15000); // EUR 150 cash
		}
		if (UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			Inv->AddItem(FName(TEXT("Papers_Small")), 10);
			Inv->AddItem(FName(TEXT("Bud_SilverHaze")), 20, 13.f, 70.f); // 20g gedroogde Silver Haze
		}
		return;
	}

	if (GS) { GS->SetFreeBuild(true); } // testing/sandbox: overal bouwen toegestaan
	if (!P) { return; }

	const bool bSandbox = (Mode == EGameStartMode::Sandbox);

	// Geld.
	if (UEconomyComponent* Econ = P->FindComponentByClass<UEconomyComponent>())
	{
		Econ->SetBalanceCents(bSandbox ? 100000000 : 500000); // Sandbox EUR 1.000.000 / Testing EUR 5.000
		Econ->SetBankCents(bSandbox ? 100000000 : 500000);
	}

	// Level: Testing en Sandbox starten op max level (alles ontgrendeld om te testen).
	if (ULevelComponent* Lv = GS->GetLeveling())
	{
		Lv->GrantLevel(ULevelComponent::MaxLevel);
	}

	// Starter-items.
	if (UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
	{
		auto Give = [Inv](const TCHAR* Id, int32 N) { Inv->AddItem(FName(Id), N); };
		if (bSandbox)
		{
			// Sandbox = NETTE set i.p.v. een volgepropte inventory. Eerst leeg (anders stapelt 'ie op de
			// gewone starter), dan een standaard starter + precies de plaatsbare meubels die je nodig hebt
			// om de furniture-templates in te richten (free-build = overal plaatsen).
			Inv->ClearAll();
			Give(TEXT("Papers_Small"), 5);
			Give(TEXT("Soil_Basic"), 5);
			Give(TEXT("WaterBottle_Plastic"), 2);
			Give(TEXT("Pot_Clay"), 2);
			Give(TEXT("Cont_Bag5"), 10);
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
			Give(TEXT("Cont_Bag5"),           10);
			Give(TEXT("Cont_Jar10"),          5);
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
	}

	// Dev-modes: warm alle NPC's op (goede stats + ontgrendeld) + zet wat contacten in de telefoon,
	// zodat je meteen overal kunt dealen/appen voor grondig testen.
	if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry())
	{
		Reg->WarmAllForTesting(GS->GetContacts());
	}

	UWeedToast::Notify(-1, 5.f, FColor::Green, bSandbox
		? TEXT("SANDBOX - loaded with cash + a full starter kit.")
		: TEXT("TESTING - good NPC stats, contacts + starter items added."));
}

bool USaveGameSubsystem::QuickContinue()
{
	// Continue: pak het nieuwste (handmatig of autosave) zodat je verdergaat waar je was.
	if (HasSaveInSlot(CurrentSlot)) { return LoadGame(true); }
	for (int32 i = 0; i < NumSlots; ++i) { if (HasSaveInSlot(i)) { SetSlot(i); return LoadGame(true); } }
	return false;
}

AWeedShopGameState* USaveGameSubsystem::GetWeedGameState() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return World ? World->GetGameState<AWeedShopGameState>() : nullptr;
}

bool USaveGameSubsystem::HasAuthorityWorld() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return World && World->GetNetMode() != NM_Client; // alleen host/server schrijft + herstelt
}

bool USaveGameSubsystem::HasSave() const
{
	return HasSaveInSlot(CurrentSlot);
}

void USaveGameSubsystem::PlayerKeys(const APawn* Pawn, FString& OutId, FString& OutName)
{
	OutId.Empty();
	OutName = TEXT("Player");
	if (!Pawn) { return; }
	if (const APlayerState* PS = Pawn->GetPlayerState())
	{
		const FString N = PS->GetPlayerName();
		if (!N.IsEmpty()) { OutName = N; }
		// Stabiele platform-id (Steam/EOS/...). Offline/PIE is dit meestal ongeldig -> leeg.
		const FUniqueNetIdRepl& Repl = PS->GetUniqueId();
		if (Repl.IsValid())
		{
			const FString IdStr = Repl->ToString();
			if (!IdStr.IsEmpty() && IdStr != TEXT("INVALID")) { OutId = IdStr; }
		}
	}
}

bool USaveGameSubsystem::Matches(const FPlayerSaveData& Rec, const FString& Id, const FString& Name)
{
	// Heb je een stabiele id, match daarop (naam mag wijzigen). Anders (offline) op naam.
	if (!Id.IsEmpty()) { return Rec.PlayerId == Id; }
	return Rec.PlayerId.IsEmpty() && Rec.PlayerName == Name;
}

void USaveGameSubsystem::GatherPlayer(APawn* Pawn, FPlayerSaveData& Out) const
{
	Out = FPlayerSaveData();
	PlayerKeys(Pawn, Out.PlayerId, Out.PlayerName);
	if (!Pawn) { return; }

	if (const UEconomyComponent* E = Pawn->FindComponentByClass<UEconomyComponent>())
	{
		Out.CashCents = E->GetBalanceCents();
		Out.BankCents = E->GetBankCents();
		Out.DepositedTodayCents = E->GetDepositedTodayCents();
		Out.DepositDay = E->GetDepositDay();
		Out.TransfersToday = E->GetTransfersToday();
	}
	if (const UPhoneClientComponent* Ph = Pawn->FindComponentByClass<UPhoneClientComponent>())
	{
		Out.bBankAppUnlocked = Ph->IsBankAppUnlocked();
		Out.OwnedHomes = Ph->GetOwnedHomes();
		Out.ActiveHome = Ph->GetActiveHome();
		Out.RentDueDay = Ph->GetRentDueDay();
		Out.bRentIntroShown = Ph->WasRentIntroShown();
		for (const UPhoneClientComponent::FPendingDelivery& D : Ph->GetPendingDeliveries())
		{
			FSavePendingDelivery P; P.Ids = D.Ids; P.Qtys = D.Qtys; Out.Pending.Add(P);
		}
	}
	if (const UInventoryComponent* Inv = Pawn->FindComponentByClass<UInventoryComponent>())
	{
		for (const FInventoryStack& S : Inv->GetStacks())
		{
			if (S.ItemId == FName(TEXT("Cash")) || S.Quantity <= 0) { continue; } // cash = afgeleid van economy
			FInvSaveItem It; It.ItemId = S.ItemId; It.Quantity = S.Quantity; It.Thc = S.Quality; It.QualityPct = S.QualityPct;
			It.GridCell = Inv->GetStackCell(S.StackId); // bewaar de exacte slot-positie
			Out.Items.Add(It);
		}
		// Hotbar: per slot de grid-cel van de toegewezen stapel (stabiel over reload; -1 = leeg).
		for (int32 h = 0; h < UInventoryComponent::HotbarSize; ++h)
		{
			const int32 Sid = Inv->GetHotbarStackId(h);
			Out.HotbarCells.Add(Sid != 0 ? Inv->GetStackCell(Sid) : -1);
		}
		Out.ActiveSlot = Inv->GetActiveSlot();
	}
	if (const UWaterCanComponent* Can = Pawn->FindComponentByClass<UWaterCanComponent>())
	{
		Out.WaterCharges = Can->GetCharges();
	}

	// Sla op waar de speler staat + kijkt (kijkrichting van de controller, niet de body).
	Out.bHasTransform = true;
	Out.Location = Pawn->GetActorLocation();
	Out.Rotation = Pawn->GetController() ? Pawn->GetController()->GetControlRotation() : Pawn->GetActorRotation();
}

void USaveGameSubsystem::ApplyPlayer(APawn* Pawn, const FPlayerSaveData& Data)
{
	if (!Pawn) { return; }
	if (UEconomyComponent* E = Pawn->FindComponentByClass<UEconomyComponent>())
	{
		E->SetBalanceCents(Data.CashCents);
		E->SetBankCents(Data.BankCents);
		E->RestoreDailyLimits(Data.DepositedTodayCents, Data.DepositDay, Data.TransfersToday);
	}
	if (UPhoneClientComponent* Ph = Pawn->FindComponentByClass<UPhoneClientComponent>())
	{
		Ph->SetBankAppUnlocked(Data.bBankAppUnlocked);
		Ph->RestoreProperty(Data.OwnedHomes, Data.ActiveHome);
		Ph->RestoreRent(Data.RentDueDay, Data.bRentIntroShown);
		for (const FSavePendingDelivery& P : Data.Pending) { Ph->RestoreDeliverInstant(P.Ids, P.Qtys); }
	}
	if (UInventoryComponent* Inv = Pawn->FindComponentByClass<UInventoryComponent>())
	{
		// Zet de stacks EXACT terug op hun opgeslagen slot (geen merge/sortering -> slots wisselen niet).
		TArray<FInventoryStack> RestoredStacks; TArray<int32> Cells;
		RestoredStacks.Reserve(Data.Items.Num()); Cells.Reserve(Data.Items.Num());
		for (const FInvSaveItem& It : Data.Items)
		{
			FInventoryStack S; S.ItemId = It.ItemId; S.Quantity = It.Quantity; S.Quality = It.Thc; S.QualityPct = It.QualityPct;
			RestoredStacks.Add(S); Cells.Add(It.GridCell);
		}
		Inv->RestoreStacksAndGrid(RestoredStacks, Cells);
		// Hotbar terugzetten: zoek per opgeslagen cel de (nieuwe) stapel-id en wijs 'm aan het slot toe.
		for (int32 h = 0; h < Data.HotbarCells.Num() && h < UInventoryComponent::HotbarSize; ++h)
		{
			const int32 Cell = Data.HotbarCells[h];
			if (Cell < 0) { continue; }
			const int32 Sid = Inv->GetStackIdAtCell(Cell);
			if (Sid != 0) { Inv->AssignHotbarStack(h, Sid); }
		}
		Inv->SetActiveSlot(Data.ActiveSlot);
	}
	if (UWaterCanComponent* Can = Pawn->FindComponentByClass<UWaterCanComponent>())
	{
		Can->RestoreCharges(Data.WaterCharges);
	}

	// Zet de speler terug op de opgeslagen plek + kijkrichting (echte "ga naar het save-punt").
	if (Data.bHasTransform)
	{
		// BELANGRIJK: de ACTOR (capsule + body) mag alleen YAW krijgen. De opgeslagen rotatie is de
		// kijkrichting van de controller en bevat PITCH. Zou je die pitch op de actor zetten, dan
		// staat de capsule scheef: hij detecteert geen vloer meer (eeuwig vallen na een sprong) en de
		// body wijst niet recht omlaag. De pitch hoort alleen op de camera (control rotation).
		const FRotator YawOnly(0.f, Data.Rotation.Yaw, 0.f);
		Pawn->TeleportTo(Data.Location, YawOnly, false, true);
		if (AController* C = Pawn->GetController())
		{
			C->SetControlRotation(Data.Rotation); // volledige kijkrichting (incl. pitch) op de camera
		}
	}
}

bool USaveGameSubsystem::SaveGame(bool bAutosave)
{
	if (!HasAuthorityWorld()) { return false; }
	AWeedShopGameState* GS = GetWeedGameState();
	if (!GS) { UE_LOG(LogWeedShop, Warning, TEXT("SaveGame: geen GameState.")); return false; }
	UWorld* World = GS->GetWorld();

	UWeedShopSaveGame* Save = Cast<UWeedShopSaveGame>(UGameplayStatics::CreateSaveGameObject(UWeedShopSaveGame::StaticClass()));
	if (!Save) { return false; }

	// Handmatige save -> echte slot; autosave -> apart bestand (overschrijft je echte save NIET).
	const FString TargetName = bAutosave ? AutoSlotNameFor(CurrentSlot) : SlotNameFor(CurrentSlot);

	// Bestaande spelers behouden (zo blijft een co-op vriend die nu offline is bewaard).
	UWeedShopSaveGame* Prev = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(TargetName, 0));
	if (!Prev)
	{
		// Eerste schrijf naar dit bestand: erf de spelerslijst van het andere bestand in dit slot.
		const FString OtherName = bAutosave ? SlotNameFor(CurrentSlot) : AutoSlotNameFor(CurrentSlot);
		Prev = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(OtherName, 0));
	}
	if (Prev) { Save->Players = Prev->Players; }

	Save->SavedAt = FDateTime::UtcNow();
	Save->bIsAutosave = bAutosave;
	Save->PlaytimeSeconds = CurrentPlaytimeSeconds();
	if (const ULevelComponent* Lv = GS->GetLeveling()) { Save->CrewLevel = Lv->GetLevel(); Save->CrewXP = Lv->GetCurrentXP(); }
	Save->bFreeBuild = GS->IsFreeBuild();

	// Gedeelde wereld-staat.
	if (const UDayCycleComponent* Day = GS->GetDayCycle()) { Save->TimeOfDaySeconds = Day->GetTimeOfDaySeconds(); Save->DayNumber = Day->GetDayNumber(); }
	if (const UMilestoneComponent* Ms = GS->GetMilestones()) { Save->TotalEarnedCents = Ms->GetTotalEarnedCents(); Save->MilestonePhase = (uint8)Ms->GetCurrentPhase(); }
	if (const UUpgradeComponent* Up = GS->GetUpgrades()) { Save->PurchasedUpgrades = Up->GetPurchasedIds(); }
	// NPC-relaties + telefoon-contacten/berichten (waren voorheen niet opgeslagen).
	if (const UNpcRegistryComponent* Reg = GS->GetNpcRegistry()) { Save->Npcs = Reg->GetStatesForSave(); }
	if (const UContactsComponent* Con = GS->GetContacts()) { Save->Contacts = Con->GetContacts(); Save->Messages = Con->GetMessages(); }
	if (const UHeatComponent* Ht = GS->GetHeat()) { Save->Heat = Ht->GetHeat(); Save->HeatEventTimer = Ht->GetEventTimer(); Save->HeatLastEventDay = Ht->GetLastEventDay(); }

	// Geplaatste wereld-objecten (potten/planten, shelves/chests, rekken, tafels, meubels, ATM).
	GatherPlaced(World, Save->Placed);

	// Alle nu verbonden spelers (op username) upserten.
	int32 NumPlayers = 0;
	if (World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
			if (!P) { continue; }
			FPlayerSaveData Data; GatherPlayer(P, Data);
			const int32 Idx = Save->Players.IndexOfByPredicate([&](const FPlayerSaveData& E) { return Matches(E, Data.PlayerId, Data.PlayerName); });
			if (Idx != INDEX_NONE) { Save->Players[Idx] = Data; } else { Save->Players.Add(Data); }
			++NumPlayers;
		}
	}

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, TargetName, 0);
	if (bOk) { Loaded = Save; GS->NotifySaved(); } // cache + save-indicator bij alle spelers
	UE_LOG(LogWeedShop, Log, TEXT("SaveGame %s (%s): %d speler(s), dag %d, fase %d"),
		bOk ? TEXT("OK") : TEXT("MISLUKT"), bAutosave ? TEXT("auto") : TEXT("handmatig"), NumPlayers, Save->DayNumber, Save->MilestonePhase);
	return bOk;
}

bool USaveGameSubsystem::LoadGame(bool bPreferNewest)
{
	return LoadGameFromName(ResolveLoadName(bPreferNewest));
}

bool USaveGameSubsystem::LoadSlotSpecific(int32 Slot, bool bAutosave)
{
	SetSlot(Slot);
	return LoadGameFromName(bAutosave ? AutoSlotNameFor(Slot) : SlotNameFor(Slot));
}

bool USaveGameSubsystem::LoadGameFromName(const FString& LoadName)
{
	if (!HasAuthorityWorld()) { return false; }
	AWeedShopGameState* GS = GetWeedGameState();
	if (!GS) { return false; }
	UWorld* World = GS->GetWorld();

	if (LoadName.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(LoadName, 0)) { return false; }
	UWeedShopSaveGame* Save = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(LoadName, 0));
	if (!Save) { return false; }
	Loaded = Save;
	RestoredPlayers.Reset();
	PlaytimeBaseSeconds = Save->PlaytimeSeconds; // speeltijd verder tellen vanaf de save
	PlaytimeMark = FDateTime::UtcNow();

	// Gedeelde staat terugzetten.
	GS->SetFreeBuild(Save->bFreeBuild);
	if (UDayCycleComponent* Day = GS->GetDayCycle()) { Day->SetTimeOfDaySeconds(Save->TimeOfDaySeconds); }
	if (UMilestoneComponent* Ms = GS->GetMilestones()) { Ms->RestoreState(Save->TotalEarnedCents, Save->MilestonePhase); }
	if (UUpgradeComponent* Up = GS->GetUpgrades()) { Up->RestorePurchased(Save->PurchasedUpgrades); }
	// Level + XP, NPC-relaties en telefoon-contacten/berichten terugzetten (waren kwijt na reload).
	if (ULevelComponent* Lv = GS->GetLeveling()) { Lv->RestoreLevel(Save->CrewLevel, Save->CrewXP); }
	if (UNpcRegistryComponent* Reg = GS->GetNpcRegistry()) { Reg->RestoreStates(Save->Npcs); }
	if (UContactsComponent* Con = GS->GetContacts()) { Con->RestoreContacts(Save->Contacts, Save->Messages); }
	if (UHeatComponent* Ht = GS->GetHeat()) { Ht->RestoreHeat(Save->Heat); Ht->RestoreEventState(Save->HeatEventTimer, Save->HeatLastEventDay); }

	// Geplaatste wereld-objecten opnieuw opbouwen (vervangt de huidige set).
	RespawnPlaced(World, Save->Placed);

	// Legacy-save (v1: alleen host-cash/bank, geen Players-array) -> naar de host-speler.
	const bool bLegacy = (Save->Players.Num() == 0) && (Save->BalanceCents != 0 || Save->BankCents != 0 || Save->bBankAppUnlocked);
	if (bLegacy && World)
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* P = PC->GetPawn())
			{
				FString Id, Name; PlayerKeys(P, Id, Name);
				FPlayerSaveData D; D.CashCents = Save->BalanceCents; D.BankCents = Save->BankCents; D.bBankAppUnlocked = Save->bBankAppUnlocked;
				ApplyPlayer(P, D); RestoredPlayers.Add(Id.IsEmpty() ? Name : Id);
			}
		}
	}

	// Alle nu verbonden spelers herstellen (op username).
	if (World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
			if (P) { RestorePlayerByPawn(P); }
		}
	}

	GS->NotifyLoaded(); // "Loaded"-melding bij alle spelers
	UE_LOG(LogWeedShop, Log, TEXT("LoadGame: %d speler(s) in save, dag %d hersteld."), Save->Players.Num(), Save->DayNumber);
	return true;
}

void USaveGameSubsystem::GatherPlaced(UWorld* World, TArray<FPlacedObjectSave>& Out) const
{
	if (!World) { return; }

	for (TActorIterator<AGrowPlant> It(World); It; ++It)
	{
		FPlacedObjectSave O; O.Kind = 1; O.ItemId = It->GetPotTier();
		O.Location = It->GetActorLocation(); O.Rotation = It->GetActorRotation();
		FGrowPlantState St; It->CaptureState(St);
		O.PotUpgradeMask = St.PotUpgradeMask; O.SoilId = St.SoilId; O.SoilUsesLeft = St.SoilUsesLeft;
		O.CareMultiplier = St.CareMultiplier; O.CareAvg = St.CareAvg; O.WaterLevel = St.WaterLevel;
		O.FertYieldMult = St.FertYieldMult;
		for (int32 i = 0; i < St.SlotStrain.Num(); ++i)
		{
			FSavePlantSlot S; S.Strain = St.SlotStrain[i];
			S.Growth = St.SlotGrowth.IsValidIndex(i) ? St.SlotGrowth[i] : 0.f;
			S.Phase = St.SlotPhase.IsValidIndex(i) ? St.SlotPhase[i] : 0;
			S.Afflict = St.SlotAfflict.IsValidIndex(i) ? St.SlotAfflict[i] : 0;
			S.AfflictTime = St.SlotAfflictTime.IsValidIndex(i) ? St.SlotAfflictTime[i] : 0.f;
			O.Slots.Add(S);
		}
		Out.Add(O);
	}
	for (TActorIterator<AStorageShelf> It(World); It; ++It)
	{
		FPlacedObjectSave O; O.Kind = 4; O.ItemId = It->ShelfTier;
		O.Location = It->GetActorLocation(); O.Rotation = It->GetActorRotation();
		for (const FShelfStack& S : It->Contents) { FSaveStack T; T.ItemId = S.ItemId; T.Quantity = S.Quantity; T.Thc = S.Thc; T.QualityPct = S.QualityPct; O.ShelfItems.Add(T); }
		Out.Add(O);
	}
	for (TActorIterator<ADryingRack> It(World); It; ++It)
	{
		FPlacedObjectSave O; O.Kind = 2; O.ItemId = It->RackTier;
		O.Location = It->GetActorLocation(); O.Rotation = It->GetActorRotation();
		for (const FDryEntry& E : It->GetEntries()) { FSaveDry D; D.DryItemId = E.DryItemId; D.Quantity = E.Quantity; D.Thc = E.Thc; D.Quality = E.Quality; D.Elapsed = E.Elapsed; D.bDone = E.bDone; D.OverTime = E.OverTime; O.DryEntries.Add(D); }
		Out.Add(O);
	}
	for (TActorIterator<APackBench> It(World); It; ++It)
	{
		FPlacedObjectSave O; O.Kind = 3; O.ItemId = It->BenchTier;
		O.Location = It->GetActorLocation(); O.Rotation = It->GetActorRotation(); Out.Add(O);
	}
	for (TActorIterator<AAtm> It(World); It; ++It)
	{
		FPlacedObjectSave O; O.Kind = 5; O.ItemId = FName(TEXT("Atm"));
		O.Location = It->GetActorLocation(); O.Rotation = It->GetActorRotation(); Out.Add(O);
	}
	for (TActorIterator<APlaceableProp> It(World); It; ++It)
	{
		FPlacedObjectSave O; O.Kind = 0; O.ItemId = It->ItemId;
		O.Location = It->GetActorLocation(); O.Rotation = It->GetActorRotation(); Out.Add(O);
	}
	// Plafondlampen zijn een eigen actor (ACeilingLamp), geen APlaceableProp -> apart opslaan (kind 6).
	for (TActorIterator<ACeilingLamp> It(World); It; ++It)
	{
		FPlacedObjectSave O; O.Kind = 6; O.ItemId = FName(TEXT("Lamp_Ceiling"));
		O.Location = It->GetActorLocation(); O.Rotation = It->GetActorRotation(); Out.Add(O);
	}
	// Hasj-machines (mesh/press): tier + lopende batches (hergebruik de DryEntries-velden, kind 7).
	for (TActorIterator<AProcessorMachine> It(World); It; ++It)
	{
		FPlacedObjectSave O; O.Kind = 7; O.ItemId = It->MachineTier;
		O.Location = It->GetActorLocation(); O.Rotation = It->GetActorRotation();
		for (const FProcEntry& E : It->GetEntries()) { FSaveDry D; D.DryItemId = E.OutItemId; D.Quantity = E.Quantity; D.Thc = E.Thc; D.Quality = E.Quality; D.Elapsed = E.Elapsed; D.bDone = E.bDone; O.DryEntries.Add(D); }
		Out.Add(O);
	}
}

void USaveGameSubsystem::RespawnPlaced(UWorld* World, const TArray<FPlacedObjectSave>& In)
{
	if (!World || In.Num() == 0) { return; }

	// Bestaande placeables verwijderen (we vervangen ze door de opgeslagen set).
	TArray<AActor*> ToKill;
	for (TActorIterator<AGrowPlant> It(World); It; ++It) { ToKill.Add(*It); }
	for (TActorIterator<AStorageShelf> It(World); It; ++It) { ToKill.Add(*It); }
	for (TActorIterator<ADryingRack> It(World); It; ++It) { ToKill.Add(*It); }
	for (TActorIterator<APackBench> It(World); It; ++It) { ToKill.Add(*It); }
	for (TActorIterator<AAtm> It(World); It; ++It) { ToKill.Add(*It); }
	for (TActorIterator<APlaceableProp> It(World); It; ++It) { ToKill.Add(*It); }
	for (TActorIterator<ACeilingLamp> It(World); It; ++It) { ToKill.Add(*It); }
	for (TActorIterator<AProcessorMachine> It(World); It; ++It) { ToKill.Add(*It); }
	for (AActor* A : ToKill) { if (A) { A->Destroy(); } }

	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FPlacedObjectSave& O : In)
	{
		const FTransform TM(O.Rotation, O.Location);
		switch (O.Kind)
		{
		case 1: // pot/plant
		{
			AGrowPlant* P = World->SpawnActorDeferred<AGrowPlant>(AGrowPlant::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (P) { P->PotTier = O.ItemId; P->FinishSpawning(TM);
				FGrowPlantState St; St.PotUpgradeMask = O.PotUpgradeMask; St.SoilId = O.SoilId; St.SoilUsesLeft = O.SoilUsesLeft;
				St.CareMultiplier = O.CareMultiplier; St.CareAvg = O.CareAvg; St.WaterLevel = O.WaterLevel;
				St.FertYieldMult = O.FertYieldMult;
				for (const FSavePlantSlot& S : O.Slots) { St.SlotStrain.Add(S.Strain); St.SlotGrowth.Add(S.Growth); St.SlotPhase.Add(S.Phase); St.SlotAfflict.Add(S.Afflict); St.SlotAfflictTime.Add(S.AfflictTime); }
				P->RestoreState(St);
			}
			break;
		}
		case 4: // shelf/chest
		{
			AStorageShelf* S = World->SpawnActorDeferred<AStorageShelf>(AStorageShelf::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (S) { S->ShelfTier = O.ItemId; S->FinishSpawning(TM);
				S->Contents.Reset();
				for (const FSaveStack& T : O.ShelfItems) { FShelfStack St; St.ItemId = T.ItemId; St.Quantity = T.Quantity; St.Thc = T.Thc; St.QualityPct = T.QualityPct; S->Contents.Add(St); }
			}
			break;
		}
		case 2: // droogrek
		{
			ADryingRack* R = World->SpawnActorDeferred<ADryingRack>(ADryingRack::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (R) { R->RackTier = O.ItemId; R->FinishSpawning(TM);
				TArray<FDryEntry> Es;
				for (const FSaveDry& D : O.DryEntries) { FDryEntry E; E.DryItemId = D.DryItemId; E.Quantity = D.Quantity; E.Thc = D.Thc; E.Quality = D.Quality; E.Elapsed = D.Elapsed; E.bDone = D.bDone; E.OverTime = D.OverTime; Es.Add(E); }
				R->RestoreEntries(Es);
			}
			break;
		}
		case 3: // verpak-tafel
		{
			APackBench* B = World->SpawnActorDeferred<APackBench>(APackBench::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (B) { B->BenchTier = O.ItemId; B->FinishSpawning(TM); }
			break;
		}
		case 5: // ATM
			World->SpawnActor<AAtm>(AAtm::StaticClass(), TM, SP);
			break;
		case 6: // plafondlamp (eigen actor)
			World->SpawnActor<ACeilingLamp>(ACeilingLamp::StaticClass(), TM, SP);
			break;
		case 7: // hasj-machine (mesh/press)
		{
			AProcessorMachine* Proc = World->SpawnActorDeferred<AProcessorMachine>(AProcessorMachine::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Proc) { Proc->MachineTier = O.ItemId; Proc->FinishSpawning(TM);
				TArray<FProcEntry> Es;
				for (const FSaveDry& D : O.DryEntries) { FProcEntry E; E.OutItemId = D.DryItemId; E.Quantity = D.Quantity; E.Thc = D.Thc; E.Quality = D.Quality; E.Elapsed = D.Elapsed; E.bDone = D.bDone; Es.Add(E); }
				Proc->RestoreEntries(Es);
			}
			break;
		}
		default: // generieke prop / meubel
		{
			APlaceableProp* Pr = World->SpawnActorDeferred<APlaceableProp>(APlaceableProp::StaticClass(), TM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Pr) { Pr->ItemId = O.ItemId; Pr->FinishSpawning(TM); }
			break;
		}
		}
	}
	UE_LOG(LogWeedShop, Log, TEXT("Wereld hersteld: %d objecten gespawned."), In.Num());
}

void USaveGameSubsystem::RestorePlayerByPawn(APawn* Pawn)
{
	if (!HasAuthorityWorld() || !Loaded || !Pawn) { return; }
	FString Id, Name; PlayerKeys(Pawn, Id, Name);
	const FString Key = Id.IsEmpty() ? Name : Id;
	if (RestoredPlayers.Contains(Key)) { return; }
	const FPlayerSaveData* Found = Loaded->Players.FindByPredicate([&](const FPlayerSaveData& E) { return Matches(E, Id, Name); });
	if (!Found) { return; }
	ApplyPlayer(Pawn, *Found);
	RestoredPlayers.Add(Key);
	UE_LOG(LogWeedShop, Log, TEXT("Speler hersteld uit save: id='%s' naam='%s' (cash %lld, %d items)"), *Id, *Name, (long long)Found->CashCents, Found->Items.Num());
}
