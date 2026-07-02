#include "Save/AssetKeepAliveSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UAssetKeepAliveSubsystem::Deinitialize()
{
	Kept.Empty(); // roots loslaten -> assets komen bij de volgende GC gewoon vrij
	Super::Deinitialize();
}

void UAssetKeepAliveSubsystem::KeepAlive(UObject* Obj)
{
	if (!Obj) { return; }
	// Dedup op de raw pointer: KeepAlive wordt bij elke skin-/outfit-wissel opnieuw aangeroepen
	// met dezelfde (nu memory-resident) assets.
	if (Kept.ContainsByPredicate([Obj](const TStrongObjectPtr<UObject>& P) { return P.Get() == Obj; })) { return; }
	Kept.Emplace(Obj);
}

void UAssetKeepAliveSubsystem::Keep(const UObject* WorldContext, UObject* Obj)
{
	if (!WorldContext || !Obj) { return; }
	const UWorld* World = WorldContext->GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (UAssetKeepAliveSubsystem* Sys = GI ? GI->GetSubsystem<UAssetKeepAliveSubsystem>() : nullptr)
	{
		Sys->KeepAlive(Obj);
	}
}
