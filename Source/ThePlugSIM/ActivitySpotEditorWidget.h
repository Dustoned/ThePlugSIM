// UActivitySpotEditorWidget - net in-game menu om een geplaatste activity-NPC in te stellen: welke animatie,
// van welk uur tot welk uur hij verschijnt, en een wis-knop. Opent als je (host, free-build) een activity-NPC
// aankijkt en de plaats/bewerk-toets indrukt. Bewerkt rechtstreeks de AActivitySpotManager (server-only).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActivitySpotEditorWidget.generated.h"

class AActivitySpotManager;
class ACustomerBase;
class UCanvasPanel;
class UTextBlock;
class UBorder;

UCLASS()
class THEPLUGSIM_API UActivitySpotEditorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Open het menu voor deze NPC (laadt de huidige instellingen + pint 'm tegen despawnen).
	void Setup(AActivitySpotManager* InMgr, ACustomerBase* InNpc);
	bool IsEditing(const ACustomerBase* N) const { return Npc.Get() == N; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void BuildShell(UCanvasPanel* Root);
	void RefreshLabels();
	void Apply();
	void OnButton(int32 Action, int32 Param);
	void CloseSelf();

	TWeakObjectPtr<AActivitySpotManager> Mgr;
	TWeakObjectPtr<ACustomerBase> Npc;

	int32 AnimIdx = 0;
	int32 HourStart = 0;
	int32 HourEnd = 24;

	UPROPERTY() TObjectPtr<UTextBlock> AnimLabel = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> FromLabel = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> ToLabel = nullptr;
};
