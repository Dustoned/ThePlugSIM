// UPauseMenuWidget — pauze-/menu-overlay (ESC). Knoppen: Resume, Settings, Save, Load, Main Menu,
// Quit. Volledig in C++ opgebouwd; zichtbaarheid volgt UPhoneClientComponent::IsPauseOpen().

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PauseMenuWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UTextBlock;

UCLASS(Blueprintable)
class WEEDSHOPCORE_API UPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

	// Naam van de main-menu map die "Main Menu" laadt (pas aan als jouw menu-level anders heet).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pause")
	FName MainMenuLevel = TEXT("MainMenu");

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	UFUNCTION() void OnResume();
	UFUNCTION() void OnUnstuck();
	UFUNCTION() void OnSave();
	UFUNCTION() void OnLoad();
	UFUNCTION() void OnSettings();
	UFUNCTION() void OnMainMenu();
	UFUNCTION() void OnQuit();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Backdrop;   // verduisterende achtergrond + paneel
	UPROPERTY() TObjectPtr<UTextBlock> StatusText; // feedback (Saved / Loaded / ...)

	bool bLastOpen = false;
};
