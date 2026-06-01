// UMainMenuWidget — het titelscherm ("THE PLUG SIMULATOR"). Volledig dekkend overlay met
// Start / Continue (load) / Settings / Quit. Wordt bij het opstarten getoond en is opnieuw te
// openen via het pauze-menu ("Main menu"). Zichtbaarheid volgt UPhoneClientComponent.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UTextBlock;

UCLASS()
class WEEDSHOPCORE_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	void OnStart();
	void OnContinue();
	void OnSettings();
	void OnQuit();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Backdrop;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<class UWeedActionButton> ContinueBtn;

	bool bLastOpen = false;
};
