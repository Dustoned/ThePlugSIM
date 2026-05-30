#include "Phone/PhoneClientComponent.h"

#include "WeedShopCore.h"
#include "Game/WeedShopGameState.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Phone/ContactsComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

UPhoneClientComponent::UPhoneClientComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AWeedShopGameState* UPhoneClientComponent::GetGS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
}

APlayerController* UPhoneClientComponent::GetPC() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(Pawn->GetController());
	}
	return nullptr;
}

UInventoryComponent* UPhoneClientComponent::GetOwnerInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
}

void UPhoneClientComponent::UpdateCursor()
{
	const bool bAnyUI = bOpen || bRollOpen;
	if (APlayerController* PC = GetPC())
	{
		PC->SetShowMouseCursor(bAnyUI);
		PC->bEnableClickEvents = bAnyUI;
		PC->bEnableMouseOverEvents = bAnyUI;
		if (bAnyUI)
		{
			PC->SetInputMode(FInputModeGameAndUI());
		}
		else
		{
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
}

void UPhoneClientComponent::Toggle()
{
	bOpen = !bOpen;
	if (bOpen)
	{
		bRollOpen = false; // niet allebei tegelijk
	}
	UpdateCursor();
}

void UPhoneClientComponent::ToggleRollUI()
{
	bRollOpen = !bRollOpen;
	if (bRollOpen)
	{
		bOpen = false;
		RollGrams = FMath::Clamp(RollGrams, MinGrams, GetMaxJointGrams());
	}
	UpdateCursor();
}

void UPhoneClientComponent::SetRollGrams(int32 Grams)
{
	RollGrams = FMath::Clamp(Grams, MinGrams, GetMaxJointGrams());
}

void UPhoneClientComponent::ConfirmRoll()
{
	ServerRollJoint(RollGrams);
	bRollOpen = false;
	UpdateCursor();
}

int32 UPhoneClientComponent::GetMaxJointGrams() const
{
	int32 Max = BaseMaxGrams;
	if (const AWeedShopGameState* GS = GetGS())
	{
		if (const UUpgradeComponent* Upg = GS->GetUpgrades())
		{
			Max += FMath::RoundToInt(Upg->GetEffectTotal(TEXT("JointGramMax")));
		}
	}
	return FMath::Clamp(Max, MinGrams, GramsHardMax);
}

void UPhoneClientComponent::ServerRollJoint_Implementation(int32 Grams)
{
	Grams = FMath::Clamp(Grams, MinGrams, GetMaxJointGrams());
	UInventoryComponent* Inv = GetOwnerInventory();
	if (!Inv)
	{
		return;
	}

	// Zoek een bud-stapel met genoeg gram.
	FName BudItem = NAME_None;
	for (const FInventoryStack& Stack : Inv->GetStacks())
	{
		if (Stack.ItemId.ToString().StartsWith(TEXT("Bud_")) && Stack.Quantity >= Grams)
		{
			BudItem = Stack.ItemId;
			break;
		}
	}
	if (BudItem.IsNone() || !Inv->RemoveItem(BudItem, Grams))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Orange,
				FString::Printf(TEXT("Niet genoeg wiet (%d g nodig)."), Grams));
		}
		return;
	}

	// Joint-kwaliteit zit in de item-id (Joint_<G>g): meer gram = betere joint.
	const FName JointId(*FString::Printf(TEXT("Joint_%dg"), Grams));
	Inv->AddItem(JointId, 1);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Green,
			FString::Printf(TEXT("Joint gedraaid (%d g)."), Grams));
	}
}

void UPhoneClientComponent::SetTab(int32 NewTab)
{
	Tab = FMath::Clamp(NewTab, 0, 3);
}

void UPhoneClientComponent::CycleTab()
{
	if (bOpen)
	{
		Tab = (Tab + 1) % 4;
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

	if (Tab == 1) // Suppliers: zaad kopen
	{
		if (GS->GetStore())
		{
			const TArray<FName> Seeds = GS->GetStore()->GetSeedCatalog();
			if (Seeds.IsValidIndex(Index))
			{
				ServerBuySeed(Seeds[Index]);
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
			GS->GetUpgrades()->BuyUpgrade(UpgradeId);
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
			GS->GetContacts()->RespondTopPending(bAccept);
		}
	}
}
