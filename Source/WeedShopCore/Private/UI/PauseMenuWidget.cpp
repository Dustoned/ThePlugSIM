#include "UI/PauseMenuWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Save/SaveGameSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/GameInstance.h"

void UPauseMenuWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* PauseBtn(UWidgetTree* Tree, const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 10.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 10.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 10.f);
		S.NormalPadding = FMargin(14.f, 10.f); S.PressedPadding = FMargin(14.f, 10.f);
		B->SetStyle(S);
		B->SetContent(WeedUI::Text(Tree, Label, 15, FLinearColor::White, true, true));
		return B;
	}
}

TSharedRef<SWidget> UPauseMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UPauseMenuWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Verduisterende achtergrond over het hele scherm.
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.72f), 0.f));
	Dim->SetHorizontalAlignment(HAlign_Center);
	Dim->SetVerticalAlignment(VAlign_Center);
	Backdrop = Dim;
	UCanvasPanelSlot* DS = Root->AddChildToCanvas(Dim);
	DS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	DS->SetOffsets(FMargin(0.f));

	// Menu-paneel in het midden.
	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(360.f);
	Dim->SetContent(Sz);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.99f), 18.f));
	Panel->SetPadding(FMargin(22.f));
	Sz->SetContent(Panel);

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(VB);

	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("PAUSED"), 24, FLinearColor(0.6f, 1.f, 0.6f), true, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

	auto AddBtn = [this, VB](const FString& Label, const FLinearColor& Col, TFunction<void()> Fn)
	{
		VB->AddChildToVerticalBox(PauseBtn(WidgetTree, Label, Col, Fn))->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
	};

	AddBtn(TEXT("Resume"),        FLinearColor(0.20f, 0.50f, 0.30f), [this]() { OnResume(); });
	AddBtn(TEXT("Settings"),      FLinearColor(0.24f, 0.30f, 0.42f), [this]() { OnSettings(); });
	AddBtn(TEXT("Save game"),     FLinearColor(0.22f, 0.40f, 0.52f), [this]() { OnSave(); });
	AddBtn(TEXT("Load game"),     FLinearColor(0.22f, 0.40f, 0.52f), [this]() { OnLoad(); });
	AddBtn(TEXT("Main menu"),     FLinearColor(0.42f, 0.32f, 0.20f), [this]() { OnMainMenu(); });
	AddBtn(TEXT("Quit to desktop"), FLinearColor(0.48f, 0.20f, 0.20f), [this]() { OnQuit(); });

	StatusText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.7f, 0.85f, 1.f), true);
	VB->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
}

void UPauseMenuWidget::OnResume()
{
	if (PhoneComp.IsValid()) { PhoneComp->ClosePause(); }
}

void UPauseMenuWidget::OnSave()
{
	// Via de telefoon -> host doet de echte save (neemt alle spelers mee). Werkt voor host én client.
	if (PhoneComp.IsValid()) { PhoneComp->RequestSaveGame(); }
	if (StatusText) { StatusText->SetText(FText::FromString(TEXT("Saving... (host writes the save)"))); }
}

void UPauseMenuWidget::OnLoad()
{
	bool bOk = false;
	if (const UWorld* W = GetWorld())
	{
		if (UGameInstance* GI = W->GetGameInstance())
		{
			if (USaveGameSubsystem* Save = GI->GetSubsystem<USaveGameSubsystem>())
			{
				bOk = Save->HasSave() && Save->LoadGame();
			}
		}
	}
	if (StatusText) { StatusText->SetText(FText::FromString(bOk ? TEXT("Game loaded.") : TEXT("No save found."))); }
}

void UPauseMenuWidget::OnSettings()
{
	// Hergebruik de telefoon-Settings-app (controls + status).
	if (PhoneComp.IsValid()) { PhoneComp->OpenToApp(4); }
}

void UPauseMenuWidget::OnMainMenu()
{
	// Toon het titelscherm-overlay (geen aparte map nodig).
	if (PhoneComp.IsValid()) { PhoneComp->ShowMainMenu(); }
}

void UPauseMenuWidget::OnQuit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UPauseMenuWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsPauseOpen();
	if (Backdrop) { Backdrop->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (bOpen != bLastOpen)
	{
		bLastOpen = bOpen;
		if (bOpen && StatusText) { StatusText->SetText(FText::GetEmpty()); } // status wissen bij heropenen
	}
}
