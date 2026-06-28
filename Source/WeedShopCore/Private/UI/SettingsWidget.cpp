#include "UI/SettingsWidget.h"
#include "WeedShopCore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "UI/WeedUiStyle.h"
#include "UI/HotkeyHintWidget.h"
#include "Phone/PhoneClientComponent.h"
#include "Input/ControlSettings.h"
#include "Interaction/PlayerNpcActions.h"
#include "Components/ScrollBox.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/ComboBoxString.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWindow.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "UnrealClient.h"
#include "GenericPlatform/GenericApplication.h" // FDisplayMetrics/FMonitorInfo (multi-monitor fullscreen)

namespace
{
	UWeedActionButton* SetBtn(UWidgetTree* Tree, const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick, int32 Font = 12)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 7.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 7.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 7.f);
		S.NormalPadding = FMargin(10.f, 5.f); S.PressedPadding = FMargin(10.f, 5.f);
		B->SetStyle(S);
		B->SetContent(WeedUI::Text(Tree, Label, Font, FLinearColor::White, true));
		return B;
	}

	UGameUserSettings* GUS() { return GEngine ? GEngine->GetGameUserSettings() : nullptr; }
}

void USettingsWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

UPhoneClientComponent* USettingsWidget::GetPhone() const
{
	if (PhoneComp.IsValid()) { return PhoneComp.Get(); }
	APawn* P = GetOwningPlayerPawn();
	return P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;
}

TSharedRef<SWidget> USettingsWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void USettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true); // nodig om een toets-aanslag op te vangen bij rebind
}

FReply USettingsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bRebinding)
	{
		const FKey K = InKeyEvent.GetKey();
		if (K == EKeys::Escape)
		{
			bRebinding = false; RebindMsg = TEXT("Cancelled."); RefreshContent();
			return FReply::Handled();
		}
		if (K == EKeys::BackSpace || K == EKeys::Delete)
		{
			UControlSettings::Get()->ClearKey(RebindAction, bRebindAlt);
			RebindMsg = FString::Printf(TEXT("Cleared %s key for '%s'"), bRebindAlt ? TEXT("alt") : TEXT("main"), *UControlSettings::DisplayName(RebindAction).ToString());
			bRebinding = false; RefreshContent();
			return FReply::Handled();
		}
		FName Conflict;
		if (UControlSettings::Get()->SetKey(RebindAction, bRebindAlt, K, Conflict))
		{
			RebindMsg = FString::Printf(TEXT("'%s' %s -> %s"), *UControlSettings::DisplayName(RebindAction).ToString(),
				bRebindAlt ? TEXT("(alt)") : TEXT("(main)"), *K.GetDisplayName().ToString());
		}
		else
		{
			RebindMsg = Conflict.IsNone() ? TEXT("That key can't be used.")
				: FString::Printf(TEXT("Already used by: %s"), *UControlSettings::DisplayName(Conflict).ToString());
		}
		bRebinding = false; RefreshContent();
		return FReply::Handled();
	}
// Esc sluit het settings-menu (de grote Back-knop is vervangen door Save; back via Esc of de < Back linksboven).
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (UPhoneClientComponent* Ph = GetPhone()) { Ph->CloseSettings(); }
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void USettingsWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Verduisterende achtergrond.
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SetDim"));
	Dim->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.82f), 0.f));
	// Verticaal vullen (tall = fullscreen-feel) maar horizontaal een GECENTREERDE kolom met vaste
	// max-breedte -> op widescreen geen edge-to-edge uitrekken (geen enorme lege middenruimte),
	// maar een nette gecentreerde settings-kolom.
	Dim->SetHorizontalAlignment(HAlign_Center); Dim->SetVerticalAlignment(VAlign_Fill);
	Dim->SetPadding(FMargin(0.f, 50.f));
	Card = Dim;
	UCanvasPanelSlot* DS = Root->AddChildToCanvas(Dim);
	DS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); DS->SetOffsets(FMargin(0.f));

	USizeBox* Col = WidgetTree->ConstructWidget<USizeBox>();
	Col->SetWidthOverride(920.f); // content-breedte cap -> optimaal op widescreen; hoogte vult mee
	Dim->SetContent(Col);
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.99f), 18.f));
	Panel->SetPadding(FMargin(34.f, 26.f));
	Col->SetContent(Panel);

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(VB);

	// Header: kleine back-knop linksboven + titel.
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	UWeedActionButton* BackTop = SetBtn(WidgetTree, TEXT("< Back"), FLinearColor(0.4f, 0.34f, 0.16f),
		[this]() { if (UPhoneClientComponent* Ph = GetPhone()) { Ph->CloseSettings(); } }, 12);
	Header->AddChildToHorizontalBox(BackTop)->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	Header->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("SETTINGS"), 20, FLinearColor(0.6f, 1.f, 0.6f), false, true));
	VB->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// Categorie-tabs.
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
	TabGraphics = SetBtn(WidgetTree, TEXT("Graphics"), FLinearColor(0.22f, 0.32f, 0.46f), [this]() { Category = 0; RefreshTabs(); RefreshContent(); }, 13);
	TabGame = SetBtn(WidgetTree, TEXT("Game"), FLinearColor(0.22f, 0.32f, 0.46f), [this]() { Category = 1; RefreshTabs(); RefreshContent(); }, 13);
	TabControls = SetBtn(WidgetTree, TEXT("Controls"), FLinearColor(0.22f, 0.32f, 0.46f), [this]() { Category = 2; RefreshTabs(); RefreshContent(); }, 13);
	TabAudio = SetBtn(WidgetTree, TEXT("Audio"), FLinearColor(0.22f, 0.32f, 0.46f), [this]() { Category = 3; RefreshTabs(); RefreshContent(); }, 13);
	Tabs->AddChildToHorizontalBox(TabGraphics)->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	Tabs->AddChildToHorizontalBox(TabGame)->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	Tabs->AddChildToHorizontalBox(TabControls)->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	Tabs->AddChildToHorizontalBox(TabAudio)->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	VB->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// Inhoud - scrollbaar zodat alle (graphics-)rijen netjes BINNEN het paneel passen i.p.v. eroverheen.
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Body);
	UVerticalBoxSlot* BSlot = VB->AddChildToVerticalBox(Scroll);
	BSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Save onderaan (waar Back stond). Back gaat nu via Esc + de knop linksboven.
	UWeedActionButton* SaveBtn = SetBtn(WidgetTree, TEXT("Save settings"), FLinearColor(0.18f, 0.42f, 0.22f),
		[this]() { if (UGameUserSettings* GG = GUS()) { GG->SaveSettings(); } bool bL, bP, bM, bV, bR; WeedShop_ReadGfxFlags(bL, bP, bM, bV, bR); WeedShop_WriteGfxFlags(bL, bP, bM, bV, bR); RefreshContent(); }, 13);
	VB->AddChildToVerticalBox(SaveBtn)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));

	// === Modale "herstart nodig"-popup (verschijnt bij een schaduw-grens-wissel Potato <-> hoger). ===
	UBorder* PopDim = WidgetTree->ConstructWidget<UBorder>();
	PopDim->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.62f), 0.f));
	PopDim->SetHorizontalAlignment(HAlign_Center); PopDim->SetVerticalAlignment(VAlign_Center);
	RestartPopup = PopDim;
	UCanvasPanelSlot* PSlot = Root->AddChildToCanvas(PopDim);
	PSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); PSlot->SetOffsets(FMargin(0.f));
	USizeBox* PopBox = WidgetTree->ConstructWidget<USizeBox>(); PopBox->SetWidthOverride(560.f); PopDim->SetContent(PopBox);
	UBorder* PopPanel = WidgetTree->ConstructWidget<UBorder>(); PopPanel->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.10f, 0.14f, 1.f), 16.f)); PopPanel->SetPadding(FMargin(28.f, 24.f)); PopBox->SetContent(PopPanel);
	UVerticalBox* PopVB = WidgetTree->ConstructWidget<UVerticalBox>(); PopPanel->SetContent(PopVB);
	PopVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Restart required"), 18, FLinearColor(1.f, 0.85f, 0.4f), true, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	UTextBlock* PopMsg = WeedUI::Text(WidgetTree, TEXT("Changes require a restart."), 12, FLinearColor(0.82f, 0.86f, 0.92f), true, false);
	PopMsg->SetAutoWrapText(true);
	PopVB->AddChildToVerticalBox(PopMsg)->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
	UWeedActionButton* PopOk = SetBtn(WidgetTree, TEXT("OK"), FLinearColor(0.18f, 0.42f, 0.22f), [this]() { if (RestartPopup) { RestartPopup->SetVisibility(ESlateVisibility::Collapsed); } }, 13);
	PopVB->AddChildToVerticalBox(PopOk);
	RestartPopup->SetVisibility(ESlateVisibility::Collapsed);

	Card->SetVisibility(ESlateVisibility::Collapsed);
}

void USettingsWidget::ShowRestartPopup()
{
	if (RestartPopup) { RestartPopup->SetVisibility(ESlateVisibility::Visible); }
}

void USettingsWidget::AddValueRow(const FString& Label, const FString& Value, TFunction<void()> OnClick)
{
	if (!Body) { return; }
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Label, 14, FLinearColor(0.88f, 0.9f, 1.f)));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);
	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(220.f);
	Sz->SetContent(SetBtn(WidgetTree, Value, FLinearColor(0.18f, 0.2f, 0.27f), OnClick, 12));
	Row->AddChildToHorizontalBox(Sz)->SetVerticalAlignment(VAlign_Center);
	Body->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 5.f, 0.f, 5.f));
}

USlider* USettingsWidget::AddSliderRow(const FString& Label, float Normalized, TObjectPtr<UTextBlock>& OutValue)
{
	USlider* Slider = WidgetTree->ConstructWidget<USlider>();
	if (!Body) { return Slider; }
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Label, 14, FLinearColor(0.88f, 0.9f, 1.f)));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);

	Slider->SetMinValue(0.f); Slider->SetMaxValue(1.f);
	Slider->SetValue(FMath::Clamp(Normalized, 0.f, 1.f));
	Slider->SetSliderBarColor(FLinearColor(0.18f, 0.2f, 0.27f));
	Slider->SetSliderHandleColor(FLinearColor(0.55f, 0.8f, 1.f));
	USizeBox* SSz = WidgetTree->ConstructWidget<USizeBox>(); SSz->SetWidthOverride(220.f); SSz->SetHeightOverride(20.f); SSz->SetContent(Slider);
	Row->AddChildToHorizontalBox(SSz)->SetVerticalAlignment(VAlign_Center);

	OutValue = WeedUI::Text(WidgetTree, TEXT(""), 14, FLinearColor::White, true);
	USizeBox* VSz = WidgetTree->ConstructWidget<USizeBox>(); VSz->SetWidthOverride(56.f); VSz->SetContent(OutValue);
	Row->AddChildToHorizontalBox(VSz)->SetVerticalAlignment(VAlign_Center);

	Body->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 6.f, 0.f, 6.f));
	return Slider;
}

void USettingsWidget::AddResolutionRow()
{
	if (!Body) { return; }
	UGameUserSettings* G = GUS();
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("Resolution"), 14, FLinearColor(0.88f, 0.9f, 1.f)));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);

	ResCombo = WidgetTree->ConstructWidget<UComboBoxString>();
	// Ondersteunde resoluties van de monitor vullen.
	TArray<FIntPoint> Modes;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Modes);
	if (Modes.Num() == 0) { Modes = { FIntPoint(1280,720), FIntPoint(1600,900), FIntPoint(1920,1080), FIntPoint(2560,1440) }; }
	FIntPoint Cur = G ? G->GetScreenResolution() : FIntPoint(1920, 1080);
	// De ECHTE huidige render-resolutie van het venster (GetScreenResolution kan achterlopen in windowed
	// of na een resize) -> de dropdown toont wat er nu daadwerkelijk op staat.
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint VP = GEngine->GameViewport->Viewport->GetSizeXY();
		if (VP.X > 0 && VP.Y > 0) { Cur = VP; }
	}
	FString CurOpt;
	for (const FIntPoint& M : Modes)
	{
		const FString Opt = FString::Printf(TEXT("%d x %d"), M.X, M.Y);
		ResCombo->AddOption(Opt);
		if (M.X == Cur.X && M.Y == Cur.Y) { CurOpt = Opt; }
	}
	if (CurOpt.IsEmpty()) { CurOpt = FString::Printf(TEXT("%d x %d"), Cur.X, Cur.Y); ResCombo->AddOption(CurOpt); }
	ResCombo->SetSelectedOption(CurOpt);
	ResCombo->OnSelectionChanged.AddDynamic(this, &USettingsWidget::OnResolutionChanged);

	USizeBox* CSz = WidgetTree->ConstructWidget<USizeBox>(); CSz->SetWidthOverride(220.f); CSz->SetContent(ResCombo);
	Row->AddChildToHorizontalBox(CSz)->SetVerticalAlignment(VAlign_Center);
	Body->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 5.f, 0.f, 5.f));
}

void USettingsWidget::OnResolutionChanged(FString Item, ESelectInfo::Type SelectType)
{
	if (SelectType == ESelectInfo::Direct) { return; } // alleen door de speler
	int32 X = 0, Y = 0;
	FString L, R;
	if (Item.Split(TEXT(" x "), &L, &R)) { X = FCString::Atoi(*L); Y = FCString::Atoi(*R); }
	if (X > 0 && Y > 0)
	{
		if (UGameUserSettings* G = GUS())
		{
			G->SetScreenResolution(FIntPoint(X, Y));
			G->ApplyResolutionSettings(false);
			G->SaveSettings();
		}
	}
}

void USettingsWidget::RefreshTabs()
{
	auto Tint = [](UWeedActionButton* B, bool bSel)
	{
		if (!B) { return; }
		const FLinearColor C = bSel ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.22f, 0.32f, 0.46f);
		FButtonStyle S; S.Normal = WeedUI::Rounded(C, 7.f); S.Hovered = WeedUI::Rounded(C * 1.3f, 7.f); S.Pressed = WeedUI::Rounded(C * 0.8f, 7.f);
		S.NormalPadding = FMargin(10.f, 5.f); S.PressedPadding = FMargin(10.f, 5.f);
		B->SetStyle(S);
	};
	Tint(TabGraphics, Category == 0);
	Tint(TabGame, Category == 1);
	Tint(TabControls, Category == 2);
	Tint(TabAudio, Category == 3);
}

void USettingsWidget::RefreshContent()
{
	if (!Body) { return; }
	Body->ClearChildren();
	FovSlider = nullptr; SensSlider = nullptr; FovVal = nullptr; SensVal = nullptr; ResCombo = nullptr;
	VolUiSlider = nullptr; VolGameSlider = nullptr; VolMusicSlider = nullptr; VolUiVal = nullptr; VolGameVal = nullptr; VolMusicVal = nullptr;

	if (Category == 0) // Graphics
	{
		UGameUserSettings* G = GUS();
		if (!G) { Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Graphics unavailable."), 13, FLinearColor::Gray)); return; }

		// Resolutie-dropdown.
		AddResolutionRow();

		// Window mode - lees de ECHTE huidige vensterstatus van het live venster. GameUserSettings::
		// GetFullscreenMode() kan achterlopen (na alt-tab / OS-wissel / fallback), waardoor het label
		// niet klopte met wat je zag en je vanaf een verkeerde stand cyclede.
		EWindowMode::Type ActualWM = G->GetFullscreenMode();
		if (GEngine && GEngine->GameViewport)
		{
			TSharedPtr<SWindow> Win = GEngine->GameViewport->GetWindow();
			if (Win.IsValid()) { ActualWM = Win->GetWindowMode(); }
		}
		const FString WMName = (ActualWM == EWindowMode::Fullscreen) ? TEXT("Fullscreen")
			: (ActualWM == EWindowMode::WindowedFullscreen) ? TEXT("Borderless") : TEXT("Windowed");
		AddValueRow(TEXT("Window mode"), WMName, [this, ActualWM]()
		{
			if (UGameUserSettings* GG = GUS())
			{
				const EWindowMode::Type Next = (ActualWM == EWindowMode::Fullscreen) ? EWindowMode::WindowedFullscreen
					: (ActualWM == EWindowMode::WindowedFullscreen) ? EWindowMode::Windowed : EWindowMode::Fullscreen;
				// Multi-monitor: bij Fullscreen/Borderless kiest UE standaard de PRIMAIRE monitor i.p.v. de monitor
				// waar je venster staat -> de game springt naar het verkeerde scherm. Zoek de monitor onder het
				// venster, veranker het daar en zet de resolutie op die monitor, zodat UE op DAT scherm fullscreent.
				if (Next != EWindowMode::Windowed && GEngine && GEngine->GameViewport)
					{
						TSharedPtr<SWindow> FsWin = GEngine->GameViewport->GetWindow();
						if (FsWin.IsValid())
						{
#if PLATFORM_WINDOWS
							// Pak (DPI-onafhankelijk) de monitor ONDER het venster + diens HUIDIGE desktop-resolutie en zet die als
							// fullscreen-resolutie -> UE fullscreent op DAT scherm op de juiste resolutie (i.p.v. 1600x900 / primair).
							TSharedPtr<FGenericWindow> Native = FsWin->GetNativeWindow();
							HWND Hwnd = Native.IsValid() ? (HWND)Native->GetOSWindowHandle() : nullptr;
							HMONITOR Hmon = Hwnd ? MonitorFromWindow(Hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
							if (Hmon)
							{
								MONITORINFOEXW Mi; FMemory::Memzero(&Mi, sizeof(Mi)); Mi.cbSize = sizeof(Mi);
								DEVMODEW Dm; FMemory::Memzero(&Dm, sizeof(Dm)); Dm.dmSize = sizeof(Dm);
								if (::GetMonitorInfoW(Hmon, &Mi) && ::EnumDisplaySettingsW(Mi.szDevice, ENUM_CURRENT_SETTINGS, &Dm) && Dm.dmPelsWidth > 0 && Dm.dmPelsHeight > 0)
								{
									GG->SetScreenResolution(FIntPoint((int32)Dm.dmPelsWidth, (int32)Dm.dmPelsHeight));
								}
							}
#endif
						}
					}
				GG->SetFullscreenMode(Next);
				GG->ApplyResolutionSettings(false); // past venster-modus + resolutie direct + betrouwbaar toe
				GG->SaveSettings();
				RefreshContent();
			}
		});

		// PRESET - zet alles in één keer. Potato (onder Low, voor zwakke pc's) -> Low -> Medium -> High -> Epic.
		{
			bool bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff;
			WeedShop_ReadGfxFlags(bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff);
			int32 ScalQ = G->GetOverallScalabilityLevel(); if (ScalQ < 0) { ScalQ = 2; }
			const int32 TierNow = bPotato ? -1 : ScalQ;
			static const TCHAR* QN[5] = { TEXT("Potato"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
			AddValueRow(TEXT("Preset (all)"), QN[TierNow + 1], [this, TierNow]()
			{
				const int32 Next = (TierNow >= 3) ? -1 : TierNow + 1;
				WeedShop_ApplyGraphicsTier(Next);
				bool bLum, bPot, bMb, bVSM, bRT; WeedShop_ReadGfxFlags(bLum, bPot, bMb, bVSM, bRT);
				// Vlaggen volgen de preset: Lumen + ray tracing alleen op Epic; VSM uit op Potato/Low;
				// MotionBlur behouden.
				WeedShop_WriteGfxFlags((Next < 3), (Next <= -1), bMb, bVSM, (Next < 3)); // VSMOff (schaduwen) NIET via de preset -> de Shadows-toggle bezit hem
				if (UWorld* WB = GetWorld()) { if (WB->GetOutermost()->GetName().StartsWith(TEXT("/Game/CityBeachStrip"))) { WeedShop_ApplyBeachShadows(bVSM); } } // tier-wissel past de beach-gate opnieuw toe -> schaduwen komen deterministisch terug
				RefreshContent();
			});
		}


		// --- Losse kwaliteit-instellingen (elk Low/Medium/High/Epic; samen vormen ze het preset) ---
		auto AddQ = [this](const FString& Lbl, int32 Cur, TFunction<void(UGameUserSettings*, int32)> Apply)
		{
			static const TCHAR* LV[4] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
			AddValueRow(Lbl, (Cur >= 0 && Cur <= 3) ? LV[Cur] : TEXT("Custom"), [this, Cur, Apply]()
			{
				if (UGameUserSettings* GG = GUS())
				{
					const int32 c = (Cur < 0) ? 2 : Cur;
					Apply(GG, (c + 1) % 4);
					GG->ApplySettings(false); GG->SaveSettings();
					// De beach forceert de VSM-config met console-prio -> her-toepassen zodat o.a. de Shadows-setting
					// (VSM-scherpte) METEEN zichtbaar wordt i.p.v. pas na opnieuw laden van de map.
					if (UWorld* WB = GetWorld())
					{
						if (WB->GetOutermost()->GetName().StartsWith(TEXT("/Game/CityBeachStrip")))
						{
							bool bL2, bP2, bM2, bV2, bR2; WeedShop_ReadGfxFlags(bL2, bP2, bM2, bV2, bR2);
							WeedShop_ApplyBeachShadows(bV2); // gedeelde gate: Potato=uit, Low+=VSM (forceerde eerder VSM altijd aan)
						}
					}
					RefreshContent();
				}
			});
		};
		AddQ(TEXT("Textures"),         G->GetTextureQuality(),        [](UGameUserSettings* GG, int32 n){ GG->SetTextureQuality(n); });
		{ // Shadows: ON/OFF (eigen vlag, los van de Preset; VSM aan/uit). Geldt na herstart (VSM-pool).
			bool bL0, bP0, bM0, bV0, bR0; WeedShop_ReadGfxFlags(bL0, bP0, bM0, bV0, bR0);
			AddValueRow(TEXT("Shadows"), bV0 ? TEXT("Off") : TEXT("On"), [this, bL0, bP0, bM0, bV0, bR0]()
			{
				WeedShop_WriteGfxFlags(bL0, bP0, bM0, !bV0, bR0); // VSMOff togglen = schaduwen aan/uit
				if (UWorld* WB = GetWorld()) { if (WB->GetOutermost()->GetName().StartsWith(TEXT("/Game/CityBeachStrip"))) { WeedShop_ApplyBeachShadows(!bV0); } }
				ShowRestartPopup(); // VSM-pool kan niet live togglen -> geldt na herstart
				RefreshContent();
			});
		}

		// Resolutie-schaal (render-%): 50 -> 75 -> 100.
		{
			const int32 RS = FMath::RoundToInt(G->GetResolutionScaleNormalized() * 100.f);
			AddValueRow(TEXT("Resolution scale"), FString::Printf(TEXT("%d%%"), RS), [this]()
			{
				if (UGameUserSettings* GG = GUS())
				{
					const int32 Cur = FMath::RoundToInt(GG->GetResolutionScaleNormalized() * 100.f);
					const int32 Next = (Cur < 63) ? 75 : (Cur < 88) ? 100 : 50;
					GG->SetResolutionScaleNormalized(Next / 100.f);
					GG->ApplySettings(false); GG->SaveSettings(); RefreshContent();
				}
			});
		}

		// Lumen (GI + reflecties op de goedkope methode) - aan/uit.
		{
			bool bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff;
			WeedShop_ReadGfxFlags(bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff);
			AddValueRow(TEXT("Lumen (GI + reflections)"), bLumenOff ? TEXT("Off") : TEXT("On"), [this, bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff]()
			{
				WeedShop_ApplyLumen(!bLumenOff);
				WeedShop_WriteGfxFlags(!bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff);
				RefreshContent();
			});
		}



		// Motion blur - aan/uit.
		{
			bool bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff;
			WeedShop_ReadGfxFlags(bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff);
			AddValueRow(TEXT("Motion blur"), bMbOff ? TEXT("Off") : TEXT("On"), [this, bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff]()
			{
				WeedShop_ApplyMotionBlur(!bMbOff);
				WeedShop_WriteGfxFlags(bLumenOff, bPotato, !bMbOff, bVSMOff, bRTOff);
				RefreshContent();
			});
		}

		// VSync.
		AddValueRow(TEXT("V-Sync"), G->IsVSyncEnabled() ? TEXT("On") : TEXT("Off"), [this]()
		{
			if (UGameUserSettings* GG = GUS()) { GG->SetVSyncEnabled(!GG->IsVSyncEnabled()); GG->ApplySettings(false); GG->SaveSettings(); RefreshContent(); }
		});

		// Frame rate limit.
		const float FL = G->GetFrameRateLimit();
		const FString FLName = (FL <= 0.f) ? TEXT("Uncapped") : FString::Printf(TEXT("%d FPS"), (int32)FL);
		AddValueRow(TEXT("Frame limit"), FLName, [this]()
		{
			if (UGameUserSettings* GG = GUS())
			{
				const float Cur = GG->GetFrameRateLimit();
				float Next = 60.f;
				if (Cur <= 0.f) Next = 30.f; else if (Cur < 45.f) Next = 60.f; else if (Cur < 90.f) Next = 120.f; else if (Cur < 132.f) Next = 144.f; else Next = 0.f;
				GG->SetFrameRateLimit(Next); GG->ApplySettings(false); GG->SaveSettings(); RefreshContent();
			}
		});
	}
	else if (Category == 1) // Game
	{
		// Character: Man/Vrouw-keuze. Het MODEL/gezicht + bijbehorende kleren kies je daarna in de Wardrobe.
		if (IPlayerNpcActions* PA = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
		{
			const uint8 Sk = PA->GetPlayerSkinIndex();
			const bool bMale = (Sk == 5 || Sk == 6 || Sk == 0); // 5/6 = man (Tony/Citizen), 0 = Manny (legacy) ; 1-4 = vrouw
			AddValueRow(TEXT("Character"), bMale ? TEXT("Male") : TEXT("Female"), [this]()
			{
				if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
				{
					const uint8 Cur = P->GetPlayerSkinIndex();
					const bool bIsMale = (Cur == 5 || Cur == 6 || Cur == 0);
					P->SetPlayerSkinIndex(bIsMale ? 2 : 5); // -> vrouw (Girl 1) of man (Tony)
					RefreshContent();
				}
			});
		}

		UPhoneClientComponent* Ph = GetPhone();
		const float Fov = Ph ? Ph->GetFov() : 90.f;          // 60..120
		const float Sens = Ph ? Ph->GetLookSensitivity() : 1.f; // 0.1..3.0

		FovSlider = AddSliderRow(TEXT("Field of view"), (Fov - 60.f) / 60.f, FovVal);
		if (FovVal) { FovVal->SetText(FText::FromString(FString::Printf(TEXT("%d"), (int32)Fov))); }
		LastFovApplied = (int32)Fov;

		SensSlider = AddSliderRow(TEXT("Mouse sensitivity"), (Sens - 0.1f) / 2.9f, SensVal);
		if (SensVal) { SensVal->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), Sens))); }
		LastSensApplied = FMath::RoundToInt(Sens * 10.f);

		// Head bobbing aan/uit (camera loop-wiebel).
		const bool bBobNow = Ph ? Ph->GetHeadBob() : true;
		AddValueRow(TEXT("Head bobbing"), bBobNow ? TEXT("On") : TEXT("Off"), [this, bBobNow]()
		{
			if (UPhoneClientComponent* P2 = GetPhone()) { P2->SetHeadBob(!bBobNow); }
			RefreshContent();
		});
	}
	else if (Category == 3) // Audio
	{
		VolUiSlider = AddSliderRow(TEXT("UI volume"), WeedUI::SoundCategoryVolume(0), VolUiVal);
		if (VolUiVal) { VolUiVal->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(WeedUI::SoundCategoryVolume(0) * 100.f)))); }
		LastVolUi = FMath::RoundToInt(WeedUI::SoundCategoryVolume(0) * 100.f);

		VolGameSlider = AddSliderRow(TEXT("Game volume"), WeedUI::SoundCategoryVolume(1), VolGameVal);
		if (VolGameVal) { VolGameVal->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(WeedUI::SoundCategoryVolume(1) * 100.f)))); }
		LastVolGame = FMath::RoundToInt(WeedUI::SoundCategoryVolume(1) * 100.f);

		VolMusicSlider = AddSliderRow(TEXT("Music volume"), WeedUI::SoundCategoryVolume(2), VolMusicVal);
		if (VolMusicVal) { VolMusicVal->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(WeedUI::SoundCategoryVolume(2) * 100.f)))); }
		LastVolMusic = FMath::RoundToInt(WeedUI::SoundCategoryVolume(2) * 100.f);

		Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Music comes later; the slider is ready for it."), 11, FLinearColor(0.55f, 0.6f, 0.7f)))
			->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
	}
	else // Controls
	{
		// Controls-overlay (de toetsen-hint rechtsonder) aan/uit.
		AddValueRow(TEXT("Controls overlay"), UHotkeyHintWidget::AreHintsEnabled() ? TEXT("On") : TEXT("Off"),
			[this]() { UHotkeyHintWidget::SetHintsEnabled(!UHotkeyHintWidget::AreHintsEnabled()); RefreshContent(); });

		Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Click a key, press the new one.  Esc = cancel."), 11, FLinearColor(0.6f, 0.65f, 0.76f)))
			->SetPadding(FMargin(0.f, 6.f, 0.f, 4.f));

		// Scrollbare lijst met alle acties (Main + Alt toets).
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		UVerticalBoxSlot* ScS = Body->AddChildToVerticalBox(Scroll);
		ScS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		UControlSettings* Cfg = UControlSettings::Get();
		for (const FName& Action : UControlSettings::AllActions())
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* NameT = WeedUI::Text(WidgetTree, UControlSettings::DisplayName(Action).ToString(), 13, FLinearColor(0.9f, 0.92f, 1.f));
			UHorizontalBoxSlot* NS = Row->AddChildToHorizontalBox(NameT);
			NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NS->SetVerticalAlignment(VAlign_Center);

			for (int32 SlotIdx = 0; SlotIdx < 2; ++SlotIdx)
			{
				const bool bAlt = (SlotIdx == 1);
				const bool bThis = bRebinding && (RebindAction == Action) && (bRebindAlt == bAlt);
				const FKey K = Cfg->GetKey(Action, bAlt);
				const FString Lbl = bThis ? TEXT("Press...") : (K.IsValid() ? K.GetDisplayName().ToString() : (bAlt ? TEXT("+ Alt") : TEXT("-")));
				const FLinearColor BtnCol = bThis ? FLinearColor(0.5f, 0.4f, 0.12f) : FLinearColor(0.18f, 0.22f, 0.3f);
				USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
				Sz->SetWidthOverride(96.f);
				Sz->SetContent(SetBtn(WidgetTree, Lbl, BtnCol,
					[this, Action, bAlt]() { bRebinding = true; bRebindAlt = bAlt; RebindAction = Action; RebindMsg.Reset(); SetKeyboardFocus(); RefreshContent(); }, 11));
				Row->AddChildToHorizontalBox(Sz)->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			}
			Scroll->AddChild(Row);
			Scroll->AddChild(WeedUI::Text(WidgetTree, TEXT(""), 3, FLinearColor::Transparent));
		}

		if (!RebindMsg.IsEmpty())
		{
			Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, RebindMsg, 12, FLinearColor(1.f, 0.85f, 0.45f)))
				->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		}
		Body->AddChildToVerticalBox(SetBtn(WidgetTree, TEXT("Reset to defaults"), FLinearColor(0.4f, 0.34f, 0.16f),
			[this]() { UControlSettings::Get()->ResetToDefaults(); bRebinding = false; RebindMsg = TEXT("Controls reset to defaults."); RefreshContent(); }, 12))
			->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}
}

void USettingsWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const UPhoneClientComponent* Ph = GetPhone();
	const bool bOpen = Ph && Ph->IsSettingsOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (bOpen != bLastOpen)
	{
		bLastOpen = bOpen;
		if (bOpen) { RefreshTabs(); RefreshContent(); }
	}
	if (!bOpen) { return; }

	// Game-sliders live toepassen (alleen als de waarde echt verandert -> geen config-spam).
	if (UPhoneClientComponent* PhMut = GetPhone())
	{
		if (FovSlider)
		{
			const int32 Fov = FMath::RoundToInt(60.f + FovSlider->GetValue() * 60.f);
			if (Fov != LastFovApplied)
			{
				LastFovApplied = Fov;
				PhMut->ApplyFov((float)Fov);
				if (FovVal) { FovVal->SetText(FText::FromString(FString::Printf(TEXT("%d"), Fov))); }
			}
		}
		if (SensSlider)
		{
			const float SensRaw = 0.1f + SensSlider->GetValue() * 2.9f;
			const int32 S10 = FMath::RoundToInt(SensRaw * 10.f);
			if (S10 != LastSensApplied)
			{
				LastSensApplied = S10;
				PhMut->SetLookSensitivity(S10 / 10.f);
				if (SensVal) { SensVal->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), S10 / 10.f))); }
			}
		}
	}

	// Audio-sliders -> categorie-volume (alleen schrijven als de waarde echt verandert).
	auto PollVol = [this](USlider* S, int32 Cat, int32& Last, UTextBlock* Val)
	{
		if (!S) { return; }
		const int32 Pct = FMath::RoundToInt(S->GetValue() * 100.f);
		if (Pct != Last)
		{
			Last = Pct;
			WeedUI::SetSoundCategoryVolume(Cat, Pct / 100.f);
			if (Val) { Val->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), Pct))); }
		}
	};
	PollVol(VolUiSlider, 0, LastVolUi, VolUiVal);
	PollVol(VolGameSlider, 1, LastVolGame, VolGameVal);
	PollVol(VolMusicSlider, 2, LastVolMusic, VolMusicVal);
}
