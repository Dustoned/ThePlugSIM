#include "Npc/NpcRegistryComponent.h"
#include "Customer/CustomerBase.h" // ACustomerBase::StableLookSeed voor de skin->tier-bodem
#include "UI/WeedToast.h"

#include "WeedShopCore.h"
#include "Data/NpcDef.h"
#include "Game/WeedShopGameState.h"
#include "Phone/ContactsComponent.h"
#include "Save/SaveGameSubsystem.h" // StablePlayerId: competitive contact-eigenaarschap
#include "World/DayCycleComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// Per-speler relatie-sleutels ("BaseNpc#PlayerId", zie EnsurePlayerNpc) herkennen: dat zijn RELATIE-entries,
	// geen echte uitdeelbare NPC-identiteiten -> uitsluiten bij round-robin uitdelen en bij tellingen.
	bool IsPlayerRelationKey(FName NpcId)
	{
		return NpcId.ToString().Contains(TEXT("#"));
	}
}

UNpcRegistryComponent::UNpcRegistryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// MEET-MARKER (boot-gap-diagnose, B.15): de FObjectFinder sync-laadt DT_NPCs (+ eventuele hard-refs).
	// De static laadt maar 1x; latere constructor-runs zijn ~0.00s.
	const double _NpcT0 = FPlatformTime::Seconds();
	static ConstructorHelpers::FObjectFinder<UDataTable> Finder(TEXT("/Game/_Project/Data/DT_NPCs.DT_NPCs"));
	if (Finder.Succeeded())
	{
		NpcTable = Finder.Object;
	}
	UE_LOG(LogWeedShop, Display, TEXT("[BOOTMARK] NpcRegistry-ctor: DT_NPCs-load %.2fs (+%.2fs sinds start)"), FPlatformTime::Seconds() - _NpcT0, FPlatformTime::Seconds() - GStartTime);
}

void UNpcRegistryComponent::BeginPlay()
{
	Super::BeginPlay();
	// D29: het seeden van 250 NPC's (EnsureSeeded) is ALLEEN nodig op de speelbare NPC-map (de beach). Op de
	// menu-map (Map_MainMenu) draaide dit ook, VOOR NIETS - en dev-only kost het ~27s doordat de crowd-skin-
	// compilatie de game-thread verhongert tijdens dat venster (bewezen met [BOOTMARK]-meting). Daarom hier op
	// de beach-map gaten. Wordt sowieso lazy geseed via EnsureNpc/AssignNpc/de getters mocht een pad 'm toch
	// nodig hebben; en een geladen save vult States al via RestoreStates (dan is EnsureSeeded een no-op).
	const UWorld* W = GetWorld();
	const bool bNpcMap = W && W->GetOutermost() && W->GetOutermost()->GetName().StartsWith(TEXT("/Game/CityBeachStrip"));
	if (GetOwnerRole() == ROLE_Authority && bNpcMap)
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
	// Migratie (competitive-saves van vóór de per-key ValueMult-seed): '#'-entries met de default 1.0
	// krijgen de ValueMult van hun BASIS-entry (deterministisch; de basis komt uit dezelfde save).
	// Fallback = dezelfde seed-formule als EnsureSeeded. Idempotent: goed geseede entries blijven gelijk.
	for (FNpcState& S : States)
	{
		if (!IsPlayerRelationKey(S.NpcId) || S.ValueMult != 1.f) { continue; }
		const FString KeyStr = S.NpcId.ToString();
		int32 HashPos = INDEX_NONE;
		if (!KeyStr.FindChar(TEXT('#'), HashPos)) { continue; }
		const FName BaseNpc(*KeyStr.Left(HashPos));
		if (const FNpcState* Base = In.FindByPredicate([BaseNpc](const FNpcState& B) { return B.NpcId == BaseNpc; }))
		{
			S.ValueMult = Base->ValueMult;
		}
		else
		{
			FRandomStream Rng(static_cast<int32>(GetTypeHash(BaseNpc)) ^ 0x5bd1e995);
			S.ValueMult = 0.6f + Rng.FRand() * 1.0f;
		}
	}
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

		// BIJECTIEVE 2D-decompositie: voornaam = Index % 70 (in CleanFirstName), achternaam = (Index / 70) % 100.
		// Zo geeft elke Index in [0, 7000) een UNIEKE (voor+achternaam)-combinatie. De oude formule
		// (Index*37 + Index/7) had een verborgen periode van 70 (Index en Index+70 -> zelfde naam), waardoor de
		// effectieve ruimte ~70 namen was en de uniekheid-retry-loop in EnsureSeeded naar 199M iteraties
		// explodeerde (~27s stall = de hoofdoorzaak van de trage beach-load). LET OP: de deler 70 MOET gelijk
		// zijn aan het aantal voornamen in CleanFirstName (First[]).
		const int32 LastIdx = (FMath::Abs(Index) / 70) % UE_ARRAY_COUNT(Last);
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
	TArray<FString> BaseNames;
	int32 RowIndex = 0;
	for (const FName& RowName : NpcTable->GetRowNames())
	{
		const FNpcDef* Def = NpcTable->FindRow<FNpcDef>(RowName, TEXT("EnsureSeeded"), false);
		if (!Def)
		{
			continue;
		}
		BaseNames.Add(Def->DisplayName.ToString());
		FNpcState S;
		S.NpcId = RowName;
		// De bijectieve naam-mapping (zie ShortFullName) maakt de eerste poging vrijwel altijd al uniek.
		// De cap (Try < 128) is een vangrail: nooit meer eindeloos rondtollen mocht de naam-pool ooit krimpen.
		FString UniqueName = ShortFullName(Def->DisplayName.ToString(), RowIndex);
		for (int32 Try = 1; UsedNames.Contains(UniqueName) && Try < 128; ++Try)
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
	// Pool aanvullen tot ~250 zodat de straat-crowd dagelijks door genoeg verschillende mensen kan roteren.
	// Procedureel: eigen NpcId (= eigen seed -> eigen skin + persoonlijkheid), naam = variant van een bestaande naam.
	{
		const int32 PoolTarget = 250;
		int32 Pad = 0;
		while (States.Num() < PoolTarget && BaseNames.Num() > 0)
		{
			const FName Id(*FString::Printf(TEXT("NpcGen%03d"), 101 + Pad));
			FNpcState S;
			S.NpcId = Id;
			FString Nm = ShortFullName(BaseNames[Pad % BaseNames.Num()], 1000 + Pad);
			for (int32 Try = 1; UsedNames.Contains(Nm) && Try < 128; ++Try) { Nm = ShortFullName(BaseNames[Pad % BaseNames.Num()], 1000 + Pad + Try * 97); }
			UsedNames.Add(Nm);
			S.DisplayName = FText::FromString(Nm);
			PredictPersonality(Id, S.Respect, S.Loyalty, S.Addiction);
			FRandomStream Rng(static_cast<int32>(GetTypeHash(Id)) ^ 0x5bd1e995);
			S.ValueMult = 0.6f + Rng.FRand() * 1.0f;
			States.Add(S);
			++Pad;
		}
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

void UNpcRegistryComponent::SetDisplayName(FName NpcId, const FText& DisplayName)
{
	if (GetOwnerRole() != ROLE_Authority || DisplayName.IsEmpty()) { return; }
	if (FNpcState* S = Find(NpcId))
	{
		// Alleen bij een echte wijziging schrijven -> geen onnodige replicatie elke BuildAppearance.
		if (!S->DisplayName.EqualTo(DisplayName)) { S->DisplayName = DisplayName; }
	}
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
	// Persoonlijke klant-honger OOK vanuit de basis (bleef eerst op default 1.0 hangen -> per-speler band/XP
	// week af van de basis-entry). Kopieer de live basis-waarde (klopt ook voor oude saves); fallback = dezelfde
	// deterministische seed-formule als EnsureSeeded.
	if (const FNpcState* Base = Find(BaseNpc))
	{
		S.ValueMult = Base->ValueMult;
	}
	else
	{
		FRandomStream Rng(static_cast<int32>(GetTypeHash(BaseNpc)) ^ 0x5bd1e995);
		S.ValueMult = 0.6f + Rng.FRand() * 1.0f;
	}
	States.Add(S);
	return Key;
}

bool UNpcRegistryComponent::NotePlayerLoyalty(FName BaseNpc, const FString& PlayerId, float Loyalty, FString& OutPrevOwnerId)
{
	OutPrevOwnerId.Reset();
	if (GetOwnerRole() != ROLE_Authority || BaseNpc.IsNone() || PlayerId.IsEmpty()) { return false; }
	FNpcState* S = Find(BaseNpc);
	if (!S) { return false; }
	// Word je de nieuwe favoriet van deze klant?
	if (Loyalty <= S->TopLoyalty + 0.01f) { return false; } // (nog) niet de hoogste -> geen overname
	const FString Prev = S->TopPlayerId;
	const float PrevLoy = S->TopLoyalty;
	S->TopPlayerId = PlayerId;
	S->TopLoyalty = Loyalty;
	// Afpakken telt alleen als er al een ANDERE speler met een stevige band (>=30) zat.
	if (!Prev.IsEmpty() && Prev != PlayerId && PrevLoy >= 30.f)
	{
		OutPrevOwnerId = Prev;
		return true;
	}
	return false;
}

FString UNpcRegistryComponent::GetTopOwner(FName BaseNpc) const
{
	const FNpcState* S = Find(BaseNpc);
	return S ? S->TopPlayerId : FString();
}

int32 UNpcRegistryComponent::CountPlayerCustomers(const FString& PlayerId) const
{
	if (PlayerId.IsEmpty()) { return 0; }
	const FString Suffix = TEXT("#") + PlayerId;
	int32 N = 0;
	for (const FNpcState& S : States)
	{
		if (S.NpcId.ToString().EndsWith(Suffix) && (S.bUnlocked || S.Loyalty >= 20.f)) { ++N; }
	}
	return N;
}

FNpcState* UNpcRegistryComponent::Find(FName NpcId)
{
	return States.FindByPredicate([NpcId](const FNpcState& S) { return S.NpcId == NpcId; });
}

const FNpcState* UNpcRegistryComponent::Find(FName NpcId) const
{
	return States.FindByPredicate([NpcId](const FNpcState& S) { return S.NpcId == NpcId; });
}

FName UNpcRegistryComponent::ResolveNpcKey(FName BaseNpc, const FString& PlayerId) const
{
	// PlayerId leeg OF geen competitive -> gewoon de basis-NPC (co-op/solo bit-voor-bit ongewijzigd).
	if (PlayerId.IsEmpty()) { return BaseNpc; }
	const AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());
	if (!GS || !GS->IsCompetitive()) { return BaseNpc; }
	// Zelfde sleutel-formaat als EnsurePlayerNpc: "BaseNpc#PlayerId".
	return FName(*FString::Printf(TEXT("%s#%s"), *BaseNpc.ToString(), *PlayerId));
}

FNpcState* UNpcRegistryComponent::FindOrAddPlayerEntry(FName BaseNpc, FName Key)
{
	if (FNpcState* Existing = Find(Key)) { return Existing; }
	// Nog geen relatie-entry (bv. eerste weigering/afspraak vóór een deal) -> maak 'm aan zodat de
	// dual-write niet verloren gaat. EnsurePlayerNpc seed personality + ValueMult vanuit de basis.
	const FNpcState* Base = Find(BaseNpc);
	EnsurePlayerNpc(Key, BaseNpc, Base ? Base->DisplayName : FText());
	return Find(Key);
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

void UNpcRegistryComponent::NoteAppointment(FName NpcId, const FString& PlayerId)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	FNpcState* S = Find(NpcId);
	if (!S) { return; }
	const int32 Day = CurrentDay();
	if (S->ApptDay != Day) { S->ApptDay = Day; S->ApptCountToday = 0; }
	++S->ApptCountToday;
	S->LastApptAbs = NowAbs(); // start de cooldown zodat dezelfde persoon niet blijft vragen
	// Competitive: de afspraak-cooldown geldt per speler -> ALLEEN LastApptAbs ook op de per-speler entry.
	// De dag-cap (ApptDay/ApptCountToday) blijft bewust op de basis (gedeeld over spelers).
	const FName Key = ResolveNpcKey(NpcId, PlayerId);
	if (Key != NpcId)
	{
		if (FNpcState* PS = FindOrAddPlayerEntry(NpcId, Key)) { PS->LastApptAbs = S->LastApptAbs; }
	}
}

void UNpcRegistryComponent::MarkDealt(FName NpcId, const FString& PlayerId)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	const float Now = NowAbs();
	// Basis ALTIJD markeren (o.a. AssignNpc-rotatie blijft basis-gedreven) + de per-speler entry (dual-write).
	if (FNpcState* S = Find(NpcId)) { S->LastDealAbs = Now; }
	const FName Key = ResolveNpcKey(NpcId, PlayerId);
	if (Key != NpcId)
	{
		if (FNpcState* PS = FindOrAddPlayerEntry(NpcId, Key)) { PS->LastDealAbs = Now; }
	}
}

void UNpcRegistryComponent::MarkRefused(FName NpcId, const FString& PlayerId)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	const float Now = NowAbs();
	// Dual-write: basis + per-speler entry (competitive), zodat de her-aanbied-cooldown per speler klopt.
	if (FNpcState* S = Find(NpcId)) { S->LastRefusalAbs = Now; }
	const FName Key = ResolveNpcKey(NpcId, PlayerId);
	if (Key != NpcId)
	{
		if (FNpcState* PS = FindOrAddPlayerEntry(NpcId, Key)) { PS->LastRefusalAbs = Now; }
	}
}

bool UNpcRegistryComponent::IsOnRefusalCooldown(FName NpcId, const FString& PlayerId) const
{
	// Per-speler read: onbekende per-key = verse relatie -> geen cooldown.
	const FNpcState* S = Find(ResolveNpcKey(NpcId, PlayerId));
	if (!S) { return false; }
	const float Now = NowAbs();
	return (S->LastRefusalAbs >= 0.f) && ((Now - S->LastRefusalAbs) < RefusalCooldownSeconds);
}

void UNpcRegistryComponent::MarkSampled(FName NpcId, const FString& PlayerId)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	const float Now = NowAbs();
	// Dual-write: basis + per-speler entry (competitive), zodat sample-maxen per speler geblokt blijft.
	if (FNpcState* S = Find(NpcId)) { S->LastSampleAbs = Now; }
	const FName Key = ResolveNpcKey(NpcId, PlayerId);
	if (Key != NpcId)
	{
		if (FNpcState* PS = FindOrAddPlayerEntry(NpcId, Key)) { PS->LastSampleAbs = Now; }
	}
}

bool UNpcRegistryComponent::IsOnSampleCooldown(FName NpcId, const FString& PlayerId) const
{
	// Per-speler read: onbekende per-key = verse relatie -> geen cooldown.
	const FNpcState* S = Find(ResolveNpcKey(NpcId, PlayerId));
	if (!S) { return false; }
	const float Now = NowAbs();
	return (S->LastSampleAbs >= 0.f) && ((Now - S->LastSampleAbs) < SampleCooldownSeconds);
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

int32 UNpcRegistryComponent::GetCustomerXP(FName NpcId, const FString& PlayerId) const
{
	// Per-speler read via de resolver; onbekende per-key = verse relatie -> 0 XP.
	const FNpcState* S = Find(ResolveNpcKey(NpcId, PlayerId));
	return S ? S->CustomerXP : 0;
}

int32 UNpcRegistryComponent::GetCustomerTier(FName NpcId, const FString& PlayerId) const
{
	int32 T = TierFromXP(GetCustomerXP(NpcId, PlayerId));
	// SKIN-TIER-KOPPELING (speler-wens): NPC's met een "kant-en-klare" complete skin (crowd V==2 = schoolgirl/
	// gamergirl + Karl/Casual/Tony) zijn HOGERE-tier klanten. Maar pas ONTGRENDELD op level 5 (Heavy User/tier 3)
	// en 10 (VIP/tier 4): daarvoor tonen ze een modulaire casual-skin (zie BuildAppearance) en krijgen ze GEEN
	// tier-bodem, zodat look en gedrag matchen. Deterministisch op de look-seed -> geen save-mutatie, host==joiner.
	const uint32 LookSeed = ACustomerBase::StableLookSeed(NpcId);
	if (ACustomerBase::IsCompleteSkinUnlocked(LookSeed, ACustomerBase::SharedPlayerLevel(this)))
	{
		T = FMath::Max(T, ((LookSeed >> 5) & 1u) ? 4 : 3); // ~50/50 tier 3 (Heavy) vs 4 (VIP)
	}
	return T;
}

int32 UNpcRegistryComponent::GetOrAssignSkin(FName NpcId, int32 Tier, int32 Seed)
{
	FNpcState* S = Find(NpcId);
	if (!S) { return 0; }
	const int32 kSkinVer = 2; // bump dit om ALLE NPC's 1x opnieuw te laten rollen na een schema-wijziging
	if (S->SkinIndex >= 0 && S->SkinVer >= kSkinVer) { return S->SkinIndex; } // al toegewezen op huidig schema
	// 3-5 = MODULAIRE Casual (honderden combinaties); 0-2 Karl, 6-9 Tony (vaste meshes + kleur). Modulair is
	// dubbel gewogen zodat ~de helft van de bevolking de grote variatie krijgt (de rest blijft goedkoop).
	static const int32 Low[]  = { 0, 1, 2, 3, 4, 5, 3, 4, 5, 6, 7 };          // ~55% modulair
	static const int32 Mid[]  = { 0, 1, 2, 3, 4, 5, 3, 4, 5, 6, 7, 8, 9 };    // modulair + wat Tony
	static const int32 High[] = { 6, 7, 8, 9, 3, 4, 5, 3, 4, 5 };            // net geklede Tony + veel modulair
	const int32* Band = Low; int32 BN = (int32)UE_ARRAY_COUNT(Low);
	if (Tier >= 4)      { Band = High; BN = (int32)UE_ARRAY_COUNT(High); }
	else if (Tier == 3) { Band = Mid;  BN = (int32)UE_ARRAY_COUNT(Mid); }
	const uint32 H = (uint32)Seed * 2654435761u + 12345u;
	S->SkinIndex = Band[H % (uint32)BN];
	S->SkinVer = kSkinVer;
	return S->SkinIndex;
}

void UNpcRegistryComponent::AddCustomerValue(FName NpcId, int32 GramsSold, const FString& PlayerId)
{
	if (GetOwnerRole() != ROLE_Authority || GramsSold <= 0) { return; }
	// Verkochte grammen x loyaliteit-factor x persoonlijke honger. Grotere klanten klimmen vanzelf sneller.
	// Elke entry rekent met z'n EIGEN loyaliteit/honger (per-speler band groeit op eigen tempo).
	auto AddTo = [GramsSold](FNpcState& S)
	{
		const int32 Gain = FMath::Max(1, FMath::RoundToInt(GramsSold * (2.f + S.Loyalty / 50.f) * S.ValueMult));
		S.CustomerXP += Gain;
	};
	// Dual-write: basis blijft ALTIJD meegroeien (skin/productsmaak) + de per-speler entry (order-grootte).
	if (FNpcState* S = Find(NpcId)) { AddTo(*S); }
	const FName Key = ResolveNpcKey(NpcId, PlayerId);
	if (Key != NpcId)
	{
		if (FNpcState* PS = FindOrAddPlayerEntry(NpcId, Key)) { AddTo(*PS); }
	}
}

float UNpcRegistryComponent::GetTierProgress01(FName NpcId, const FString& PlayerId) const
{
	const int32 XP = GetCustomerXP(NpcId, PlayerId);
	const int32 Tier = TierFromXP(XP);
	if (Tier >= 5) { return 1.f; }
	static const int32 Floors[6] = { 0, 0, 80, 220, 500, 1000 };
	static const int32 Nexts[6]  = { 0, 80, 220, 500, 1000, 1000 };
	const int32 Lo = Floors[Tier]; const int32 Hi = Nexts[Tier];
	return (Hi > Lo) ? FMath::Clamp((float)(XP - Lo) / (float)(Hi - Lo), 0.f, 1.f) : 1.f;
}

void UNpcRegistryComponent::GetTierOrderGrams(FName NpcId, int32& OutMin, int32& OutMax, const FString& PlayerId) const
{
	// Tier van de RESOLVED key (per-speler band in competitive); ValueMult van de BASIS-entry
	// (immuun voor oude saves waar de per-key ValueMult nog niet geseed was).
	const FNpcState* Resolved = Find(ResolveNpcKey(NpcId, PlayerId));
	const FNpcState* Base = Find(NpcId);
	const int32 Tier = TierFromXP(Resolved ? Resolved->CustomerXP : 0);
	int32 Mn, Mx;
	switch (Tier)
	{
	case 5: Mn = 70; Mx = 150; break; // Whale  (V4)
	case 4: Mn = 30; Mx = 70;  break; // VIP
	case 3: Mn = 12; Mx = 30;  break; // Heavy User
	case 2: Mn = 5;  Mx = 12;  break; // Regular
	default: Mn = 2; Mx = 5;   break; // Casual
	}
	const float VM = Base ? FMath::Clamp(Base->ValueMult, 0.7f, 1.5f) : 1.f; // persoonlijke gulzigheid
	OutMin = FMath::Max(1, FMath::RoundToInt(Mn * VM));
	OutMax = FMath::Max(OutMin, FMath::RoundToInt(Mx * VM));
}

bool UNpcRegistryComponent::IsOnCooldown(FName NpcId, const FString& PlayerId) const
{
	// Per-speler read: onbekende per-key = verse relatie -> geen cooldown.
	const FNpcState* S = Find(ResolveNpcKey(NpcId, PlayerId));
	if (!S) { return false; }
	const float Now = NowAbs();
	// Cooldown na een echte deal OF na een afspraak-vraag (tegen blijven-vragen).
	if (S->LastDealAbs >= 0.f && (Now - S->LastDealAbs) < DealCooldownSeconds) { return true; }
	if (S->LastApptAbs >= 0.f && (Now - S->LastApptAbs) < DealCooldownSeconds * FMath::Max(0.1f, S->ApptCooldownMult)) { return true; }
	return false;
}

void UNpcRegistryComponent::SetApptCooldownMult(FName NpcId, float Mult, const FString& PlayerId)
{
	if (GetOwnerRole() != ROLE_Authority) { return; }
	const float Clamped = FMath::Clamp(Mult, 0.25f, 6.f);
	// Dual-write: basis + per-speler entry (competitive).
	if (FNpcState* S = Find(NpcId)) { S->ApptCooldownMult = Clamped; }
	const FName Key = ResolveNpcKey(NpcId, PlayerId);
	if (Key != NpcId)
	{
		if (FNpcState* PS = FindOrAddPlayerEntry(NpcId, Key)) { PS->ApptCooldownMult = Clamped; }
	}
}

FName UNpcRegistryComponent::AssignNpc()
{
	EnsureSeeded();
	if (States.Num() == 0)
	{
		return NAME_None;
	}
	// Sla NPC's over die net een deal deden (cooldown). Als iedereen op cooldown is, val terug op round-robin.
	// Per-speler '#'-relatie-entries ALTIJD overslaan: dat zijn geen uitdeelbare identiteiten (anders krijgt
	// de crowd een fantoom "Npc042#7656..." uitgedeeld).
	const int32 N = States.Num();
	for (int32 k = 0; k < N; ++k)
	{
		const int32 Idx = (AssignCursor + k) % N;
		if (IsPlayerRelationKey(States[Idx].NpcId)) { continue; }
		if (!IsOnCooldown(States[Idx].NpcId))
		{
			AssignCursor = Idx + 1;
			return States[Idx].NpcId;
		}
	}
	// Iedereen op cooldown -> eerstvolgende ECHTE basis-NPC vanaf de cursor (nooit een '#'-relatie-entry).
	for (int32 k = 0; k < N; ++k)
	{
		const int32 Idx = (AssignCursor + k) % N;
		if (IsPlayerRelationKey(States[Idx].NpcId)) { continue; }
		AssignCursor = Idx + 1;
		return States[Idx].NpcId;
	}
	return NAME_None; // theoretisch: registry bevat alleen relatie-entries
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

void UNpcRegistryComponent::ApplyStats(FName NpcId, float Respect, float Loyalty, float Addiction, APawn* DealingPawn)
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
	CheckUnlock(*S, DealingPawn);
}

void UNpcRegistryComponent::CheckUnlock(FNpcState& State, APawn* DealingPawn)
{
	// Nummer delen is RESPECT-gedreven: genoeg vertrouwen opgebouwd -> je krijgt z'n nummer.
	if (State.bUnlocked || State.Respect < UnlockRespect)
	{
		return;
	}
	State.bUnlocked = true;

	AWeedShopGameState* GS = Cast<AWeedShopGameState>(GetOwner());

	// COMPETITIVE: het contact hoort bij de DEALENDE speler (OwnerPlayerId). Co-op (nullptr/niet-competitive) =
	// gedeeld (leeg). De registry-sleutel State.NpcId kan een "#spelerId"-suffix dragen (per-speler-entry);
	// RegisterContact strip die zelf naar de BASIS-NpcId, zodat het contact op de basis-id staat.
	FString OwnerId;
	if (GS && GS->IsCompetitive() && DealingPawn)
	{
		OwnerId = USaveGameSubsystem::StablePlayerId(DealingPawn);
	}

	if (GS && GS->GetContacts())
	{
		GS->GetContacts()->RegisterContact(State.NpcId, State.DisplayName, State.Loyalty, OwnerId);
	}

	UE_LOG(LogWeedShop, Log, TEXT("Number unlocked: %s"), *State.DisplayName.ToString());
	// Per-speler unlock-toast: alleen de dealende speler (competitive); co-op = alle spelers.
	if (GEngine)
	{
		const FString Note = FString::Printf(TEXT("You got %s's number!"), *State.DisplayName.ToString());
		if (!OwnerId.IsEmpty() && GetWorld())
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
				if (P && USaveGameSubsystem::StablePlayerId(P) == OwnerId)
				{
					UWeedToast::NotifyPawn(P, -1, 5.f, FColor(120, 200, 255), Note);
					break;
				}
			}
		}
		else
		{
			// Co-op: gedeeld contact -> iedereen ziet de unlock. NotifyAllPawns route per speler (host lokaal +
			// joiners via Client-RPC); een kaal Notify() bereikt op een listen-server alleen het host-scherm,
			// waardoor de joiner z'n eigen "nummer ontgrendeld"-toast miste.
			UWeedToast::NotifyAllPawns(this, -1, 5.f, FColor(120, 200, 255), Note);
		}
	}
}

int32 UNpcRegistryComponent::GetUnlockedCount() const
{
	int32 N = 0;
	for (const FNpcState& S : States)
	{
		// Per-speler '#'-relatie-entries niet meetellen (anders telt dezelfde persoon dubbel).
		if (IsPlayerRelationKey(S.NpcId)) { continue; }
		if (S.bUnlocked) ++N;
	}
	return N;
}

int32 UNpcRegistryComponent::GetTotalCount() const
{
	// Alleen echte basis-NPC's tellen; per-speler '#'-relatie-entries zijn geen extra mensen.
	int32 N = 0;
	for (const FNpcState& S : States)
	{
		if (!IsPlayerRelationKey(S.NpcId)) { ++N; }
	}
	return N;
}
