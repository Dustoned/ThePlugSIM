#include "UI/MainMenuWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Save/SaveGameSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/GameInstance.h"

void UMainMenuWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* MenuBtn(UWidgetTree* Tree, const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 12.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 12.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 12.f);
		S.NormalPadding = FMargin(16.f, 12.f); S.PressedPadding = FMargin(16.f, 12.f);
		B->SetStyle(S);
		B->SetContent(WeedUI::Text(Tree, Label, 17, FLinearColor::White, true, true));
		return B;
	}

	USaveGameSubsystem* GetSave(const UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<USaveGameSubsystem>() : nullptr;
	}
}

TSharedRef<SWidget> UMainMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UMainMenuWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Volledig dekkende basis-achtergrond (heel donker groen). De wereld erachter is verborgen.
	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuBg"));
	Bg->SetBrush(WeedUI::Rounded(FLinearColor(0.035f, 0.06f, 0.045f, 1.f), 0.f));
	Backdrop = Bg;
	UCanvasPanelSlot* BS = Root->AddChildToCanvas(Bg);
	BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BS->SetOffsets(FMargin(0.f));

	// Overlay om meerdere lagen op elkaar te stapelen (achtergrond-kunst + menu).
	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
	Bg->SetContent(Layers);

	auto AddLayer = [this, Layers](UWidget* W, EHorizontalAlignment HA, EVerticalAlignment VA, const FMargin& Pad) -> UOverlaySlot*
	{
		UOverlaySlot* S = Layers->AddChildToOverlay(W);
		S->SetHorizontalAlignment(HA); S->SetVerticalAlignment(VA); S->SetPadding(Pad);
		return S;
	};

	// 1) Diagonale kleur-accenten (groen-tinten) als grote, schuine "banner"-vlakken.
	{
		USizeBox* StripeBox = WidgetTree->ConstructWidget<USizeBox>();
		StripeBox->SetHeightOverride(260.f);
		UBorder* Stripe = WidgetTree->ConstructWidget<UBorder>();
		Stripe->SetBrush(WeedUI::Rounded(FLinearColor(0.10f, 0.22f, 0.13f, 0.55f), 0.f));
		StripeBox->SetContent(Stripe);
		AddLayer(StripeBox, HAlign_Fill, VAlign_Top, FMargin(0.f, 70.f, 0.f, 0.f));
	}
	// 2) Donkere band onderaan (diepte/vignette-gevoel).
	{
		USizeBox* BandBox = WidgetTree->ConstructWidget<USizeBox>();
		BandBox->SetHeightOverride(300.f);
		UBorder* Band = WidgetTree->ConstructWidget<UBorder>();
		Band->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.45f), 0.f));
		BandBox->SetContent(Band);
		AddLayer(BandBox, HAlign_Fill, VAlign_Bottom, FMargin(0.f));
	}
	// 3) Groot wiet-blad watermerk in het midden (subtiel).
	{
		AddLayer(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Leaf, 560.f, FLinearColor(0.30f, 0.65f, 0.32f, 0.12f)),
			HAlign_Center, VAlign_Center, FMargin(0.f, 30.f, 0.f, 0.f));
	}
	// 4) Zachte groene "glow" achter de titel.
	{
		USizeBox* GlowBox = WidgetTree->ConstructWidget<USizeBox>();
		GlowBox->SetWidthOverride(720.f); GlowBox->SetHeightOverride(280.f);
		UBorder* Glow = WidgetTree->ConstructWidget<UBorder>();
		Glow->SetBrush(WeedUI::Rounded(FLinearColor(0.15f, 0.55f, 0.25f, 0.18f), 140.f));
		GlowBox->SetContent(Glow);
		AddLayer(GlowBox, HAlign_Center, VAlign_Top, FMargin(0.f, 120.f, 0.f, 0.f));
	}

	// 5) De menu-inhoud zelf, gecentreerd, op een halftransparante kaart voor leesbaarheid.
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrush(WeedUI::Rounded(FLinearColor(0.04f, 0.06f, 0.05f, 0.55f), 22.f));
	Card->SetPadding(FMargin(40.f, 28.f, 40.f, 32.f));
	Card->SetHorizontalAlignment(HAlign_Center);
	Card->SetVerticalAlignment(VAlign_Center);
	AddLayer(Card, HAlign_Center, VAlign_Center, FMargin(0.f));

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Card->SetContent(VB);

	// Titel.
	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("THE PLUG"), 60, FLinearColor(0.45f, 1.f, 0.5f), true, true));
	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("SIMULATOR"), 42, FLinearColor(0.88f, 0.96f, 1.f), true, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("grow  .  dry  .  pack  .  deal"), 14, FLinearColor(0.55f, 0.7f, 0.6f), true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 28.f));

	// Knoppen in een vaste-breedte kolom.
	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(320.f);
	UVerticalBox* Btns = WidgetTree->ConstructWidget<UVerticalBox>();
	Sz->SetContent(Btns);
	VB->AddChildToVerticalBox(Sz)->SetHorizontalAlignment(HAlign_Center);

	auto AddBtn = [this, Btns](const FString& Label, const FLinearColor& Col, TFunction<void()> Fn) -> UWeedActionButton*
	{
		UWeedActionButton* B = MenuBtn(WidgetTree, Label, Col, Fn);
		Btns->AddChildToVerticalBox(B)->SetPadding(FMargin(0.f, 5.f, 0.f, 5.f));
		return B;
	};

	AddBtn(TEXT("Start"),    FLinearColor(0.20f, 0.52f, 0.30f), [this]() { OnStart(); });
	ContinueBtn = AddBtn(TEXT("Continue"), FLinearColor(0.22f, 0.40f, 0.52f), [this]() { OnContinue(); });
	AddBtn(TEXT("Settings"), FLinearColor(0.24f, 0.30f, 0.42f), [this]() { OnSettings(); });
	AddBtn(TEXT("Quit"),     FLinearColor(0.48f, 0.20f, 0.20f), [this]() { OnQuit(); });

	StatusText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.7f, 0.85f, 1.f), true);
	VB->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
}

void UMainMenuWidget::OnStart()
{
	if (PhoneComp.IsValid()) { PhoneComp->HideMainMenu(); }
}

void UMainMenuWidget::OnContinue()
{
	bool bOk = false;
	if (USaveGameSubsystem* Save = GetSave(GetWorld()))
	{
		bOk = Save->HasSave() && Save->LoadGame();
	}
	if (bOk)
	{
		if (PhoneComp.IsValid()) { PhoneComp->HideMainMenu(); }
	}
	else if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("No save found.")));
	}
}

void UMainMenuWidget::OnSettings()
{
	// Verberg het titelscherm en open de telefoon-Settings-app.
	if (PhoneComp.IsValid()) { PhoneComp->HideMainMenu(); PhoneComp->OpenToApp(4); }
}

void UMainMenuWidget::OnQuit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UMainMenuWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsMainMenuOpen();
	if (Backdrop) { Backdrop->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (bOpen != bLastOpen)
	{
		bLastOpen = bOpen;
		if (bOpen)
		{
			if (StatusText) { StatusText->SetText(FText::GetEmpty()); }
			// "Continue" dimmen als er geen save is.
			if (ContinueBtn)
			{
				const USaveGameSubsystem* Save = GetSave(GetWorld());
				ContinueBtn->SetIsEnabled(Save && Save->HasSave());
			}
		}
	}
}
