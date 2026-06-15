#include "UI/SettingsWidget.h"
#include "WeedShopCore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "UI/WeedUiStyle.h"
#include "UI/HotkeyHintWidget.h"
#include "Phone/PhoneClientComponent.h"
#include "Input/ControlSettings.h"
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
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void USettingsWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Verduisterende achtergrond.
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SetDim"));
	Dim->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.78f), 0.f));
	Dim->SetHorizontalAlignment(HAlign_Center); Dim->SetVerticalAlignment(VAlign_Center);
	Card = Dim;
	UCanvasPanelSlot* DS = Root->AddChildToCanvas(Dim);
	DS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); DS->SetOffsets(FMargin(0.f));

	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(620.f); Sz->SetHeightOverride(480.f);
	Dim->SetContent(Sz);
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.99f), 18.f));
	Panel->SetPadding(FMargin(22.f));
	Sz->SetContent(Panel);

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(VB);

	VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("SETTINGS"), 20, FLinearColor(0.6f, 1.f, 0.6f), false, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

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

	// Inhoud.
	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	UVerticalBoxSlot* BSlot = VB->AddChildToVerticalBox(Body);
	BSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Back onderaan.
	UWeedActionButton* Back = SetBtn(WidgetTree, TEXT("Back"), FLinearColor(0.4f, 0.34f, 0.16f),
		[this]() { if (UPhoneClientComponent* Ph = GetPhone()) { Ph->CloseSettings(); } }, 13);
	VB->AddChildToVerticalBox(Back)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));

	Card->SetVisibility(ESlateVisibility::Collapsed);
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
	const FIntPoint Cur = G ? G->GetScreenResolution() : FIntPoint(1920, 1080);
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
				GG->SetFullscreenMode(Next);
				GG->ApplyResolutionSettings(false); // past venster-modus + resolutie direct + betrouwbaar toe
				GG->SaveSettings();
				RefreshContent();
			}
		});

		// PRESET - zet alles in één keer. Potato (onder Low, voor zwakke pc's) -> Low -> Medium -> High -> Epic.
		{
			bool bLumenOff, bPotato, bMbOff;
			WeedShop_ReadGfxFlags(bLumenOff, bPotato, bMbOff);
			int32 ScalQ = G->GetOverallScalabilityLevel(); if (ScalQ < 0) { ScalQ = 2; }
			const int32 TierNow = bPotato ? -1 : ScalQ;
			static const TCHAR* QN[5] = { TEXT("Potato"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
			AddValueRow(TEXT("Preset (all)"), QN[TierNow + 1], [this, TierNow]()
			{
				const int32 Next = (TierNow >= 3) ? -1 : TierNow + 1;
				WeedShop_ApplyGraphicsTier(Next);
				bool bLum, bPot, bMb; WeedShop_ReadGfxFlags(bLum, bPot, bMb);
				WeedShop_WriteGfxFlags(bLum, (Next <= -1), bMb); // Potato-vlag bij; Lumen/MotionBlur behouden
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
					GG->ApplySettings(false); GG->SaveSettings(); RefreshContent();
				}
			});
		};
		AddQ(TEXT("View distance"),    G->GetViewDistanceQuality(),   [](UGameUserSettings* GG, int32 n){ GG->SetViewDistanceQuality(n); });
		AddQ(TEXT("Textures"),         G->GetTextureQuality(),        [](UGameUserSettings* GG, int32 n){ GG->SetTextureQuality(n); });
		AddQ(TEXT("Shadows"),          G->GetShadowQuality(),         [](UGameUserSettings* GG, int32 n){ GG->SetShadowQuality(n); });
		AddQ(TEXT("Anti-aliasing"),    G->GetAntiAliasingQuality(),   [](UGameUserSettings* GG, int32 n){ GG->SetAntiAliasingQuality(n); });
		AddQ(TEXT("Effects"),          G->GetVisualEffectQuality(),   [](UGameUserSettings* GG, int32 n){ GG->SetVisualEffectQuality(n); });
		AddQ(TEXT("Post-processing"),  G->GetPostProcessingQuality(), [](UGameUserSettings* GG, int32 n){ GG->SetPostProcessingQuality(n); });
		AddQ(TEXT("Foliage"),          G->GetFoliageQuality(),        [](UGameUserSettings* GG, int32 n){ GG->SetFoliageQuality(n); });
		AddQ(TEXT("Shading (shaders)"),G->GetShadingQuality(),        [](UGameUserSettings* GG, int32 n){ GG->SetShadingQuality(n); });
		AddQ(TEXT("Reflections"),      G->GetReflectionQuality(),     [](UGameUserSettings* GG, int32 n){ GG->SetReflectionQuality(n); });

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
			bool bLumenOff, bPotato, bMbOff;
			WeedShop_ReadGfxFlags(bLumenOff, bPotato, bMbOff);
			AddValueRow(TEXT("Lumen (GI + reflections)"), bLumenOff ? TEXT("Off") : TEXT("On"), [this, bLumenOff, bPotato, bMbOff]()
			{
				WeedShop_ApplyLumen(!bLumenOff);
				WeedShop_WriteGfxFlags(!bLumenOff, bPotato, bMbOff);
				RefreshContent();
			});
		}

		// Motion blur - aan/uit.
		{
			bool bLumenOff, bPotato, bMbOff;
			WeedShop_ReadGfxFlags(bLumenOff, bPotato, bMbOff);
			AddValueRow(TEXT("Motion blur"), bMbOff ? TEXT("Off") : TEXT("On"), [this, bLumenOff, bPotato, bMbOff]()
			{
				WeedShop_ApplyMotionBlur(!bMbOff);
				WeedShop_WriteGfxFlags(bLumenOff, bPotato, !bMbOff);
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
		UPhoneClientComponent* Ph = GetPhone();
		const float Fov = Ph ? Ph->GetFov() : 90.f;          // 60..120
		const float Sens = Ph ? Ph->GetLookSensitivity() : 1.f; // 0.1..3.0

		FovSlider = AddSliderRow(TEXT("Field of view"), (Fov - 60.f) / 60.f, FovVal);
		if (FovVal) { FovVal->SetText(FText::FromString(FString::Printf(TEXT("%d"), (int32)Fov))); }
		LastFovApplied = (int32)Fov;

		SensSlider = AddSliderRow(TEXT("Mouse sensitivity"), (Sens - 0.1f) / 2.9f, SensVal);
		if (SensVal) { SensVal->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), Sens))); }
		LastSensApplied = FMath::RoundToInt(Sens * 10.f);
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
