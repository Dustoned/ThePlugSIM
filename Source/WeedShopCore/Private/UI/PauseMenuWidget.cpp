#include "UI/PauseMenuWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Save/SaveGameSubsystem.h"
#include "GameFramework/Pawn.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
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

	// Zoek recursief het eerste UTextBlock onder een widget (loopt door Content- en Panel-children). Gebruikt
	// om het label van een kit-knop te vinden zonder de exacte (onbekende) WBP-boomstructuur te kennen.
	UTextBlock* FindFirstText(UWidget* W)
	{
		if (!W) { return nullptr; }
		if (UTextBlock* T = Cast<UTextBlock>(W)) { return T; }
		if (UContentWidget* C = Cast<UContentWidget>(W))
		{
			if (UTextBlock* T = FindFirstText(C->GetContent())) { return T; }
		}
		if (UPanelWidget* P = Cast<UPanelWidget>(W))
		{
			const int32 N = P->GetChildrenCount();
			for (int32 i = 0; i < N; ++i)
			{
				if (UTextBlock* T = FindFirstText(P->GetChildAt(i))) { return T; }
			}
		}
		return nullptr;
	}
}

void UPauseMenuWidget::RelabelButton(UButton* Btn, const FString& NewLabel)
{
	// Hertitel het label-TextBlock van een kit-knop. Faalt stil als er geen TextBlock in de knop zit
	// (dan blijft de originele tekst staan; de knop werkt sowieso al door de herbinding hierboven).
	if (!Btn) { return; }
	if (UTextBlock* T = FindFirstText(Btn->GetContent()))
	{
		T->SetText(FText::FromString(NewLabel));
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
	// Palet-card: paneel-kleur + dunne stroke-rand (card-idioom uit COMMON).
	FSlateBrush PanelBr = WeedUI::Rounded(WeedUI::ColPanel(0.98f), 18.f);
	PanelBr.OutlineSettings.Width = 1.f;
	PanelBr.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f));
	Panel->SetBrush(PanelBr);
	Panel->SetPadding(FMargin(22.f));
	Sz->SetContent(Panel);

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(VB);

	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("PAUSED"), 24, WeedUI::ColAccent(), true, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

	auto AddBtn = [this, VB](const FString& Label, const FLinearColor& Col, TFunction<void()> Fn) -> UWeedActionButton*
	{
		UWeedActionButton* B = PauseBtn(WidgetTree, Label, Col, Fn);
		VB->AddChildToVerticalBox(B)->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
		return B;
	};

	// Alle acties = één neutrale ColInner-stijl; alleen Quit is destructief -> ColWarn.
	AddBtn(TEXT("Resume"),        WeedUI::ColInner(), [this]() { OnResume(); });
	AddBtn(TEXT("Unstuck"),       WeedUI::ColInner(), [this]() { OnUnstuck(); });
	AddBtn(TEXT("Settings"),      WeedUI::ColInner(), [this]() { OnSettings(); });
	AddBtn(TEXT("Save game"),     WeedUI::ColInner(), [this]() { OnSave(); });
	UWeedActionButton* LoadBtn = AddBtn(TEXT("Load game"), WeedUI::ColInner(), [this]() { OnLoad(); });
	UWeedActionButton* MenuBtn = AddBtn(TEXT("Main menu"), WeedUI::ColInner(), [this]() { OnMainMenu(); });
	AddBtn(TEXT("Quit to desktop"), WeedUI::ColWarn(0.85f), [this]() { OnQuit(); });

	// CO-OP: Load/Main menu doen een CLIENT-lokale OpenLevel/ClientTravel -> die zou een joiner uit de
	// co-op-sessie scheuren naar een solo save. Alleen de host (Standalone of ListenServer) mag laden/naar
	// het menu. Op de joiner verbergen we die twee EN bieden we een echte "Leave session"-knop aan (nette
	// ReturnToMainMenu-teardown i.p.v. alleen wegverbergen). De widget wordt 1x gebouwd (geen rebuild-per-
	// klik), dus dit is meteen persistent.
	if (UWorld* W = GetWorld())
	{
		if (W->GetNetMode() == NM_Client)
		{
			if (LoadBtn) { LoadBtn->SetVisibility(ESlateVisibility::Collapsed); }
			if (MenuBtn) { MenuBtn->SetVisibility(ESlateVisibility::Collapsed); }

			// Zichtbare knop voor de joiner: verlaat de sessie netjes (ColWarn, zelfde destructieve-actie-
			// stijl als Quit). Komt onderaan te staan (na Quit) zodat de "weg"-acties bij elkaar staan.
			AddBtn(TEXT("Leave session"), WeedUI::ColWarn(0.85f), [this]() { OnLeaveSession(); });
		}
	}

	StatusText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColTextDim(), true);
	VB->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
}

void UPauseMenuWidget::OnResume()
{
	if (PhoneComp.IsValid()) { PhoneComp->ClosePause(); }
}

void UPauseMenuWidget::OnUnstuck()
{
	// Roep WeedUnstuck (UFUNCTION op de speler-pawn) via reflectie aan - de pawn-class zit in de boven-
	// module, dus we kunnen 'm hier niet rechtstreeks includen. Daarna het menu sluiten zodat je de
	// teleport ziet. WeedUnstuck zelf zet je op de dichtstbijzijnde begaanbare plek (weg), niet thuis.
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (UFunction* Fn = P->FindFunction(FName(TEXT("WeedUnstuck")))) { P->ProcessEvent(Fn, nullptr); }
	}
	if (PhoneComp.IsValid()) { PhoneComp->ClosePause(); }
}

void UPauseMenuWidget::OnSave()
{
	// Via de telefoon -> host doet de echte save (neemt alle spelers mee). Werkt voor host én client.
	// De bevestiging ("Saving..." -> "Saved") toont de save-indicator rechtsboven (voor iedereen).
	if (PhoneComp.IsValid()) { PhoneComp->RequestSaveGame(); }
	if (StatusText) { StatusText->SetText(FText::GetEmpty()); }
}

void UPauseMenuWidget::OnLoad()
{
	// Open het titelscherm met de Load-slot-keuze (zelfde scherm als het hoofdmenu).
	if (PhoneComp.IsValid()) { PhoneComp->OpenMainMenuLoad(); }
}

void UPauseMenuWidget::OnSettings()
{
	// Open het settings-scherm bovenop het pauze-menu.
	if (PhoneComp.IsValid()) { PhoneComp->OpenSettings(); }
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

void UPauseMenuWidget::OnLeaveSession()
{
	// CO-OP joiner: verlaat de sessie netjes. ReturnToMainMenu doet de net-teardown (disconnect van de host)
	// EN de travel terug naar het menu in een keer -> zelfde bestemming als de boot, geen lokale OpenLevel
	// die andere clients zou raken. Werkt alleen zinvol op een client; op de host tonen we deze knop niet.
	if (UGameInstance* GI = GetGameInstance()) { GI->ReturnToMainMenu(); }
}

void UPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// WBP-pad: dit is een WBP-subclass met een eigen (kit-gestylde) tree -> bind de named knoppen aan de
	// handlers. Op de pure C++-fallback bestaan deze namen niet -> no-op (de BuildShell-knoppen binden zelf).
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("Btn_Resume"))))   { B->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnResume); }
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("Btn_Unstuck"))))  { B->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnUnstuck); }
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("Btn_Settings")))) { B->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnSettings); }
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("Btn_Save"))))     { B->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnSave); }
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("Btn_Load"))))     { B->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnLoad); }
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("Btn_MainMenu")))) { B->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnMainMenu); }
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("Btn_Quit"))))     { B->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnQuit); }
	// CO-OP: op een client (joiner) doet 'Load game' een lokale OpenLevel die de joiner uit de sessie scheurt
	// -> die knop verbergen. 'Main menu' hergebruiken we als "Leave session"-knop: de handler herbinden naar
	// OnLeaveSession (nette ReturnToMainMenu-teardown i.p.v. de solo ShowMainMenu-overlay) en het label
	// hertitelen. Het WBP-pad (WBP_PauseMenu, eigen WidgetTree-root) slaat BuildShell OVER, dus deze guard MOET
	// hier in NativeConstruct staan (draait op BEIDE paden). Op de C++-fallback bestaan de namen niet ->
	// GetWidgetFromName geeft null -> no-op (daar regelt BuildShell de guard al).
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		if (UWidget* WL = GetWidgetFromName(TEXT("Btn_Load")))     { WL->SetVisibility(ESlateVisibility::Collapsed); }
		if (UButton* BM = Cast<UButton>(GetWidgetFromName(TEXT("Btn_MainMenu"))))
		{
			// Herbind: eerst de zojuist toegevoegde OnMainMenu-binding weghalen, dan OnLeaveSession binden.
			BM->OnClicked.RemoveDynamic(this, &UPauseMenuWidget::OnMainMenu);
			BM->OnClicked.AddDynamic(this, &UPauseMenuWidget::OnLeaveSession);
			// Label-swap: zoek het TextBlock in de knop-subtree en hertitel naar "Leave session". Lukt de swap
			// niet (afwijkende kit-structuur), dan blijft de knop staan met de werkende leave-flow -> dat is
			// belangrijker dan de exacte tekst.
			RelabelButton(BM, TEXT("Leave session"));
		}
	}
	if (UWidget* D = GetWidgetFromName(TEXT("Dim"))) { Backdrop = D; }
	if (UTextBlock* S = Cast<UTextBlock>(GetWidgetFromName(TEXT("StatusText")))) { StatusText = S; }
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
