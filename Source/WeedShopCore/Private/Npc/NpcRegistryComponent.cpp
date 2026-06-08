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

void UNpcRegistryComponent::RestoreStates(const TArray<FNpcState>& In)
{
	if (GetOwnerRole() != ROLE_Authority || In.Num() == 0) { return; }
	States = In; // niet-leeg -> EnsureInit overschrijft 't niet meer
}

void UNpcRegistryComponent::WarmAllForTesting(UContactsComponent* Con)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	EnsureSeeded(); // vul States uit DT als dat nog niet gebeurd is
	for (FNpcState& S : States)
	{
		S.Respect = 80.f;
		S.Loyalty = 70.f;
		S.Addiction = 70.f; // boven de koop-drempel -> wil meteen kopen
		S.bUnlocked = true;  // nummer al bekend
	}
	if (Con)
	{
		int32 N = 0;
		for (const FNpcState& S : States)
		{
			if (N >= 10) { break; }
			Con->RegisterContact(S.NpcId, S.DisplayName, 70.f);
			++N;
		}
	}
}

namespace
{
	FString CleanFirstName(const FString& Raw, int32 Index)
	{
		static const TCHAR* First[] = {
			TEXT("Jan"), TEXT("Piet"), TEXT("Kees"), TEXT("Henk"), TEXT("Cor"), TEXT("Sjonnie"), TEXT("Henkie"), TEXT("Appie"),
			TEXT("Bertus"), TEXT("Guus"), TEXT("Klaas"), TEXT("Mees"), TEXT("Sjakie"), TEXT("Dirk"), TEXT("Wim"), TEXT("Bram"),
			TEXT("Joost"), TEXT("Sven"), TEXT("Tim"), TEXT("Rick"), TEXT("Bas"), TEXT("Daan"), TEXT("Niels"), TEXT("Koen"),
			TEXT("Gijs"), TEXT("Teun"), TEXT("Luuk"), TEXT("Stijn"), TEXT("Jasper"), TEXT("Ruben"), TEXT("Lars"), TEXT("Mats"),
			TEXT("Cas"), TEXT("Sander"), TEXT("Bart"), TEXT("Wout"), TEXT("Tijn"), TEXT("Siem"), TEXT("Boaz"), TEXT("Jules"),
			TEXT("Sam"), TEXT("Mick"), TEXT("Thijs"), TEXT("Ravi"), TEXT("Roel"), TEXT("Maarten"), TEXT("Freek"), TEXT("Jelle"),
			TEXT("Floris"), TEXT("Hugo"), TEXT("Pim"), TEXT("Joris"), TEXT("Tom"), TEXT("Wessel"), TEXT("Lucas"), TEXT("Milan"),
			TEXT("Finn"), TEXT("Noud"), TEXT("Sanne"), TEXT("Emma"), TEXT("Lotte"), TEXT("Fleur"), TEXT("Iris"), TEXT("Roos"),
			TEXT("Femke"), TEXT("Tessa"), TEXT("Maud"), TEXT("Nina"), TEXT("Lieke"), TEXT("Nora") };

		// Altijd een funny voornaam uit de pool (negeer de tabel-naam): elke NPC een funny voor+achternaam.
		(void)Raw;
		return First[FMath::Abs(Index) % UE_ARRAY_COUNT(First)];
	}

	FString ShortFullName(const FString& PreferredName, int32 Index)
	{
		static const TCHAR* Last[] = {
			TEXT("Vapehoven"), TEXT("Kushman"), TEXT("Hashberg"), TEXT("Bongers"), TEXT("Highsma"), TEXT("Wietveld"), TEXT("Blunt"), TEXT("Stoner"),
			TEXT("Greenwood"), TEXT("Hazelaar"), TEXT("Spliffstra"), TEXT("Dankzaad"), TEXT("Nugteren"), TEXT("Pofadder"), TEXT("Knaller"), TEXT("Patatje"),
			TEXT("Frikandel"), TEXT("Stamppot"), TEXT("Bitterbal"), TEXT("Kroketberg"), TEXT("Pindakaas"), TEXT("Hagelslag"), TEXT("Stroopwafel"), TEXT("Kaaskop"),
			TEXT("Klompenburg"), TEXT("Tulpman"), TEXT("Windmolen"), TEXT("Fietsbel"), TEXT("Gouda"), TEXT("Poffertje"), TEXT("Bamischijf"), TEXT("Kapsalon"),
			TEXT("Snackbar"), TEXT("Wietstra"), TEXT("Blowman"), TEXT("Puffinga"), TEXT("Dampkring"), TEXT("Rookwolk"), TEXT("Jointsma"), TEXT("Dabberhof"),
			TEXT("Bongerd"), TEXT("Wietema"), TEXT("Hennep"), TEXT("Grasveld"), TEXT("Blowinga"), TEXT("Smoorman"), TEXT("Peukstra"), TEXT("Asbakker"),
			TEXT("Vloeitje"), TEXT("Aansteker"), TEXT("Hasjman"), TEXT("Wiedman"), TEXT("Coffeeshop"), TEXT("Theehuis"), TEXT("Spacecake"), TEXT("Brownie"),
			TEXT("Koekenbakker"), TEXT("Pannekoek"), TEXT("Oliebol"), TEXT("Kroket"), TEXT("Kibbeling"), TEXT("Lekkerbek"), TEXT("Haringman"), TEXT("Mosselman"),
			TEXT("Patatkraam"), TEXT("Mayoman"), TEXT("Currysaus"), TEXT("Berenburg"), TEXT("Jenever"), TEXT("Klompmaker"), TEXT("Polderman"), TEXT("Dijkgraaf"),
			TEXT("Grachtgordel"), TEXT("Fietspad"), TEXT("Marktplein"), TEXT("Tulpenveld"), TEXT("Molenaar"), TEXT("Kaasboer"), TEXT("Melkboer"), TEXT("Groenteman"),
			TEXT("Slagerman"), TEXT("Bakkerman"), TEXT("Sjekkie"), TEXT("Shagman"), TEXT("Grinder"), TEXT("Bongwater"), TEXT("Waterpijp"), TEXT("Stickie"),
			TEXT("Dampman"), TEXT("Rookgordel"), TEXT("Blowveld"), TEXT("Hasjbrik"), TEXT("Wietpot"), TEXT("Hasjpijp"), TEXT("Nederwiet"), TEXT("Skunkstra"),
			TEXT("Paddoman"), TEXT("Truffel"), TEXT("Jointman"), TEXT("Vuurtje") };

		const int32 LastIdx = FMath::Abs(Index * 37 + Index / 7) % UE_ARRAY_COUNT(Last);
		return FString::Printf(TEXT("%s %s"), *CleanFirstName(PreferredName, Index), Last[LastIdx]);
	}

}

void UNpcRegistryComponent::PredictPersonality(FName NpcId, float& OutRespect, float& OutLoyalty, float& OutAddiction)
{
	// Respect + loyaliteit blijven LAAG voor iedereen: die bouw je per persoon zelf op via deals
	// (anders valt er weinig te levelen). Alleen ADDICTION verschilt, naar boven verdeeld zodat er
	// vanaf het begin genoeg kopers zijn.
	OutRespect = 10.f;
	OutLoyalty = 0.f;
	FRandomStream Rng(static_cast<int32>(GetTypeHash(NpcId)));
	const float Roll = Rng.FRand();
	if (Roll < 0.20f)       { OutAddiction = static_cast<float>(Rng.RandRange(60, 95)); } // ~20% stevig verslaafd
	else if (Roll < 0.45f)  { OutAddiction = static_cast<float>(Rng.RandRange(32, 60)); } // ~25% boven de koop-drempel
	else                    { OutAddiction = static_cast<float>(Rng.RandRange(5, 28));  } // rest nog te overtuigen
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
		FString UniqueName = ShortFullName(Def->DisplayName.ToString(), RowIndex);
		for (int32 Try = 1; UsedNames.Contains(UniqueName); ++Try)
		{
			UniqueName = ShortFullName(Def->DisplayName.ToString(), RowIndex + Try * 97);
		}
		UsedNames.Add(UniqueName);
		S.DisplayName = FText::FromString(UniqueName);
		// Elke (tabel-)NPC krijgt een gerandomiseerde persoonlijkheid (i.p.v. de vaste tabel-waardes).
		PredictPersonality(RowName, S.Respect, S.Loyalty, S.Addiction);
		// Persoonlijke klant-honger (variatie): sommigen klimmen sneller + bestellen gretiger dan anderen.
		{
			FRandomStream Rng(static_cast<int32>(GetTypeHash(RowName)) ^ 0x5bd1e995);
			S.ValueMult = 0.6f + Rng.FRand() * 1.0f; // ~0.6 .. 1.6
		}
		States.Add(S);
		++RowIndex;
	}
	UE_LOG(LogWeedShop, Log, TEXT("NPC registry loaded: %d people."), States.Num());
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

	// Gerandomiseerde persoonlijkheid per NPC, DETERMINISTISCH op de NpcId (zelfde NPC = zelfde stats,
	// stabiel over save/load en op host+client).
	PredictPersonality(NpcId, S.Respect, S.Loyalty, S.Addiction);
	States.Add(S);
	return NpcId;
}

FName UNpcRegistryComponent::EnsurePlayerNpc(FName Key, FName BaseNpc, const FText& DisplayName)
{
	if (GetOwnerRole() != ROLE_Authority || Key.IsNone()) { return Key; }
	if (Find(Key)) { return Key; } // bestaat al -> die speler heeft al een relatie met deze NPC
	FNpcState S;
	S.NpcId = Key;
	S.DisplayName = DisplayName.IsEmpty() ? FText::FromName(BaseNpc) : DisplayName;
	// Seed vanuit de BASIS-NPC, niet de speler-sleutel: beide spelers starten identiek bij dezelfde persoon.
	PredictPersonality(BaseNpc, S.Respect, S.Loyalty, S.Addiction);
	States.Add(S);
	return Key;
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
	S->LastApptAbs = NowAbs(); // start de cooldown zodat dezelfde persoon niet blijft vragen
}

void UNpcRegistryComponent::MarkDealt(FName NpcId)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	if (FNpcState* S = Find(NpcId)) { S->LastDealAbs = NowAbs(); }
}

// --- Klant-tier (1 Casual .. 5 Whale) ---
int32 UNpcRegistryComponent::TierFromXP(int32 XP)
{
	if (XP >= 1000) { return 5; }
	if (XP >= 500)  { return 4; }
	if (XP >= 220)  { return 3; }
	if (XP >= 80)   { return 2; }
	return 1;
}

FString UNpcRegistryComponent::TierName(int32 Tier)
{
	switch (Tier)
	{
	case 5: return TEXT("Whale");
	case 4: return TEXT("VIP");
	case 3: return TEXT("Heavy User");
	case 2: return TEXT("Regular");
	default: return TEXT("Casual");
	}
}

int32 UNpcRegistryComponent::GetCustomerXP(FName NpcId) const
{
	const FNpcState* S = Find(NpcId);
	return S ? S->CustomerXP : 0;
}

int32 UNpcRegistryComponent::GetCustomerTier(FName NpcId) const
{
	return TierFromXP(GetCustomerXP(NpcId));
}

void UNpcRegistryComponent::AddCustomerValue(FName NpcId, int32 GramsSold)
{
	if (GetOwnerRole() != ROLE_Authority || GramsSold <= 0) { return; }
	FNpcState* S = Find(NpcId);
	if (!S) { return; }
	// Verkochte grammen x loyaliteit-factor x persoonlijke honger. Grotere klanten klimmen vanzelf sneller.
	const int32 Gain = FMath::Max(1, FMath::RoundToInt(GramsSold * (2.f + S->Loyalty / 50.f) * S->ValueMult));
	S->CustomerXP += Gain;
}

float UNpcRegistryComponent::GetTierProgress01(FName NpcId) const
{
	const int32 XP = GetCustomerXP(NpcId);
	const int32 Tier = TierFromXP(XP);
	if (Tier >= 5) { return 1.f; }
	static const int32 Floors[6] = { 0, 0, 80, 220, 500, 1000 };
	static const int32 Nexts[6]  = { 0, 80, 220, 500, 1000, 1000 };
	const int32 Lo = Floors[Tier]; const int32 Hi = Nexts[Tier];
	return (Hi > Lo) ? FMath::Clamp((float)(XP - Lo) / (float)(Hi - Lo), 0.f, 1.f) : 1.f;
}

void UNpcRegistryComponent::GetTierOrderGrams(FName NpcId, int32& OutMin, int32& OutMax) const
{
	const FNpcState* S = Find(NpcId);
	const int32 Tier = TierFromXP(S ? S->CustomerXP : 0);
	int32 Mn, Mx;
	switch (Tier)
	{
	case 5: Mn = 20; Mx = 50; break;
	case 4: Mn = 10; Mx = 20; break;
	case 3: Mn = 6;  Mx = 12; break;
	case 2: Mn = 3;  Mx = 6;  break;
	default: Mn = 1; Mx = 3;  break;
	}
	const float VM = S ? FMath::Clamp(S->ValueMult, 0.7f, 1.5f) : 1.f; // persoonlijke gulzigheid
	OutMin = FMath::Max(1, FMath::RoundToInt(Mn * VM));
	OutMax = FMath::Max(OutMin, FMath::RoundToInt(Mx * VM));
}

bool UNpcRegistryComponent::IsOnCooldown(FName NpcId) const
{
	const FNpcState* S = Find(NpcId);
	if (!S) { return false; }
	const float Now = NowAbs();
	// Cooldown na een echte deal OF na een afspraak-vraag (tegen blijven-vragen).
	if (S->LastDealAbs >= 0.f && (Now - S->LastDealAbs) < DealCooldownSeconds) { return true; }
	if (S->LastApptAbs >= 0.f && (Now - S->LastApptAbs) < DealCooldownSeconds * FMath::Max(0.1f, S->ApptCooldownMult)) { return true; }
	return false;
}

void UNpcRegistryComponent::SetApptCooldownMult(FName NpcId, float Mult)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	if (FNpcState* S = Find(NpcId)) { S->ApptCooldownMult = FMath::Clamp(Mult, 0.25f, 6.f); }
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

	UE_LOG(LogWeedShop, Log, TEXT("Number unlocked: %s"), *State.DisplayName.ToString());
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
