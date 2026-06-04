#include "Npc/NpcRegistryComponent.h"
#include "UI/WeedToast.h"

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

namespace
{
	FString CleanFirstName(const FString& Raw, int32 Index)
	{
		static const TCHAR* First[] = {
			TEXT("Jan"), TEXT("Piet"), TEXT("Kees"), TEXT("Sanne"), TEXT("Emma"), TEXT("Daan"), TEXT("Lotte"), TEXT("Bram"),
			TEXT("Sven"), TEXT("Fleur"), TEXT("Tim"), TEXT("Noa"), TEXT("Rick"), TEXT("Iris"), TEXT("Joost"), TEXT("Mila"),
			TEXT("Gerrit"), TEXT("Truus"), TEXT("Henk"), TEXT("Willem"), TEXT("Bep"), TEXT("Cor"), TEXT("Sjaak"), TEXT("Ria"),
			TEXT("Dirk"), TEXT("Mieke"), TEXT("Bart"), TEXT("Loes"), TEXT("Wout"), TEXT("Stijn"), TEXT("Femke"), TEXT("Jasper"),
			TEXT("Roos"), TEXT("Teun"), TEXT("Saar"), TEXT("Koen"), TEXT("Hilda"), TEXT("Bennie"), TEXT("Manon"), TEXT("Ferry"),
			TEXT("Chantal"), TEXT("Bertus"), TEXT("Sandra"), TEXT("Ronnie"), TEXT("Yvonne"), TEXT("Gijs"), TEXT("Niels"), TEXT("Maud"),
			TEXT("Tessa"), TEXT("Luuk"), TEXT("Bo"), TEXT("Sam"), TEXT("Nina"), TEXT("Mees"), TEXT("Lars"), TEXT("Kim"),
			TEXT("Isa"), TEXT("Mats"), TEXT("Jill"), TEXT("Dex"), TEXT("Puck"), TEXT("Guus"), TEXT("Floor"), TEXT("Ravi"),
			TEXT("Nova"), TEXT("Otis"), TEXT("Liva"), TEXT("Moos"), TEXT("Tijn"), TEXT("Sofie"), TEXT("Fien"), TEXT("Rens"),
			TEXT("Ome Ton"), TEXT("Tante An"), TEXT("Appie"), TEXT("Non"), TEXT("Ouwe Joop"), TEXT("Dikke Leo"), TEXT("Tinus"),
			TEXT("Sjonnie"), TEXT("Annie"), TEXT("Bennie Bob"), TEXT("Kleine Kees"), TEXT("Tante Sjaan") };

		FString FirstName = Raw.TrimStartAndEnd();
		if (FirstName.Contains(TEXT(" ")))
		{
			FirstName.Split(TEXT(" "), &FirstName, nullptr);
		}
		return FirstName.IsEmpty() ? First[Index % UE_ARRAY_COUNT(First)] : FirstName;
	}

	FString GoofyFullName(const FString& PreferredName, int32 Index)
	{
		static const TCHAR* Last[] = {
			TEXT("Pannenkoek"), TEXT("Stokvis"), TEXT("Bonk"), TEXT("Knol"), TEXT("Prummel"), TEXT("Druif"),
			TEXT("Kwast"), TEXT("Plof"), TEXT("Worst"), TEXT("Pruim"), TEXT("Klep"), TEXT("Toeter"),
			TEXT("Krent"), TEXT("Schroef"), TEXT("Boterham"), TEXT("Stamppot"), TEXT("Sok"), TEXT("Brok"),
			TEXT("Hark"), TEXT("Snuiter"), TEXT("Klont"), TEXT("Knapzak"), TEXT("Kwakkel"), TEXT("Prut"),
			TEXT("Blok"), TEXT("Pluis"), TEXT("Frikandel"), TEXT("Kroket"), TEXT("Kaaskop"), TEXT("Mosterd"),
			TEXT("Klaproos"), TEXT("Peul"), TEXT("Knaak"), TEXT("Krentenbol"), TEXT("Pindakaas"), TEXT("Knetter"),
			TEXT("Slinger"), TEXT("Fluitketel"), TEXT("Stoeptegel"), TEXT("Koffiemok"), TEXT("Dropveter"), TEXT("Bamischijf"),
			TEXT("Kapsalon"), TEXT("Draaitafel"), TEXT("Plakband"), TEXT("Kruik"), TEXT("Waslijn"), TEXT("Kruimel"),
			TEXT("Sjoelbak"), TEXT("Tosti"), TEXT("Klodder"), TEXT("Vlaai"), TEXT("Kiekeboe"), TEXT("Nattevinger"),
			TEXT("Knalpot"), TEXT("Glitterjas"), TEXT("Poffertje"), TEXT("Klusbus"), TEXT("Zilveruitje"), TEXT("Limonade"),
			TEXT("Trommel"), TEXT("Badmuts"), TEXT("Knipperlicht"), TEXT("Kaasplank"), TEXT("Hagelslag"), TEXT("Frietzak"),
			TEXT("Drol"), TEXT("Bil"), TEXT("Pielewaaier"), TEXT("Zeurpiet"), TEXT("Slok"), TEXT("Tuthola"),
			TEXT("Snor"), TEXT("Krakeling"), TEXT("Prutser"), TEXT("Klodderkont"), TEXT("Natte Krant"), TEXT("Snotneus"),
			TEXT("Kletsmajoor"), TEXT("Boterletter"), TEXT("Kruimeldief"), TEXT("Mallemolen") };

		const int32 LastIdx = FMath::Abs(Index * 37 + Index / 7) % UE_ARRAY_COUNT(Last);
		return FString::Printf(TEXT("%s %s"), *CleanFirstName(PreferredName, Index), Last[LastIdx]);
	}
}

void UNpcRegistryComponent::EnsureSeeded()
{
	if (States.Num() > 0 || !NpcTable)
	{
		return;
	}
	TSet<FString> UsedNames;
	int32 RowIndex = 0;
	for (const FName& RowName : NpcTable->GetRowNames())
	{
		const FNpcDef* Def = NpcTable->FindRow<FNpcDef>(RowName, TEXT("EnsureSeeded"), false);
		if (!Def)
		{
			continue;
		}
		FNpcState S;
		S.NpcId = RowName;
		FString UniqueName = GoofyFullName(Def->DisplayName.ToString(), RowIndex);
		for (int32 Try = 1; UsedNames.Contains(UniqueName); ++Try)
		{
			UniqueName = GoofyFullName(Def->DisplayName.ToString(), RowIndex + Try * 97);
		}
		UsedNames.Add(UniqueName);
		S.DisplayName = FText::FromString(UniqueName);
		S.Respect = Def->BaseRespect;
		S.Loyalty = Def->BaseLoyalty;
		S.Addiction = Def->BaseAddiction;
		States.Add(S);
		++RowIndex;
	}
	UE_LOG(LogWeedShop, Log, TEXT("NPC-register geladen: %d personen."), States.Num());
}

FName UNpcRegistryComponent::EnsureNpc(FName NpcId, const FText& DisplayName, float BaseRespect, float BaseLoyalty, float BaseAddiction)
{
	EnsureSeeded();
	if (NpcId.IsNone())
	{
		return AssignNpc();
	}
	if (FNpcState* Existing = Find(NpcId))
	{
		if (!DisplayName.IsEmpty())
		{
			Existing->DisplayName = DisplayName;
		}
		return NpcId;
	}

	FNpcState S;
	S.NpcId = NpcId;
	S.DisplayName = DisplayName.IsEmpty() ? FText::FromName(NpcId) : DisplayName;
	S.Respect = FMath::Clamp(BaseRespect, 0.f, 100.f);
	S.Loyalty = FMath::Clamp(BaseLoyalty, 0.f, 100.f);
	S.Addiction = FMath::Clamp(BaseAddiction, 0.f, 100.f);
	States.Add(S);
	return NpcId;
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
		UWeedToast::Notify(-1, 5.f, FColor(120, 200, 255),
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
