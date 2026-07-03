#include "Progression/LevelComponent.h"
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
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
	DOREPLIFETIME(ULevelComponent, Level);
	DOREPLIFETIME(ULevelComponent, CurrentXP);
	DOREPLIFETIME(ULevelComponent, bShopLicensed);
}

int32 ULevelComponent::XPForLevel(int32 Lvl)
{
	// Oplopende curve: 100 + (Lvl-1)*40. Lvl1->2 = 100, ... Lvl99->100 = 4020.
	// Totaal tot level 100 ~ 204.000 XP. 0 op/over max.
	if (Lvl < 1 || Lvl >= MaxLevel) { return 0; }
	return 100 + (Lvl - 1) * 40;
}

float ULevelComponent::GetLevelFraction() const
{
	const int32 Need = XPForLevel(Level);
	if (Need <= 0) { return 1.f; }
	return FMath::Clamp(float(CurrentXP) / float(Need), 0.f, 1.f);
}

void ULevelComponent::AddXP(int32 Amount, float StonedMult)
{
	if (GetOwnerRole() != ROLE_Authority || Amount <= 0 || Level >= MaxLevel)
	{
		return;
	}
	// Stoned-bonus van de VERDIENENDE speler (per-verdiener meegegeven; co-op: niet crew-breed/race-vast).
	Amount = FMath::Max(1, FMath::RoundToInt(Amount * FMath::Max(1.f, StonedMult)));
	CurrentXP += Amount;

	bool bLeveled = false;
	while (Level < MaxLevel)
	{
		const int32 Need = XPForLevel(Level);
		if (Need <= 0 || CurrentXP < Need) { break; }
		CurrentXP -= Need;
		++Level;
		bLeveled = true;
	}
	if (Level >= MaxLevel) { CurrentXP = 0; }

	if (bLeveled)
	{
		OnLevelUp.Broadcast(Level);
		if (GEngine)
		{
			// Gedeelde mijlpaal: toon de level-up bij ALLE spelers (elk op eigen client).
			NotifyAllPlayers(GetWorld(), FColor(120, 220, 255), 5.f,
				FString::Printf(TEXT("LEVEL UP!  You reached level %d"), Level));
		}
		// Eind-mijlpaal: op level 50 verdien je de SHOP-LICENTIE (gate naar het weedshop-deel).
		if (!bShopLicensed && Level >= ShopLicenseLevel)
		{
			bShopLicensed = true;
			if (GEngine)
			{
				// Gedeelde mijlpaal: shop-licentie naar ALLE spelers.
				NotifyAllPlayers(GetWorld(), FColor(255, 215, 0), 9.f,
					TEXT("SHOP LICENSE EARNED!  Level 50 reached - you can now run a legit weed shop."));
			}
		}
	}
}

void ULevelComponent::GrantLevel(int32 NewLevel)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	Level = FMath::Clamp(NewLevel, 1, MaxLevel);
	CurrentXP = 0;
	OnRep_Level();
}

void ULevelComponent::RestoreLevel(int32 InLevel, int32 InXP)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	Level = FMath::Clamp(InLevel, 1, MaxLevel);
	CurrentXP = FMath::Max(0, InXP);
	OnRep_Level();
}

void ULevelComponent::OnRep_Level()
{
	// Hook voor clients (UI werkt al via de pure getters elke frame).
}
