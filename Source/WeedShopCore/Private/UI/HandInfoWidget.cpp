#include "UI/HandInfoWidget.h"

#include "UI/WeedUiStyle.h"
#include "Inventory/InventoryComponent.h"
#include "Cultivation/WaterCanComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Cultivation/PotTypes.h" // IsPotItem (Qty-logica: pot = geen aantal tonen)

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
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "GameFramework/Pawn.h"

namespace
{
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

	FString CompactUnits(FString Value)
	{
		Value.ReplaceInline(TEXT(" g"), TEXT("g"));
		Value.ReplaceInline(TEXT(" min"), TEXT("m"));
		return Value;
	}

	FString HandTitleFor(FName ItemId)
	{
		const FString S = ItemId.ToString();
		if (S.StartsWith(TEXT("Joint_")))
		{
			const FName Strain = UInventoryComponent::JointStrain(ItemId);
			if (!Strain.IsNone()) { return PrettyName(Strain.ToString()); }
		}
		if (S.StartsWith(TEXT("Bag_")))
		{
			const FName Strain = UInventoryComponent::BagStrain(ItemId);
			if (!Strain.IsNone()) { return PrettyName(Strain.ToString()); }
		}
		return PrettyName(WeedUI::PrettyItemName(ItemId));
	}

	FString ChipTextForStat(const FString& Label, const FString& Value)
	{
		const FString V = CompactUnits(Value);
		if (Label == TEXT("Strain") || Label == TEXT("Bag count")) { return FString(); }
		if (Label == TEXT("Per joint") || Label == TEXT("Per bag")) { return V; }
		if (Label == TEXT("THC")) { return FString::Printf(TEXT("THC %s"), *V); }
		if (Label == TEXT("Quality")) { return FString::Printf(TEXT("Q %s"), *V); }
		if (Label == TEXT("THC up to")) { return FString::Printf(TEXT("THC %s"), *V); }
		if (Label == TEXT("Max yield")) { return FString::Printf(TEXT("Yield %s"), *V); }
		if (Label == TEXT("Grow time")) { return V.StartsWith(TEXT("~")) ? V : FString::Printf(TEXT("~%s"), *V); }
		if (Label == TEXT("Quality cap")) { return FString::Printf(TEXT("Cap %s"), *V); }
		if (Label == TEXT("Plants")) { return FString::Printf(TEXT("%s plants"), *V); }
		if (Label == TEXT("Lasts")) { return V; }
		if (Label == TEXT("Capacity"))
		{
			FString C = V;
			C.ReplaceInline(TEXT("up to "), TEXT(""));
			return FString::Printf(TEXT("%s max"), *C);
		}
		return FString::Printf(TEXT("%s %s"), *Label, *V);
	}

	FString HandHintFor(FName ItemId, const FString& DefaultHint)
	{
		const FString S = ItemId.ToString();
		if (S.StartsWith(TEXT("WetBud_"))) { return TEXT("Dry before use"); }
		if (S.StartsWith(TEXT("Bud_"))) { return TEXT("Pack or roll"); }
		if (S.StartsWith(TEXT("Bag_"))) { return TEXT("Sell to customers"); }
		if (S.StartsWith(TEXT("Joint_"))) { return TEXT("Smoke or sell"); }
		if (S.StartsWith(TEXT("Seed_"))) { return TEXT("Plant in soil"); }
		if (S.StartsWith(TEXT("Soil_"))) { return TEXT("Fill an empty pot"); }
		if (S.StartsWith(TEXT("WaterBottle"))) { return TEXT("Water plants"); }
		if (S.StartsWith(TEXT("Cont_"))) { return TEXT("Pack product into bags"); }
		if (S.StartsWith(TEXT("Papers_"))) { return TEXT("Load flower, then roll"); }
		if (S.StartsWith(TEXT("Pot_"))) { return TEXT("Place, fill, plant"); }
		if (S.StartsWith(TEXT("DryRack_"))) { return TEXT("Dry wet buds"); }
		if (S.StartsWith(TEXT("Bench_"))) { return TEXT("Pack buds into containers"); }
		if (S == TEXT("Shelf") || S == TEXT("Chest")) { return TEXT("Store extra stock"); }
		if (S == TEXT("Atm")) { return TEXT("Deposit or withdraw"); }
		if (S == TEXT("Table") || S == TEXT("Mattress") || S == TEXT("Fridge") || S.StartsWith(TEXT("Lamp")) || S.StartsWith(TEXT("Tent")))
		{
			return TEXT("Place at home");
		}
		return DefaultHint;
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

	// Compacte held-item kaart, gekoppeld aan de hotbar. Geen los debug-paneel links in de hoek:
	// dit leest als de detailkaart van het geselecteerde hotbar-item.
	UBorder* CardB = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HandCard"));
	FSlateBrush CardBrush = WeedUI::Rounded(WeedUI::ColPanel(0.72f), 8.f);
	CardBrush.OutlineSettings.Width = 1.f;
	CardBrush.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.42f));
	CardB->SetBrush(CardBrush);
	CardB->SetPadding(FMargin(0.f));
	Card = CardB;
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(CardB);
	CS->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
	CS->SetAlignment(FVector2D(1.f, 1.f));
	CS->SetAutoSize(true);
	const FVector2D HotbarLinkedOffset(-388.f, -18.f);
	CS->SetPosition(HotbarLinkedOffset);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	CardB->SetContent(Row);

	AccentBar = WidgetTree->ConstructWidget<UBorder>();
	AccentBar->SetBrush(WeedUI::Rounded(WeedUI::ColAccent(), 2.f));
	AccentBar->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
	UHorizontalBoxSlot* AS = Row->AddChildToHorizontalBox(AccentBar);
	AS->SetPadding(FMargin(0.f)); AS->SetVerticalAlignment(VAlign_Fill);

	USizeBox* ColSize = WidgetTree->ConstructWidget<USizeBox>();
	ColSize->SetWidthOverride(194.f);
	UHorizontalBoxSlot* SzS = Row->AddChildToHorizontalBox(ColSize);
	SzS->SetVerticalAlignment(VAlign_Center);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	ColSize->SetContent(Col);

	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	Col->AddChildToVerticalBox(Head)->SetPadding(FMargin(11.f, 9.f, 11.f, 6.f));

	UVerticalBox* TitleCol = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* TitleS = Head->AddChildToHorizontalBox(TitleCol);
	TitleS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleS->SetVerticalAlignment(VAlign_Center);

	TypeText = WeedUI::Text(WidgetTree, TEXT(""), 10, FLinearColor(0.6f, 0.95f, 0.65f), false, true);
	{
		FSlateFontInfo TagFont = WeedUI::Font(10, true);
		TagFont.OutlineSettings.OutlineSize = 1;
		TagFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.8f);
		TypeText->SetFont(TagFont);
	}
	TypeText->SetAutoWrapText(true);
	TitleCol->AddChildToVerticalBox(TypeText)->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));

	NameText = WeedUI::Text(WidgetTree, TEXT(""), 16, WeedUI::ColText(), false, true);
	NameText->SetAutoWrapText(true);
	TitleCol->AddChildToVerticalBox(NameText)->SetPadding(FMargin(0.f));

	QtyText = WeedUI::Text(WidgetTree, TEXT(""), 13, WeedUI::ColText(), false, true);
	QtyPill = WidgetTree->ConstructWidget<UBorder>();
	QtyPill->SetBrush(WeedUI::Rounded(WeedUI::ColSlot(0.92f), 7.f));
	QtyPill->SetPadding(FMargin(7.f, 2.f, 7.f, 3.f));
	QtyPill->SetContent(QtyText);
	UHorizontalBoxSlot* QtyS = Head->AddChildToHorizontalBox(QtyPill);
	QtyS->SetVerticalAlignment(VAlign_Top);
	QtyS->SetPadding(FMargin(7.f, 2.f, 0.f, 0.f));

	Divider = WidgetTree->ConstructWidget<UBorder>();
	Divider->SetBrush(WeedUI::Rounded(WeedUI::ColStroke(0.35f), 1.f));
	Divider->SetPadding(FMargin(0.f, 0.5f, 0.f, 0.5f));
	Col->AddChildToVerticalBox(Divider)->SetPadding(FMargin(11.f, 0.f, 11.f, 7.f));

	ChipBox = WidgetTree->ConstructWidget<UWrapBox>();
	Col->AddChildToVerticalBox(ChipBox)->SetPadding(FMargin(11.f, 0.f, 11.f, 5.f));
	for (int32 i = 0; i < 5; ++i)
	{
		UTextBlock* ChipT = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColText(), false, true);
		UBorder* Chip = WidgetTree->ConstructWidget<UBorder>();
		Chip->SetBrush(WeedUI::Rounded(WeedUI::ColInner(0.92f), 7.f));
		Chip->SetPadding(FMargin(7.f, 2.f, 7.f, 3.f));
		Chip->SetContent(ChipT);
		Chip->SetVisibility(ESlateVisibility::Collapsed);
		UWrapBoxSlot* ChipS = ChipBox->AddChildToWrapBox(Chip);
		ChipS->SetPadding(FMargin(0.f, 0.f, 5.f, 5.f));
		ChipPills.Add(Chip);
		ChipTexts.Add(ChipT);
	}

	HintText = WeedUI::Text(WidgetTree, TEXT(""), 11, WeedUI::ColTextDim(), false, false);
	HintText->SetAutoWrapText(true);
	Col->AddChildToVerticalBox(HintText)->SetPadding(FMargin(11.f, 0.f, 11.f, 10.f));

	Card->SetRenderOpacity(0.f);
}

void UHandInfoWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!Card) { return; }

	APawn* P = GetOwningPlayerPawn();
	const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
	const UPhoneClientComponent* Ph = P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;

	// Verberg de popup als een vol-scherm menu open is (telefoon-apps/pauze/titel).
	const bool bUiBlocking = Ph && (Ph->IsOpen() || Ph->IsPauseOpen() || Ph->IsMainMenuOpen() || Ph->IsSettingsOpen());

	FName Id = Inv ? Inv->GetActiveItemId() : NAME_None;
	const FString IdStr = Id.ToString();
	const bool bHasItem = Inv && !Id.IsNone() && !bUiBlocking; // Cash mag nu wél (toont het bedrag)

	// Geladen joint: hou je vloei vast met wiet erin geladen -> die info hoort HIER bij de hand-preview
	// (niet bij de controls-kaart). RollDesc is de opgeslagen snapshot ("2g Silver Haze - 11% THC, ...").
	const bool bRollLoaded = Ph && IdStr.StartsWith(TEXT("Papers_")) && Ph->IsRollLoadedUI();
	const FString RollDesc = bRollLoaded ? Ph->GetRollLoadDesc() : FString();

	// Fade in/uit.
	const float Target = bHasItem ? 1.f : 0.f;
	Shown = FMath::FInterpTo(Shown, Target, DeltaTime, 12.f);
	Card->SetRenderOpacity(Shown);
	// Volledig dichtklappen als 'ie weg is -> nooit een leeg kader/blok dat blijft hangen.
	Card->SetVisibility(Shown > 0.02f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (Shown <= 0.02f && !bHasItem) { return; }

	if (!bHasItem) { return; }

	// Bouw een sleutel zodat we de tekst alleen bij wijziging updaten. Neem de ACTIEVE stack-id + diens
	// Quality mee, zodat wisselen tussen 2 gelijke items (bv. 2 flessen) én het water-niveau live verversen.
	const int32 ActiveSid = Inv->GetActiveStackId();
	const TArray<FInventoryStack>& Stacks = Inv->GetStacks();
	const int32 ActiveIdx = Inv->FindStackById(ActiveSid);
	const FInventoryStack* ActiveStack = Stacks.IsValidIndex(ActiveIdx) ? &Stacks[ActiveIdx] : nullptr;
	const int32 Qty = ActiveStack ? ActiveStack->Quantity : Inv->GetQuantity(Id);
	const float Thc = ActiveStack ? ActiveStack->Quality : Inv->GetItemQuality(Id);
	const float Qpct = ActiveStack ? ActiveStack->QualityPct : Inv->GetItemQualityPct(Id);
	const FString Key = FString::Printf(TEXT("%s|%d|%.0f|%.0f|%d|%s"), *IdStr, Qty, Thc, Qpct, ActiveSid, *RollDesc);
	if (Key == LastKey) { return; }
	LastKey = Key;

	// Gedeelde detail-DATA (type-tag + kleur + hint + stat-rijen). Zelfde bron als de inventory-quick-view,
	// zodat de twee NOOIT uit elkaar lopen. De rendering (AddStat/Qty/naam-kleur) blijft hier in de widget.
	const WeedUI::FItemDetailInfo Detail = WeedUI::BuildItemDetail(this, Id, Qty, Thc, Qpct);
	FString Type = Detail.Type, Hint = Detail.Hint; FLinearColor Col = Detail.TypeColor;

	// Alleen de NAAM kleurt mee met de PER-STRAIN tag-kleur (zelfde hue als de tag-pills in de inventory/
	// hotbar), iets helderder voor leesbaarheid als losse tekst; de type-tekst erboven ("SEED"/"JOINT") houdt
	// z'n normale categorie-kleur uit ClassifyItem. Niet-strain-items geven bij TagColorForItem één vaste
	// neutrale pill-kleur terug (Value/Sat genegeerd) -> die herkennen we door twee varianten te vergelijken;
	// dan blijft de naam gewoon wit.
	FLinearColor NameCol = WeedUI::ColText();
	{
		const FLinearColor StrainCol = WeedUI::TagColorForItem(Id, 0.9f, 0.72f);
		if (!StrainCol.Equals(WeedUI::TagColorForItem(Id))) { NameCol = StrainCol; }
	}

	if (bRollLoaded) { Hint = TEXT("Loaded and ready to roll"); }

	if (AccentBar) { AccentBar->SetBrush(WeedUI::Rounded(Col, 3.f)); }
	if (TypeText) { TypeText->SetText(FText::FromString(Type)); TypeText->SetColorAndOpacity(FSlateColor(Col)); } // categorie-kleur (bold+outline blijven)
	if (NameText) { NameText->SetText(FText::FromString(HandTitleFor(Id))); NameText->SetColorAndOpacity(FSlateColor(NameCol)); }
	if (QtyPill)
	{
		FSlateBrush QtyBrush = WeedUI::Rounded(FLinearColor(Col.R, Col.G, Col.B, 0.16f), 7.f);
		QtyBrush.OutlineSettings.Width = 1.f;
		QtyBrush.OutlineSettings.Color = FSlateColor(FLinearColor(Col.R, Col.G, Col.B, 0.45f));
		QtyPill->SetBrush(QtyBrush);
	}
	if (HintText)
	{
		const FString HintLine = bRollLoaded && !RollDesc.IsEmpty() ? RollDesc : HandHintFor(Id, Hint);
		HintText->SetText(FText::FromString(HintLine));
		HintText->SetVisibility(HintLine.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	// Aantal groot bij de titel: gram voor wiet/baggies, anders "x N". Voor gereedschap/plaatsbare
	// dingen (fles, pot, rek, bench, meubels) is een aantal zinloos -> verbergen.
	if (QtyText)
	{
		const bool bBottle = IdStr.StartsWith(TEXT("WaterBottle"));
		const bool bEquip = IsPotItem(Id)
			|| IdStr.StartsWith(TEXT("DryRack_")) || IdStr.StartsWith(TEXT("Bench_"))
			|| IdStr.StartsWith(TEXT("Lamp")) || IdStr.StartsWith(TEXT("Tent"))
			|| Id == TEXT("Shelf") || Id == TEXT("Chest") || Id == TEXT("Table")
			|| Id == TEXT("Mattress") || Id == TEXT("Fridge") || Id == TEXT("Atm");
		if (bBottle)
		{
			// Fles: toon GROOT hoe vol 'ie is (water / capaciteit), live tijdens vullen.
			int32 Cur = 0, Max = 0;
			if (const APawn* Pw = GetOwningPlayerPawn())
			{
				if (const UWaterCanComponent* Can = Pw->FindComponentByClass<UWaterCanComponent>())
				{
					Cur = Can->GetCharges(); Max = Can->GetMaxCharges();
				}
			}
			if (QtyPill) { QtyPill->SetVisibility(ESlateVisibility::HitTestInvisible); }
			QtyText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Cur, FMath::Max(1, Max))));
			QtyText->SetColorAndOpacity(FSlateColor((Cur <= 0) ? WeedUI::ColWarn() : WeedUI::ColGood()));
		}
		else if (bEquip)
		{
			if (QtyPill) { QtyPill->SetVisibility(ESlateVisibility::Collapsed); }
		}
		else
		{
			if (QtyPill) { QtyPill->SetVisibility(ESlateVisibility::HitTestInvisible); }
			if (Id == TEXT("Cash"))
			{
				QtyText->SetText(FText::FromString(FString::Printf(TEXT("EUR %d"), Qty)));
			}
			else if (UInventoryComponent::IsBag(Id))
			{
				QtyText->SetText(FText::FromString(FString::Printf(TEXT("x%d"), FMath::Max(1, Qty))));
			}
			else
			{
				const bool bGrams = IdStr.StartsWith(TEXT("WetBud_")) || IdStr.StartsWith(TEXT("Bud_"))
					|| IdStr.StartsWith(TEXT("Crystal_")) || IdStr.StartsWith(TEXT("Hash_"))
					|| IdStr.StartsWith(TEXT("Moonrock_")) || IdStr.StartsWith(TEXT("Rosin_"))
					|| IdStr.StartsWith(TEXT("Bubble_"));
				QtyText->SetText(FText::FromString(bGrams ? FString::Printf(TEXT("%dg"), Qty) : FString::Printf(TEXT("x%d"), FMath::Max(1, Qty))));
			}
			QtyText->SetColorAndOpacity(FSlateColor(Col));
		}
	}

	if (ChipBox)
	{
		TArray<FString> Chips;
		Chips.Reserve(5);
		auto AddChipText = [&Chips](const FString& T)
		{
			if (!T.IsEmpty() && Chips.Num() < 5) { Chips.Add(T); }
		};
		for (const TPair<FString, FString>& Stat : Detail.Stats) { AddChipText(ChipTextForStat(Stat.Key, Stat.Value)); }
		if (bRollLoaded) { AddChipText(TEXT("Loaded")); }

		for (int32 i = 0; i < ChipPills.Num(); ++i)
		{
			if (i < Chips.Num())
			{
				if (ChipTexts.IsValidIndex(i) && ChipTexts[i])
				{
					ChipTexts[i]->SetText(FText::FromString(Chips[i]));
					ChipTexts[i]->SetColorAndOpacity(FSlateColor(WeedUI::ColText()));
				}
				FSlateBrush ChipBrush = WeedUI::Rounded(WeedUI::ColInner(0.92f), 7.f);
				ChipBrush.OutlineSettings.Width = 1.f;
				ChipBrush.OutlineSettings.Color = FSlateColor(FLinearColor(Col.R, Col.G, Col.B, 0.35f));
				if (ChipPills[i])
				{
					ChipPills[i]->SetBrush(ChipBrush);
					ChipPills[i]->SetVisibility(ESlateVisibility::HitTestInvisible);
				}
			}
			else if (ChipPills[i])
			{
				ChipPills[i]->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}
