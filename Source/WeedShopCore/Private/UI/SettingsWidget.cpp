#include "UI/SettingsWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"

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
#include "GameFramework/Pawn.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"

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
	Tabs->AddChildToHorizontalBox(TabGraphics)->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	Tabs->AddChildToHorizontalBox(TabGame)->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
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

void USettingsWidget::AddStepRow(const FString& Label, const FString& Value, TFunction<void()> OnMinus, TFunction<void()> OnPlus)
{
	if (!Body) { return; }
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, Label, 14, FLinearColor(0.88f, 0.9f, 1.f)));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);
	Row->AddChildToHorizontalBox(SetBtn(WidgetTree, TEXT("-"), FLinearColor(0.18f, 0.2f, 0.27f), OnMinus, 13))->SetVerticalAlignment(VAlign_Center);
	UTextBlock* Val = WeedUI::Text(WidgetTree, Value, 14, FLinearColor::White, true);
	USizeBox* VSz = WidgetTree->ConstructWidget<USizeBox>(); VSz->SetWidthOverride(90.f); VSz->SetContent(Val);
	Row->AddChildToHorizontalBox(VSz)->SetVerticalAlignment(VAlign_Center);
	Row->AddChildToHorizontalBox(SetBtn(WidgetTree, TEXT("+"), FLinearColor(0.18f, 0.2f, 0.27f), OnPlus, 13))->SetVerticalAlignment(VAlign_Center);
	Body->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 5.f, 0.f, 5.f));
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
}

void USettingsWidget::RefreshContent()
{
	if (!Body) { return; }
	Body->ClearChildren();

	if (Category == 0) // Graphics
	{
		UGameUserSettings* G = GUS();
		if (!G) { Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Graphics unavailable."), 13, FLinearColor::Gray)); return; }

		// Window mode.
		const EWindowMode::Type WM = G->GetFullscreenMode();
		const FString WMName = (WM == EWindowMode::Fullscreen) ? TEXT("Fullscreen")
			: (WM == EWindowMode::WindowedFullscreen) ? TEXT("Borderless") : TEXT("Windowed");
		AddValueRow(TEXT("Window mode"), WMName, [this]()
		{
			if (UGameUserSettings* GG = GUS())
			{
				const EWindowMode::Type Cur = GG->GetFullscreenMode();
				EWindowMode::Type Next = (Cur == EWindowMode::Fullscreen) ? EWindowMode::WindowedFullscreen
					: (Cur == EWindowMode::WindowedFullscreen) ? EWindowMode::Windowed : EWindowMode::Fullscreen;
				GG->SetFullscreenMode(Next); GG->ApplySettings(false); GG->SaveSettings(); RefreshContent();
			}
		});

		// Quality preset.
		const int32 Q = G->GetOverallScalabilityLevel();
		static const TCHAR* QN[4] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
		const FString QName = (Q >= 0 && Q <= 3) ? QN[Q] : TEXT("Custom");
		AddValueRow(TEXT("Quality"), QName, [this]()
		{
			if (UGameUserSettings* GG = GUS())
			{
				int32 Cur = GG->GetOverallScalabilityLevel(); if (Cur < 0) { Cur = 2; }
				const int32 Next = (Cur + 1) % 4;
				GG->SetOverallScalabilityLevel(Next); GG->ApplySettings(false); GG->SaveSettings(); RefreshContent();
			}
		});

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
	else // Game
	{
		UPhoneClientComponent* Ph = GetPhone();
		const float Fov = Ph ? Ph->GetFov() : 90.f;
		AddStepRow(TEXT("Field of view"), FString::Printf(TEXT("%d"), (int32)Fov),
			[this]() { if (UPhoneClientComponent* P = GetPhone()) { P->ApplyFov(P->GetFov() - 5.f); RefreshContent(); } },
			[this]() { if (UPhoneClientComponent* P = GetPhone()) { P->ApplyFov(P->GetFov() + 5.f); RefreshContent(); } });

		const float Sens = Ph ? Ph->GetLookSensitivity() : 1.f;
		AddStepRow(TEXT("Mouse sensitivity"), FString::Printf(TEXT("%.1f"), Sens),
			[this]() { if (UPhoneClientComponent* P = GetPhone()) { P->SetLookSensitivity(P->GetLookSensitivity() - 0.1f); RefreshContent(); } },
			[this]() { if (UPhoneClientComponent* P = GetPhone()) { P->SetLookSensitivity(P->GetLookSensitivity() + 0.1f); RefreshContent(); } });

		Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Tip: rebind keys in the phone Settings app (controls)."), 11, FLinearColor(0.55f, 0.6f, 0.7f)))
			->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
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
}
