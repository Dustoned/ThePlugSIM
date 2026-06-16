#include "World/ActivitySpotManager.h"

#include "Customer/CustomerBase.h"
#include "Game/WeedShopGameState.h"
#include "World/DayCycleComponent.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

AActivitySpotManager::AActivitySpotManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f; // we throttlen zelf via EvalTimer
	bReplicates = false;                 // server-only: spawnt replicerende NPC's, zelf niet zichtbaar
}

FString AActivitySpotManager::GetSaveFile()
{
	return FPaths::ProjectSavedDir() / TEXT("ActivitySpots.txt");
}

FString AActivitySpotManager::CurrentMap() const
{
	return GetWorld() ? GetWorld()->GetOutermost()->GetName() : TEXT("?");
}

float AActivitySpotManager::CurrentHour() const
{
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const UDayCycleComponent* DC = GS ? GS->GetDayCycle() : nullptr;
	return DC ? DC->GetClockHour() : 12.f;
}

bool AActivitySpotManager::IsWindowActive(const FActivitySpotData& S, float Hour) const
{
	if (S.HourStart <= S.HourEnd) { return Hour >= S.HourStart && Hour < S.HourEnd; }
	// Over middernacht (bv. 22 -> 6): actief vanaf Start tot 24 en van 0 tot End.
	return Hour >= S.HourStart || Hour < S.HourEnd;
}

void AActivitySpotManager::BeginPlay()
{
	Super::BeginPlay();
	LoadSpots();
}

void AActivitySpotManager::LoadSpots()
{
	Spots.Reset();
	const FString Map = CurrentMap();
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *GetSaveFile())) { return; }
	for (const FString& Raw : Lines)
	{
		FString Line = Raw.TrimStartAndEnd();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) { continue; }
		TArray<FString> F;
		Line.ParseIntoArray(F, TEXT("|"), false);
		if (F.Num() < 9) { continue; }
		FActivitySpotData S;
		S.Name      = F[0].TrimStartAndEnd();
		S.Map       = F[1].TrimStartAndEnd();
		S.Pos       = FVector(FCString::Atof(*F[2]), FCString::Atof(*F[3]), FCString::Atof(*F[4]));
		S.Yaw       = FCString::Atof(*F[5]);
		S.HourStart = FCString::Atof(*F[6]);
		S.HourEnd   = FCString::Atof(*F[7]);
		S.AnimIdx   = FCString::Atoi(*F[8]);
		if (S.Map != Map) { continue; } // alleen spots van de huidige map
		Spots.Add(S);
	}
}

void AActivitySpotManager::RewriteFile() const
{
	// Herschrijf ALLE spots van deze map; spots van andere maps blijven behouden (eerst inlezen, mergen).
	const FString Map = CurrentMap();
	TArray<FString> Out;
	TArray<FString> Existing;
	if (FFileHelper::LoadFileToStringArray(Existing, *GetSaveFile()))
	{
		for (const FString& Raw : Existing)
		{
			const FString Line = Raw.TrimStartAndEnd();
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) { Out.Add(Raw); continue; }
			TArray<FString> F; Line.ParseIntoArray(F, TEXT("|"), false);
			if (F.Num() >= 9 && F[1].TrimStartAndEnd() == Map) { continue; } // huidige-map-regels droppen
			Out.Add(Raw);
		}
	}
	for (const FActivitySpotData& S : Spots)
	{
		Out.Add(FString::Printf(TEXT("%s|%s|%.0f|%.0f|%.0f|%.0f|%.1f|%.1f|%d"),
			*S.Name, *Map, S.Pos.X, S.Pos.Y, S.Pos.Z, S.Yaw, S.HourStart, S.HourEnd, S.AnimIdx));
	}
	FFileHelper::SaveStringArrayToFile(Out, *GetSaveFile(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

ACustomerBase* AActivitySpotManager::SpawnActivityNpc(const FActivitySpotData& S)
{
	UWorld* W = GetWorld();
	if (!W) { return nullptr; }
	const FTransform T(FRotator(0.f, S.Yaw, 0.f), S.Pos + FVector(0.f, 0.f, 20.f));
	ACustomerBase* Npc = W->SpawnActorDeferred<ACustomerBase>(
		ACustomerBase::StaticClass(), T, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Npc) { return nullptr; }
	Npc->BeginActivity(S.Pos, S.Yaw, S.AnimIdx); // zet bActivityNpc VOOR BeginPlay -> inert
	Npc->FinishSpawning(T);
	return Npc;
}

void AActivitySpotManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) { return; }
	EvalTimer -= DeltaSeconds;
	if (EvalTimer > 0.f) { return; }
	EvalTimer = 1.0f; // ~1x per seconde evalueren is ruim genoeg voor een spelklok

	const float Hour = CurrentHour();
	for (FActivitySpotData& S : Spots)
	{
		const bool bActive = IsWindowActive(S, Hour);
		const bool bHasNpc = S.Npc.IsValid();
		if (bActive && !bHasNpc)
		{
			S.Npc = SpawnActivityNpc(S);
		}
		else if (!bActive && bHasNpc)
		{
			if (S.Npc.Get() == EditingNpc.Get()) { continue; } // pinned tijdens bewerken -> niet despawnen
			S.Npc->Destroy();
			S.Npc = nullptr;
		}
	}
}

void AActivitySpotManager::SetEditingNpc(ACustomerBase* Npc) { EditingNpc = Npc; }

int32 AActivitySpotManager::IndexForNpc(const ACustomerBase* Npc) const
{
	if (!Npc) { return INDEX_NONE; }
	for (int32 i = 0; i < Spots.Num(); ++i) { if (Spots[i].Npc.Get() == Npc) { return i; } }
	return INDEX_NONE;
}

bool AActivitySpotManager::GetSpotSettings(ACustomerBase* Npc, int32& OutAnimIdx, float& OutStart, float& OutEnd) const
{
	const int32 i = IndexForNpc(Npc);
	if (i == INDEX_NONE) { return false; }
	OutAnimIdx = Spots[i].AnimIdx;
	OutStart   = Spots[i].HourStart;
	OutEnd     = Spots[i].HourEnd;
	return true;
}

void AActivitySpotManager::UpdateSpotForNpc(ACustomerBase* Npc, int32 AnimIdx, float HourStart, float HourEnd)
{
	const int32 i = IndexForNpc(Npc);
	if (i == INDEX_NONE) { return; }
	Spots[i].AnimIdx   = AnimIdx;
	Spots[i].HourStart = HourStart;
	Spots[i].HourEnd   = HourEnd;
	if (Npc) { Npc->SetActivityAnimNow(AnimIdx); } // live zichtbaar
	RewriteFile();
}

void AActivitySpotManager::RemoveSpotForNpc(ACustomerBase* Npc)
{
	const int32 i = IndexForNpc(Npc);
	if (i == INDEX_NONE) { return; }
	if (EditingNpc.Get() == Npc) { EditingNpc = nullptr; }
	if (Spots[i].Npc.IsValid()) { Spots[i].Npc->Destroy(); }
	Spots.RemoveAt(i);
	RewriteFile();
}

int32 AActivitySpotManager::AddSpotLive(const FVector& Pos, float Yaw, int32 AnimIdx, float HourStart, float HourEnd)
{
	FActivitySpotData S;
	S.Name      = FString::Printf(TEXT("act%d"), Spots.Num() + 1);
	S.Map       = CurrentMap();
	S.Pos       = Pos;
	S.Yaw       = Yaw;
	S.HourStart = HourStart;
	S.HourEnd   = HourEnd;
	S.AnimIdx   = AnimIdx;
	const int32 Index = Spots.Add(S);
	RewriteFile();
	// Direct feedback: spawn nu al als het tijdvak actief is.
	if (IsWindowActive(Spots[Index], CurrentHour()))
	{
		Spots[Index].Npc = SpawnActivityNpc(Spots[Index]);
	}
	return Spots.Num();
}

FString AActivitySpotManager::RemoveNearestSpot(const FVector& Near, float MaxDist)
{
	int32 Best = INDEX_NONE;
	float BestD = MaxDist;
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		const float D = FVector::Dist2D(Spots[i].Pos, Near);
		if (D < BestD) { BestD = D; Best = i; }
	}
	if (Best == INDEX_NONE) { return FString(); }
	const FString Name = Spots[Best].Name;
	if (Spots[Best].Npc.IsValid()) { Spots[Best].Npc->Destroy(); }
	Spots.RemoveAt(Best);
	RewriteFile();
	return Name;
}
