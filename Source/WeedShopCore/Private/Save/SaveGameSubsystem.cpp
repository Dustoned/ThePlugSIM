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
#include "Cultivation/GrowPlant.h"
#include "Cultivation/DryingRack.h"
#include "World/StorageShelf.h"
#include "World/PackBench.h"
#include "World/Atm.h"
#include "Placement/PlaceableProp.h"
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
}

FString USaveGameSubsystem::SlotNameFor(int32 Slot) const
{
	return FString::Printf(TEXT("%s_%d"), *SlotName, FMath::Clamp(Slot, 0, NumSlots - 1));
}

void USaveGameSubsystem::SetSlot(int32 Slot)
{
	CurrentSlot = FMath::Clamp(Slot, 0, NumSlots - 1);
	GConfig->SetInt(TEXT("ThePlugSIM.Save"), TEXT("LastSlot"), CurrentSlot, GGameIni);
	GConfig->Flush(false, GGameIni);
}

bool USaveGameSubsystem::HasSaveInSlot(int32 Slot) const
{
	return UGameplayStatics::DoesSaveGameExist(SlotNameFor(Slot), 0);
}

bool USaveGameSubsystem::GetSlotInfo(int32 Slot, FString& OutSummary) const
{
	if (!HasSaveInSlot(Slot)) { return false; }
	if (const UWeedShopSaveGame* S = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotNameFor(Slot), 0)))
	{
		OutSummary = FString::Printf(TEXT("Day %d  -  earned EUR %lld  -  %d player(s)"),
			S->DayNumber, (long long)(S->TotalEarnedCents / 100), S->Players.Num());
		return true;
	}
	return false;
}

void USaveGameSubsystem::NewGameInSlot(int32 Slot)
{
	SetSlot(Slot);
	// Verse staat: niets herstellen. De oude save in dit slot blijft tot je opnieuw opslaat.
	Loaded = nullptr;
	RestoredPlayers.Reset();
}

bool USaveGameSubsystem::LoadSlot(int32 Slot)
{
	SetSlot(Slot);
	return LoadGame();
}

bool USaveGameSubsystem::QuickContinue()
{
	if (HasSaveInSlot(CurrentSlot)) { return LoadGame(); }
	for (int32 i = 0; i < NumSlots; ++i) { if (HasSaveInSlot(i)) { return LoadSlot(i); } }
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
	return UGameplayStatics::DoesSaveGameExist(SlotNameFor(CurrentSlot), 0);
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
	}
	if (const UPhoneClientComponent* Ph = Pawn->FindComponentByClass<UPhoneClientComponent>())
	{
		Out.bBankAppUnlocked = Ph->IsBankAppUnlocked();
	}
	if (const UInventoryComponent* Inv = Pawn->FindComponentByClass<UInventoryComponent>())
	{
		for (const FInventoryStack& S : Inv->GetStacks())
		{
			if (S.ItemId == FName(TEXT("Cash")) || S.Quantity <= 0) { continue; } // cash = afgeleid van economy
			FInvSaveItem It; It.ItemId = S.ItemId; It.Quantity = S.Quantity; It.Thc = S.Quality; It.QualityPct = S.QualityPct;
			Out.Items.Add(It);
		}
	}
}

void USaveGameSubsystem::ApplyPlayer(APawn* Pawn, const FPlayerSaveData& Data)
{
	if (!Pawn) { return; }
	if (UEconomyComponent* E = Pawn->FindComponentByClass<UEconomyComponent>())
	{
		E->SetBalanceCents(Data.CashCents);
		E->SetBankCents(Data.BankCents);
	}
	if (UPhoneClientComponent* Ph = Pawn->FindComponentByClass<UPhoneClientComponent>())
	{
		Ph->SetBankAppUnlocked(Data.bBankAppUnlocked);
	}
	if (UInventoryComponent* Inv = Pawn->FindComponentByClass<UInventoryComponent>())
	{
		Inv->ClearAll();
		for (const FInvSaveItem& It : Data.Items) { Inv->AddItem(It.ItemId, It.Quantity, It.Thc, It.QualityPct); }
	}
}

bool USaveGameSubsystem::SaveGame()
{
	if (!HasAuthorityWorld()) { return false; }
	AWeedShopGameState* GS = GetWeedGameState();
	if (!GS) { UE_LOG(LogWeedShop, Warning, TEXT("SaveGame: geen GameState.")); return false; }
	UWorld* World = GS->GetWorld();

	UWeedShopSaveGame* Save = Cast<UWeedShopSaveGame>(UGameplayStatics::CreateSaveGameObject(UWeedShopSaveGame::StaticClass()));
	if (!Save) { return false; }

	// Bestaande spelers behouden (zo blijft een co-op vriend die nu offline is bewaard).
	if (UWeedShopSaveGame* Prev = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotNameFor(CurrentSlot), 0)))
	{
		Save->Players = Prev->Players;
	}

	// Gedeelde wereld-staat.
	if (const UDayCycleComponent* Day = GS->GetDayCycle()) { Save->TimeOfDaySeconds = Day->GetTimeOfDaySeconds(); Save->DayNumber = Day->GetDayNumber(); }
	if (const UMilestoneComponent* Ms = GS->GetMilestones()) { Save->TotalEarnedCents = Ms->GetTotalEarnedCents(); Save->MilestonePhase = (uint8)Ms->GetCurrentPhase(); }
	if (const UUpgradeComponent* Up = GS->GetUpgrades()) { Save->PurchasedUpgrades = Up->GetPurchasedIds(); }

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

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, SlotNameFor(CurrentSlot), 0);
	if (bOk) { Loaded = Save; GS->NotifySaved(); } // cache + save-indicator bij alle spelers
	UE_LOG(LogWeedShop, Log, TEXT("SaveGame %s: %d speler(s), dag %d, fase %d"),
		bOk ? TEXT("OK") : TEXT("MISLUKT"), NumPlayers, Save->DayNumber, Save->MilestonePhase);
	return bOk;
}

bool USaveGameSubsystem::LoadGame()
{
	if (!HasAuthorityWorld()) { return false; }
	AWeedShopGameState* GS = GetWeedGameState();
	if (!GS || !HasSave()) { return false; }
	UWorld* World = GS->GetWorld();

	UWeedShopSaveGame* Save = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotNameFor(CurrentSlot), 0));
	if (!Save) { return false; }
	Loaded = Save;
	RestoredPlayers.Reset();

	// Gedeelde staat terugzetten.
	if (UDayCycleComponent* Day = GS->GetDayCycle()) { Day->SetTimeOfDaySeconds(Save->TimeOfDaySeconds); }
	if (UMilestoneComponent* Ms = GS->GetMilestones()) { Ms->RestoreState(Save->TotalEarnedCents, Save->MilestonePhase); }
	if (UUpgradeComponent* Up = GS->GetUpgrades()) { Up->RestorePurchased(Save->PurchasedUpgrades); }

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
		for (int32 i = 0; i < St.SlotStrain.Num(); ++i)
		{
			FSavePlantSlot S; S.Strain = St.SlotStrain[i];
			S.Growth = St.SlotGrowth.IsValidIndex(i) ? St.SlotGrowth[i] : 0.f;
			S.Phase = St.SlotPhase.IsValidIndex(i) ? St.SlotPhase[i] : 0;
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
				for (const FSavePlantSlot& S : O.Slots) { St.SlotStrain.Add(S.Strain); St.SlotGrowth.Add(S.Growth); St.SlotPhase.Add(S.Phase); }
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
