// GoalsComponent — vaste "milestone"-doelen (joints rollen, oogsten, deals, geld verdienen) met scalende
// item- + geld-rewards. Server-authoritative tellers, gerepliceerd. Apart van de quests.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GoalsComponent.generated.h"

// Eén doel. Metric: 0 = geld verdiend (cents), 1 = joints gerold, 2 = planten geoogst, 3 = deals gedaan.
USTRUCT()
struct FGoalDef
{
	GENERATED_BODY()
	FString Title;
	int32 Metric = 0;
	int64 Target = 0;
	FName RewardItem = NAME_None;
	int32 RewardQty = 0;
	int64 RewardMoneyCents = 0;
};

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UGoalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGoalsComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// De vaste lijst doelen (zelfde voor iedereen).
	static const TArray<FGoalDef>& Goals();

	int64 GetMetricValue(int32 Metric) const;             // huidige waarde van een metric
	int64 GetGoalProgress(int32 Idx) const;               // min(metric, target)
	bool IsComplete(int32 Idx) const;                     // metric >= target
	bool IsClaimed(int32 Idx) const;                      // reward al opgehaald
	int32 GetClaimableCount() const;                      // klaar + nog niet geclaimd (voor de badge)

	// Server-side tellers (vanuit de gameplay aangeroepen).
	void NoteJointsRolled(int32 N);
	void NoteHarvest(int32 N);
	void NoteDeal();

	// Markeer een doel als geclaimd (alleen server). True = nu pas geclaimd (caller geeft de reward).
	bool MarkClaimed(int32 Idx);

	UPROPERTY(Replicated) int32 JointsRolled = 0;
	UPROPERTY(Replicated) int32 PlantsHarvested = 0;
	UPROPERTY(Replicated) int32 DealsDone = 0;
	UPROPERTY(Replicated) TArray<int32> ClaimedIdx;

	// Save/load.
	void RestoreState(int32 Joints, int32 Harvests, int32 Deals, const TArray<int32>& Claimed);
};
