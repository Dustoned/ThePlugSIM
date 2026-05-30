#include "Npc/NpcRegistryComponent.h"

#include "WeedShopCore.h"
#include "Data/NpcDef.h"
#include "Game/WeedShopGameState.h"
#include "Phone/ContactsComponent.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

UNpcRegistryComponent::UNpcRegistryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	static ConstructorHelpers::FObjectFinder<UDataTable> Finder(TEXT("/Game/_Project/Data/DT_NPCs.DT_NPCs"));
	if (Finder.Succeeded())
	{
		NpcTable = Finder.Object;
	}
}

void UNpcRegistryComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwnerRole() == ROLE_Authority)
	{
		EnsureSeeded();
	}
}

void UNpcRegistryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UNpcRegistryComponent, States);
}

void UNpcRegistryComponent::EnsureSeeded()
{
	if (States.Num() > 0 || !NpcTable)
	{
		return;
	}
	for (const FName& RowName : NpcTable->GetRowNames())
	{
		const FNpcDef* Def = NpcTable->FindRow<FNpcDef>(RowName, TEXT("EnsureSeeded"), false);
		if (!Def)
		{
			continue;
		}
		FNpcState S;
		S.NpcId = RowName;
		S.DisplayName = Def->DisplayName;
		S.Respect = Def->BaseRespect;
		S.Loyalty = Def->BaseLoyalty;
		S.Addiction = Def->BaseAddiction;
		States.Add(S);
	}
	UE_LOG(LogWeedShop, Log, TEXT("NPC-register geladen: %d personen."), States.Num());
}

FNpcState* UNpcRegistryComponent::Find(FName NpcId)
{
	return States.FindByPredicate([NpcId](const FNpcState& S) { return S.NpcId == NpcId; });
}

const FNpcState* UNpcRegistryComponent::Find(FName NpcId) const
{
	return States.FindByPredicate([NpcId](const FNpcState& S) { return S.NpcId == NpcId; });
}

FName UNpcRegistryComponent::AssignNpc()
{
	EnsureSeeded();
	if (States.Num() == 0)
	{
		return NAME_None;
	}
	const FName Id = States[AssignCursor % States.Num()].NpcId;
	AssignCursor++;
	return Id;
}

bool UNpcRegistryComponent::GetStats(FName NpcId, float& OutRespect, float& OutLoyalty, float& OutAddiction, FText& OutName) const
{
	if (const FNpcState* S = Find(NpcId))
	{
		OutRespect = S->Respect;
		OutLoyalty = S->Loyalty;
		OutAddiction = S->Addiction;
		OutName = S->DisplayName;
		return true;
	}
	return false;
}

void UNpcRegistryComponent::ApplyStats(FName NpcId, float Respect, float Loyalty, float Addiction)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}
	FNpcState* S = Find(NpcId);
	if (!S)
	{
		return;
	}
	S->Respect = FMath::Clamp(Respect, 0.f, 100.f);
	S->Loyalty = FMath::Clamp(Loyalty, 0.f, 100.f);
	S->Addiction = FMath::Clamp(Addiction, 0.f, 100.f);
	CheckUnlock(*S);
}

void UNpcRegistryComponent::CheckUnlock(FNpcState& State)
{
	if (State.bUnlocked || State.Loyalty < UnlockLoyalty)
	{
		return;
	}
	State.bUnlocked = true;

	if (AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (GS->GetContacts())
		{
			GS->GetContacts()->RegisterContact(State.NpcId, State.DisplayName, State.Loyalty);
		}
	}

	UE_LOG(LogWeedShop, Log, TEXT("Nummer ontgrendeld: %s"), *State.DisplayName.ToString());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor(120, 200, 255),
			FString::Printf(TEXT("You got %s's number!"), *State.DisplayName.ToString()));
	}
}

int32 UNpcRegistryComponent::GetUnlockedCount() const
{
	int32 N = 0;
	for (const FNpcState& S : States)
	{
		if (S.bUnlocked) ++N;
	}
	return N;
}
