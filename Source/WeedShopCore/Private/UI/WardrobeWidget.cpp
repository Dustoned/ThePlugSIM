#include "UI/WardrobeWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Interaction/PlayerNpcActions.h"
#include "Customization/OutfitCatalog.h"

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
#include "GameFramework/Pawn.h"

void UWardrobeWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* WrdBtn(UWidgetTree* Tree, const FLinearColor& Col, float Radius, TFunction<void()> OnClick)
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

TSharedRef<SWidget> UWardrobeWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UWardrobeWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WardrobeCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.04f, 0.06f, 0.99f), 20.f));
	CardB->SetPadding(FMargin(16.f));
	Card = CardB;

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(false);
	CS->SetSize(FVector2D(520.f, 480.f));
	CS->SetPosition(FVector2D(0.f, 0.f));

	UVerticalBox* Outer = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(Outer);

	// Kop-balk.
	UBorder* Head = WidgetTree->ConstructWidget<UBorder>();
	Head->SetBrush(WeedUI::Rounded(FLinearColor(0.13f, 0.10f, 0.16f, 1.f), 10.f));
	Head->SetPadding(FMargin(12.f, 8.f, 12.f, 8.f));
	UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Head->SetContent(HeadRow);
	UHorizontalBoxSlot* TS = HeadRow->AddChildToHorizontalBox(WeedUI::Text(WidgetTree, TEXT("WARDROBE"), 18, FLinearColor(0.85f, 0.6f, 1.f), false, true));
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center);
	UWeedActionButton* CloseB = WrdBtn(WidgetTree, FLinearColor(0.4f, 0.2f, 0.2f), 8.f,
		[this]() { if (PhoneComp.IsValid()) { PhoneComp->CloseWardrobe(); } });
	CloseB->SetContent(WeedUI::Text(WidgetTree, TEXT("Close"), 12, FLinearColor::White, true));
	HeadRow->AddChildToHorizontalBox(CloseB);
	Outer->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UBorder* ScreenB = WidgetTree->ConstructWidget<UBorder>();
	ScreenB->SetBrush(WeedUI::Rounded(FLinearColor(0.08f, 0.06f, 0.10f, 1.f), 12.f));
	ScreenB->SetPadding(FMargin(14.f));
	UVerticalBoxSlot* ScS = Outer->AddChildToVerticalBox(ScreenB);
	ScS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	Body = WidgetTree->ConstructWidget<UVerticalBox>();
	ScreenB->SetContent(Body);
}

void UWardrobeWidget::FillBody()
{
	if (!Body) { return; }
	Body->ClearChildren();

	IPlayerNpcActions* Pl = Cast<IPlayerNpcActions>(GetOwningPlayerPawn());
	if (!Pl) { Body->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("No character."), 14, FLinearColor::Gray)); return; }
	const uint8 Skin = Pl->GetPlayerSkinIndex();

	auto Row = [this](UWidget* W, const FMargin& Pad) { Body->AddChildToVerticalBox(W)->SetPadding(Pad); };

	// --- Body-keuze ---
	Row(WeedUI::Text(WidgetTree, TEXT("Body"), 13, FLinearColor(0.8f, 0.85f, 1.f)), FMargin(0, 0, 0, 3));
	UHorizontalBox* BodyRowBox = WidgetTree->ConstructWidget<UHorizontalBox>();
	static const TCHAR* BodyNames[5] = { TEXT("Male"), TEXT("Female"), TEXT("Girl 1"), TEXT("Girl 2"), TEXT("Girl 3") };
	for (uint8 bi = 0; bi < 5; ++bi)
	{
		UWeedActionButton* BB = WrdBtn(WidgetTree,
			(Skin == bi) ? FLinearColor(0.55f, 0.30f, 0.80f) : FLinearColor(0.15f, 0.14f, 0.20f), 8.f,
			[this, bi]() { if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { P->SetPlayerSkinIndex(bi); } LastSig.Reset(); });
		BB->SetContent(WeedUI::Text(WidgetTree, BodyNames[bi], 11, FLinearColor::White, true));
		UHorizontalBoxSlot* BS = BodyRowBox->AddChildToHorizontalBox(BB);
		BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BS->SetPadding(FMargin(bi == 0 ? 0.f : 3.f, 0.f, 0.f, 0.f));
	}
	Row(BodyRowBox, FMargin(0, 0, 0, 10));

	if (Skin < 2)
	{
		Row(WeedUI::Text(WidgetTree, TEXT("Outfits are available for the girls (for now)."), 12, FLinearColor(0.65f, 0.68f, 0.78f)), FMargin(0, 6, 0, 0));
		return;
	}

	// --- Outfit-slots: < naam > per categorie ---
	for (int32 SlotIdx = 0; SlotIdx < WeedOutfit::SlotCount(); ++SlotIdx)
	{
		const int32 Count = WeedOutfit::PartCount(SlotIdx);
		const uint8 Cur = Pl->GetOutfitPart(SlotIdx);
		const WeedOutfit::FPart& Part = WeedOutfit::PartAt(SlotIdx, Cur);

		Row(WeedUI::Text(WidgetTree, WeedOutfit::SlotName(SlotIdx), 13, FLinearColor(0.8f, 0.85f, 1.f)), FMargin(0, 4, 0, 2));
		UHorizontalBox* R = WidgetTree->ConstructWidget<UHorizontalBox>();

		UWeedActionButton* PrevB = WrdBtn(WidgetTree, FLinearColor(0.2f, 0.25f, 0.4f), 8.f,
			[this, SlotIdx, Cur, Count]()
			{
				if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
				{
					P->SetOutfitPart(SlotIdx, (uint8)((Cur + Count - 1) % Count));
				}
				LastSig.Reset();
			});
		PrevB->SetContent(WeedUI::Text(WidgetTree, TEXT("<"), 13, FLinearColor::White, true));
		R->AddChildToHorizontalBox(PrevB);

		UTextBlock* NameT = WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s  (%d/%d)"), Part.Name, Cur + 1, Count), 13, FLinearColor(0.95f, 0.95f, 1.f), false, true);
		UHorizontalBoxSlot* NS = R->AddChildToHorizontalBox(NameT);
		NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NS->SetHorizontalAlignment(HAlign_Center); NS->SetVerticalAlignment(VAlign_Center);

		UWeedActionButton* NextB = WrdBtn(WidgetTree, FLinearColor(0.2f, 0.25f, 0.4f), 8.f,
			[this, SlotIdx, Cur, Count]()
			{
				if (IPlayerNpcActions* P = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
				{
					P->SetOutfitPart(SlotIdx, (uint8)((Cur + 1) % Count));
				}
				LastSig.Reset();
			});
		NextB->SetContent(WeedUI::Text(WidgetTree, TEXT(">"), 13, FLinearColor::White, true));
		R->AddChildToHorizontalBox(NextB);

		Row(R, FMargin(0, 0, 0, 4));
	}

	Row(WeedUI::Text(WidgetTree, TEXT("Press B to check yourself out in third person."), 10, FLinearColor(0.6f, 0.62f, 0.72f)), FMargin(0, 8, 0, 0));
}

void UWardrobeWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsWardrobeOpen();
	if (Card) { Card->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { LastSig.Reset(); return; }

	// Alleen herbouwen bij wijziging (skin of outfit) -> geen flicker.
	FString Sig;
	if (const IPlayerNpcActions* Pl = Cast<IPlayerNpcActions>(GetOwningPlayerPawn()))
	{
		Sig = FString::Printf(TEXT("%d|%d|%d|%d|%d"), Pl->GetPlayerSkinIndex(),
			Pl->GetOutfitPart(0), Pl->GetOutfitPart(1), Pl->GetOutfitPart(2), Pl->GetOutfitPart(3));
	}
	if (Sig != LastSig) { LastSig = Sig; FillBody(); }
}
