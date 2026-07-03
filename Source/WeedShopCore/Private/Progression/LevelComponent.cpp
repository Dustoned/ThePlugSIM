#include "Progression/LevelComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "Save/SaveGameSubsystem.h" // StablePlayerId: competitive per-speler-key
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// Co-op: stuur een melding naar ALLE spelers (elk op z'n eigen client) i.p.v. alleen lokaal op de host.
	// NotifyPawn routeert per pawn via de PhoneClientComponent naar de juiste client. Valt terug op een
	// lokale melding als er (nog) geen pawns zijn. (Zelfde idioom als HeatComponent.cpp.)
	void NotifyAllPlayers(UWorld* W, const FColor& Color, float Time, const FString& Msg)
	{
		bool bAny = false;
		if (W)
		{
			for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
			{
				if (APawn* Pw = It->Get() ? It->Get()->GetPawn() : nullptr)
				{
					UWeedToast::NotifyPawn(Pw, -1, Time, Color, Msg);
					bAny = true;
				}
			}
		}
		if (!bAny) { UWeedToast::Notify(-1, Time, Color, Msg); }
	}
}

ULevelComponent::ULevelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void ULevelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ULevelComponent, Shared);
	DOREPLIFETIME(ULevelComponent, Players);
}

int32 ULevelComponent::XPForLevel(int32 Lvl)
{
	// Oplopende curve: 100 + (Lvl-1)*40. Lvl1->2 = 100, ... Lvl99->100 = 4020.
	// Totaal tot level 100 ~ 204.000 XP. 0 op/over max.
	if (Lvl < 1 || Lvl >= MaxLevel) { return 0; }
	return 100 + (Lvl - 1) * 40;
}

// ============================ Resolvers ============================

FLevelState& ULevelComponent::StateForPawn(const APawn* Pawn)
{
	const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	const bool bComp = GS && GS->IsCompetitive();
	if (!Pawn || !bComp)
	{
		return Shared;
	}
	const FName Key(*USaveGameSubsystem::StablePlayerId(Pawn));
	if (Key.IsNone())
	{
		return Shared; // geen stabiele id (offline/verse pawn) -> val terug op gedeeld
	}
	for (FLevelPlayerEntry& E : Players)
	{
		if (E.Key == Key) { return E.State; }
	}
	// Lazy-create (server): nieuwe speler start op level 1 met 0 XP (default FLevelState).
	FLevelPlayerEntry& New = Players.AddDefaulted_GetRef();
	New.Key = Key;
	return New.State;
}

const FLevelState& ULevelComponent::StateForPawnConst(const APawn* Pawn) const
{
	const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	const bool bComp = GS && GS->IsCompetitive();
	if (!Pawn || !bComp)
	{
		return Shared;
	}
	const FName Key(*USaveGameSubsystem::StablePlayerId(Pawn));
	if (!Key.IsNone())
	{
		for (const FLevelPlayerEntry& E : Players)
		{
			if (E.Key == Key) { return E.State; }
		}
	}
	// Nog geen entry (client die de rep nog niet zag, of onbekende pawn) -> Shared (nooit een dangling ref).
	return Shared;
}

// ============================ Read/gate ============================

int32 ULevelComponent::GetLevelFor(const APawn* Pawn) const
{
	return StateForPawnConst(Pawn).Level;
}

int32 ULevelComponent::GetCurrentXPFor(const APawn* Pawn) const
{
	return StateForPawnConst(Pawn).CurrentXP;
}

int32 ULevelComponent::GetXPToNextFor(const APawn* Pawn) const
{
	return XPForLevel(StateForPawnConst(Pawn).Level);
}

float ULevelComponent::GetLevelFractionFor(const APawn* Pawn) const
{
	const FLevelState& St = StateForPawnConst(Pawn);
	const int32 Need = XPForLevel(St.Level);
	if (Need <= 0) { return 1.f; }
	return FMath::Clamp(float(St.CurrentXP) / float(Need), 0.f, 1.f);
}

bool ULevelComponent::IsShopLicensedFor(const APawn* Pawn) const
{
	return StateForPawnConst(Pawn).bShopLicensed;
}

// ============================ Write ============================

void ULevelComponent::ProcessXP(FLevelState& St, const APawn* Earner, int32 Amount, float StonedMult, bool bCompetitive)
{
	if (Amount <= 0 || St.Level >= MaxLevel) { return; }

	// Stoned-bonus van de VERDIENENDE speler (per-verdiener meegegeven; co-op: niet crew-breed/race-vast).
	Amount = FMath::Max(1, FMath::RoundToInt(Amount * FMath::Max(1.f, StonedMult)));
	St.CurrentXP += Amount;

	bool bLeveled = false;
	while (St.Level < MaxLevel)
	{
		const int32 Need = XPForLevel(St.Level);
		if (Need <= 0 || St.CurrentXP < Need) { break; }
		St.CurrentXP -= Need;
		++St.Level;
		bLeveled = true;
	}
	if (St.Level >= MaxLevel) { St.CurrentXP = 0; }

	if (!bLeveled) { return; }

	OnLevelUp.Broadcast(St.Level);
	if (GEngine)
	{
		const FString LvlMsg = FString::Printf(TEXT("LEVEL UP!  You reached level %d"), St.Level);
		// Competitive: alleen de VERDIENER ziet de melding (persoonlijke mijlpaal). Co-op: alle spelers (gedeeld).
		if (bCompetitive && Earner)
		{
			UWeedToast::NotifyPawn(const_cast<APawn*>(Earner), -1, 5.f, FColor(120, 220, 255), LvlMsg);
		}
		else
		{
			NotifyAllPlayers(GetWorld(), FColor(120, 220, 255), 5.f, LvlMsg);
		}
	}

	// Eind-mijlpaal: op level 50 verdien je de SHOP-LICENTIE (gate naar het weedshop-deel).
	if (!St.bShopLicensed && St.Level >= ShopLicenseLevel)
	{
		St.bShopLicensed = true;
		if (GEngine)
		{
			const FString LicMsg = TEXT("SHOP LICENSE EARNED!  Level 50 reached - you can now run a legit weed shop.");
			if (bCompetitive && Earner)
			{
				UWeedToast::NotifyPawn(const_cast<APawn*>(Earner), -1, 9.f, FColor(255, 215, 0), LicMsg);
			}
			else
			{
				NotifyAllPlayers(GetWorld(), FColor(255, 215, 0), 9.f, LicMsg);
			}
		}
	}
}

void ULevelComponent::AddXPFor(const APawn* Earner, int32 Amount, float StonedMult)
{
	if (GetOwnerRole() != ROLE_Authority || Amount <= 0)
	{
		return;
	}
	const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	const bool bComp = GS && GS->IsCompetitive();
	FLevelState& St = StateForPawn(Earner);
	if (St.Level >= MaxLevel) { return; }
	ProcessXP(St, Earner, Amount, StonedMult, bComp);
}

void ULevelComponent::GrantLevelFor(const APawn* Pawn, int32 NewLevel)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	FLevelState& St = StateForPawn(Pawn);
	St.Level = FMath::Clamp(NewLevel, 1, MaxLevel);
	St.CurrentXP = 0;
	OnRep_Level();
}

void ULevelComponent::RestoreLevelFor(const FName& Key, int32 InLevel, int32 InXP, bool bLicensed)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }

	// Kies de doel-state op sleutel: NAME_None => Shared (co-op/legacy). Anders de competitive per-speler-entry.
	FLevelState* St = &Shared;
	if (!Key.IsNone())
	{
		FLevelPlayerEntry* Found = Players.FindByPredicate([&Key](const FLevelPlayerEntry& E) { return E.Key == Key; });
		if (!Found)
		{
			Found = &Players.AddDefaulted_GetRef();
			Found->Key = Key;
		}
		St = &Found->State;
	}

	St->Level = FMath::Clamp(InLevel, 1, MaxLevel);
	St->CurrentXP = FMath::Max(0, InXP);
	St->bShopLicensed = bLicensed;
	OnRep_Level();
}

void ULevelComponent::OnRep_Level()
{
	// Hook voor clients (UI werkt al via de pure getters elke frame).
}
