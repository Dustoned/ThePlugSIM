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
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

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
	return UGameplayStatics::DoesSaveGameExist(SlotName, 0);
}

FString USaveGameSubsystem::PlayerNameOf(const APawn* Pawn)
{
	if (Pawn)
	{
		if (const APlayerState* PS = Pawn->GetPlayerState())
		{
			const FString N = PS->GetPlayerName();
			if (!N.IsEmpty()) { return N; }
		}
	}
	return TEXT("Player");
}

void USaveGameSubsystem::GatherPlayer(APawn* Pawn, FPlayerSaveData& Out) const
{
	Out = FPlayerSaveData();
	Out.PlayerName = PlayerNameOf(Pawn);
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
	if (UWeedShopSaveGame* Prev = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
	{
		Save->Players = Prev->Players;
	}

	// Gedeelde wereld-staat.
	if (const UDayCycleComponent* Day = GS->GetDayCycle()) { Save->TimeOfDaySeconds = Day->GetTimeOfDaySeconds(); Save->DayNumber = Day->GetDayNumber(); }
	if (const UMilestoneComponent* Ms = GS->GetMilestones()) { Save->TotalEarnedCents = Ms->GetTotalEarnedCents(); Save->MilestonePhase = (uint8)Ms->GetCurrentPhase(); }
	if (const UUpgradeComponent* Up = GS->GetUpgrades()) { Save->PurchasedUpgrades = Up->GetPurchasedIds(); }

	// Alle nu verbonden spelers (op username) upserten.
	int32 NumPlayers = 0;
	if (World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
			if (!P) { continue; }
			FPlayerSaveData Data; GatherPlayer(P, Data);
			const int32 Idx = Save->Players.IndexOfByPredicate([&](const FPlayerSaveData& E) { return E.PlayerName == Data.PlayerName; });
			if (Idx != INDEX_NONE) { Save->Players[Idx] = Data; } else { Save->Players.Add(Data); }
			++NumPlayers;
		}
	}

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
	if (bOk) { Loaded = Save; } // cache zodat late-joiners de nieuwste staat krijgen
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

	UWeedShopSaveGame* Save = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Save) { return false; }
	Loaded = Save;
	RestoredPlayers.Reset();

	// Gedeelde staat terugzetten.
	if (UDayCycleComponent* Day = GS->GetDayCycle()) { Day->SetTimeOfDaySeconds(Save->TimeOfDaySeconds); }
	if (UMilestoneComponent* Ms = GS->GetMilestones()) { Ms->RestoreState(Save->TotalEarnedCents, Save->MilestonePhase); }
	if (UUpgradeComponent* Up = GS->GetUpgrades()) { Up->RestorePurchased(Save->PurchasedUpgrades); }

	// Legacy-save (v1: alleen host-cash/bank, geen Players-array) -> naar de host-speler.
	const bool bLegacy = (Save->Players.Num() == 0) && (Save->BalanceCents != 0 || Save->BankCents != 0 || Save->bBankAppUnlocked);
	if (bLegacy && World)
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* P = PC->GetPawn())
			{
				FPlayerSaveData D; D.PlayerName = PlayerNameOf(P);
				D.CashCents = Save->BalanceCents; D.BankCents = Save->BankCents; D.bBankAppUnlocked = Save->bBankAppUnlocked;
				ApplyPlayer(P, D); RestoredPlayers.Add(D.PlayerName);
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

	UE_LOG(LogWeedShop, Log, TEXT("LoadGame: %d speler(s) in save, dag %d hersteld."), Save->Players.Num(), Save->DayNumber);
	return true;
}

void USaveGameSubsystem::RestorePlayerByPawn(APawn* Pawn)
{
	if (!HasAuthorityWorld() || !Loaded || !Pawn) { return; }
	const FString Name = PlayerNameOf(Pawn);
	if (RestoredPlayers.Contains(Name)) { return; }
	const FPlayerSaveData* Found = Loaded->Players.FindByPredicate([&](const FPlayerSaveData& E) { return E.PlayerName == Name; });
	if (!Found) { return; }
	ApplyPlayer(Pawn, *Found);
	RestoredPlayers.Add(Name);
	UE_LOG(LogWeedShop, Log, TEXT("Speler hersteld uit save: %s (cash %lld, %d items)"), *Name, (long long)Found->CashCents, Found->Items.Num());
}
