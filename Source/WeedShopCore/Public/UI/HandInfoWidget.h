// UHandInfoWidget — zachte "wat heb ik in m'n hand"-popup, rechts-midden in beeld. Toont de naam,
// een duidelijke type-tag (Seed / Dried bud / Baggie / Joint / Soil / Container / Spray / ...) en de
// relevante stats (gram, THC%, kwaliteit%, capaciteit). Faded zachtjes in/uit. Leest het actieve
// item van de lokale speler-inventory.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HandInfoWidget.generated.h"

class UWidget;
class UTextBlock;
class UBorder;
class USizeBox;
class UWrapBox;

UCLASS()
class WEEDSHOPCORE_API UHandInfoWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(class UCanvasPanel* Root);

	UPROPERTY() TObjectPtr<UWidget> Card;        // het hele kaartje (fade)
	UPROPERTY() TObjectPtr<UBorder> AccentBar;   // gekleurde balk links (type-kleur)
	UPROPERTY() TObjectPtr<UTextBlock> TypeText; // type-tag (SEED / BAGGIE / ...)
	UPROPERTY() TObjectPtr<UTextBlock> NameText; // item-naam
	UPROPERTY() TObjectPtr<UTextBlock> QtyText;  // aantal/gram, groot bij de titel
	UPROPERTY() TObjectPtr<UBorder> QtyPill;     // quantity badge naast de titel
	UPROPERTY() TObjectPtr<UBorder> Divider;     // dun lijntje onder de naam
	UPROPERTY() TObjectPtr<UWrapBox> ChipBox;    // vaste pool met stat-chips
	UPROPERTY() TArray<TObjectPtr<UBorder>> ChipPills;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ChipTexts;
	UPROPERTY() TObjectPtr<UTextBlock> HintText; // korte hint onderaan (wat je ermee doet)

	float Shown = 0.f;       // huidige fade (0..1)

	// Change-guard: herbouw tekst alleen bij wijziging. Losse velden i.p.v. een geprintfde
	// sleutel-string zodat de tick geen verse FString per frame alloceert (heap-churn).
	FName LastId;
	int32 LastQty = MIN_int32;
	int32 LastThcR = MIN_int32;  // THC afgerond op hele punten (zelfde korrel als de oude %.0f-sleutel)
	int32 LastQpctR = MIN_int32;
	int32 LastSid = MIN_int32;
	bool bLastRollLoaded = false;
	FString LastRollDesc;
	FName CachedIdStrId;     // welke Id CachedIdStr representeert
	FString CachedIdStr;     // Id.ToString()-cache: prefix-checks per frame zonder allocatie
};
