#include "UI/RollWidget.h"

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
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/ProgressBar.h"
#include "GameFramework/Pawn.h"

void URollWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* RollBtn(UWidgetTree* Tree, const FLinearColor& Col, float Radius, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, Radius);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, Radius);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, Radius);
		S.NormalPadding = FMargin(8.f, 5.f); S.PressedPadding = FMargin(8.f, 5.f);
		B->SetStyle(S);
		return B;
	}
}

TSharedRef<SWidget> URollWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void URollWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RollCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.98f), 24.f));
	CardB->SetPadding(FMargin(20.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(520.f, 320.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Body);
}

void URollWidget::RebuildContent()
{
	if (!Body || !PhoneComp.IsValid()) { return; }
	UPhoneClientComponent* Ph = PhoneComp.Get();
	Body->ClearChildren();

	Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("ROLL JOINT"), 18, FLinearColor(0.6f, 1.f, 0.6f), false, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	const int32 MaxG = Ph->GetMaxJointGrams();
	if (MaxG <= 0)
	{
		Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("No papers! Buy a pack from Suppliers (phone)."), 13, FLinearColor(1.f, 0.5f, 0.5f)))
			->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
		UWeedActionButton* CloseB = RollBtn(WidgetTree, FLinearColor(0.4f, 0.34f, 0.16f), 10.f, [Ph]() { Ph->ToggleRollUI(); });
		CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 13, FLinearColor::White, true));
		Body->AddChildToVerticalBox(CloseB);
		return;
	}

	const int32 G = FMath::Clamp(Ph->GetRollGrams(), 1, MaxG);

	Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
		FString::Printf(TEXT("Grams per joint: %d   (your papers allow up to %dg)"), G, MaxG), 13, FLinearColor(0.88f, 0.92f, 1.f)))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Gram-keuze: een rij klikbare gram-knoppen (geselecteerde licht groen op).
	UHorizontalBox* Grams = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 g = 1; g <= MaxG; ++g)
	{
		const bool bSel = (g == G);
		const FLinearColor Col = bSel ? FLinearColor(0.22f, 0.55f, 0.30f) : FLinearColor(0.15f, 0.16f, 0.21f);
		UWeedActionButton* B = RollBtn(WidgetTree, Col, 8.f, [Ph, g]() { Ph->SetRollGrams(g); });
		B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%dg"), g), 13, bSel ? FLinearColor::White : FLinearColor(0.7f, 0.72f, 0.8f), true));
		UHorizontalBoxSlot* BS = Grams->AddChildToHorizontalBox(B);
		BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
	}
	Body->AddChildToVerticalBox(Grams)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	// --- Sterkte (verwachte high) — schaalt mee met gram + THC% + kwaliteit% ---
	float Thc = 0.f, Qpct = 0.f;
	const bool bHasWeed = Ph->GetRollWeedInfo(G, Thc, Qpct);
	const float Intensity = bHasWeed ? UPhoneClientComponent::JointIntensity(G, Thc, Qpct) : 0.f;

	if (bHasWeed)
	{
		const FLinearColor LblCol = Intensity >= 0.6f ? FLinearColor(0.5f, 1.f, 0.55f)
			: (Intensity >= 0.3f ? FLinearColor(1.f, 0.75f, 0.3f) : FLinearColor(1.f, 0.55f, 0.45f));
		Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("Joint strength: %.0f%%   (%dg of %.0f%% quality weed - %.0f%% THC)"), Intensity * 100.f, G, Qpct, Thc),
			13, LblCol, false, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			TEXT("More grams = stronger joint."),
			10, FLinearColor(0.6f, 0.64f, 0.74f)))->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}
	else
	{
		Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("No weed for a %dg joint - grow/buy buds first."), G), 13, FLinearColor(1.f, 0.55f, 0.45f)))
			->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>();
	BarSz->SetHeightOverride(18.f);
	UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
	Bar->SetFillColorAndOpacity(Intensity >= 0.6f ? FLinearColor(0.4f, 0.9f, 0.45f)
		: (Intensity >= 0.3f ? FLinearColor(0.95f, 0.7f, 0.25f) : FLinearColor(0.9f, 0.45f, 0.4f)));
	Bar->SetPercent(Intensity);
	BarSz->SetContent(Bar);
	Body->AddChildToVerticalBox(BarSz)->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

	// Actie-knoppen.
	UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
	// Alleen een Load-knop tonen als je ook echt wiet hebt -> geen lege joint kunnen laden/rollen.
	if (bHasWeed)
	{
		UWeedActionButton* RollB = RollBtn(WidgetTree, FLinearColor(0.2f, 0.55f, 0.27f), 10.f, [Ph]() { Ph->LoadRoll(); });
		RollB->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Load  (%dg)  -  then hold right-click to roll"), G), 13, FLinearColor::White, true));
		UHorizontalBoxSlot* RS = Btns->AddChildToHorizontalBox(RollB);
		RS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); RS->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	}

	UWeedActionButton* CloseB = RollBtn(WidgetTree, FLinearColor(0.4f, 0.34f, 0.16f), 10.f, [Ph]() { Ph->ToggleRollUI(); });
	CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 13, FLinearColor::White, true));
	Btns->AddChildToHorizontalBox(CloseB);
	Body->AddChildToVerticalBox(Btns);
}

void URollWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsRollOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastGrams = -1; LastMaxG = -2; return; }

	// Alleen herbouwen als er iets veranderde (grams of papers-capaciteit) -> geen flash.
	const int32 G = PhoneComp->GetRollGrams();
	const int32 MaxG = PhoneComp->GetMaxJointGrams();
	if (G != LastGrams || MaxG != LastMaxG)
	{
		LastGrams = G; LastMaxG = MaxG;
		RebuildContent();
	}
}
