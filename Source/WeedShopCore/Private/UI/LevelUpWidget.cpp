#include "UI/LevelUpWidget.h"

#include "UI/WeedUiStyle.h"
#include "Game/WeedShopGameState.h"
#include "Progression/LevelComponent.h"
#include "Progression/StoreComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Engine/World.h"

TSharedRef<SWidget> ULevelUpWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void ULevelUpWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Zachte achtergrond-dim (cosmetisch; vangt geen input).
	UBorder* DimB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LvlDim"));
	DimB->SetBrush(WeedUI::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f));
	DimB->SetVisibility(ESlateVisibility::HitTestInvisible);
	Dim = DimB;
	UCanvasPanelSlot* DS = Root->AddChildToCanvas(DimB);
	DS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f)); DS->SetOffsets(FMargin(0.f));

	// Gecentreerde kaart.
	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LvlCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.06f, 0.09f, 0.98f), 22.f));
	CardB->SetPadding(FMargin(34.f, 26.f, 34.f, 28.f));
	CardB->SetHorizontalAlignment(HAlign_Center);
	CardB->SetVisibility(ESlateVisibility::HitTestInvisible);
	Card = CardB;
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 0.42f, 0.5f, 0.42f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
	CardB->SetContent(VB);

	UTextBlock* Tag = WeedUI::Text(WidgetTree, TEXT("LEVEL UP"), 14, FLinearColor(0.55f, 0.8f, 1.f), true, true);
	VB->AddChildToVerticalBox(Tag)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

	LevelText = WeedUI::Text(WidgetTree, TEXT("Level 2"), 40, FLinearColor(1.f, 0.9f, 0.5f), true, true);
	VB->AddChildToVerticalBox(LevelText)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	SubText = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor(0.7f, 0.95f, 0.75f), true, true);
	VB->AddChildToVerticalBox(SubText)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	USizeBox* GridSz = WidgetTree->ConstructWidget<USizeBox>();
	GridSz->SetMaxDesiredWidth(560.f);
	UnlockBox = WidgetTree->ConstructWidget<UWrapBox>();
	UnlockBox->SetInnerSlotPadding(FVector2D(10.f, 10.f));
	GridSz->SetContent(UnlockBox);
	VB->AddChildToVerticalBox(GridSz)->SetHorizontalAlignment(HAlign_Center);

	Card->SetRenderOpacity(0.f);
	Dim->SetRenderOpacity(0.f);
}

void ULevelUpWidget::ShowForLevel(int32 PrevLevel, int32 NewLevel)
{
	if (!UnlockBox || !LevelText) { return; }

	WeedUI::PlayUiSound(this, TEXT("levelup"), 0.85f, /*Game*/ 1);
	LevelText->SetText(FText::FromString(FString::Printf(TEXT("Level %d"), NewLevel)));

	// Verzamel alle items die in (PrevLevel .. NewLevel] zijn vrijgespeeld.
	UnlockBox->ClearChildren();
	int32 Count = 0;
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (Store)
	{
		auto Consider = [&](FName CatalogId, FName IconId, const FString& Name)
		{
			const int32 Req = Store->RequiredLevelFor(CatalogId);
			if (Req <= PrevLevel || Req > NewLevel) { return; }
			if (Count >= 18) { return; } // niet eindeloos vol

			UVerticalBox* Cell = WidgetTree->ConstructWidget<UVerticalBox>();
			USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>();
			IcoSz->SetWidthOverride(44.f); IcoSz->SetHeightOverride(44.f);
			IcoSz->SetContent(WeedUI::ItemIcon(WidgetTree, IconId, 44.f));
			Cell->AddChildToVerticalBox(IcoSz)->SetHorizontalAlignment(HAlign_Center);
			UTextBlock* NameT = WeedUI::Text(WidgetTree, Name, 10, FLinearColor(0.88f, 0.9f, 0.96f), true);
			NameT->SetAutoWrapText(true);
			USizeBox* NameSz = WidgetTree->ConstructWidget<USizeBox>();
			NameSz->SetWidthOverride(76.f);
			NameSz->SetContent(NameT);
			Cell->AddChildToVerticalBox(NameSz)->SetHorizontalAlignment(HAlign_Center);
			UnlockBox->AddChildToWrapBox(Cell);
			++Count;
		};

		for (const FName& Strain : Store->GetSeedCatalog())
		{
			Consider(Strain, UStoreComponent::SeedItemId(Strain), Store->GetCatalogName(Strain).ToString());
		}
		for (const FName& Sup : Store->GetSupplyCatalog())
		{
			Consider(Sup, Sup, Store->GetCatalogName(Sup).ToString());
		}
	}

	SubText->SetText(Count > 0
		? FText::FromString(TEXT("New in the shop:"))
		: FText::FromString(TEXT("Keep grinding - more unlocks ahead.")));

	HoldTimer = 6.f;   // zichtbaar houden, daarna uitfaden
	Shown = 0.001f;    // begin met infaden
}

void ULevelUpWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!Card) { return; }

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const ULevelComponent* Lv = GS ? GS->GetLeveling() : nullptr;
	if (Lv)
	{
		const int32 Cur = Lv->GetLevel();
		if (LastSeenLevel < 0)
		{
			LastSeenLevel = Cur; // eerste observatie (ook na een Testing-grant) toont niets
		}
		else if (Cur > LastSeenLevel)
		{
			ShowForLevel(LastSeenLevel, Cur);
			LastSeenLevel = Cur;
		}
	}

	// Fade: in zolang HoldTimer loopt, anders uit.
	if (HoldTimer > 0.f)
	{
		HoldTimer = FMath::Max(0.f, HoldTimer - DeltaTime);
		Shown = FMath::FInterpTo(Shown, 1.f, DeltaTime, 9.f);
	}
	else
	{
		Shown = FMath::FInterpTo(Shown, 0.f, DeltaTime, 6.f);
	}
	Card->SetRenderOpacity(Shown);
	if (Dim) { Dim->SetRenderOpacity(Shown * 0.9f); }
}
