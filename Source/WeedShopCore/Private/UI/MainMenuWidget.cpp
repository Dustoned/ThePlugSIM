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
#include "Components/Image.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"

void UMainMenuWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* MenuBtn(UWidgetTree* Tree, const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 6.f);
		S.Hovered = WeedUI::Rounded(Col * 1.4f, 6.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 6.f);
		S.NormalPadding = FMargin(20.f, 12.f); S.PressedPadding = FMargin(20.f, 12.f);
		B->SetStyle(S);
		// Links uitgelijnde tekst (zoals de mockup-knoppen).
		UTextBlock* T = WeedUI::Text(Tree, Label.ToUpper(), 16, FLinearColor(0.95f, 0.96f, 1.f), false, true);
		T->SetJustification(ETextJustify::Left);
		B->SetContent(T);
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

UBorder* UMainMenuWidget::AddGlow(UOverlay* Layers, const FLinearColor& Color, float W, float H,
	EHorizontalAlignment HA, EVerticalAlignment VA, const FMargin& Pad, float Freq)
{
	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
	Box->SetWidthOverride(W); Box->SetHeightOverride(H);
	UBorder* Glow = WidgetTree->ConstructWidget<UBorder>();
	Glow->SetBrush(WeedUI::Rounded(Color, FMath::Min(W, H) * 0.5f)); // sterk afgerond = zachte "blob"
	Box->SetContent(Glow);
	UOverlaySlot* S = Layers->AddChildToOverlay(Box);
	S->SetHorizontalAlignment(HA); S->SetVerticalAlignment(VA); S->SetPadding(Pad);

	Glows.Add(Glow);
	GlowBase.Add(Color);
	GlowPhase.Add(Glows.Num() * 1.7f);   // uiteenlopende fasen zodat lampen niet synchroon flikkeren
	GlowFreq.Add(Freq);
	return Glow;
}

void UMainMenuWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Volledig dekkende basis-achtergrond (donkere kelder). De wereld erachter is verborgen.
	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuBg"));
	Bg->SetBrush(WeedUI::Rounded(FLinearColor(0.035f, 0.03f, 0.05f, 1.f), 0.f));
	Backdrop = Bg;
	UCanvasPanelSlot* BS = Root->AddChildToCanvas(Bg);
	BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BS->SetOffsets(FMargin(0.f));

	// Overlay om foto/lampen + menu op elkaar te stapelen.
	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
	Bg->SetContent(Layers);

	// Achtergrond-foto + logo vanaf schijf laden (losse PNG's in Content/_Project/UI).
	const FString UIDir = FPaths::ProjectContentDir() / TEXT("_Project/UI/");
	if (!BgTex)   { BgTex   = FImageUtils::ImportFileAsTexture2D(UIDir + TEXT("T_MainMenuBG.png")); }
	if (!LogoTex) { LogoTex = FImageUtils::ImportFileAsTexture2D(UIDir + TEXT("T_MainMenuLogo.png")); }

	if (BgTex)
	{
		// 1) Volledig-scherm achtergrond-foto.
		UImage* BgImg = WidgetTree->ConstructWidget<UImage>();
		BgImg->SetBrushFromTexture(BgTex, false);
		UOverlaySlot* IS = Layers->AddChildToOverlay(BgImg);
		IS->SetHorizontalAlignment(HAlign_Fill); IS->SetVerticalAlignment(VAlign_Fill);

		// 2) Subtiele flikker-gloeden over de lampen in de foto (plafond-buis, bordje, schap).
		AddGlow(Layers, FLinearColor(0.95f, 0.20f, 0.85f, 0.16f), 480.f, 36.f, HAlign_Center, VAlign_Top, FMargin(120.f, 70.f, 0.f, 0.f), 6.5f);
		AddGlow(Layers, FLinearColor(1.0f, 0.25f, 0.75f, 0.16f), 220.f, 120.f, HAlign_Center, VAlign_Center, FMargin(0.f, -40.f, 0.f, 0.f), 9.0f);
		AddGlow(Layers, FLinearColor(0.20f, 0.45f, 1.0f, 0.16f), 170.f, 440.f, HAlign_Right, VAlign_Center, FMargin(0.f, 0.f, 90.f, 0.f), 3.5f);

		// 3) Donkere "scrim" links zodat logo + menu goed leesbaar zijn op de foto.
		USizeBox* ScrimBox = WidgetTree->ConstructWidget<USizeBox>();
		ScrimBox->SetWidthOverride(640.f);
		UBorder* Scrim = WidgetTree->ConstructWidget<UBorder>();
		Scrim->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.42f), 0.f));
		ScrimBox->SetContent(Scrim);
		UOverlaySlot* ScS = Layers->AddChildToOverlay(ScrimBox);
		ScS->SetHorizontalAlignment(HAlign_Left); ScS->SetVerticalAlignment(VAlign_Fill);
	}
	else
	{
		// Geen foto gevonden -> val terug op de vector-neon-scene.
		AddGlow(Layers, FLinearColor(0.95f, 0.20f, 0.85f, 0.42f), 520.f, 40.f, HAlign_Center, VAlign_Top, FMargin(120.f, 70.f, 0.f, 0.f), 6.5f);
		AddGlow(Layers, FLinearColor(1.0f, 0.25f, 0.75f, 0.40f), 240.f, 130.f, HAlign_Center, VAlign_Center, FMargin(0.f, -40.f, 0.f, 0.f), 9.0f);
		AddGlow(Layers, FLinearColor(0.20f, 0.45f, 1.0f, 0.38f), 180.f, 460.f, HAlign_Right, VAlign_Center, FMargin(0.f, 0.f, 90.f, 0.f), 3.5f);
		AddGlow(Layers, FLinearColor(0.55f, 0.18f, 0.95f, 0.30f), 820.f, 360.f, HAlign_Center, VAlign_Bottom, FMargin(0.f, 0.f, 0.f, 0.f), 1.8f);
		AddGlow(Layers, FLinearColor(0.55f, 0.20f, 0.9f, 0.22f), 560.f, 360.f, HAlign_Left, VAlign_Center, FMargin(20.f, 0.f, 0.f, 0.f), 5.0f);

		USizeBox* BandBox = WidgetTree->ConstructWidget<USizeBox>();
		BandBox->SetHeightOverride(260.f);
		UBorder* Band = WidgetTree->ConstructWidget<UBorder>();
		Band->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.40f), 0.f));
		BandBox->SetContent(Band);
		UOverlaySlot* BSl = Layers->AddChildToOverlay(BandBox);
		BSl->SetHorizontalAlignment(HAlign_Fill); BSl->SetVerticalAlignment(VAlign_Bottom); BSl->SetPadding(FMargin(0.f));
	}

	// --- Logo + menu, links uitgelijnd (zoals de referentie). ---
	UVerticalBox* Left = WidgetTree->ConstructWidget<UVerticalBox>();
	{
		UOverlaySlot* LS = Layers->AddChildToOverlay(Left);
		LS->SetHorizontalAlignment(HAlign_Left); LS->SetVerticalAlignment(VAlign_Center); LS->SetPadding(FMargin(70.f, 0.f, 0.f, 0.f));
	}

	// Logo: de losse PNG indien aanwezig (aspect behouden), anders tekst-fallback.
	if (LogoTex)
	{
		UImage* Logo = WidgetTree->ConstructWidget<UImage>();
		Logo->SetBrushFromTexture(LogoTex, false);
		const float LW = 470.f, LH = LW * 724.f / 2172.f;
		Logo->SetDesiredSizeOverride(FVector2D(LW, LH));
		Left->AddChildToVerticalBox(Logo)->SetPadding(FMargin(0.f, 0.f, 0.f, 20.f));
	}
	else
	{
		Left->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("PLUG"), 72, FLinearColor(0.97f, 0.98f, 1.f), false, true));
		Left->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("SIMULATOR"), 40, FLinearColor(0.72f, 0.35f, 1.f), false, true))
			->SetPadding(FMargin(2.f, 0.f, 0.f, 26.f));
	}

	auto AddBtn = [this, Left](const FString& Label, const FLinearColor& Col, TFunction<void()> Fn) -> UWeedActionButton*
	{
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(300.f);
		UWeedActionButton* B = MenuBtn(WidgetTree, Label, Col, Fn);
		Sz->SetContent(B);
		Left->AddChildToVerticalBox(Sz)->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
		return B;
	};

	const FLinearColor Hi(0.42f, 0.18f, 0.70f);   // highlight (paars) zoals de geselecteerde knop
	const FLinearColor Dark(0.11f, 0.12f, 0.15f); // gewone donkere knop
	AddBtn(TEXT("Continue"),   Hi,   [this]() { OnStart(); });
	AddBtn(TEXT("New game"),   Dark, [this]() { OnStart(); });
	ContinueBtn = AddBtn(TEXT("Load game"), Dark, [this]() { OnContinue(); });
	AddBtn(TEXT("Settings"),   Dark, [this]() { OnSettings(); });
	AddBtn(TEXT("Credits"),    Dark, [this]() { OnCredits(); });
	AddBtn(TEXT("Exit game"),  FLinearColor(0.30f, 0.13f, 0.14f), [this]() { OnQuit(); });

	StatusText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.8f, 0.7f, 1.f), false);
	Left->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(2.f, 14.f, 0.f, 0.f));

	// Versie linksonder.
	{
		UTextBlock* Ver = WeedUI::Text(WidgetTree, TEXT("v1.0.0"), 11, FLinearColor(0.5f, 0.5f, 0.6f), false);
		UOverlaySlot* VS = Layers->AddChildToOverlay(Ver);
		VS->SetHorizontalAlignment(HAlign_Left); VS->SetVerticalAlignment(VAlign_Bottom); VS->SetPadding(FMargin(22.f, 0.f, 0.f, 14.f));
	}
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

void UMainMenuWidget::OnCredits()
{
	if (StatusText) { StatusText->SetText(FText::FromString(TEXT("THE PLUG SIMULATOR  -  a co-op grow & deal sim.  Built in C++/UE5."))); }
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
			// "Load game" dimmen als er geen save is.
			if (ContinueBtn)
			{
				const USaveGameSubsystem* Save = GetSave(GetWorld());
				ContinueBtn->SetIsEnabled(Save && Save->HasSave());
			}
		}
	}
	if (!bOpen) { return; }

	// Neon-lampen zacht laten flikkeren (alsof ze echt aan staan).
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (int32 i = 0; i < Glows.Num(); ++i)
	{
		if (!Glows[i]) { continue; }
		const float Ph = GlowPhase.IsValidIndex(i) ? GlowPhase[i] : 0.f;
		const float Fr = GlowFreq.IsValidIndex(i) ? GlowFreq[i] : 4.f;
		// Twee sinussen voor een onregelmatige puls + een kleine per-frame shimmer.
		float Osc = 0.82f
			+ 0.12f * FMath::Sin(T * Fr + Ph)
			+ 0.07f * FMath::Sin(T * Fr * 2.7f + Ph * 1.6f)
			+ FMath::FRandRange(-0.03f, 0.03f);
		Osc = FMath::Clamp(Osc, 0.45f, 1.08f);

		const FLinearColor Base = GlowBase.IsValidIndex(i) ? GlowBase[i] : FLinearColor::White;
		FLinearColor C(Base.R * Osc, Base.G * Osc, Base.B * Osc, FMath::Clamp(Base.A * Osc, 0.f, 1.f));
		Glows[i]->SetBrushColor(C);
	}
}
