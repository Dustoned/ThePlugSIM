#include "UI/HandInfoWidget.h"

#include "UI/WeedUiStyle.h"
#include "Inventory/InventoryComponent.h"
#include "Phone/PhoneClientComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "GameFramework/Pawn.h"

namespace
{
	// Bepaal type-tag + accent-kleur op basis van het item-id-prefix.
	void ClassifyItem(const FString& Id, FString& OutType, FLinearColor& OutCol)
	{
		auto Starts = [&Id](const TCHAR* P) { return Id.StartsWith(P); };
		if (Starts(TEXT("WetBud_")))      { OutType = TEXT("WET BUD - needs drying"); OutCol = FLinearColor(0.45f, 0.75f, 1.f); }
		else if (Starts(TEXT("Bud_")))    { OutType = TEXT("DRIED BUD - pack to sell"); OutCol = FLinearColor(0.55f, 1.f, 0.6f); }
		else if (Starts(TEXT("Bag_")))    { OutType = TEXT("BAGGIE - sellable"); OutCol = FLinearColor(0.4f, 0.95f, 0.55f); }
		else if (Starts(TEXT("Joint_")))  { OutType = TEXT("JOINT - give/smoke"); OutCol = FLinearColor(0.8f, 0.95f, 0.55f); }
		else if (Starts(TEXT("Seed_")))   { OutType = TEXT("SEED - plant in a pot"); OutCol = FLinearColor(0.6f, 0.9f, 0.5f); }
		else if (Starts(TEXT("Soil_")))   { OutType = TEXT("SOIL - fill a pot"); OutCol = FLinearColor(0.7f, 0.55f, 0.35f); }
		else if (Starts(TEXT("WaterBottle"))) { OutType = TEXT("WATER BOTTLE - water plants"); OutCol = FLinearColor(0.4f, 0.7f, 1.f); }
		else if (Starts(TEXT("Cont_")))   { OutType = TEXT("EMPTY CONTAINER - pack weed"); OutCol = FLinearColor(0.7f, 0.7f, 0.8f); }
		else if (Starts(TEXT("Spray_")))  { OutType = TEXT("SPRAY - treat sick plants"); OutCol = FLinearColor(0.5f, 0.85f, 0.9f); }
		else if (Starts(TEXT("Fertilizer_"))) { OutType = TEXT("FERTILIZER - boost yield"); OutCol = FLinearColor(0.6f, 0.85f, 0.4f); }
		else if (Starts(TEXT("Papers_"))) { OutType = TEXT("ROLLING PAPERS - roll joints"); OutCol = FLinearColor(0.85f, 0.85f, 0.7f); }
		else if (Starts(TEXT("Pot_")))    { OutType = TEXT("PLANT POT - place it"); OutCol = FLinearColor(0.8f, 0.6f, 0.4f); }
		else if (Starts(TEXT("DryRack_")) || Starts(TEXT("Bench_")) || Id == TEXT("Shelf") || Id == TEXT("Chest")
			|| Id == TEXT("Table") || Id == TEXT("Mattress") || Id == TEXT("Fridge") || Id == TEXT("Atm"))
			{ OutType = TEXT("PLACEABLE - place it"); OutCol = FLinearColor(0.7f, 0.7f, 0.85f); }
		else { OutType = TEXT("ITEM"); OutCol = FLinearColor(0.8f, 0.82f, 0.9f); }
	}
}

TSharedRef<SWidget> UHandInfoWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UHandInfoWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Kaart LINKS-midden (zo valt 'ie nooit achter de telefoon, die rechts opent).
	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HandCard"));
	CardB->SetBrush(WeedUI::Rounded(FLinearColor(0.04f, 0.05f, 0.07f, 0.84f), 12.f));
	CardB->SetPadding(FMargin(0.f));
	Card = CardB;
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.f, 0.5f, 0.f, 0.5f));
	CS->SetAlignment(FVector2D(0.f, 0.5f));
	CS->SetAutoSize(true);
	CS->SetPosition(FVector2D(24.f, 0.f));

	// Accent-balk links + tekstkolom (vaste, smalle breedte -> kaart wordt verticaler dan breed).
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	CardB->SetContent(Row);

	AccentBar = WidgetTree->ConstructWidget<UBorder>();
	AccentBar->SetBrush(WeedUI::Rounded(FLinearColor(0.5f, 1.f, 0.6f, 1.f), 3.f));
	AccentBar->SetPadding(FMargin(3.f, 0.f, 3.f, 0.f));
	UHorizontalBoxSlot* AS = Row->AddChildToHorizontalBox(AccentBar);
	AS->SetPadding(FMargin(0.f)); AS->SetVerticalAlignment(VAlign_Fill);

	USizeBox* ColSize = WidgetTree->ConstructWidget<USizeBox>();
	ColSize->SetWidthOverride(180.f); // smal -> tekst wrapt, kaart wordt hoger
	UHorizontalBoxSlot* SzS = Row->AddChildToHorizontalBox(ColSize);
	SzS->SetVerticalAlignment(VAlign_Center);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	ColSize->SetContent(Col);

	TypeText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.6f, 0.95f, 0.65f), false, true);
	TypeText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(TypeText)->SetPadding(FMargin(14.f, 12.f, 14.f, 2.f));
	NameText = WeedUI::Text(WidgetTree, TEXT(""), 21, FLinearColor(0.96f, 0.97f, 1.f), false, true);
	NameText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(NameText)->SetPadding(FMargin(14.f, 2.f, 14.f, 4.f));
	StatText = WeedUI::Text(WidgetTree, TEXT(""), 14, FLinearColor(0.75f, 0.8f, 0.9f));
	StatText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(StatText)->SetPadding(FMargin(14.f, 2.f, 14.f, 12.f));

	Card->SetRenderOpacity(0.f);
}

void UHandInfoWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!Card) { return; }

	APawn* P = GetOwningPlayerPawn();
	const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;

	// Verberg de popup als een vol-scherm menu open is (telefoon-apps/pauze/titel).
	bool bUiBlocking = false;
	if (P)
	{
		if (const UPhoneClientComponent* Ph = P->FindComponentByClass<UPhoneClientComponent>())
		{
			bUiBlocking = Ph->IsOpen() || Ph->IsPauseOpen() || Ph->IsMainMenuOpen() || Ph->IsSettingsOpen();
		}
	}

	FName Id = Inv ? Inv->GetActiveItemId() : NAME_None;
	const FString IdStr = Id.ToString();
	const bool bHasItem = Inv && !Id.IsNone() && Id != FName(TEXT("Cash")) && !bUiBlocking;

	// Fade in/uit.
	const float Target = bHasItem ? 1.f : 0.f;
	Shown = FMath::FInterpTo(Shown, Target, DeltaTime, 12.f);
	Card->SetRenderOpacity(Shown);
	if (Shown <= 0.02f && !bHasItem) { return; }

	if (!bHasItem) { return; }

	// Bouw een sleutel zodat we de tekst alleen bij wijziging updaten.
	const int32 Qty = Inv->GetQuantity(Id);
	const float Thc = Inv->GetItemQuality(Id);
	const float Qpct = Inv->GetItemQualityPct(Id);
	const FString Key = FString::Printf(TEXT("%s|%d|%.0f|%.0f"), *IdStr, Qty, Thc, Qpct);
	if (Key == LastKey) { return; }
	LastKey = Key;

	FString Type; FLinearColor Col;
	ClassifyItem(IdStr, Type, Col);

	if (AccentBar) { AccentBar->SetBrush(WeedUI::Rounded(Col, 3.f)); }
	if (TypeText) { TypeText->SetText(FText::FromString(Type)); TypeText->SetColorAndOpacity(FSlateColor(Col)); }
	if (NameText) { NameText->SetText(FText::FromString(WeedUI::PrettyItemName(Id))); }

	// Stats per type.
	FString Stat;
	if (IdStr.StartsWith(TEXT("WetBud_")))
	{
		Stat = FString::Printf(TEXT("%dg  -  THC %.0f%%  -  Q %.0f%%"), Qty, Thc, Qpct);
	}
	else if (IdStr.StartsWith(TEXT("Bud_")) || IdStr.StartsWith(TEXT("Bag_")))
	{
		Stat = FString::Printf(TEXT("%dg  -  THC %.0f%%  -  Q %.0f%%"), Qty, Thc, Qpct);
	}
	else if (IdStr.StartsWith(TEXT("Joint_")))
	{
		const int32 G = FCString::Atoi(*IdStr.Mid(6)); // Joint_3g -> 3
		Stat = FString::Printf(TEXT("x%d  -  %dg each  -  THC %.0f%%"), Qty, G, Thc);
	}
	else if (IdStr.StartsWith(TEXT("Cont_")))
	{
		const int32 Cap = UPhoneClientComponent::ContainerCapacity(Id);
		Stat = FString::Printf(TEXT("holds up to %dg  -  x%d"), Cap, Qty);
	}
	else
	{
		Stat = FString::Printf(TEXT("x%d"), Qty);
	}
	if (StatText) { StatText->SetText(FText::FromString(Stat)); }
}
