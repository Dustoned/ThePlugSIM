#include "Save/SaveGameSubsystem.h"

#include "WeedShopCore.h"
#include "Save/WeedShopSaveGame.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AWeedShopGameState* USaveGameSubsystem::GetWeedGameState() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return World ? World->GetGameState<AWeedShopGameState>() : nullptr;
}

bool USaveGameSubsystem::HasAuthorityWorld() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	// Standalone of (listen-)server mag de gedeelde state schrijven/herstellen; clients niet.
	return World && World->GetNetMode() != NM_Client;
}

bool USaveGameSubsystem::HasSave() const
{
	return UGameplayStatics::DoesSaveGameExist(SlotName, 0);
}

bool USaveGameSubsystem::SaveGame()
{
	if (!HasAuthorityWorld())
	{
		return false;
	}

	AWeedShopGameState* GS = GetWeedGameState();
	if (!GS)
	{
		UE_LOG(LogWeedShop, Warning, TEXT("SaveGame: geen AWeedShopGameState."));
		return false;
	}

	UWeedShopSaveGame* Save = Cast<UWeedShopSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UWeedShopSaveGame::StaticClass()));
	if (!Save)
	{
		return false;
	}

	if (const UEconomyComponent* Econ = GS->GetEconomy())
	{
		Save->BalanceCents = Econ->GetBalanceCents();
	}
	if (const UDayCycleComponent* Day = GS->GetDayCycle())
	{
		Save->TimeOfDaySeconds = Day->GetTimeOfDaySeconds();
	}

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
	UE_LOG(LogWeedShop, Log, TEXT("SaveGame %s: kas %lld, tijd %.0f"),
		bOk ? TEXT("OK") : TEXT("MISLUKT"), (long long)Save->BalanceCents, Save->TimeOfDaySeconds);
	return bOk;
}

bool USaveGameSubsystem::LoadGame()
{
	if (!HasAuthorityWorld())
	{
		return false;
	}

	AWeedShopGameState* GS = GetWeedGameState();
	if (!GS || !HasSave())
	{
		return false;
	}

	UWeedShopSaveGame* Save = Cast<UWeedShopSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Save)
	{
		return false;
	}

	if (UEconomyComponent* Econ = GS->GetEconomy())
	{
		Econ->SetBalanceCents(Save->BalanceCents);
	}
	if (UDayCycleComponent* Day = GS->GetDayCycle())
	{
		Day->SetTimeOfDaySeconds(Save->TimeOfDaySeconds);
	}

	UE_LOG(LogWeedShop, Log, TEXT("LoadGame: kas %lld, tijd %.0f hersteld."),
		(long long)Save->BalanceCents, Save->TimeOfDaySeconds);
	return true;
}
