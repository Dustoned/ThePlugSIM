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
#include "Components/Slider.h"
#include "Components/SizeBox.h"
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
	GramSlider = nullptr; GramLabel = nullptr; PackBtnLabel = nullptr;
	UPhoneClientComponent* Ph = PhoneComp.Get();
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	if (!Inv) { return; }

	auto Row = [this](UWidget* W, const FMargin& P) { Body->AddChildToVerticalBox(W)->SetPadding(P); };

	// === Uitpakken: haal wiet weer LOS uit een zakje (om te herverpakken of rollen) ===
	{
		bool bAnyBag = false;
		for (const FInventoryStack& S : Inv->GetStacks())
		{
			if (!S.ItemId.ToString().StartsWith(TEXT("Bag_"))) { continue; }
			if (!bAnyBag) { Row(WeedUI::Text(WidgetTree, TEXT("Unpack a bag (back to loose weed)"), 13, FLinearColor(1.f, 0.85f, 0.6f), false, true), FMargin(0, 0, 0, 4)); }
			bAnyBag = true;
			const FName Bag = S.ItemId; const int32 Qty = S.Quantity;
			UWeedActionButton* B = PackBtn(WidgetTree, FLinearColor(0.42f, 0.30f, 0.18f),
				[this, Ph, Bag]() { Ph->RequestUnpack(Bag); LastSig.Reset(); });
			B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("Unpack %s  -  %dg -> loose"), *WeedUI::PrettyItemName(Bag), Qty), 12, FLinearColor::White, true));
			Row(B, FMargin(0, 2, 0, 2));
		}
		if (bAnyBag) { Row(WeedUI::Text(WidgetTree, TEXT("- or pack new -"), 11, FLinearColor::Gray, true), FMargin(0, 8, 0, 8)); }
	}

	// === 1) Kies een gedroogde strain (Bud_) ===
	Row(WeedUI::Text(WidgetTree, TEXT("1.  Pick dried weed"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 0, 0, 4));
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
	if (!bAnyBud) { Row(WeedUI::Text(WidgetTree, TEXT("No dried weed. Dry it on a rack first."), 11, FLinearColor::Gray), FMargin(0, 6, 0, 6)); return; }

	const int32 BudHave = SelStrain.IsNone() ? 0 : Inv->GetQuantity(SelStrain);
	if (SelStrain.IsNone() || BudHave <= 0) { return; }

	// === 2) Kies een container ===
	Row(WeedUI::Text(WidgetTree, TEXT("2.  Pick a bag/jar"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 10, 0, 4));
	static const TCHAR* Conts[6] = { TEXT("Cont_Bag2"), TEXT("Cont_Bag5"), TEXT("Cont_Jar10"), TEXT("Cont_Jar15"), TEXT("Cont_Block100"), TEXT("Cont_Garbage500") };
	bool bAnyCont = false;
	for (int32 i = 0; i < 6; ++i)
	{
		const FName ContId(Conts[i]);
		const int32 Owned = Inv->GetQuantity(ContId);
		if (Owned <= 0) { continue; }
		bAnyCont = true;
		const int32 Cap = UPhoneClientComponent::ContainerCapacity(ContId);
		const bool bSel = (ContId == SelContainer);
		UWeedActionButton* B = PackBtn(WidgetTree, bSel ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f),
			[this, ContId, Cap, BudHave]() { SelContainer = ContId; CurCap = FMath::Max(1, FMath::Min(Cap, BudHave)); SelGrams = CurCap; LastSig.Reset(); FillBody(); });
		B->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s   up to %dg   x%d"), *WeedUI::PrettyItemName(ContId), Cap, Owned), 12, FLinearColor::White, true));
		Row(B, FMargin(0, 2, 0, 2));
	}
	if (!bAnyCont) { Row(WeedUI::Text(WidgetTree, TEXT("No bags/jars. Buy them in the Grow shop."), 11, FLinearColor(1.f, 0.7f, 0.5f)), FMargin(0, 4, 0, 0)); return; }

	// === 3) Gram-slider + Pack ===
	if (SelContainer.IsNone() || !Inv->HasItem(SelContainer, 1)) { return; }
	const int32 Cap = UPhoneClientComponent::ContainerCapacity(SelContainer);
	CurCap = FMath::Max(1, FMath::Min(Cap, BudHave));
	SelGrams = FMath::Clamp(SelGrams, 1, CurCap);

	Row(WeedUI::Text(WidgetTree, TEXT("3.  How many grams?"), 13, FLinearColor(0.7f, 1.f, 0.7f), false, true), FMargin(0, 10, 0, 2));
	GramLabel = WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d g   (max %d)"), SelGrams, CurCap), 16, FLinearColor::White, false, true);
	Row(GramLabel, FMargin(0, 0, 0, 4));

	GramSlider = WidgetTree->ConstructWidget<USlider>();
	GramSlider->SetMinValue(0.f);
	GramSlider->SetMaxValue(1.f);
	GramSlider->SetValue(CurCap > 1 ? float(SelGrams - 1) / float(CurCap - 1) : 1.f);
	GramSlider->SetSliderHandleColor(FLinearColor(0.5f, 1.f, 0.6f));
	GramSlider->SetSliderBarColor(FLinearColor(0.25f, 0.4f, 0.3f));
	USizeBox* SliderBox = WidgetTree->ConstructWidget<USizeBox>();
	SliderBox->SetHeightOverride(24.f);
	SliderBox->SetContent(GramSlider);
	Row(SliderBox, FMargin(0, 0, 0, 8));

	UWeedActionButton* PackB = PackBtn(WidgetTree, FLinearColor(0.2f, 0.5f, 0.3f),
		[this, Ph]() { Ph->RequestPackGrams(SelStrain, SelContainer, SelGrams); LastSig.Reset(); });
	PackBtnLabel = WeedUI::Text(WidgetTree, FString::Printf(TEXT("Pack %dg into %s"), SelGrams, *WeedUI::PrettyItemName(SelContainer)), 13, FLinearColor::White, true);
	PackB->SetContent(PackBtnLabel);
	Row(PackB, FMargin(0, 2, 0, 2));
}

void UPackWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsPackOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Live de gram-slider uitlezen (zonder herbouw, anders springt de slider).
	if (GramSlider)
	{
		const int32 NewG = (CurCap <= 1) ? 1 : FMath::Clamp(1 + FMath::RoundToInt(GramSlider->GetValue() * float(CurCap - 1)), 1, CurCap);
		if (NewG != SelGrams)
		{
			SelGrams = NewG;
			if (GramLabel) { GramLabel->SetText(FText::FromString(FString::Printf(TEXT("%d g   (max %d)"), SelGrams, CurCap))); }
			if (PackBtnLabel) { PackBtnLabel->SetText(FText::FromString(FString::Printf(TEXT("Pack %dg into %s"), SelGrams, *WeedUI::PrettyItemName(SelContainer)))); }
		}
	}

	// Herbouw als de relevante voorraad of de strain-keuze wijzigt (NIET bij slider-bewegen).
	UInventoryComponent* Inv = GetInv(GetOwningPlayerPawn());
	FString Sig = SelStrain.ToString() + TEXT("/") + SelContainer.ToString();
	if (Inv) { for (const FInventoryStack& S : Inv->GetStacks()) { const FString Id = S.ItemId.ToString(); if (Id.StartsWith(TEXT("Bud_")) || Id.StartsWith(TEXT("Cont_")) || Id.StartsWith(TEXT("Bag_"))) { Sig += FString::Printf(TEXT("|%s:%d"), *Id, S.Quantity); } } }
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}
