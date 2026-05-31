#include "UI/PackWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Inventory/InventoryComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "GameFramework/Pawn.h"

void UPackWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* PackBtn(UWidgetTree* Tree, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 8.f);
		S.Hovered = WeedUI::Rounded(Col * 1.3f, 8.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
		S.NormalPadding = FMargin(8.f, 4.f); S.PressedPadding = FMargin(8.f, 4.f);
		B->SetStyle(S);
		return B;
	}

	UInventoryComponent* GetInv(APawn* P) { return P ? P->FindComponentByClass<UInventoryComponent>() : nullptr; }
}

TSharedRef<SWidget> UPackWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UPackWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PackCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.06f, 0.07f, 0.10f, 0.98f), 22.f));
	CardB->SetPadding(FMargin(18.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(560.f, 460.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* TS = Head->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("PACKING BENCH"), 18, FLinearColor(0.6f, 1.f, 0.6f), false, true));
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	UWeedActionButton* CloseB = PackBtn(WidgetTree, FLinearColor(0.4f, 0.34f, 0.16f), [this]() { if (PhoneComp.IsValid()) { PhoneComp->ClosePack(); } });
	CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 12, FLinearColor::White, true));
	Head->AddChildToHorizontalBox(CloseB);
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* ScS = Outer->AddChildToVerticalBox(Scroll);
	ScS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Body);
}

void UPackWidget::FillBody()
{
	if (!Body || !PhoneComp.IsValid()) { return; }
	Body->ClearChildren();
	UPhoneClientComponent* Ph = PhoneComp.Get();
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }

	auto Row = [this](UWidget* W, const FMargin& P) { Body->AddChildToVerticalBox(W)->SetPadding(P); };

	// 1) Kies een gedroogde strain (Bud_).
	Row(WeedUI::Text(WidgetTree, TEXT("Dried weed (pick a strain):"), 12, FLinearColor(0.8f, 0.85f, 0.95f)), FMargin(0, 0, 0, 4));
	bool bAnyBud = false;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (!S.ItemId.ToString().StartsWith(TEXT("Bud_"))) { continue; }
		bAnyBud = true;
		const FName Bud = S.ItemId;
		const bool bSel = (Bud == SelStrain);
		UWeedActionButton* B = PackBtn(WidgetTree, bSel ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f),
			[this, Bud]() { SelStrain = Bud; LastSig.Reset(); FillBody(); });
		B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s   %dg  (THC %.0f%%, Q %.0f%%)"),
			*WeedUI::PrettyItemName(Bud), S.Quantity, S.Quality, S.QualityPct), 12, FLinearColor::White, true));
		Row(B, FMargin(0, 2, 0, 2));
	}
	if (!bAnyBud) { Row(WeedUI::Text(WidgetTree, TEXT("No dried weed. Dry harvested buds on a drying rack first."), 11, FLinearColor::Gray), FMargin(0, 0, 0, 6)); return; }

	if (SelStrain.IsNone() || Inv->GetQuantity(SelStrain) <= 0) { return; }

	// 2) Kies een container die je hebt -> verpak.
	const int32 Batch = Ph->GetPackBatch();
	Row(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Pack %s into:  (this bench bags %d at a time)"), *WeedUI::PrettyItemName(SelStrain), Batch), 12, FLinearColor(0.8f, 0.85f, 0.95f)), FMargin(0, 8, 0, 4));
	static const TCHAR* Conts[4] = { TEXT("Cont_Bag2"), TEXT("Cont_Bag5"), TEXT("Cont_Jar10"), TEXT("Cont_Jar15") };
	bool bAnyCont = false;
	for (int32 i = 0; i < 4; ++i)
	{
		const FName ContId(Conts[i]);
		const int32 Owned = Inv->GetQuantity(ContId);
		if (Owned <= 0) { continue; }
		bAnyCont = true;
		const int32 Cap = UPhoneClientComponent::ContainerCapacity(ContId);
		UWeedActionButton* B = PackBtn(WidgetTree, FLinearColor(0.2f, 0.45f, 0.3f),
			[this, Ph, ContId]() { Ph->RequestPack(SelStrain, ContId); LastSig.Reset(); });
		B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s  (%dg)  x%d"), *WeedUI::PrettyItemName(ContId), Cap, Owned), 12, FLinearColor::White, true));
		Row(B, FMargin(0, 2, 0, 2));
	}
	if (!bAnyCont) { Row(WeedUI::Text(WidgetTree, TEXT("No containers. Buy baggies/jars from Suppliers (Pots tab)."), 11, FLinearColor(1.f, 0.7f, 0.5f)), FMargin(0, 4, 0, 0)); }
}

void UPackWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsPackOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Herbouw als de relevante voorraad wijzigt.
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	FString Sig = SelStrain.ToString();
	if (Inv) { for (const FInventoryStack& S : Inv->GetStacks()) { const FString Id = S.ItemId.ToString(); if (Id.StartsWith(TEXT("Bud_")) || Id.StartsWith(TEXT("Cont_"))) { Sig += FString::Printf(TEXT("|%s:%d"), *Id, S.Quantity); } } }
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}
