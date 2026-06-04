// UWeedToast — nette in-game meldingen midden-boven in beeld (i.p.v. de debug-tekst linksboven).
// Gebruik overal UWeedToast::Notify(...) met DEZELFDE argumenten als GEngine->AddOnScreenDebugMessage,
// zodat het een drop-in vervanging is. Toont een stapel korte pillen die zacht in-/uitfaden.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WeedToast.generated.h"

class UVerticalBox;
class UCanvasPanel;

UCLASS()
class WEEDSHOPCORE_API UWeedToast : public UUserWidget
{
	GENERATED_BODY()

public:
	// Drop-in vervanging voor GEngine->AddOnScreenDebugMessage(Key, Time, Color, Msg). Toont LOKAAL.
	static void Notify(int32 Key, float Time, const FColor& Color, const FString& Msg);

	// Co-op: toon de melding bij de SPELER die de actie deed (ForActor = de pawn of een component/actor
	// waarvan de eigenaar de speler is). Routeert via de PhoneClientComponent naar de juiste client.
	// Valt terug op lokaal tonen als er geen speler-component gevonden wordt.
	static void NotifyPawn(class AActor* ForActor, int32 Key, float Time, const FColor& Color, const FString& Msg);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void Push(int32 Key, float Time, const FLinearColor& Color, const FString& Msg);

	struct FEntry { FString Msg; FLinearColor Color; int32 Key = -1; float Born = 0.f; float Expire = 0.f; };
	TArray<FEntry> Entries;

	UPROPERTY() TObjectPtr<UVerticalBox> Stack;

	FString LastSig;

	static TWeakObjectPtr<UWeedToast> Active;
};
