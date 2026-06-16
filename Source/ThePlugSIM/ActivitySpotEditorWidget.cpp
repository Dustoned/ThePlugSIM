#include "ActivitySpotEditorWidget.h"

#include "UI/WeedUiStyle.h"
#include "World/ActivitySpotManager.h"
#include "Customer/CustomerBase.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "GameFramework/PlayerController.h"

namespace
{
	FString HourStr(int32 H) { return FString::Printf(TEXT("%02d:00"), FMath::Clamp(H, 0, 24)); }
}

void UActivitySpotEditorWidget::Setup(AActivitySpotManager* InMgr, ACustomerBase* InNpc)
{
	Mgr = InMgr;
	Npc = InNpc;
	int32 A = 0; float S = 0.f, E = 24.f;
	if (InMgr && InMgr->GetSpotSettings(InNpc, A, S, E))
	{
		AnimIdx = A; HourStart = FMath::RoundToInt(S); HourEnd = FMath::RoundToInt(E);
	}
	if (InMgr) { InMgr->SetEditingNpc(InNpc); } // pin tegen despawn tijdens bewerken
	RefreshLabels();
}

TSharedRef<SWidget> UActivitySpotEditorWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UActivitySpotEditorWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ActCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.98f), 22.f));
	CardB->SetPadding(FMargin(24.f, 20.f));

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(VB);

	UTextBlock* Title = WeedUI::Text(WidgetTree, TEXT("Activity NPC"), 20, FLinearColor(0.85f, 0.95f, 1.f), true, true);
	VB->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	// Knop-maker (afgeronde groene knop met label).
	auto MakeBtn = [this](const FString& Txt, int32 Action, const FLinearColor& Col, int32 FontSz) -> UWeedActionButton*
	{
		UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
		B->Action = Action;
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindUObject(this, &UActivitySpotEditorWidget::OnButton);
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 9.f);
		St.Hovered = WeedUI::Rounded(Col * 1.3f, 9.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 9.f);
		St.NormalPadding = FMargin(12.f, 7.f); St.PressedPadding = FMargin(12.f, 7.f);
		B->SetStyle(St);
		B->SetContent(WeedUI::Text(WidgetTree, Txt, FontSz, FLinearColor::White, true, true));
		return B;
	};

	// Stepper-rij: [◀] label-met-vaste-breedte [▶], met een titel ervoor.
	auto AddRow = [&](const FString& RowTitle, int32 ActLeft, int32 ActRight, TObjectPtr<UTextBlock>& OutLabel, float LabelW)
	{
		UHorizontalBox* HB = WidgetTree->ConstructWidget<UHorizontalBox>();

		UTextBlock* Lbl = WeedUI::Text(WidgetTree, RowTitle, 15, FLinearColor(0.7f, 0.78f, 0.88f), false, true);
		USizeBox* TSz = WidgetTree->ConstructWidget<USizeBox>(); TSz->SetWidthOverride(110.f); TSz->SetContent(Lbl);
		HB->AddChildToHorizontalBox(TSz)->SetVerticalAlignment(VAlign_Center);

		HB->AddChildToHorizontalBox(MakeBtn(TEXT("<"), ActLeft, FLinearColor(0.22f, 0.28f, 0.4f), 16))->SetVerticalAlignment(VAlign_Center);

		OutLabel = WeedUI::Text(WidgetTree, TEXT("-"), 16, FLinearColor(1.f, 0.95f, 0.7f), true, true);
		USizeBox* VSz = WidgetTree->ConstructWidget<USizeBox>(); VSz->SetWidthOverride(LabelW); VSz->SetContent(OutLabel);
		UHorizontalBoxSlot* VSlot = HB->AddChildToHorizontalBox(VSz);
		VSlot->SetVerticalAlignment(VAlign_Center); VSlot->SetPadding(FMargin(8.f, 0.f));

		HB->AddChildToHorizontalBox(MakeBtn(TEXT(">"), ActRight, FLinearColor(0.22f, 0.28f, 0.4f), 16))->SetVerticalAlignment(VAlign_Center);

		VB->AddChildToVerticalBox(HB)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	};

	AddRow(TEXT("Animation"), 1, 2, AnimLabel, 200.f);
	AddRow(TEXT("From"),      3, 4, FromLabel, 90.f);
	AddRow(TEXT("To"),        5, 6, ToLabel,   90.f);

	// Onderbalk: Wissen (rood) + Klaar (groen).
	UHorizontalBox* Bottom = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* DelSlot = Bottom->AddChildToHorizontalBox(MakeBtn(TEXT("Delete"), 7, FLinearColor(0.5f, 0.18f, 0.18f), 14));
	DelSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	Bottom->AddChildToHorizontalBox(MakeBtn(TEXT("Done"), 8, FLinearColor(0.2f, 0.45f, 0.3f), 14));
	UVerticalBoxSlot* BSlot = VB->AddChildToVerticalBox(Bottom);
	BSlot->SetHorizontalAlignment(HAlign_Center); BSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));

	RefreshLabels();
}

void UActivitySpotEditorWidget::RefreshLabels()
{
	if (AnimLabel) { AnimLabel->SetText(FText::FromString(ACustomerBase::ActivityAnimLabel(AnimIdx))); }
	if (FromLabel) { FromLabel->SetText(FText::FromString(HourStr(HourStart))); }
	if (ToLabel)   { ToLabel->SetText(FText::FromString(HourStr(HourEnd))); }
}

void UActivitySpotEditorWidget::Apply()
{
	if (AActivitySpotManager* M = Mgr.Get())
	{
		M->UpdateSpotForNpc(Npc.Get(), AnimIdx, (float)HourStart, (float)HourEnd);
	}
	RefreshLabels();
}

void UActivitySpotEditorWidget::OnButton(int32 Action, int32 /*Param*/)
{
	const int32 N = FMath::Max(1, ACustomerBase::ActivityAnimNum());
	switch (Action)
	{
		case 1: AnimIdx = (AnimIdx + N - 1) % N; Apply(); break;
		case 2: AnimIdx = (AnimIdx + 1) % N;     Apply(); break;
		case 3: HourStart = FMath::Clamp(HourStart - 1, 0, 24); Apply(); break;
		case 4: HourStart = FMath::Clamp(HourStart + 1, 0, 24); Apply(); break;
		case 5: HourEnd   = FMath::Clamp(HourEnd - 1,   0, 24); Apply(); break;
		case 6: HourEnd   = FMath::Clamp(HourEnd + 1,   0, 24); Apply(); break;
		case 7: // Delete
			if (AActivitySpotManager* M = Mgr.Get()) { M->RemoveSpotForNpc(Npc.Get()); }
			CloseSelf();
			break;
		case 8: CloseSelf(); break;
		default: break;
	}
}

void UActivitySpotEditorWidget::CloseSelf()
{
	if (AActivitySpotManager* M = Mgr.Get()) { M->SetEditingNpc(nullptr); } // unpin -> tijdvak-regels weer actief
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	RemoveFromParent();
}
