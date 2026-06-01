// USettingsWidget — instellingen-scherm met Graphics (window mode, kwaliteit, VSync, FPS-limiet)
// en Game (FOV, muisgevoeligheid). Graphics via UGameUserSettings; game-instellingen via de
// PhoneClientComponent op de pawn. Zichtbaarheid volgt UPhoneClientComponent::IsSettingsOpen().

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SettingsWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UVerticalBox;
class UTextBlock;

UCLASS()
class WEEDSHOPCORE_API USettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void RefreshTabs();
	void RefreshContent();

	UPhoneClientComponent* GetPhone() const;

	// Rij-helper: label links + een knop rechts met de huidige waarde (klik = OnClick).
	void AddValueRow(const FString& Label, const FString& Value, TFunction<void()> OnClick);
	// Rij-helper: label + [-] waarde [+].
	void AddStepRow(const FString& Label, const FString& Value, TFunction<void()> OnMinus, TFunction<void()> OnPlus);

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;     // rechter inhoud (per categorie)
	UPROPERTY() TObjectPtr<class UWeedActionButton> TabGraphics;
	UPROPERTY() TObjectPtr<class UWeedActionButton> TabGame;

	int32 Category = 0; // 0 = Graphics, 1 = Game
	bool bLastOpen = false;
};
