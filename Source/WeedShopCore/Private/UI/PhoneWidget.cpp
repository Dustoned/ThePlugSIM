#include "UI/PhoneWidget.h"

#include "Phone/PhoneClientComponent.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"
#include "World/HeatComponent.h"
#include "Progression/LevelComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Phone/ContactsComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/ProgressBar.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"

namespace
{
	const TCHAR* GAppName[6] = { TEXT("Upgrades"), TEXT("Suppliers"), TEXT("Contacts"), TEXT("Messages"), TEXT("Settings"), TEXT("Map") };
	const TCHAR* GAppGlyph[6] = { TEXT("UP"), TEXT("EUR"), TEXT("@"), TEXT("MSG"), TEXT("SET"), TEXT("MAP") };
	const FLinearColor GAppCol[6] = {
		FLinearColor(0.45f, 0.35f, 0.85f), FLinearColor(0.18f, 0.55f, 0.30f), FLinearColor(0.20f, 0.50f, 0.80f),
		FLinearColor(0.90f, 0.55f, 0.20f), FLinearColor(0.40f, 0.42f, 0.48f), FLinearColor(0.18f, 0.62f, 0.58f),
	};

	FSlateBrush RoundedBrush(const FLinearColor& Color, float Radius)
	{
		FSlateBrush B;
		B.DrawAs = ESlateBrushDrawType::RoundedBox;
		B.TintColor = FSlateColor(Color);
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		B.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		return B;
	}

	FSlateFontInfo PhoneFont(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", Size);
	}
}

void UPhoneButton::HandleClicked()
{
	if (Owner.IsValid())
	{
		Owner->HandlePhoneButton(ActionId, ActionParam);
	}
}

TSharedRef<SWidget> UPhoneWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

void UPhoneWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bContentDirty = true;
}

UTextBlock* UPhoneWidget::MakeText(const FString& Txt, int32 Size, const FLinearColor& Col, bool bCenter)
{
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetText(FText::FromString(Txt));
	T->SetFont(PhoneFont(Size));
	T->SetColorAndOpacity(FSlateColor(Col));
	if (bCenter) { T->SetJustification(ETextJustify::Center); }
	return T;
}

UPhoneButton* UPhoneWidget::MakeButton(const FString& Label, int32 Action, int32 Param, const FLinearColor& Col)
{
	UPhoneButton* B = WidgetTree->ConstructWidget<UPhoneButton>();
	B->ActionId = Action; B->ActionParam = Param; B->Owner = this;
	B->OnClicked.AddDynamic(B, &UPhoneButton::HandleClicked);
	FButtonStyle S;
	S.Normal = RoundedBrush(Col, 10.f);
	S.Hovered = RoundedBrush(Col * 1.3f, 10.f);
	S.Pressed = RoundedBrush(Col * 0.8f, 10.f);
	S.NormalPadding = FMargin(10.f, 5.f);
	S.PressedPadding = FMargin(10.f, 5.f);
	B->SetStyle(S);
	B->SetContent(MakeText(Label, 13, FLinearColor::White, true));
	return B;
}

UWidget* UPhoneWidget::MakeAppCell(int32 AppIndex, const FString& Name, const FString& Glyph, const FLinearColor& Col)
{
	UVerticalBox* Cell = WidgetTree->ConstructWidget<UVerticalBox>();

	UPhoneButton* Btn = WidgetTree->ConstructWidget<UPhoneButton>();
	Btn->ActionId = 0; Btn->ActionParam = AppIndex; Btn->Owner = this;
	Btn->OnClicked.AddDynamic(Btn, &UPhoneButton::HandleClicked);
	FButtonStyle S;
	S.Normal = RoundedBrush(Col, 18.f);
	S.Hovered = RoundedBrush(Col * 1.3f, 18.f);
	S.Pressed = RoundedBrush(Col * 0.8f, 18.f);
	Btn->SetStyle(S);
	Btn->SetContent(MakeText(Glyph, 18, FLinearColor::White, true));

	USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
	Sz->SetWidthOverride(64.f);
	Sz->SetHeightOverride(64.f);
	Sz->SetContent(Btn);

	UVerticalBoxSlot* S1 = Cell->AddChildToVerticalBox(Sz);
	S1->SetHorizontalAlignment(HAlign_Center);

	UVerticalBoxSlot* S2 = Cell->AddChildToVerticalBox(MakeText(Name, 11, FLinearColor(0.85f, 0.88f, 0.95f), true));
	S2->SetHorizontalAlignment(HAlign_Center);
	S2->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	return Cell;
}

void UPhoneWidget::AddInfoRow(const FString& Txt, const FLinearColor& Col, int32 Size)
{
	if (!ContentBox) { return; }
	UVerticalBoxSlot* RowSlot = ContentBox->AddChildToVerticalBox(MakeText(Txt, Size, Col));
	RowSlot->SetPadding(FMargin(2.f, 3.f, 2.f, 3.f));
}

void UPhoneWidget::BuildShell(UCanvasPanel* Root)
{
	// Lege ruimte naast de telefoon mag klikken doorlaten naar de canvas-winkel.
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Telefoon-frame: afgerond, donker, rechts in beeld.
	Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Frame"));
	Frame->SetBrush(RoundedBrush(FLinearColor(0.04f, 0.05f, 0.07f, 0.98f), 38.f));
	Frame->SetPadding(FMargin(12.f));
	Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UCanvasPanelSlot* FS = Root->AddChildToCanvas(Frame);
	FS->SetAnchors(FAnchors(1.f, 0.5f, 1.f, 0.5f));
	FS->SetAlignment(FVector2D(1.f, 0.5f));
	FS->SetAutoSize(false);
	FS->SetSize(FVector2D(360.f, 720.f));
	FS->SetPosition(FVector2D(-26.f, 0.f));

	UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FrameVB"));
	Frame->SetContent(VB);

	// Statusbalk: tijd | level | cash.
	UHorizontalBox* Status = WidgetTree->ConstructWidget<UHorizontalBox>();
	TimeText = MakeText(TEXT("Day 00:00"), 12, FLinearColor(0.7f, 0.8f, 0.95f));
	LevelText = MakeText(TEXT("Lv 1"), 12, FLinearColor(0.6f, 1.f, 0.7f), true);
	CashText = MakeText(TEXT("EUR 0"), 12, FLinearColor(0.7f, 1.f, 0.7f));
	{
		UHorizontalBoxSlot* H1 = Status->AddChildToHorizontalBox(TimeText);  H1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); H1->SetHorizontalAlignment(HAlign_Left);
		UHorizontalBoxSlot* H2 = Status->AddChildToHorizontalBox(LevelText); H2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); H2->SetHorizontalAlignment(HAlign_Center);
		UHorizontalBoxSlot* H3 = Status->AddChildToHorizontalBox(CashText);  H3->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); H3->SetHorizontalAlignment(HAlign_Right);
	}
	UVerticalBoxSlot* StatusSlot = VB->AddChildToVerticalBox(Status);
	StatusSlot->SetPadding(FMargin(6.f, 4.f, 6.f, 8.f));

	// Scherm-vlak met de app-inhoud.
	UBorder* Screen = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Screen"));
	Screen->SetBrush(RoundedBrush(FLinearColor(0.07f, 0.08f, 0.11f, 1.f), 26.f));
	Screen->SetPadding(FMargin(12.f));
	UVerticalBoxSlot* ScreenSlot = VB->AddChildToVerticalBox(Screen);
	ScreenSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	ContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentBox"));
	Screen->SetContent(ContentBox);

	// Home-indicator-balkje onderaan.
	UBorder* HomeBar = WidgetTree->ConstructWidget<UBorder>();
	HomeBar->SetBrush(RoundedBrush(FLinearColor(0.5f, 0.5f, 0.55f, 0.9f), 3.f));
	USizeBox* HbSz = WidgetTree->ConstructWidget<USizeBox>();
	HbSz->SetWidthOverride(110.f); HbSz->SetHeightOverride(5.f);
	HbSz->SetContent(HomeBar);
	UVerticalBoxSlot* HbSlot = VB->AddChildToVerticalBox(HbSz);
	HbSlot->SetHorizontalAlignment(HAlign_Center);
	HbSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));
}

void UPhoneWidget::RefreshContent()
{
	if (!ContentBox || !Phone.IsValid()) { return; }
	ContentBox->ClearChildren();

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	if (Phone->IsHomeScreen())
	{
		AddInfoRow(TEXT("Apps"), FLinearColor(0.6f, 1.f, 0.6f), 16);
		UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
		Grid->SetSlotPadding(FMargin(10.f));
		for (int32 i = 0; i < 6; ++i)
		{
			UUniformGridSlot* GSlot = Grid->AddChildToUniformGrid(MakeAppCell(i, GAppName[i], GAppGlyph[i], GAppCol[i]), i / 3, i % 3);
			GSlot->SetHorizontalAlignment(HAlign_Center);
		}
		UVerticalBoxSlot* GridSlot = ContentBox->AddChildToVerticalBox(Grid);
		GridSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		return;
	}

	const int32 App = FMath::Clamp(Phone->GetTab(), 0, 5);

	// App-header: back-knop + titel.
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Header->AddChildToHorizontalBox(MakeButton(TEXT("< Home"), 1, 0, FLinearColor(0.2f, 0.3f, 0.45f)));
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(MakeText(GAppName[App], 16, FLinearColor(0.9f, 0.95f, 1.f)));
	TitleSlot->SetPadding(FMargin(12.f, 4.f, 0.f, 0.f));
	UVerticalBoxSlot* HeaderSlot = ContentBox->AddChildToVerticalBox(Header);
	HeaderSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	if (App == 0) // Upgrades
	{
		UUpgradeComponent* Upg = GS ? GS->GetUpgrades() : nullptr;
		if (Upg)
		{
			int32 idx = 0;
			for (const FName& Id : Upg->GetAllUpgradeIds())
			{
				FText Name; int32 Cost = 0; bool bPurchased = false; bool bAvailable = false;
				if (Upg->GetUpgradeDisplay(Id, Name, Cost, bPurchased, bAvailable))
				{
					UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
					UHorizontalBoxSlot* L = Row->AddChildToHorizontalBox(MakeText(
						FString::Printf(TEXT("%s  -  EUR %.2f"), *Name.ToString(), Cost / 100.f),
						13, bPurchased ? FLinearColor::Gray : (bAvailable ? FLinearColor::White : FLinearColor(0.8f, 0.55f, 0.55f))));
					L->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					L->SetVerticalAlignment(VAlign_Center);
					if (!bPurchased && bAvailable)
					{
						Row->AddChildToHorizontalBox(MakeButton(TEXT("Buy"), 3, idx, FLinearColor(0.2f, 0.5f, 0.28f)));
					}
					else
					{
						Row->AddChildToHorizontalBox(MakeText(bPurchased ? TEXT("owned") : TEXT("locked"), 11, FLinearColor::Gray));
					}
					UVerticalBoxSlot* RS = ContentBox->AddChildToVerticalBox(Row);
					RS->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f));
				}
				if (++idx >= 10) { break; }
			}
		}
	}
	else if (App == 1) // Suppliers
	{
		AddInfoRow(TEXT("The store opens to the left."), FLinearColor(0.8f, 0.9f, 1.f), 14);
		AddInfoRow(TEXT("Browse, set amounts, add to"), FLinearColor(0.65f, 0.68f, 0.78f));
		AddInfoRow(TEXT("cart and checkout there."), FLinearColor(0.65f, 0.68f, 0.78f));
	}
	else if (App == 2) // Contacts
	{
		UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
		if (!Con || Con->GetContacts().Num() == 0)
		{
			AddInfoRow(TEXT("No contacts yet."), FLinearColor::Gray, 13);
			AddInfoRow(TEXT("Deal with customers to get"), FLinearColor::Gray);
			AddInfoRow(TEXT("their number."), FLinearColor::Gray);
		}
		else
		{
			for (const FPhoneContact& C : Con->GetContacts())
			{
				AddInfoRow(FString::Printf(TEXT("%s   (%.0f%%)"), *C.DisplayName.ToString(), C.Relationship), FLinearColor::White);
			}
		}
	}
	else if (App == 3) // Messages
	{
		UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* A = Btns->AddChildToHorizontalBox(MakeButton(TEXT("Accept"), 5, 0, FLinearColor(0.2f, 0.5f, 0.28f)));
		A->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		Btns->AddChildToHorizontalBox(MakeButton(TEXT("Decline"), 6, 0, FLinearColor(0.5f, 0.28f, 0.2f)));
		UVerticalBoxSlot* BSlot = ContentBox->AddChildToVerticalBox(Btns);
		BSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		if (Con)
		{
			if (Con->GetMessages().Num() == 0) { AddInfoRow(TEXT("No messages."), FLinearColor::Gray); }
			for (const FPhoneMessage& M : Con->GetMessages())
			{
				const TCHAR* Tag = (M.Status == 1) ? TEXT("[yes]") : (M.Status == 2 ? TEXT("[no]") : TEXT("[open]"));
				AddInfoRow(FString::Printf(TEXT("%s %s: %s"), Tag, *M.SenderName.ToString(), *M.Body.ToString()),
					FLinearColor(0.88f, 0.92f, 1.f), 12);
			}
		}
	}
	else if (App == 4) // Settings
	{
		if (GS && GS->GetLeveling())
		{
			ULevelComponent* Lv = GS->GetLeveling();
			AddInfoRow(FString::Printf(TEXT("Level %d"), Lv->GetLevel()), FLinearColor(0.7f, 1.f, 0.7f), 15);
			UProgressBar* XpBar = WidgetTree->ConstructWidget<UProgressBar>();
			XpBar->SetPercent(Lv->GetLevelFraction());
			XpBar->SetFillColorAndOpacity(FLinearColor(0.3f, 0.7f, 1.f));
			UVerticalBoxSlot* XS = ContentBox->AddChildToVerticalBox(XpBar);
			XS->SetPadding(FMargin(0.f, 2.f, 0.f, 4.f));
			AddInfoRow(Lv->GetLevel() >= ULevelComponent::MaxLevel ? TEXT("MAX")
				: *FString::Printf(TEXT("%d / %d XP"), Lv->GetCurrentXP(), Lv->GetXPToNext()), FLinearColor(0.7f, 0.75f, 0.85f), 12);
		}
		if (GS && GS->GetHeat())
		{
			AddInfoRow(TEXT("Heat"), FLinearColor(1.f, 0.7f, 0.6f), 14);
			UProgressBar* HeatBar = WidgetTree->ConstructWidget<UProgressBar>();
			HeatBar->SetPercent(GS->GetHeat()->GetHeat() / 100.f);
			HeatBar->SetFillColorAndOpacity(FLinearColor(1.f, 0.45f, 0.3f));
			ContentBox->AddChildToVerticalBox(HeatBar);
		}
		AddInfoRow(TEXT(""), FLinearColor::White, 6);
		AddInfoRow(TEXT("Controls"), FLinearColor(0.7f, 0.85f, 1.f), 14);
		AddInfoRow(TEXT("Tab phone   I inventory   Q home"), FLinearColor(0.8f, 0.8f, 0.85f), 12);
		AddInfoRow(TEXT("1-8 hotbar   LMB use   RMB roll/smoke"), FLinearColor(0.8f, 0.8f, 0.85f), 12);
		AddInfoRow(TEXT("R rotate   G pick up   F sample"), FLinearColor(0.8f, 0.8f, 0.85f), 12);
	}
	else // Map
	{
		AddInfoRow(TEXT("Map"), FLinearColor(0.7f, 0.95f, 0.9f), 15);
		AddInfoRow(TEXT("(mini-map coming to the phone)"), FLinearColor::Gray, 12);
	}
}

void UPhoneWidget::HandlePhoneButton(int32 Action, int32 Param)
{
	if (!Phone.IsValid()) { return; }
	switch (Action)
	{
	case 0: Phone->OpenApp(Param); break;
	case 1: Phone->GoHome(); break;
	case 2: Phone->Toggle(); break;
	case 3: Phone->DoAction(Param); break;     // koop upgrade
	case 5: Phone->DoAction(0); break;         // accept bericht
	case 6: Phone->DoAction(1); break;         // decline bericht
	default: break;
	}
	bContentDirty = true;
}

void UPhoneWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	if (!Phone.IsValid())
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const bool bOpen = Phone->IsOpen();
	SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (!bOpen) { return; }

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS)
	{
		if (CashText && GS->GetEconomy())
		{
			CashText->SetText(FText::FromString(FString::Printf(TEXT("EUR %.0f"), GS->GetEconomy()->GetBalanceEuros())));
		}
		if (TimeText && GS->GetDayCycle())
		{
			const int32 T = FMath::RoundToInt(GS->GetDayCycle()->GetCycleFraction() * 24.f * 60.f);
			TimeText->SetText(FText::FromString(FString::Printf(TEXT("%s %02d:%02d"),
				GS->GetDayCycle()->IsNight() ? TEXT("Night") : TEXT("Day"), (T / 60) % 24, T % 60)));
		}
		if (LevelText && GS->GetLeveling())
		{
			LevelText->SetText(FText::FromString(FString::Printf(TEXT("Lv %d"), GS->GetLeveling()->GetLevel())));
		}
	}

	const bool bHome = Phone->IsHomeScreen();
	const int32 App = Phone->GetTab();
	if (bContentDirty || bHome != bLastHome || App != bLastApp)
	{
		bLastHome = bHome; bLastApp = App; bContentDirty = false;
		RefreshContent();
	}
}
