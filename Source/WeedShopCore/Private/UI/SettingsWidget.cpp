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
	// OutLabel (optioneel) = het tekstblok IN de knop, zodat een klik-lambda later ALLEEN die tekst kan
	// bijwerken (SetText) i.p.v. de knop/rij te herbouwen -> geen flash/scroll-sprong.
	UWeedActionButton* SetBtn(UWidgetTree* Tree, const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick, int32 Font = 12, UTextBlock** OutLabel = nullptr)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 7.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 7.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 7.f);
		S.NormalPadding = FMargin(13.f, 7.f); S.PressedPadding = FMargin(13.f, 7.f);
		B->SetStyle(S);
		UTextBlock* Lbl = WeedUI::Text(Tree, Label, Font, FLinearColor::White, true);
		B->SetContent(Lbl);
		if (OutLabel) { *OutLabel = Lbl; }
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
		const FName WasAction = RebindAction; // de actie waarvan de knoppen moeten bijwerken (highlight + evt. nieuwe key)
		const FKey K = InKeyEvent.GetKey();
		if (K == EKeys::Escape)
		{
			bRebinding = false; RebindMsg = TEXT("Cancelled.");
			RefreshKeyButtonsFor(WasAction); // alleen de 2 knoppen van deze actie + de msg-tekst (geen lijst-rebuild)
			return FReply::Handled();
		}
		if (K == EKeys::BackSpace || K == EKeys::Delete)
		{
			UControlSettings::Get()->ClearKey(RebindAction, bRebindAlt);
			RebindMsg = FString::Printf(TEXT("Cleared %s key for '%s'"), bRebindAlt ? TEXT("alt") : TEXT("main"), *UControlSettings::DisplayName(RebindAction).ToString());
			bRebinding = false;
			RefreshKeyButtonsFor(WasAction);
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
		bRebinding = false;
		RefreshKeyButtonsFor(WasAction);
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

	// === Full-page achtergrond: dekt het HELE scherm (geen gecentreerd popup-kaartje meer). ===
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SetDim"));
	Dim->SetBrush(WeedUI::Rounded(WeedUI::ColBg(0.95f), 0.f));
	Dim->SetHorizontalAlignment(HAlign_Fill); Dim->SetVerticalAlignment(VAlign_Fill);
	Dim->SetPadding(FMargin(0.f));
	Card = Dim;
	UCanvasPanelSlot* DS = Root->AddChildToCanvas(Dim);
	DS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); DS->SetOffsets(FMargin(0.f));

	// Marge t.o.v. de schermranden (full-page, maar niet edge-glued).
	UBorder* PagePad = WidgetTree->ConstructWidget<UBorder>();
	PagePad->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.f), 0.f));
	PagePad->SetHorizontalAlignment(HAlign_Center); PagePad->SetVerticalAlignment(VAlign_Fill); // content gecentreerd -> ultra-wide niet links-zwaar
	PagePad->SetPadding(FMargin(74.f, 48.f, 74.f, 40.f));
	Dim->SetContent(PagePad);

	UVerticalBox* MainVB = WidgetTree->ConstructWidget<UVerticalBox>();
	PagePad->SetContent(MainVB);

	// --- Header: grote titel links, Back rechts ---
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* TitleS = Header->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("SETTINGS"), 30, WeedUI::ColAccent(), false, true));
	TitleS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TitleS->SetVerticalAlignment(VAlign_Center);
	UWeedActionButton* BackTop = SetBtn(WidgetTree, TEXT("< Back"), WeedUI::ColInner(),
		[this]() { if (UPhoneClientComponent* Ph = GetPhone()) { Ph->CloseSettings(); } }, 13);
	Header->AddChildToHorizontalBox(BackTop)->SetVerticalAlignment(VAlign_Center);
	MainVB->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	// --- Accent-divider onder de header ---
	UBorder* HDiv = WidgetTree->ConstructWidget<UBorder>();
	HDiv->SetBrush(WeedUI::Rounded(WeedUI::ColAccent(0.75f), 2.f));
	USizeBox* HDivSz = WidgetTree->ConstructWidget<USizeBox>(); HDivSz->SetHeightOverride(2.f); HDivSz->SetContent(HDiv);
	MainVB->AddChildToVerticalBox(HDivSz)->SetPadding(FMargin(0.f, 0.f, 0.f, 22.f));

	// --- Content: links de categorie-nav (verticale tabs), rechts de instellingen ---
	UHorizontalBox* ContentRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* ContentVS = MainVB->AddChildToVerticalBox(ContentRow);
	ContentVS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Linker nav (240px breed; tabs vullen de breedte).
	USizeBox* NavSz = WidgetTree->ConstructWidget<USizeBox>(); NavSz->SetWidthOverride(240.f);
	UVerticalBox* Nav = WidgetTree->ConstructWidget<UVerticalBox>(); NavSz->SetContent(Nav);
	ContentRow->AddChildToHorizontalBox(NavSz)->SetPadding(FMargin(0.f, 0.f, 30.f, 0.f));

	// Tab-wissel: alleen de highlight her-tinten (RefreshTabs) + het zichtbare paneel togglen (ShowActiveCategory).
	// GEEN RefreshContent -> de kit-sliders/toggles blijven bestaan (geen teardown/flash per klik).
	TabGraphics = SetBtn(WidgetTree, TEXT("Graphics"), WeedUI::ColInner(), [this]() { Category = 0; RefreshTabs(); ShowActiveCategory(); }, 15);
	TabGame     = SetBtn(WidgetTree, TEXT("Game"),     WeedUI::ColInner(), [this]() { Category = 1; RefreshTabs(); ShowActiveCategory(); }, 15);
	TabControls = SetBtn(WidgetTree, TEXT("Controls"), WeedUI::ColInner(), [this]() { Category = 2; RefreshTabs(); ShowActiveCategory(); }, 15);
	TabAudio    = SetBtn(WidgetTree, TEXT("Audio"),    WeedUI::ColInner(), [this]() { Category = 3; RefreshTabs(); ShowActiveCategory(); }, 15);
	auto AddTab = [&](UWeedActionButton* B) { UVerticalBoxSlot* TS = Nav->AddChildToVerticalBox(B); TS->SetHorizontalAlignment(HAlign_Fill); TS->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f)); };
	AddTab(TabGraphics); AddTab(TabGame); AddTab(TabControls); AddTab(TabAudio);

	// Rechter content (scrollbaar; rijen in een leesbare max-breedte, links uitgelijnd).
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UHorizontalBox* BodyRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	USizeBox* BodyCap = WidgetTree->ConstructWidget<USizeBox>(); BodyCap->SetWidthOverride(860.f);
	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	BodyCap->SetContent(Body);
	BodyRow->AddChildToHorizontalBox(BodyCap);
	Scroll->AddChild(BodyRow);
	UHorizontalBoxSlot* ScS = ContentRow->AddChildToHorizontalBox(Scroll);
	ScS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); // scroll = natuurlijke 860-breedte -> content blijft compact + centreerbaar

	// Body bevat de 4 categorie-panelen; alleen het actieve is zichtbaar (tab-wissel togglet Visibility).
	PanelGraphics = WidgetTree->ConstructWidget<UVerticalBox>();
	PanelGame     = WidgetTree->ConstructWidget<UVerticalBox>();
	PanelControls = WidgetTree->ConstructWidget<UVerticalBox>();
	PanelAudio    = WidgetTree->ConstructWidget<UVerticalBox>();
	Body->AddChildToVerticalBox(PanelGraphics);
	Body->AddChildToVerticalBox(PanelGame);
	Body->AddChildToVerticalBox(PanelControls);
	Body->AddChildToVerticalBox(PanelAudio);

	// --- Footer: Esc-hint links, Save rechts ---
	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* HintS = Footer->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("Esc to go back"), 11, WeedUI::ColTextDim()));
	HintS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); HintS->SetVerticalAlignment(VAlign_Center);
	// "Saved"-cue: een persistent label dat na Save even oplicht (fade in NativeTick), i.p.v. de body te herbouwen.
	SavedMsg = WeedUI::Text(WidgetTree, TEXT("Saved"), 13, WeedUI::ColGood(), false, true);
	SavedMsg->SetRenderOpacity(0.f);
	Footer->AddChildToHorizontalBox(SavedMsg)->SetVerticalAlignment(VAlign_Center);
	UWeedActionButton* SaveBtn = SetBtn(WidgetTree, TEXT("Save settings"), WeedUI::ColAccent(),
		[this]()
		{
			if (UGameUserSettings* GG = GUS()) { GG->SaveSettings(); }
			bool bL, bP, bM, bV, bR; WeedShop_ReadGfxFlags(bL, bP, bM, bV, bR); WeedShop_WriteGfxFlags(bL, bP, bM, bV, bR, WeedShop_ReadTier());
			// Opslaan verandert GEEN zichtbare waarde -> geen RefreshContent (dat was pure flash). Enkel de Saved-cue.
			SavedMsgOpacity = 1.f; if (SavedMsg) { SavedMsg->SetRenderOpacity(1.f); }
		}, 14);
	UHorizontalBoxSlot* SaveS = Footer->AddChildToHorizontalBox(SaveBtn);
	SaveS->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f)); SaveS->SetVerticalAlignment(VAlign_Center);
	MainVB->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));

	// === Modale "herstart nodig"-popup (verschijnt bij een schaduw-grens-wissel Potato <-> hoger). ===
	UBorder* PopDim = WidgetTree->ConstructWidget<UBorder>();
	PopDim->SetBrush(WeedUI::Rounded(WeedUI::ColBg(0.62f), 0.f));
	PopDim->SetHorizontalAlignment(HAlign_Center); PopDim->SetVerticalAlignment(VAlign_Center);
	RestartPopup = PopDim;
	UCanvasPanelSlot* PSlot = Root->AddChildToCanvas(PopDim);
	PSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); PSlot->SetOffsets(FMargin(0.f));
	USizeBox* PopBox = WidgetTree->ConstructWidget<USizeBox>(); PopBox->SetWidthOverride(560.f); PopDim->SetContent(PopBox);
	// Palet-card: paneel + dunne stroke-rand (card-idioom uit COMMON).
	UBorder* PopPanel = WidgetTree->ConstructWidget<UBorder>();
	FSlateBrush PopBr = WeedUI::Rounded(WeedUI::ColPanel(0.98f), 16.f);
	PopBr.OutlineSettings.Width = 1.f;
	PopBr.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f));
	PopPanel->SetBrush(PopBr); PopPanel->SetPadding(FMargin(28.f, 24.f)); PopBox->SetContent(PopPanel);
	UVerticalBox* PopVB = WidgetTree->ConstructWidget<UVerticalBox>(); PopPanel->SetContent(PopVB);
	PopVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Restart required"), 18, WeedUI::ColHighlight(), true, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	UTextBlock* PopMsg = WeedUI::Text(WidgetTree, TEXT("Changes require a restart."), 12, WeedUI::ColText(), true, false);
	PopMsg->SetAutoWrapText(true);
	PopVB->AddChildToVerticalBox(PopMsg)->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
	UWeedActionButton* PopOk = SetBtn(WidgetTree, TEXT("OK"), WeedUI::ColAccent(), [this]() { if (RestartPopup) { RestartPopup->SetVisibility(ESlateVisibility::Collapsed); } }, 13);
	PopVB->AddChildToVerticalBox(PopOk);
	RestartPopup->SetVisibility(ESlateVisibility::Collapsed);

	Card->SetVisibility(ESlateVisibility::Collapsed);
}

void USettingsWidget::ShowRestartPopup()
{
	if (RestartPopup) { RestartPopup->SetVisibility(ESlateVisibility::Visible); }
}

void USettingsWidget::AddValueRow(UVerticalBox* Into, const FString& Label, const FString& Value, TFunction<void()> OnClick, TObjectPtr<UTextBlock>* OutValueLabel)
{
	if (!Into) { return; }
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Label, 18, WeedUI::ColText()));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);
	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(220.f);
	UTextBlock* ValLbl = nullptr;
	Sz->SetContent(SetBtn(WidgetTree, Value, WeedUI::ColInner(), OnClick, 16, &ValLbl));
	if (OutValueLabel) { *OutValueLabel = ValLbl; } // klik werkt straks alleen dit label bij (geen rebuild)
	Row->AddChildToHorizontalBox(Sz)->SetVerticalAlignment(VAlign_Center);
	Into->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));
}

void USettingsWidget::AddResolutionRow(UVerticalBox* Into)
{
	if (!Into) { return; }
	UGameUserSettings* G = GUS();
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("Resolution"), 18, WeedUI::ColText()));
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
	Into->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));
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

// --- Kit-W_Slider Value-reflectie (BP-var "Value" = double; val terug op float) ---
static double KitSliderValue(UUserWidget* W)
{
	if (!W) { return 0.0; }
	if (FDoubleProperty* P = FindFProperty<FDoubleProperty>(W->GetClass(), TEXT("Value"))) { return P->GetPropertyValue_InContainer(W); }
	if (FFloatProperty* P = FindFProperty<FFloatProperty>(W->GetClass(), TEXT("Value"))) { return (double)P->GetPropertyValue_InContainer(W); }
	return 0.0;
}
static void SetKitSliderValue(UUserWidget* W, double V)
{
	if (!W) { return; }
	if (FDoubleProperty* DP = FindFProperty<FDoubleProperty>(W->GetClass(), TEXT("Value"))) { DP->SetPropertyValue_InContainer(W, V); }
	else if (FFloatProperty* FP = FindFProperty<FFloatProperty>(W->GetClass(), TEXT("Value"))) { FP->SetPropertyValue_InContainer(W, (float)V); }
}

void USettingsWidget::AddKitToggle(UVerticalBox* Into, const FString& Label, bool Initial, TFunction<void(bool)> Apply, TWeakObjectPtr<UUserWidget>* OutW)
{
	if (!Into) { return; }
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Label, 18, WeedUI::ColText()));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);
	UClass* ToggleCls = LoadClass<UUserWidget>(nullptr, TEXT("/Game/minimalist_gui/widgets/templates/toggle/W_Toggle_Template.W_Toggle_Template_C")); // NIET static -> GC kan de class opruimen, dangling pointer = crash
	if (ToggleCls)
	{
		if (UUserWidget* Tog = CreateWidget<UUserWidget>(this, ToggleCls))
		{
			if (FBoolProperty* P = FindFProperty<FBoolProperty>(Tog->GetClass(), TEXT("IsToggled"))) { P->SetPropertyValue_InContainer(Tog, Initial); }
			Row->AddChildToHorizontalBox(Tog)->SetVerticalAlignment(VAlign_Center);
			KitToggles.Add({ Tog, Initial, Apply });
			if (OutW) { *OutW = Tog; } // handle zodat een andere rij (Preset) de waarde er later in kan duwen (reflectie, geen rebuild)
		}
	}
	Into->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));
}

void USettingsWidget::AddKitSlider(UVerticalBox* Into, const FString& Label, double InitialNorm, TFunction<FString(double, int32&)> Apply)
{
	if (!Into) { return; }
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Label, 18, WeedUI::ColText()));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);

	UTextBlock* ValText = WeedUI::Text(WidgetTree, TEXT(""), 16, WeedUI::ColAccent());
	UClass* SliderCls = LoadClass<UUserWidget>(nullptr, TEXT("/Game/minimalist_gui/widgets/templates/slider/W_Slider_Template.W_Slider_Template_C")); // NIET static -> GC-veilig (herlaadt indien opgeruimd)
	if (SliderCls)
	{
		if (UUserWidget* Sld = CreateWidget<UUserWidget>(this, SliderCls))
		{
			SetKitSliderValue(Sld, InitialNorm);
			USizeBox* SBox = WidgetTree->ConstructWidget<USizeBox>();
			SBox->SetWidthOverride(250.f); SBox->SetHeightOverride(34.f);
			SBox->SetContent(Sld);
			UHorizontalBoxSlot* SS = Row->AddChildToHorizontalBox(SBox);
			SS->SetVerticalAlignment(VAlign_Center); SS->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));

			FKitSlider KS; KS.W = Sld; KS.Apply = Apply; KS.ValText = ValText;
			const FString Disp = Apply ? Apply(InitialNorm, KS.LastKey) : FString();
			if (!Disp.IsEmpty()) { ValText->SetText(FText::FromString(Disp)); }
			KitSliders.Add(KS);
		}
	}
	USizeBox* VBox = WidgetTree->ConstructWidget<USizeBox>(); VBox->SetMinDesiredWidth(56.f); VBox->SetContent(ValText);
	Row->AddChildToHorizontalBox(VBox)->SetVerticalAlignment(VAlign_Center);
	Into->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));
}

void USettingsWidget::RefreshTabs()
{
	auto Tint = [](UWeedActionButton* B, bool bSel)
	{
		if (!B) { return; }
		// Geselecteerd = accent-vlak (ColAccentDim), anders neutraal binnen-paneel. Tall sidebar-tabs.
		const FLinearColor C = bSel ? WeedUI::ColAccentDim(0.98f) : WeedUI::ColInner(0.92f);
		const FLinearColor H = bSel ? WeedUI::ColAccentDim(1.f)   : WeedUI::ColInner(0.96f);
		FButtonStyle S; S.Normal = WeedUI::Rounded(C, 8.f); S.Hovered = WeedUI::Rounded(H, 8.f); S.Pressed = WeedUI::Rounded(H, 8.f);
		S.NormalPadding = FMargin(16.f, 13.f); S.PressedPadding = FMargin(16.f, 13.f);
		B->SetStyle(S);
	};
	Tint(TabGraphics, Category == 0);
	Tint(TabGame, Category == 1);
	Tint(TabControls, Category == 2);
	Tint(TabAudio, Category == 3);
}

// Zet een kit-W_Toggle's IsToggled via reflectie ÉN houdt de matchende FKitToggle.Last bij, zodat de
// NativeTick-poll deze programmatische wijziging niet als speler-toggle ziet (anders vuurt Apply dubbel).
void USettingsWidget::SetKitToggleValueInPlace(TWeakObjectPtr<UUserWidget> W, bool bValue)
{
	UUserWidget* Tog = W.Get();
	if (!Tog) { return; }
	if (FBoolProperty* P = FindFProperty<FBoolProperty>(Tog->GetClass(), TEXT("IsToggled"))) { P->SetPropertyValue_InContainer(Tog, bValue); }
	for (FKitToggle& KT : KitToggles) { if (KT.W.Get() == Tog) { KT.Last = bValue; break; } }
}

// Bouwt de 4 categorie-panelen ÉÉN keer. Daarna wisselt een tab-klik alleen Visibility -> geen teardown/flash,
// kit-sliders/toggles blijven bestaan (en blijven gepolld).
void USettingsWidget::BuildAllPanels()
{
	if (bPanelsBuilt || !Body) { return; }
	bPanelsBuilt = true;
	BuildGraphicsPanel(PanelGraphics);
	BuildGamePanel(PanelGame);
	BuildAudioPanel(PanelAudio);
	BuildControlsPanel(PanelControls);
	ShowActiveCategory();
}

// Alleen de tekst van de Graphics cycle-labels naar de LIVE status zetten (bv. window mode kan door de OS
// zijn gewijzigd tussen twee keer openen). Geen widget-constructie -> geen flash, enkel SetText.
void USettingsWidget::RefreshGraphicsLabels()
{
	UGameUserSettings* G = GUS();
	if (!G) { return; }

	// Window mode (live venster).
	EWindowMode::Type WM = G->GetFullscreenMode();
	if (GEngine && GEngine->GameViewport)
	{
		TSharedPtr<SWindow> Win = GEngine->GameViewport->GetWindow();
		if (Win.IsValid()) { WM = Win->GetWindowMode(); }
	}
	if (WindowModeVal)
	{
		const FString WMName = (WM == EWindowMode::Fullscreen) ? TEXT("Fullscreen")
			: (WM == EWindowMode::WindowedFullscreen) ? TEXT("Borderless") : TEXT("Windowed");
		WindowModeVal->SetText(FText::FromString(WMName));
	}

	// Preset (tier). Persistente Tier-key = source of truth (GetOverallScalabilityLevel geeft -1 "custom"
	// zodra één sub-level afwijkt, zoals Epic's GI-op-High -> daar kun je het label niet meer op baseren).
	{
		const int32 TierNow = WeedShop_ReadTier();
		static const TCHAR* QN[5] = { TEXT("Potato"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
		if (PresetVal && TierNow + 1 >= 0 && TierNow + 1 < 5) { PresetVal->SetText(FText::FromString(QN[TierNow + 1])); }
	}

	// Textures.
	{
		static const TCHAR* LV[4] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
		const int32 TQ = G->GetTextureQuality();
		if (TexturesVal) { TexturesVal->SetText(FText::FromString((TQ >= 0 && TQ <= 3) ? LV[TQ] : TEXT("Custom"))); }
	}

	// Resolution scale.
	if (ResScaleVal)
	{
		const int32 RS = FMath::RoundToInt(G->GetResolutionScaleNormalized() * 100.f);
		ResScaleVal->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), RS)));
	}

	// Frame limit.
	if (FrameLimitVal)
	{
		const float FL = G->GetFrameRateLimit();
		FrameLimitVal->SetText(FText::FromString((FL <= 0.f) ? FString(TEXT("Uncapped")) : FString::Printf(TEXT("%d FPS"), (int32)FL)));
	}
}

void USettingsWidget::ShowActiveCategory()
{
	auto Vis = [](UVerticalBox* P, bool bShow)
	{
		if (P) { P->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	};
	Vis(PanelGraphics, Category == 0);
	Vis(PanelGame,     Category == 1);
	Vis(PanelControls, Category == 2);
	Vis(PanelAudio,    Category == 3);
}

void USettingsWidget::BuildGraphicsPanel(UVerticalBox* P)
{
	if (!P) { return; }
	UGameUserSettings* G = GUS();
	if (!G) { P->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Graphics unavailable."), 13, WeedUI::ColTextDim())); return; }

	// Resolutie-dropdown.
	AddResolutionRow(P);

	// Window mode - lees de ECHTE huidige vensterstatus van het live venster. GameUserSettings::
	// GetFullscreenMode() kan achterlopen (na alt-tab / OS-wissel / fallback), waardoor het label
	// niet klopte met wat je zag en je vanaf een verkeerde stand cyclede.
	auto ReadWindowMode = []() -> EWindowMode::Type
	{
		EWindowMode::Type WM = GUS() ? GUS()->GetFullscreenMode() : EWindowMode::Windowed;
		if (GEngine && GEngine->GameViewport)
		{
			TSharedPtr<SWindow> Win = GEngine->GameViewport->GetWindow();
			if (Win.IsValid()) { WM = Win->GetWindowMode(); }
		}
		return WM;
	};
	auto WMLabel = [](EWindowMode::Type WM) -> FString
	{
		return (WM == EWindowMode::Fullscreen) ? TEXT("Fullscreen")
			: (WM == EWindowMode::WindowedFullscreen) ? TEXT("Borderless") : TEXT("Windowed");
	};
	AddValueRow(P, TEXT("Window mode"), WMLabel(ReadWindowMode()), [this, ReadWindowMode, WMLabel]()
	{
		if (UGameUserSettings* GG = GUS())
		{
			const EWindowMode::Type ActualWM = ReadWindowMode(); // LIVE gelezen (geen rebuild meer, dus niet uit een captured build-waarde)
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
			if (WindowModeVal) { WindowModeVal->SetText(FText::FromString(WMLabel(Next))); } // alleen dit label bijwerken (geen rebuild)
		}
	}, &WindowModeVal);

	// PRESET - zet alles in één keer. Potato (onder Low, voor zwakke pc's) -> Low -> Medium -> High -> Epic.
	{
		const int32 TierNow = WeedShop_ReadTier(); // persistente Tier-key (GetOverallScalabilityLevel kan -1 "custom" zijn)
		static const TCHAR* QN[5] = { TEXT("Potato"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
		AddValueRow(P, TEXT("Preset (all)"), QN[TierNow + 1], [this]()
		{
			// Huidige tier LIVE lezen (persistente Tier-key; geen captured build-waarde meer nu we niet meer rebuilden).
			const int32 CurTier = WeedShop_ReadTier();
			const int32 Next = (CurTier >= 3) ? -1 : CurTier + 1;
			WeedShop_ApplyGraphicsTier(Next);
			bool bLum, bPot, bMb, bVSM, bRT; WeedShop_ReadGfxFlags(bLum, bPot, bMb, bVSM, bRT);
			// Vlaggen volgen de preset: Lumen alleen op Epic; MotionBlur behouden. VSMOff (schaduwen) en
			// RTOff (ray tracing experimental) NIET via de preset -> hun eigen toggles bezitten die vlaggen.
			WeedShop_WriteGfxFlags((Next < 3), (Next <= -1), bMb, bVSM, bRT, Next); // + Tier=N persistent (boot leest 'm terug)
			if (UWorld* WB = GetWorld()) { if (WB->GetOutermost()->GetName().StartsWith(TEXT("/Game/CityBeachStrip")))
			{
				WeedShop_ApplyDistanceFieldGI(true); // vangnet live-switch: scalability-wissel mag DF-AO/shadows (OOM-bron) nooit terug aanzetten
				WeedShop_ApplyBeachShadows(bVSM);    // tier-wissel past de beach-gate opnieuw toe -> schaduwen komen deterministisch terug
			} }
			// In-place labels + afgeleide kit-toggles bijwerken (geen rebuild):
			static const TCHAR* QNames[5] = { TEXT("Potato"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
			if (PresetVal) { PresetVal->SetText(FText::FromString(QNames[Next + 1])); }
			if (UGameUserSettings* GG = GUS())
			{
				const int32 TQ = GG->GetTextureQuality();
				static const TCHAR* LV[4] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
				if (TexturesVal) { TexturesVal->SetText(FText::FromString((TQ >= 0 && TQ <= 3) ? LV[TQ] : TEXT("Custom"))); }
			}
			SetKitToggleValueInPlace(LumenToggleW, (Next >= 3));      // Lumen aan alleen op Epic
			SetKitToggleValueInPlace(MotionBlurToggleW, !bMb);       // MotionBlur ongewijzigd -> volg de vlag
		}, &PresetVal);
	}


	// --- Losse kwaliteit-instellingen (elk Low/Medium/High/Epic; samen vormen ze het preset) ---
	// Textures: cycle-rij met persistent waarde-label (klik = SetText alleen dit label).
	{
		static const TCHAR* LV[4] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
		const int32 TQ = G->GetTextureQuality();
		AddValueRow(P, TEXT("Textures"), (TQ >= 0 && TQ <= 3) ? LV[TQ] : TEXT("Custom"), [this]()
		{
			if (UGameUserSettings* GG = GUS())
			{
				int32 Cur = GG->GetTextureQuality(); // LIVE
				const int32 c = (Cur < 0) ? 2 : Cur;
				const int32 NextQ = (c + 1) % 4;
				GG->SetTextureQuality(NextQ);
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
				static const TCHAR* LVn[4] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
				if (TexturesVal) { TexturesVal->SetText(FText::FromString(LVn[NextQ])); }
			}
		}, &TexturesVal);
	}
	{ // Shadows: ON/OFF (eigen vlag, los van de Preset; VSM aan/uit). Geldt na herstart (VSM-pool).
		bool bL0, bP0, bM0, bV0, bR0; WeedShop_ReadGfxFlags(bL0, bP0, bM0, bV0, bR0);
		AddKitToggle(P, TEXT("Shadows"), !bV0, [this](bool bOn)
		{
			bool bLn, bPn, bMn, bVn, bRn; WeedShop_ReadGfxFlags(bLn, bPn, bMn, bVn, bRn); // LIVE (andere vlaggen niet overschrijven)
			WeedShop_WriteGfxFlags(bLn, bPn, bMn, !bOn, bRn, WeedShop_ReadTier()); // bOn = schaduwen aan -> VSMOff = !bOn
			if (UWorld* WB = GetWorld()) { if (WB->GetOutermost()->GetName().StartsWith(TEXT("/Game/CityBeachStrip"))) { WeedShop_ApplyBeachShadows(bOn); } }
			ShowRestartPopup(); // VSM-pool kan niet live togglen -> geldt na herstart
		}, &ShadowsToggleW);
	}

	// Renderer: DirectX 12 (SM6, volledige features) <-> DirectX 11 (SM5, lichter: geen Nanite/Lumen/VSM,
	// omzeilt de D3D12 GPU-Scene reserved-buffer-VRAM-crash). RHI wordt bij boot gekozen -> geldt na herstart.
	{
		const int32 RHINow = WeedShop_ReadPreferredRHI(); // 0 = DX12, 1 = DX11
		AddValueRow(P, TEXT("Renderer"), (RHINow == 1) ? TEXT("DirectX 11") : TEXT("DirectX 12"), [this]()
		{
			const bool bNowDX11 = (WeedShop_ReadPreferredRHI() == 1);
			WeedShop_WritePreferredRHI(!bNowDX11); // togglen: DX12 <-> DX11
			if (RendererVal) { RendererVal->SetText(FText::FromString(!bNowDX11 ? TEXT("DirectX 11") : TEXT("DirectX 12"))); }
			ShowRestartPopup(); // RHI wordt bij het opstarten gekozen -> pas actief na herstart
		}, &RendererVal);
		P->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("DX11 = lighter, no Nanite/Lumen; needs restart"), 12, WeedUI::ColTextDim()))->SetPadding(FMargin(0.f, -6.f, 0.f, 8.f));
	}

	// Resolutie-schaal (render-%): 50 -> 75 -> 100.
	{
		const int32 RS = FMath::RoundToInt(G->GetResolutionScaleNormalized() * 100.f);
		AddValueRow(P, TEXT("Resolution scale"), FString::Printf(TEXT("%d%%"), RS), [this]()
		{
			if (UGameUserSettings* GG = GUS())
			{
				const int32 Cur = FMath::RoundToInt(GG->GetResolutionScaleNormalized() * 100.f);
				const int32 Next = (Cur < 63) ? 75 : (Cur < 88) ? 100 : 50;
				GG->SetResolutionScaleNormalized(Next / 100.f);
				GG->ApplySettings(false); GG->SaveSettings();
				if (ResScaleVal) { ResScaleVal->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), Next))); }
			}
		}, &ResScaleVal);
	}

	// Lumen (GI + reflecties op de goedkope methode) - aan/uit.
	{
		bool bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff;
		WeedShop_ReadGfxFlags(bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff);
		AddKitToggle(P, TEXT("Lumen (GI + reflections)"), !bLumenOff, [this](bool bOn)
		{
			WeedShop_ApplyLumen(bOn);
			bool bLn, bPn, bMn, bVn, bRn; WeedShop_ReadGfxFlags(bLn, bPn, bMn, bVn, bRn); // LIVE
			WeedShop_WriteGfxFlags(!bOn, bPn, bMn, bVn, bRn, WeedShop_ReadTier());
		}, &LumenToggleW);
	}

	// Ray tracing (experimenteel) - hardware-RT-effecten (RT-schaduwen + RT-AO). Eigen opt-in-vlag (RTOff),
	// default UIT; de presets zetten dit NOOIT aan (per-frame TLAS over de hele geinstancede stad = zwaar).
	{
		bool bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff;
		WeedShop_ReadGfxFlags(bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff);
		AddKitToggle(P, TEXT("Ray tracing (experimental)"), !bRTOff, [this](bool bOn)
		{
			WeedShop_ApplyRayTracing(!bOn); // bOn = RT aan -> ApplyRayTracing(bOff=false)
			bool bLn, bPn, bMn, bVn, bRn; WeedShop_ReadGfxFlags(bLn, bPn, bMn, bVn, bRn); // LIVE (andere vlaggen niet overschrijven)
			WeedShop_WriteGfxFlags(bLn, bPn, bMn, bVn, !bOn, WeedShop_ReadTier()); // de toggle BEZIT de RTOff-vlag (zoals VSMOff)
		}, &RayTracingToggleW);
		// Dim-hint onder de toggle (persistent, één keer gebouwd - geen rebuild).
		P->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Heavy - needs a strong GPU"), 12, WeedUI::ColTextDim()))->SetPadding(FMargin(0.f, -6.f, 0.f, 8.f));
	}

	// Motion blur - aan/uit.
	{
		bool bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff;
		WeedShop_ReadGfxFlags(bLumenOff, bPotato, bMbOff, bVSMOff, bRTOff);
		AddKitToggle(P, TEXT("Motion blur"), !bMbOff, [this](bool bOn)
		{
			WeedShop_ApplyMotionBlur(bOn);
			bool bLn, bPn, bMn, bVn, bRn; WeedShop_ReadGfxFlags(bLn, bPn, bMn, bVn, bRn); // LIVE
			WeedShop_WriteGfxFlags(bLn, bPn, !bOn, bVn, bRn, WeedShop_ReadTier());
		}, &MotionBlurToggleW);
	}

	// VSync — kit-W_Toggle (gepolld) i.p.v. cycle-knop (proof voor de kit-controls).
	AddKitToggle(P, TEXT("V-Sync"), G->IsVSyncEnabled(), [this](bool b)
	{
		if (UGameUserSettings* GG = GUS()) { GG->SetVSyncEnabled(b); GG->ApplySettings(false); GG->SaveSettings(); }
	});

	// Frame rate limit.
	const float FL = G->GetFrameRateLimit();
	const FString FLName = (FL <= 0.f) ? TEXT("Uncapped") : FString::Printf(TEXT("%d FPS"), (int32)FL);
	AddValueRow(P, TEXT("Frame limit"), FLName, [this]()
	{
		if (UGameUserSettings* GG = GUS())
		{
			const float Cur = GG->GetFrameRateLimit();
			float Next = 60.f;
			if (Cur <= 0.f) Next = 30.f; else if (Cur < 45.f) Next = 60.f; else if (Cur < 90.f) Next = 120.f; else if (Cur < 132.f) Next = 144.f; else Next = 0.f;
			GG->SetFrameRateLimit(Next); GG->ApplySettings(false); GG->SaveSettings();
			if (FrameLimitVal) { FrameLimitVal->SetText(FText::FromString((Next <= 0.f) ? FString(TEXT("Uncapped")) : FString::Printf(TEXT("%d FPS"), (int32)Next))); }
		}
	}, &FrameLimitVal);
}

void USettingsWidget::BuildGamePanel(UVerticalBox* P)
{
	if (!P) { return; }

	// Character: Man/Vrouw-keuze. Het MODEL/gezicht + bijbehorende kleren kies je daarna in de Wardrobe.
	if (IPlayerNpcActions* PA = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
	{
		const uint8 Sk = PA->GetPlayerSkinIndex();
		const bool bMale = (Sk == 5 || Sk == 6 || Sk == 0); // 5/6 = man (Tony/Citizen), 0 = Manny (legacy) ; 1-4 = vrouw
		AddValueRow(P, TEXT("Character"), bMale ? TEXT("Male") : TEXT("Female"), [this]()
		{
			if (IPlayerNpcActions* Pn = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
			{
				const uint8 Cur = Pn->GetPlayerSkinIndex();
				const bool bIsMale = (Cur == 5 || Cur == 6 || Cur == 0);
				Pn->SetPlayerSkinIndex(bIsMale ? 2 : 5); // -> vrouw (Girl 1) of man (Tony)
				if (CharacterVal) { CharacterVal->SetText(FText::FromString(bIsMale ? TEXT("Female") : TEXT("Male"))); } // alleen dit label (geen rebuild)
			}
		}, &CharacterVal);
	}

	UPhoneClientComponent* Ph = GetPhone();
	const float Fov = Ph ? Ph->GetFov() : 90.f;          // 60..120
	const float Sens = Ph ? Ph->GetLookSensitivity() : 1.f; // 0.1..3.0

	AddKitSlider(P, TEXT("Field of view"), (Fov - 60.f) / 60.f, [this](double n, int32& Key) -> FString
	{
		const int32 V = FMath::RoundToInt(60.0 + n * 60.0);
		if (V == Key) { return FString(); }
		Key = V;
		if (UPhoneClientComponent* Pp = GetPhone()) { Pp->ApplyFov((float)V); }
		return FString::Printf(TEXT("%d"), V);
	});

	AddKitSlider(P, TEXT("Mouse sensitivity"), (Sens - 0.1f) / 2.9f, [this](double n, int32& Key) -> FString
	{
		const int32 S10 = FMath::RoundToInt((0.1 + n * 2.9) * 10.0);
		if (S10 == Key) { return FString(); }
		Key = S10;
		if (UPhoneClientComponent* Pp = GetPhone()) { Pp->SetLookSensitivity(S10 / 10.f); }
		return FString::Printf(TEXT("%.1f"), S10 / 10.f);
	});

	// Head bobbing aan/uit (camera loop-wiebel).
	const bool bBobNow = Ph ? Ph->GetHeadBob() : true;
	AddKitToggle(P, TEXT("Head bobbing"), bBobNow, [this](bool bOn)
	{
		if (UPhoneClientComponent* P2 = GetPhone()) { P2->SetHeadBob(bOn); }
	});
}

void USettingsWidget::BuildAudioPanel(UVerticalBox* P)
{
	if (!P) { return; }
	auto AddVol = [this, P](const FString& L, int32 Cat)
	{
		AddKitSlider(P, L, WeedUI::SoundCategoryVolume(Cat), [Cat](double n, int32& Key) -> FString
		{
			const int32 Pct = FMath::RoundToInt(n * 100.0);
			if (Pct == Key) { return FString(); }
			Key = Pct;
			WeedUI::SetSoundCategoryVolume(Cat, Pct / 100.f);
			return FString::Printf(TEXT("%d%%"), Pct);
		});
	};
	AddVol(TEXT("UI volume"), 0);
	AddVol(TEXT("Game volume"), 1);
	AddVol(TEXT("Music volume"), 2);

	P->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Music comes later; the slider is ready for it."), 13, WeedUI::ColTextDim()))
		->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
}

void USettingsWidget::BuildControlsPanel(UVerticalBox* P)
{
	if (!P) { return; }

	// Controls-overlay (de toetsen-hint rechtsonder) aan/uit.
	AddKitToggle(P, TEXT("Controls overlay"), UHotkeyHintWidget::AreHintsEnabled(),
		[this](bool bOn) { UHotkeyHintWidget::SetHintsEnabled(bOn); });

	P->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Click a key, press the new one.  Esc = cancel."), 13, WeedUI::ColTextDim()))
		->SetPadding(FMargin(0.f, 6.f, 0.f, 4.f));

	// Scrollbare lijst met alle acties (Main + Alt toets) — ÉÉN keer gebouwd; rebind/capture werkt
	// straks alleen de getroffen key-knop(pen) bij (SetText/kleur) -> geen ClearChildren, scroll blijft.
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* ScS = P->AddChildToVerticalBox(Scroll);
	ScS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	KeyButtons.Reset();
	UControlSettings* Cfg = UControlSettings::Get();
	for (const FName& Action : UControlSettings::AllActions())
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* NameT = WeedUI::Text(WidgetTree, UControlSettings::DisplayName(Action).ToString(), 16, WeedUI::ColText());
		UHorizontalBoxSlot* NS = Row->AddChildToHorizontalBox(NameT);
		NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NS->SetVerticalAlignment(VAlign_Center);

		for (int32 SlotIdx = 0; SlotIdx < 2; ++SlotIdx)
		{
			const bool bAlt = (SlotIdx == 1);
			const FKey K = Cfg->GetKey(Action, bAlt);
			const FString Lbl = K.IsValid() ? K.GetDisplayName().ToString() : (bAlt ? TEXT("+ Alt") : TEXT("-"));
			UTextBlock* BtnLbl = nullptr;
			USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
			Sz->SetWidthOverride(96.f);
			UWeedActionButton* Btn = SetBtn(WidgetTree, Lbl, WeedUI::ColInner(),
				[this, Action, bAlt]() { bRebinding = true; bRebindAlt = bAlt; RebindAction = Action; RebindMsg.Reset(); SetKeyboardFocus(); RefreshKeyButtonsFor(Action); }, 13, &BtnLbl);
			Sz->SetContent(Btn);
			Row->AddChildToHorizontalBox(Sz)->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			FKeyBtn KB; KB.Btn = Btn; KB.Label = BtnLbl; KB.Action = Action; KB.bAlt = bAlt;
			KeyButtons.Add(KB);
		}
		Scroll->AddChild(Row);
		Scroll->AddChild(WeedUI::Text(WidgetTree, TEXT(""), 3, FLinearColor::Transparent));
	}

	// Persistent RebindMsg-tekst (in-place SetText; leeg -> Collapsed).
	RebindMsgText = WeedUI::Text(WidgetTree, TEXT(""), 12, WeedUI::ColHighlight());
	RebindMsgText->SetVisibility(ESlateVisibility::Collapsed);
	P->AddChildToVerticalBox(RebindMsgText)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

	P->AddChildToVerticalBox(SetBtn(WidgetTree, TEXT("Reset to defaults"), WeedUI::ColInner(),
		[this]() { UControlSettings::Get()->ResetToDefaults(); bRebinding = false; RebindMsg = TEXT("Controls reset to defaults."); RefreshAllKeyButtons(); }, 12))
		->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
}

// Werk één key-knop bij (label + kleur) zonder de rij/lijst te herbouwen.
void USettingsWidget::UpdateKeyButtonLabel(const FKeyBtn& KB)
{
	UControlSettings* Cfg = UControlSettings::Get();
	const bool bThis = bRebinding && (RebindAction == KB.Action) && (bRebindAlt == KB.bAlt);
	const FKey K = Cfg ? Cfg->GetKey(KB.Action, KB.bAlt) : FKey();
	const FString Lbl = bThis ? TEXT("Press...") : (K.IsValid() ? K.GetDisplayName().ToString() : (KB.bAlt ? TEXT("+ Alt") : TEXT("-")));
	if (UTextBlock* L = KB.Label.Get()) { L->SetText(FText::FromString(Lbl)); }
	if (UWeedActionButton* B = KB.Btn.Get())
	{
		const FLinearColor Col = bThis ? WeedUI::ColAccentDim() : WeedUI::ColInner();
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 7.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 7.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 7.f);
		S.NormalPadding = FMargin(13.f, 7.f); S.PressedPadding = FMargin(13.f, 7.f);
		B->SetStyle(S);
	}
}

void USettingsWidget::UpdateRebindMsg()
{
	if (!RebindMsgText) { return; }
	if (RebindMsg.IsEmpty()) { RebindMsgText->SetVisibility(ESlateVisibility::Collapsed); }
	else { RebindMsgText->SetText(FText::FromString(RebindMsg)); RebindMsgText->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
}

// Update alleen de knoppen die deze actie tonen (main+alt) + de msg-tekst. Geen lijst-teardown.
void USettingsWidget::RefreshKeyButtonsFor(FName Action)
{
	for (const FKeyBtn& KB : KeyButtons) { if (KB.Action == Action) { UpdateKeyButtonLabel(KB); } }
	UpdateRebindMsg();
}

// Na Reset-to-defaults: alle labels opnieuw zetten (nog steeds geen rebuild van de lijst).
void USettingsWidget::RefreshAllKeyButtons()
{
	for (const FKeyBtn& KB : KeyButtons) { UpdateKeyButtonLabel(KB); }
	UpdateRebindMsg();
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
		if (bOpen)
		{
			BuildAllPanels(); RefreshTabs(); ShowActiveCategory(); RefreshGraphicsLabels(); // panelen ÉÉN keer bouwen; labels naar live status (geen rebuild)
			// NIET SetKeyboardFocus() aanroepen: in de FInputModeGameAndUI-modus (met CaptureDuringMouseDown)
			// brak dat het KLIKKEN op alle sliders/toggles/knoppen (de focus-greep verstoort de muis-capture-
			// routing, net als NoCapture eerder deed). IN-GAME sluit Esc de settings al focus-onafhankelijk via
			// AThePlugSIMCharacter::OnPauseKey -> CloseAllUI(); op het HOOFDMENU (geen character-input) sluit je
			// met de "< Back"-knop. NativeOnKeyDown-Esc blijft als extra werken zodra het widget wel focus heeft.
		}
	}
	if (!bOpen) { return; }

	// "Saved"-cue vervagen (in-place opacity, geen rebuild).
	if (SavedMsgOpacity > 0.f)
	{
		SavedMsgOpacity = FMath::Max(0.f, SavedMsgOpacity - DeltaTime * 0.6f);
		if (SavedMsg) { SavedMsg->SetRenderOpacity(SavedMsgOpacity); }
	}

	// Kit-sliders pollen (W_Slider.Value via reflectie -> Apply mapt+past toe+geeft display; leeg = onveranderd).
	for (FKitSlider& KS : KitSliders)
	{
		if (UUserWidget* W = KS.W.Get())
		{
			const FString Disp = KS.Apply ? KS.Apply(KitSliderValue(W), KS.LastKey) : FString();
			if (!Disp.IsEmpty()) { if (UTextBlock* VT = KS.ValText.Get()) { VT->SetText(FText::FromString(Disp)); } }
		}
	}

	// Kit-toggles pollen (W_Toggle.IsToggled via reflectie -> apply bij wijziging).
	for (FKitToggle& KT : KitToggles)
	{
		if (UUserWidget* W = KT.W.Get())
		{
			if (FBoolProperty* P = FindFProperty<FBoolProperty>(W->GetClass(), TEXT("IsToggled")))
			{
				const bool Cur = P->GetPropertyValue_InContainer(W);
				if (Cur != KT.Last) { KT.Last = Cur; if (KT.Apply) { KT.Apply(Cur); } }
			}
		}
	}
}
