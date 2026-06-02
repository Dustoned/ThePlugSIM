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
	// Type-tag + korte hint + accent-kleur op basis van het item-id-prefix.
	void ClassifyItem(const FString& Id, FString& OutType, FString& OutHint, FLinearColor& OutCol)
	{
		auto Starts = [&Id](const TCHAR* P) { return Id.StartsWith(P); };
		// Hints zijn PUUR beschrijvend (geen toetsen) - de controls staan altijd links-onder.
		if (Starts(TEXT("WetBud_")))      { OutType = TEXT("WET BUD"); OutHint = TEXT("Hang on a drying rack before you can use it"); OutCol = FLinearColor(0.45f, 0.75f, 1.f); }
		else if (Starts(TEXT("Bud_")))    { OutType = TEXT("DRIED BUD"); OutHint = TEXT("Pack into baggies or jars, or roll into joints"); OutCol = FLinearColor(0.55f, 1.f, 0.6f); }
		else if (Starts(TEXT("Bag_")))    { OutType = TEXT("BAGGIE"); OutHint = TEXT("A packed deal - sell it to customers"); OutCol = FLinearColor(0.4f, 0.95f, 0.55f); }
		else if (Starts(TEXT("Joint_")))  { OutType = TEXT("JOINT"); OutHint = TEXT("Hand one to a customer, or smoke it yourself"); OutCol = FLinearColor(0.8f, 0.95f, 0.55f); }
		else if (Starts(TEXT("Seed_")))   { OutType = TEXT("SEED"); OutHint = TEXT("Plant it in a pot filled with soil"); OutCol = FLinearColor(0.6f, 0.9f, 0.5f); }
		else if (Starts(TEXT("Soil_")))   { OutType = TEXT("SOIL"); OutHint = TEXT("Fill an empty pot before planting"); OutCol = FLinearColor(0.7f, 0.55f, 0.35f); }
		else if (Starts(TEXT("WaterBottle"))) { OutType = TEXT("WATER BOTTLE"); OutHint = TEXT("Water your plants to keep them growing"); OutCol = FLinearColor(0.4f, 0.7f, 1.f); }
		else if (Starts(TEXT("Cont_")))   { OutType = TEXT("CONTAINER"); OutHint = TEXT("Used at the packing bench to bag your weed"); OutCol = FLinearColor(0.7f, 0.7f, 0.8f); }
		else if (Starts(TEXT("Spray_")))  { OutType = TEXT("SPRAY"); OutHint = TEXT("Cures mould or pests on a plant"); OutCol = FLinearColor(0.5f, 0.85f, 0.9f); }
		else if (Starts(TEXT("Fertilizer_"))) { OutType = TEXT("FERTILIZER"); OutHint = TEXT("Boosts a plant's yield for this harvest"); OutCol = FLinearColor(0.6f, 0.85f, 0.4f); }
		else if (Starts(TEXT("Papers_"))) { OutType = TEXT("ROLLING PAPERS"); OutHint = TEXT("Roll your dried buds into joints"); OutCol = FLinearColor(0.85f, 0.85f, 0.7f); }
		else if (Starts(TEXT("Pot_")))    { OutType = TEXT("PLANT POT"); OutHint = TEXT("Place it, fill with soil, then plant a seed"); OutCol = FLinearColor(0.8f, 0.6f, 0.4f); }
		else if (Starts(TEXT("DryRack_")))   { OutType = TEXT("DRYING RACK"); OutHint = TEXT("Place it, then hang wet buds to dry"); OutCol = FLinearColor(0.7f, 0.7f, 0.85f); }
		else if (Starts(TEXT("Bench_")))     { OutType = TEXT("PACKING BENCH"); OutHint = TEXT("Place it to pack buds into containers"); OutCol = FLinearColor(0.7f, 0.7f, 0.85f); }
		else if (Id == TEXT("Shelf") || Id == TEXT("Chest")) { OutType = TEXT("STORAGE"); OutHint = TEXT("Place it to stash extra stock"); OutCol = FLinearColor(0.7f, 0.7f, 0.85f); }
		else if (Id == TEXT("Atm"))          { OutType = TEXT("ATM"); OutHint = TEXT("Place it to deposit or withdraw money"); OutCol = FLinearColor(0.5f, 0.8f, 1.f); }
		else if (Id == TEXT("Table") || Id == TEXT("Mattress") || Id == TEXT("Fridge"))
			{ OutType = TEXT("FURNITURE"); OutHint = TEXT("Place it down in your home"); OutCol = FLinearColor(0.7f, 0.7f, 0.85f); }
		else { OutType = TEXT("ITEM"); OutHint = TEXT(""); OutCol = FLinearColor(0.8f, 0.82f, 0.9f); }
	}

	// "SilverHaze" -> "Silver Haze": spaties voor binnenste hoofdletters/cijfers.
	FString PrettyName(const FString& In)
	{
		FString Out;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (i > 0)
			{
				const TCHAR Prev = In[i - 1];
				const bool bBoundary = (FChar::IsUpper(C) && !FChar::IsUpper(Prev) && Prev != TEXT(' '))
					|| (FChar::IsDigit(C) && !FChar::IsDigit(Prev) && Prev != TEXT(' '));
				if (bBoundary) { Out.AppendChar(TEXT(' ')); }
			}
			Out.AppendChar(C);
		}
		return Out;
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

	// Type-tag (klein, hoofdletters, gekleurd).
	TypeText = WeedUI::Text(WidgetTree, TEXT(""), 11, FLinearColor(0.6f, 0.95f, 0.65f), false, true);
	TypeText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(TypeText)->SetPadding(FMargin(14.f, 12.f, 14.f, 1.f));

	// Naam (groot).
	NameText = WeedUI::Text(WidgetTree, TEXT(""), 20, FLinearColor(0.97f, 0.98f, 1.f), false, true);
	NameText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(NameText)->SetPadding(FMargin(14.f, 0.f, 14.f, 8.f));

	// Dun scheidingslijntje.
	Divider = WidgetTree->ConstructWidget<UBorder>();
	Divider->SetBrush(WeedUI::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.12f), 1.f));
	Divider->SetPadding(FMargin(0.f, 0.7f, 0.f, 0.7f));
	Col->AddChildToVerticalBox(Divider)->SetPadding(FMargin(14.f, 0.f, 14.f, 8.f));

	// Nette label/waarde-rijen.
	StatBox = WidgetTree->ConstructWidget<UVerticalBox>();
	Col->AddChildToVerticalBox(StatBox)->SetPadding(FMargin(14.f, 0.f, 14.f, 6.f));

	// Korte hint (dim) onderaan.
	HintText = WeedUI::Text(WidgetTree, TEXT(""), 11, FLinearColor(0.62f, 0.66f, 0.78f), false, false);
	HintText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(HintText)->SetPadding(FMargin(14.f, 2.f, 14.f, 12.f));

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

	FString Type, Hint; FLinearColor Col;
	ClassifyItem(IdStr, Type, Hint, Col);

	if (AccentBar) { AccentBar->SetBrush(WeedUI::Rounded(Col, 3.f)); }
	if (TypeText) { TypeText->SetText(FText::FromString(Type)); TypeText->SetColorAndOpacity(FSlateColor(Col)); }
	if (NameText) { NameText->SetText(FText::FromString(PrettyName(WeedUI::PrettyItemName(Id)))); }
	if (HintText) { HintText->SetText(FText::FromString(Hint)); }

	// Nette label/waarde-rijen opbouwen.
	if (StatBox)
	{
		StatBox->ClearChildren();
		auto AddStat = [this](const FString& Label, const FString& Value)
		{
			UHorizontalBox* RowH = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* L = WeedUI::Text(WidgetTree, Label, 12, FLinearColor(0.6f, 0.64f, 0.74f));
			UHorizontalBoxSlot* LS = RowH->AddChildToHorizontalBox(L);
			LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS->SetVerticalAlignment(VAlign_Center);
			UTextBlock* V = WeedUI::Text(WidgetTree, Value, 13, FLinearColor(0.95f, 0.97f, 1.f), false, true);
			V->SetJustification(ETextJustify::Right);
			UHorizontalBoxSlot* VS = RowH->AddChildToHorizontalBox(V);
			VS->SetVerticalAlignment(VAlign_Center);
			StatBox->AddChildToVerticalBox(RowH)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
		};

		if (IdStr.StartsWith(TEXT("WetBud_")) || IdStr.StartsWith(TEXT("Bud_")) || IdStr.StartsWith(TEXT("Bag_")))
		{
			AddStat(TEXT("Amount"), FString::Printf(TEXT("%d g"), Qty));
			AddStat(TEXT("THC"), FString::Printf(TEXT("%.0f%%"), Thc));
			AddStat(TEXT("Quality"), FString::Printf(TEXT("%.0f%%"), Qpct));
		}
		else if (IdStr.StartsWith(TEXT("Joint_")))
		{
			const int32 G = FCString::Atoi(*IdStr.Mid(6)); // Joint_3g -> 3
			AddStat(TEXT("In hand"), FString::Printf(TEXT("x%d"), Qty));
			AddStat(TEXT("Per joint"), FString::Printf(TEXT("%d g"), G));
			AddStat(TEXT("THC"), FString::Printf(TEXT("%.0f%%"), Thc));
		}
		else if (IdStr.StartsWith(TEXT("Cont_")))
		{
			const int32 Cap = UPhoneClientComponent::ContainerCapacity(Id);
			AddStat(TEXT("Capacity"), FString::Printf(TEXT("up to %d g"), Cap));
			AddStat(TEXT("In stock"), FString::Printf(TEXT("x%d"), Qty));
		}
		else
		{
			AddStat(TEXT("In stock"), FString::Printf(TEXT("x%d"), Qty));
		}
	}
}
