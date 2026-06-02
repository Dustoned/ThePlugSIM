#include "Npc/NpcRegistryComponent.h"

#include "WeedShopCore.h"
#include "Data/NpcDef.h"
#include "Game/WeedShopGameState.h"
#include "Phone/ContactsComponent.h"
#include "World/DayCycleComponent.h"
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

float UNpcRegistryComponent::NowAbs() const
{
	if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (const UDayCycleComponent* Day = GS->GetDayCycle())
		{
			return Day->GetDayNumber() * 1000000.f + Day->GetTimeOfDaySeconds();
		}
	}
	return 0.f;
}

int32 UNpcRegistryComponent::CurrentDay() const
{
	if (const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner()))
	{
		if (const UDayCycleComponent* Day = GS->GetDayCycle()) { return Day->GetDayNumber(); }
	}
	return 0;
}

bool UNpcRegistryComponent::IsUnlocked(FName NpcId) const
{
	const FNpcState* S = Find(NpcId);
	return S && S->bUnlocked;
}

bool UNpcRegistryComponent::CanAppointToday(FName NpcId) const
{
	const FNpcState* S = Find(NpcId);
	if (!S) { return false; }
	if (S->ApptDay != CurrentDay()) { return MaxApptsPerDay > 0; } // nieuwe dag -> teller reset straks
	return S->ApptCountToday < MaxApptsPerDay;
}

void UNpcRegistryComponent::NoteAppointment(FName NpcId)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	FNpcState* S = Find(NpcId);
	if (!S) { return; }
	const int32 Day = CurrentDay();
	if (S->ApptDay != Day) { S->ApptDay = Day; S->ApptCountToday = 0; }
	++S->ApptCountToday;
}

void UNpcRegistryComponent::MarkDealt(FName NpcId)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	if (FNpcState* S = Find(NpcId)) { S->LastDealAbs = NowAbs(); }
}

bool UNpcRegistryComponent::IsOnCooldown(FName NpcId) const
{
	const FNpcState* S = Find(NpcId);
	if (!S || S->LastDealAbs < 0.f) { return false; }
	return (NowAbs() - S->LastDealAbs) < DealCooldownSeconds;
}

FName UNpcRegistryComponent::AssignNpc()
{
	EnsureSeeded();
	if (States.Num() == 0)
	{
		return NAME_None;
	}
	// Sla NPC's over die net een deal deden (cooldown). Als iedereen op cooldown is, val terug op round-robin.
	const int32 N = States.Num();
	for (int32 k = 0; k < N; ++k)
	{
		const int32 Idx = (AssignCursor + k) % N;
		if (!IsOnCooldown(States[Idx].NpcId))
		{
			AssignCursor = Idx + 1;
			return States[Idx].NpcId;
		}
	}
	const FName Id = States[AssignCursor % N].NpcId;
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
	// Nummer delen is RESPECT-gedreven: genoeg vertrouwen opgebouwd -> je krijgt z'n nummer.
	if (State.bUnlocked || State.Respect < UnlockRespect)
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
