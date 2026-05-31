// UPhoneWidget — de telefoon als echte UMG-UI (volledig in C++ opgebouwd), voor een iPhone-feel:
// afgeronde frame + scherm, nette Roboto-font, app-iconen met afgeronde hoeken + hover/press, en
// app-schermen met een back-knop. Wordt door UPhoneClientComponent aangemaakt en getoond.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "UI/WeedUiStyle.h"
#include "PhoneWidget.generated.h"

class UPhoneClientComponent;
class UPhoneWidget;
class UCanvasPanel;
class UBorder;
class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UUniformGridPanel;

// Knop die een actie-id + parameter onthoudt en bij klik terugroept naar de telefoon-widget.
UCLASS()
class WEEDSHOPCORE_API UPhoneButton : public UButton
{
	GENERATED_BODY()

public:
	int32 ActionId = 0;
	int32 ActionParam = 0;
	TWeakObjectPtr<UPhoneWidget> Owner;

	UFUNCTION()
	void HandleClicked();
};

UCLASS()
class WEEDSHOPCORE_API UPhoneWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone) { Phone = InPhone; }

	// Door de knoppen aangeroepen (Action: 0=open app[Param], 1=home, 2=sluit, 3=koop[Param],
	// 5=accept bericht, 6=decline bericht).
	void HandlePhoneButton(int32 Action, int32 Param);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	TWeakObjectPtr<UPhoneClientComponent> Phone;

	// Opgebouwde onderdelen.
	UPROPERTY() TObjectPtr<UBorder> Frame;
	UPROPERTY() TObjectPtr<UTextBlock> TimeText;
	UPROPERTY() TObjectPtr<UTextBlock> LevelText;
	UPROPERTY() TObjectPtr<UTextBlock> CashText;
	UPROPERTY() TObjectPtr<UVerticalBox> ContentBox;

	// Staat-tracking voor het verversen van de inhoud.
	bool bLastHome = true;
	int32 bLastApp = -1;
	bool bContentDirty = true;

	void BuildShell(UCanvasPanel* Root);
	void RefreshContent();

	// Bouw-helpers.
	UTextBlock* MakeText(const FString& Txt, int32 Size, const FLinearColor& Col, bool bCenter = false);
	UWidget* MakeAppCell(int32 AppIndex, const FString& Name, WeedUI::EIcon Icon, const FLinearColor& Col);
	UPhoneButton* MakeButton(const FString& Label, int32 Action, int32 Param, const FLinearColor& Col);
	void AddInfoRow(const FString& Txt, const FLinearColor& Col, int32 Size = 13);
};
