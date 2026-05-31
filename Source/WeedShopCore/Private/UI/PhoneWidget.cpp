#include "UI/PhoneWidget.h"

#include "Phone/PhoneClientComponent.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"
#include "World/HeatComponent.h"
#include "Progression/LevelComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Phone/ContactsComponent.h"
#include "Input/ControlSettings.h"
#include "GameFramework/Pawn.h"
#include "Components/ScrollBox.h"
#include "InputCoreTypes.h"

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
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/ProgressBar.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "UI/WeedUiStyle.h"

namespace
{
	// (Bank-app op de telefoon komt pas terug na een telefoon-upgrade; bankieren gaat nu via de ATM.)
	constexpr int32 GNumApps = 6;
	const TCHAR* GAppName[GNumApps] = { TEXT("Upgrades"), TEXT("Suppliers"), TEXT("Contacts"), TEXT("Messages"), TEXT("Settings"), TEXT("Map") };
	const WeedUI::EIcon GAppIcon[GNumApps] = { WeedUI::EIcon::Upgrade, WeedUI::EIcon::Shop, WeedUI::EIcon::Person, WeedUI::EIcon::Message, WeedUI::EIcon::Gear, WeedUI::EIcon::Map };
	const FLinearColor GAppCol[GNumApps] = {
		FLinearColor(0.45f, 0.35f, 0.85f), FLinearColor(0.18f, 0.55f, 0.30f), FLinearColor(0.20f, 0.50f, 0.80f),
		FLinearColor(0.90f, 0.55f, 0.20f), FLinearColor(0.40f, 0.42f, 0.48f), FLinearColor(0.18f, 0.62f, 0.58f),
	};

	// Compact geldbedrag (hele euro's) zodat het in smalle balken past: 1.0M / 12k / 523.
	FString CompactEuros(double Euros)
	{
		const double A = FMath::Abs(Euros);
		if (A >= 1000000.0) { return FString::Printf(TEXT("%.1fM"), Euros / 1000000.0); }
		if (A >= 1000.0)    { return FString::Printf(TEXT("%.1fk"), Euros / 1000.0); }
		return FString::Printf(TEXT("%.0f"), Euros);
	}

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

void UPhoneWidget::SetPhone(UPhoneClientComponent* InPhone)
{
	Phone = InPhone;
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
	SetIsFocusable(true); // nodig om in Settings -> Controls een toets-aanslag op te vangen (rebind)
}

FReply UPhoneWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bRebinding)
	{
		const FKey K = InKeyEvent.GetKey();
		if (K == EKeys::Escape)
		{
			bRebinding = false; RebindMsg = TEXT("Cancelled."); FillSettingsBody();
			return FReply::Handled();
		}
		if (K == EKeys::BackSpace || K == EKeys::Delete)
		{
			UControlSettings::Get()->ClearKey(RebindAction, bRebindAlt);
			RebindMsg = FString::Printf(TEXT("Cleared %s key for '%s'"), bRebindAlt ? TEXT("alt") : TEXT("main"), *UControlSettings::DisplayName(RebindAction).ToString());
			bRebinding = false; FillSettingsBody();
			return FReply::Handled();
		}
		FName Conflict;
		if (UControlSettings::Get()->SetKey(RebindAction, bRebindAlt, K, Conflict))
		{
			RebindMsg = FString::Printf(TEXT("'%s' %s -> %s"), *UControlSettings::DisplayName(RebindAction).ToString(),
				bRebindAlt ? TEXT("(alt)") : TEXT("(main)"), *K.GetDisplayName().ToString());
		}
		else
		{
			RebindMsg = Conflict.IsNone() ? TEXT("That key can't be used.")
				: FString::Printf(TEXT("Already used by: %s"), *UControlSettings::DisplayName(Conflict).ToString());
		}
		bRebinding = false; FillSettingsBody();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPhoneWidget::BuildSettingsApp()
{
	if (!ContentBox) { return; }

	// Categorie-knoppen (Status eerst + standaard, dan Controls) — blijven staan; alleen de body ververst.
	SettingsTabBtns.Reset();
	static const TCHAR* CatNames[2] = { TEXT("Status"), TEXT("Controls") };
	UHorizontalBox* Cats = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 i = 0; i < 2; ++i)
	{
		const FLinearColor Col = (i == SettingsCat) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
		UWeedActionButton* B = MakeActionBtn(CatNames[i], Col,
			[this, i]() { SettingsCat = i; bRebinding = false; RebindMsg.Reset(); RefreshSettingsTabs(); FillSettingsBody(); }, 12);
		UHorizontalBoxSlot* CS = Cats->AddChildToHorizontalBox(B);
		CS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
		SettingsTabBtns.Add(B);
	}
	ContentBox->AddChildToVerticalBox(Cats)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	SettingsBody = WidgetTree->ConstructWidget<UVerticalBox>();
	ContentBox->AddChildToVerticalBox(SettingsBody);
	FillSettingsBody();
}

void UPhoneWidget::RefreshSettingsTabs()
{
	for (int32 i = 0; i < SettingsTabBtns.Num(); ++i)
	{
		if (!SettingsTabBtns[i]) { continue; }
		const FLinearColor Col = (i == SettingsCat) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
		FButtonStyle St;
		St.Normal = RoundedBrush(Col, 8.f);
		St.Hovered = RoundedBrush(Col * 1.3f, 8.f);
		St.Pressed = RoundedBrush(Col * 0.8f, 8.f);
		St.NormalPadding = FMargin(6.f, 4.f); St.PressedPadding = FMargin(6.f, 4.f);
		SettingsTabBtns[i]->SetStyle(St);
	}
}

void UPhoneWidget::FillSettingsBody()
{
	if (!SettingsBody) { return; }
	SettingsBody->ClearChildren();
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	auto BodyRow = [this](UWidget* W, const FMargin& Pad) { SettingsBody->AddChildToVerticalBox(W)->SetPadding(Pad); };

	if (SettingsCat == 1) // Controls
	{
		BodyRow(MakeText(TEXT("Click a key, press the new key. Esc = cancel, Backspace = clear. No key twice."),
			10, FLinearColor(0.62f, 0.66f, 0.76f)), FMargin(0.f, 0.f, 0.f, 6.f));

		// Kop-rij.
		{
			UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
			UHorizontalBoxSlot* L = H->AddChildToHorizontalBox(MakeText(TEXT("Action"), 10, FLinearColor(0.6f, 0.65f, 0.75f)));
			L->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			H->AddChildToHorizontalBox(MakeText(TEXT("Main      Alt"), 10, FLinearColor(0.6f, 0.65f, 0.75f)));
			BodyRow(H, FMargin(0.f, 0.f, 0.f, 2.f));
		}

		UControlSettings* Cfg = UControlSettings::Get();
		for (const FName& Action : UControlSettings::AllActions())
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* NameT = MakeText(UControlSettings::DisplayName(Action).ToString(), 12, FLinearColor(0.9f, 0.92f, 1.f));
			NameT->SetClipping(EWidgetClipping::ClipToBounds);
			UHorizontalBoxSlot* NS = Row->AddChildToHorizontalBox(NameT);
			NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NS->SetVerticalAlignment(VAlign_Center);

			for (int32 SlotIdx = 0; SlotIdx < 2; ++SlotIdx)
			{
				const bool bAlt = (SlotIdx == 1);
				const bool bThis = bRebinding && (RebindAction == Action) && (bRebindAlt == bAlt);
				const FKey K = Cfg->GetKey(Action, bAlt);
				FString Lbl = bThis ? TEXT("Press...") : (K.IsValid() ? K.GetDisplayName().ToString() : (bAlt ? TEXT("+ Alt") : TEXT("-")));
				const FLinearColor BtnCol = bThis ? FLinearColor(0.5f, 0.4f, 0.12f) : FLinearColor(0.18f, 0.22f, 0.3f);
				USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
				Sz->SetMinDesiredWidth(70.f);
				Sz->SetContent(MakeActionBtn(Lbl, BtnCol,
					[this, Action, bAlt]() { bRebinding = true; bRebindAlt = bAlt; RebindAction = Action; RebindMsg.Reset(); SetKeyboardFocus(); FillSettingsBody(); }, 11));
				Row->AddChildToHorizontalBox(Sz)->SetPadding(FMargin(3.f, 0.f, 0.f, 0.f));
			}
			BodyRow(Row, FMargin(0.f, 3.f, 0.f, 3.f));
		}

		if (!RebindMsg.IsEmpty()) { BodyRow(MakeText(RebindMsg, 11, FLinearColor(1.f, 0.85f, 0.45f)), FMargin(0.f, 4.f, 0.f, 0.f)); }

		BodyRow(MakeActionBtn(TEXT("Reset to defaults"), FLinearColor(0.4f, 0.34f, 0.16f),
			[this]() { UControlSettings::Get()->ResetToDefaults(); bRebinding = false; RebindMsg = TEXT("Controls reset to defaults."); FillSettingsBody(); }, 12),
			FMargin(0.f, 8.f, 0.f, 0.f));
	}
	else // Status
	{
		if (GS && GS->GetLeveling())
		{
			ULevelComponent* Lv = GS->GetLeveling();
			BodyRow(MakeText(FString::Printf(TEXT("Level %d"), Lv->GetLevel()), 15, FLinearColor(0.7f, 1.f, 0.7f)), FMargin(0.f, 0.f, 0.f, 2.f));
			UProgressBar* XpBar = WidgetTree->ConstructWidget<UProgressBar>();
			XpBar->SetPercent(Lv->GetLevelFraction());
			XpBar->SetFillColorAndOpacity(FLinearColor(0.3f, 0.7f, 1.f));
			BodyRow(XpBar, FMargin(0.f, 2.f, 0.f, 4.f));
			BodyRow(MakeText(Lv->GetLevel() >= ULevelComponent::MaxLevel ? TEXT("MAX")
				: *FString::Printf(TEXT("%d / %d XP"), Lv->GetCurrentXP(), Lv->GetXPToNext()), 12, FLinearColor(0.7f, 0.75f, 0.85f)), FMargin(0.f, 0.f, 0.f, 6.f));
		}
		if (GS && GS->GetHeat())
		{
			BodyRow(MakeText(TEXT("Heat"), 14, FLinearColor(1.f, 0.7f, 0.6f)), FMargin(0.f, 0.f, 0.f, 2.f));
			UProgressBar* HeatBar = WidgetTree->ConstructWidget<UProgressBar>();
			HeatBar->SetPercent(GS->GetHeat()->GetHeat() / 100.f);
			HeatBar->SetFillColorAndOpacity(FLinearColor(1.f, 0.45f, 0.3f));
			BodyRow(HeatBar, FMargin(0.f, 0.f, 0.f, 0.f));
		}
	}
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

UWeedActionButton* UPhoneWidget::MakeActionBtn(const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick, int32 FontSize)
{
	UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
	B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
	B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
	FButtonStyle S;
	S.Normal = RoundedBrush(Col, 8.f);
	S.Hovered = RoundedBrush(Col * 1.3f, 8.f);
	S.Pressed = RoundedBrush(Col * 0.8f, 8.f);
	S.NormalPadding = FMargin(6.f, 4.f); S.PressedPadding = FMargin(6.f, 4.f);
	B->SetStyle(S);
	B->SetContent(MakeText(Label, FontSize, FLinearColor::White, true));
	return B;
}

void UPhoneWidget::UpdateStoreCartText()
{
	if (StoreCartText && Phone.IsValid())
	{
		// Toon het NETTO bedrag (koop + bezorging - verkoop). Negatief = je ontvangt geld (+).
		const int32 Net = Phone->GetCartNetCents(DeliveryOpt);
		const FString Amt = (Net < 0)
			? FString::Printf(TEXT("+EUR %.2f"), -Net / 100.f)
			: FString::Printf(TEXT("EUR %.2f"), Net / 100.f);
		StoreCartText->SetText(FText::FromString(FString::Printf(TEXT("Cart %d   %s"), Phone->GetCartNumLines(), *Amt)));
	}
}

void UPhoneWidget::BuildStoreApp(UVerticalBox* Into)
{
	if (!Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();
	StoreQtyTexts.Reset();
	StoreTabBtns.Reset();

	// Categorie-pillen (Seeds/Papers/Pots/Soil/Water/Sell) — blijven staan; alleen de lijst ververst.
	static const TCHAR* CatNames[6] = { TEXT("Seeds"), TEXT("Papers"), TEXT("Pots"), TEXT("Soil"), TEXT("Water"), TEXT("Sell") };
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 i = 0; i < 6; ++i)
	{
		const FLinearColor Col = (i == Ph->GetSupplierCat()) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
		UWeedActionButton* Pill = MakeActionBtn(CatNames[i], Col, [this, Ph, i]() { Ph->SetSupplierCat(i); bCartView = false; RefreshStore(); }, 10);
		UHorizontalBoxSlot* PS = Tabs->AddChildToHorizontalBox(Pill);
		PS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PS->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
		StoreTabBtns.Add(Pill);
	}
	Into->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Winkelwagen-balk: totaal + toggle naar cart/shop.
	UHorizontalBox* CartBar = WidgetTree->ConstructWidget<UHorizontalBox>();
	StoreCartText = MakeText(TEXT("Cart 0"), 13, FLinearColor(1.f, 0.95f, 0.6f));
	StoreCartText->SetClipping(EWidgetClipping::ClipToBounds);
	UHorizontalBoxSlot* CL = CartBar->AddChildToHorizontalBox(StoreCartText);
	CL->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CL->SetVerticalAlignment(VAlign_Center);
	UpdateStoreCartText();
	StoreCartToggle = MakeActionBtn(bCartView ? TEXT("Shop") : TEXT("View cart"), FLinearColor(0.2f, 0.35f, 0.5f), [this]() { bPackagesView = false; bCartView = !bCartView; RefreshStore(); }, 11);
	CartBar->AddChildToHorizontalBox(StoreCartToggle)->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
	// (De Packages-knop staat rechtsboven in de app-header naast "Suppliers".)
	Into->AddChildToVerticalBox(CartBar)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Scrollbare lijst (alleen deze ververst bij categorie/cart-acties).
	StoreScroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* ScS = Into->AddChildToVerticalBox(StoreScroll);
	ScS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Vaste voettekst onderaan (bezorgopties + totaal + checkout in de cart-weergave).
	StoreFooter = WidgetTree->ConstructWidget<UVerticalBox>();
	Into->AddChildToVerticalBox(StoreFooter);

	FillStoreList();
}

void UPhoneWidget::RefreshStore()
{
	if (!Phone.IsValid()) { return; }
	const int32 Cat = Phone->GetSupplierCat();
	for (int32 i = 0; i < StoreTabBtns.Num(); ++i)
	{
		if (!StoreTabBtns[i]) { continue; }
		const FLinearColor Col = (i == Cat) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
		FButtonStyle St;
		St.Normal = RoundedBrush(Col, 8.f);
		St.Hovered = RoundedBrush(Col * 1.3f, 8.f);
		St.Pressed = RoundedBrush(Col * 0.8f, 8.f);
		St.NormalPadding = FMargin(6.f, 4.f); St.PressedPadding = FMargin(6.f, 4.f);
		StoreTabBtns[i]->SetStyle(St);
	}
	if (StoreCartToggle) { StoreCartToggle->SetContent(MakeText(bCartView ? TEXT("Shop") : TEXT("View cart"), 11, FLinearColor::White, true)); }
	if (StorePackagesLabel)
	{
		StorePackagesLabel->SetText(FText::FromString(bPackagesView ? TEXT("Shop")
			: FString::Printf(TEXT("Packages (%d)"), Phone->GetPendingCount())));
	}
	UpdateStoreCartText();
	FillStoreList();
}

int32 UPhoneWidget::PackagesSignature() const
{
	if (!Phone.IsValid()) { return 0; }
	const TArray<UPhoneClientComponent::FPendingDelivery>& Pend = Phone->GetPendingDeliveries();
	int32 Sig = Pend.Num() * 1000003;
	for (const UPhoneClientComponent::FPendingDelivery& D : Pend) { Sig = Sig * 31 + D.OrderId + (D.bArrived ? 7 : 0); }
	return Sig;
}

void UPhoneWidget::UpdatePackagesLive()
{
	if (!Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();
	for (const UPhoneClientComponent::FPendingDelivery& D : Ph->GetPendingDeliveries())
	{
		if (TObjectPtr<UProgressBar>* B = PkgBars.Find(D.OrderId)) { if (*B) { (*B)->SetPercent(Ph->GetDeliveryProgress(D)); } }
		if (TObjectPtr<UTextBlock>* E = PkgEtas.Find(D.OrderId))
		{
			if (*E)
			{
				const int32 Left = FMath::CeilToInt(Ph->GetDeliverySecondsLeft(D));
				(*E)->SetText(FText::FromString(FString::Printf(TEXT("Arrives in %d:%02d"), Left / 60, Left % 60)));
			}
		}
	}
}

void UPhoneWidget::FillStoreList()
{
	if (!StoreScroll || !Phone.IsValid()) { return; }
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return; }
	UPhoneClientComponent* Ph = Phone.Get();

	StoreScroll->ClearChildren();
	if (StoreFooter) { StoreFooter->ClearChildren(); }
	StoreQtyTexts.Reset();
	PkgBars.Reset();
	PkgEtas.Reset();
	const int32 Cat = Ph->GetSupplierCat();

	auto AddGap = [this]() {
		UBorder* Gap = WidgetTree->ConstructWidget<UBorder>();
		Gap->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
		Gap->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		StoreScroll->AddChild(Gap);
	};

	// --- Packages: onderweg zijnde bestellingen (voortgang + ETA + annuleren) ---
	if (bPackagesView)
	{
		const TArray<UPhoneClientComponent::FPendingDelivery>& Pend = Ph->GetPendingDeliveries();
		if (Pend.Num() == 0)
		{
			StoreScroll->AddChild(MakeText(TEXT("No packages on the way. Order from the shop and pick a delivery speed."), 12, FLinearColor::Gray));
		}
		for (const UPhoneClientComponent::FPendingDelivery& D : Pend)
		{
			const int32 OrderId = D.OrderId;

			UBorder* CardB = WidgetTree->ConstructWidget<UBorder>();
			CardB->SetBrush(RoundedBrush(FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 8.f));
			CardB->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
			UVerticalBox* CVB = WidgetTree->ConstructWidget<UVerticalBox>();
			CardB->SetContent(CVB);

			// Titel-rij: bezorgnaam + aantal stuks.
			CVB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%s delivery   -   %d item(s)"),
				*UPhoneClientComponent::DeliveryName(D.DeliveryOpt), D.ItemCount), 13, FLinearColor(0.92f, 0.94f, 1.f)));
			// Inhoud (kort).
			{
				FString Sum = D.Summary;
				if (Sum.Len() > 40) { Sum = Sum.Left(39) + TEXT("."); }
				UTextBlock* SumT = MakeText(Sum, 10, FLinearColor(0.62f, 0.66f, 0.76f));
				SumT->SetAutoWrapText(true);
				CVB->AddChildToVerticalBox(SumT);
			}

			const bool bArrived = D.bArrived;

			// Progress bar (vol + groen als 'ie bij de deur ligt).
			USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>();
			BarSz->SetHeightOverride(14.f);
			UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
			Bar->SetFillColorAndOpacity(bArrived ? FLinearColor(0.4f, 0.9f, 0.45f) : FLinearColor(0.4f, 0.75f, 1.f));
			Bar->SetPercent(bArrived ? 1.f : Ph->GetDeliveryProgress(D));
			BarSz->SetContent(Bar);
			CVB->AddChildToVerticalBox(BarSz)->SetPadding(FMargin(0.f, 5.f, 0.f, 0.f));
			if (!bArrived) { PkgBars.Add(OrderId, Bar); }

			// Status-regel: ETA + Cancel (onderweg), of "bij de deur" (aangekomen).
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			if (bArrived)
			{
				UTextBlock* AtDoor = MakeText(TEXT("At the door - go pick it up"), 12, FLinearColor(0.6f, 1.f, 0.6f));
				Row->AddChildToHorizontalBox(AtDoor)->SetVerticalAlignment(VAlign_Center);
			}
			else
			{
				const int32 Left = FMath::CeilToInt(Ph->GetDeliverySecondsLeft(D));
				UTextBlock* EtaT = MakeText(FString::Printf(TEXT("Drone on the way - %d:%02d"), Left / 60, Left % 60), 12, FLinearColor(0.85f, 0.9f, 1.f));
				UHorizontalBoxSlot* ES = Row->AddChildToHorizontalBox(EtaT);
				ES->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); ES->SetVerticalAlignment(VAlign_Center);
				PkgEtas.Add(OrderId, EtaT);
				Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Cancel"), FLinearColor(0.45f, 0.18f, 0.18f),
					[this, Ph, OrderId]() { Ph->CancelDelivery(OrderId); LastPkgSig = -1; RefreshStore(); }, 11));
			}
			CVB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 5.f, 0.f, 0.f));

			StoreScroll->AddChild(CardB);
			AddGap();
		}
		return;
	}

	if (bCartView)
	{
		const int32 Lines = Ph->GetCartNumLines();
		if (Lines == 0) { StoreScroll->AddChild(MakeText(TEXT("Cart is empty."), 13, FLinearColor::Gray)); }
		for (int32 li = 0; li < Lines; ++li)
		{
			FName LId; int32 LQty = 0; bool bSell = false;
			if (!Ph->GetCartLine(li, LId, LQty, bSell)) { continue; }
			const int32 Unit = bSell ? Store->GetSellValueCents(LId) : Store->GetCatalogPriceCents(LId);
			const int32 LineP = Unit * LQty;
			FString LName = bSell ? WeedUI::PrettyItemName(LId) : Store->GetCatalogName(LId).ToString();
			if (LName.Len() > 26) { LName = LName.Left(25) + TEXT("."); }
			if (bSell) { LName = TEXT("Sell: ") + LName; }
			const int32 Idx = li;

			UBorder* CardB = WidgetTree->ConstructWidget<UBorder>();
			CardB->SetBrush(RoundedBrush(bSell ? FLinearColor(0.10f, 0.15f, 0.11f, 0.95f) : FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 8.f));
			CardB->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
			UVerticalBox* CVB = WidgetTree->ConstructWidget<UVerticalBox>();
			CardB->SetContent(CVB);
			UTextBlock* NameT = MakeText(LName, 12, bSell ? FLinearColor(0.7f, 1.f, 0.75f) : FLinearColor(0.9f, 0.92f, 1.f));
			NameT->SetClipping(EWidgetClipping::ClipToBounds);
			CVB->AddChildToVerticalBox(NameT);
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UHorizontalBoxSlot* T = Row->AddChildToHorizontalBox(MakeText(
				bSell ? FString::Printf(TEXT("x%d   +EUR %.2f"), LQty, LineP / 100.f) : FString::Printf(TEXT("x%d   EUR %.2f"), LQty, LineP / 100.f),
				12, bSell ? FLinearColor(0.7f, 1.f, 0.7f) : FLinearColor(1.f, 0.95f, 0.7f)));
			T->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); T->SetVerticalAlignment(VAlign_Center);
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("-"), FLinearColor(0.18f, 0.19f, 0.24f), [this, Ph, Idx]() { Ph->AdjustCartLine(Idx, -1); RefreshStore(); }));
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("+"), FLinearColor(0.18f, 0.19f, 0.24f), [this, Ph, Idx]() { Ph->AdjustCartLine(Idx, +1); RefreshStore(); }));
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("x"), FLinearColor(0.42f, 0.18f, 0.18f), [this, Ph, Idx]() { Ph->AdjustCartLine(Idx, -100000); RefreshStore(); }));
			CVB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
			StoreScroll->AddChild(CardB);
			AddGap();
		}
		// Vaste voettekst: bezorgopties (op koopdeel) + netto-totaal (rechtsonder) + checkout.
		if (StoreFooter)
		{
			const int32 BuySub = Ph->GetCartBuyCents();
			const int32 SellSub = Ph->GetCartSellCents();

			// Bezorgopties (alleen relevant als je koopt). Fee = % van het koop-subtotaal.
			StoreFooter->AddChildToVerticalBox(MakeText(BuySub > 0 ? TEXT("Delivery") : TEXT("Selling only - no delivery needed"), 11, FLinearColor(0.75f, 0.8f, 0.95f)))->SetPadding(FMargin(0.f, 4.f, 0.f, 2.f));
			if (BuySub > 0)
			{
				UHorizontalBox* Opts = WidgetTree->ConstructWidget<UHorizontalBox>();
				for (int32 d = 0; d < 3; ++d)
				{
					const int32 OptFee = FMath::RoundToInt(BuySub * UPhoneClientComponent::DeliveryFeePct(d));
					const FLinearColor Col = (d == DeliveryOpt) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
					const FString Lbl = FString::Printf(TEXT("%s\n%s\nEUR %.2f"), *UPhoneClientComponent::DeliveryName(d), *UPhoneClientComponent::DeliveryTimeText(d), OptFee / 100.f);
					UWeedActionButton* OB = MakeActionBtn(Lbl, Col, [this, d]() { DeliveryOpt = d; RefreshStore(); }, 9);
					UHorizontalBoxSlot* OS = Opts->AddChildToHorizontalBox(OB);
					OS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); OS->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
				}
				StoreFooter->AddChildToVerticalBox(Opts)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			}

			// Opbouw-regel: koop - verkoop - bezorging.
			const int32 Fee = FMath::RoundToInt(BuySub * UPhoneClientComponent::DeliveryFeePct(DeliveryOpt));
			UTextBlock* Breakdown = MakeText(FString::Printf(TEXT("Buy EUR %.2f   -   Sell EUR %.2f   +   Delivery EUR %.2f"),
				BuySub / 100.f, SellSub / 100.f, Fee / 100.f), 10, FLinearColor(0.68f, 0.72f, 0.82f));
			Breakdown->SetJustification(ETextJustify::Right);
			StoreFooter->AddChildToVerticalBox(Breakdown)->SetPadding(FMargin(0.f, 2.f, 4.f, 0.f));

			// Netto: positief = betalen, negatief = ontvangen.
			const int32 Net = Ph->GetCartNetCents(DeliveryOpt);
			UTextBlock* TotT = MakeText(Net >= 0 ? FString::Printf(TEXT("Total: EUR %.2f"), Net / 100.f)
				: FString::Printf(TEXT("You receive: EUR %.2f"), -Net / 100.f),
				15, Net >= 0 ? FLinearColor(1.f, 0.95f, 0.55f) : FLinearColor(0.6f, 1.f, 0.65f));
			TotT->SetJustification(ETextJustify::Right);
			StoreFooter->AddChildToVerticalBox(TotT)->SetPadding(FMargin(0.f, 2.f, 4.f, 4.f));

			StoreFooter->AddChildToVerticalBox(MakeActionBtn(TEXT("CHECKOUT"), FLinearColor(0.2f, 0.55f, 0.27f),
				[this, Ph]() { Ph->Checkout(DeliveryOpt); bCartView = false; RefreshStore(); }, 14));
		}
		return;
	}

	if (Cat == 5) // Sell — net als de koop-pagina: aantal kiezen + Add naar de winkelwagen.
	{
		APawn* P = GetOwningPlayerPawn();
		const UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr;
		// Verkoopbare items samenvoegen per item-id (totaal aantal).
		TArray<FName> Order; TMap<FName, int32> Totals;
		if (Inv)
		{
			for (const FInventoryStack& S : Inv->GetStacks())
			{
				if (Store->GetSellValueCents(S.ItemId) <= 0) { continue; }
				if (!Totals.Contains(S.ItemId)) { Order.Add(S.ItemId); }
				Totals.FindOrAdd(S.ItemId) += S.Quantity;
			}
		}
		if (Order.Num() == 0) { StoreScroll->AddChild(MakeText(TEXT("(nothing sellable)"), 13, FLinearColor::Gray)); return; }

		for (const FName& Id : Order)
		{
			const int32 Have = Totals[Id];
			const int32 Val = Store->GetSellValueCents(Id);
			const int32 Pend = Ph->GetPendingSellQty(Id);

			UBorder* CardB = WidgetTree->ConstructWidget<UBorder>();
			CardB->SetBrush(RoundedBrush(FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 8.f));
			CardB->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
			UVerticalBox* CardVB = WidgetTree->ConstructWidget<UVerticalBox>();
			CardB->SetContent(CardVB);
			CardVB->AddChildToVerticalBox(MakeText(WeedUI::PrettyItemName(Id), 14, FLinearColor(0.95f, 0.97f, 1.f)));
			CardVB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("You have %d   -   sells for EUR %.2f each"), Have, Val / 100.f), 10, FLinearColor(0.62f, 0.66f, 0.76f)));

			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UHorizontalBoxSlot* PT = Row->AddChildToHorizontalBox(MakeText(FString::Printf(TEXT("+EUR %.2f"), (Val * Pend) / 100.f), 13, FLinearColor(0.7f, 1.f, 0.7f)));
			PT->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); PT->SetVerticalAlignment(VAlign_Center);
			const FName PickId = Id;
			UTextBlock* QtyT = MakeText(FString::Printf(TEXT("  %d  "), Pend), 13, FLinearColor::White);
			StoreQtyTexts.Add(PickId, QtyT);
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("-"), FLinearColor(0.18f, 0.19f, 0.24f), [this, Ph, PickId]() { Ph->AdjustPendingSellQty(PickId, -1); if (TObjectPtr<UTextBlock>* X = StoreQtyTexts.Find(PickId)) { (*X)->SetText(FText::FromString(FString::Printf(TEXT("  %d  "), Ph->GetPendingSellQty(PickId)))); } }));
			Row->AddChildToHorizontalBox(QtyT)->SetVerticalAlignment(VAlign_Center);
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("+"), FLinearColor(0.18f, 0.19f, 0.24f), [this, Ph, PickId]() { Ph->AdjustPendingSellQty(PickId, +1); if (TObjectPtr<UTextBlock>* X = StoreQtyTexts.Find(PickId)) { (*X)->SetText(FText::FromString(FString::Printf(TEXT("  %d  "), Ph->GetPendingSellQty(PickId)))); } }));
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Add"), FLinearColor(0.2f, 0.45f, 0.28f), [this, Ph, PickId]() { Ph->AddSellToCart(PickId); UpdateStoreCartText(); }))->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
			CardVB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

			StoreScroll->AddChild(CardB);
			AddGap();
		}
		return;
	}

	// --- Catalogus (kopen) ---
	for (const FName& Id : Store->GetSupplierCategory(Cat))
	{
		const int32 Price = Store->GetCatalogPriceCents(Id);
		const int32 Pend = Ph->GetPendingQty(Id);

		UBorder* CardB = WidgetTree->ConstructWidget<UBorder>();
		CardB->SetBrush(RoundedBrush(FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 8.f));
		CardB->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
		UVerticalBox* CardVB = WidgetTree->ConstructWidget<UVerticalBox>();
		CardB->SetContent(CardVB);
		// Titel (kort) + beschrijving eronder.
		CardVB->AddChildToVerticalBox(MakeText(Store->GetCatalogName(Id).ToString(), 14, FLinearColor(0.95f, 0.97f, 1.f)));
		const FString DescStr = Store->GetCatalogDesc(Id).ToString();
		if (!DescStr.IsEmpty())
		{
			UTextBlock* Desc = MakeText(DescStr, 10, FLinearColor(0.62f, 0.66f, 0.76f));
			Desc->SetAutoWrapText(true);
			CardVB->AddChildToVerticalBox(Desc);
		}

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* PT = Row->AddChildToHorizontalBox(MakeText(FString::Printf(TEXT("EUR %.2f"), Price / 100.f), 13, FLinearColor(0.7f, 1.f, 0.7f)));
		PT->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); PT->SetVerticalAlignment(VAlign_Center);
		const FName PickId = Id;
		UTextBlock* QtyT = MakeText(FString::Printf(TEXT("  %d  "), Pend), 13, FLinearColor::White);
		StoreQtyTexts.Add(PickId, QtyT);
		Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("-"), FLinearColor(0.18f, 0.19f, 0.24f), [this, Ph, PickId]() { Ph->AdjustPendingQty(PickId, -1); if (TObjectPtr<UTextBlock>* X = StoreQtyTexts.Find(PickId)) { (*X)->SetText(FText::FromString(FString::Printf(TEXT("  %d  "), Ph->GetPendingQty(PickId)))); } }));
		Row->AddChildToHorizontalBox(QtyT)->SetVerticalAlignment(VAlign_Center);
		Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("+"), FLinearColor(0.18f, 0.19f, 0.24f), [this, Ph, PickId]() { Ph->AdjustPendingQty(PickId, +1); if (TObjectPtr<UTextBlock>* X = StoreQtyTexts.Find(PickId)) { (*X)->SetText(FText::FromString(FString::Printf(TEXT("  %d  "), Ph->GetPendingQty(PickId)))); } }));
		Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Add"), FLinearColor(0.2f, 0.4f, 0.55f), [this, Ph, PickId]() { Ph->AddToCart(PickId); UpdateStoreCartText(); }))->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		CardVB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

		StoreScroll->AddChild(CardB);
		AddGap();
	}
}

UWidget* UPhoneWidget::MakeAppCell(int32 AppIndex, const FString& Name, WeedUI::EIcon Icon, const FLinearColor& Col)
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
	// Flat icoon in plaats van een letter.
	USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>();
	IcoSz->SetWidthOverride(34.f); IcoSz->SetHeightOverride(34.f);
	IcoSz->SetContent(WeedUI::Icon(WidgetTree, Icon, 34.f, FLinearColor::White));
	Btn->SetContent(IcoSz);

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
	TimeText->SetClipping(EWidgetClipping::ClipToBounds);
	LevelText->SetClipping(EWidgetClipping::ClipToBounds);
	CashText->SetClipping(EWidgetClipping::ClipToBounds);
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

	// Ronde zwarte home-knop onderaan (zoals een telefoon-home-knop).
	UWeedActionButton* HomeBtn = WidgetTree->ConstructWidget<UWeedActionButton>();
	HomeBtn->OnClicked.AddDynamic(HomeBtn, &UWeedActionButton::Handle);
	HomeBtn->OnAction.BindLambda([this](int32, int32) { if (Phone.IsValid()) { Phone->GoHome(); } });
	{
		FButtonStyle HS;
		HS.Normal = RoundedBrush(FLinearColor(0.02f, 0.02f, 0.03f, 1.f), 24.f);
		HS.Hovered = RoundedBrush(FLinearColor(0.12f, 0.13f, 0.16f, 1.f), 24.f);
		HS.Pressed = RoundedBrush(FLinearColor(0.0f, 0.0f, 0.0f, 1.f), 24.f);
		HS.Normal.OutlineSettings.Color = FSlateColor(FLinearColor(0.5f, 0.5f, 0.55f, 0.9f));
		HS.Normal.OutlineSettings.Width = 2.f;
		HS.Hovered.OutlineSettings = HS.Normal.OutlineSettings;
		HS.Pressed.OutlineSettings = HS.Normal.OutlineSettings;
		HomeBtn->SetStyle(HS);
	}
	// Klein huisje in de knop.
	{
		USizeBox* Hi = WidgetTree->ConstructWidget<USizeBox>();
		Hi->SetWidthOverride(20.f); Hi->SetHeightOverride(20.f);
		Hi->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Home, 20.f, FLinearColor(0.85f, 0.87f, 0.95f)));
		HomeBtn->SetContent(Hi);
	}
	USizeBox* HbSz = WidgetTree->ConstructWidget<USizeBox>();
	HbSz->SetWidthOverride(48.f); HbSz->SetHeightOverride(48.f);
	HbSz->SetContent(HomeBtn);
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
		for (int32 i = 0; i < GNumApps; ++i)
		{
			UUniformGridSlot* GSlot = Grid->AddChildToUniformGrid(MakeAppCell(i, GAppName[i], GAppIcon[i], GAppCol[i]), i / 3, i % 3);
			GSlot->SetHorizontalAlignment(HAlign_Center);
		}
		UVerticalBoxSlot* GridSlot = ContentBox->AddChildToVerticalBox(Grid);
		GridSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		return;
	}

	const int32 App = FMath::Clamp(Phone->GetTab(), 0, GNumApps - 1);

	// App-header: back-knop + titel (+ rechtsboven de Packages-knop in de Suppliers-app).
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	// Linksboven: altijd een Back-knop (terug naar het home-scherm).
	Header->AddChildToHorizontalBox(MakeButton(TEXT("< Back"), 1, 0, FLinearColor(0.2f, 0.3f, 0.45f)));
	UTextBlock* TitleText = MakeText(GAppName[App], 15, FLinearColor(0.9f, 0.95f, 1.f));
	TitleText->SetClipping(EWidgetClipping::ClipToBounds);
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(TitleText);
	TitleSlot->SetPadding(FMargin(10.f, 4.f, 6.f, 0.f));
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	if (App == 1) // Suppliers: Packages-knop rechtsboven, naast de titel.
	{
		const int32 PkgN = Phone->GetPendingCount();
		const FLinearColor PkgCol(0.40f, 0.30f, 0.52f); // paars, voor onderscheid t.o.v. de blauwe Back

		StorePackagesToggle = WidgetTree->ConstructWidget<UWeedActionButton>();
		StorePackagesToggle->OnClicked.AddDynamic(StorePackagesToggle, &UWeedActionButton::Handle);
		StorePackagesToggle->OnAction.BindLambda([this](int32, int32) { bPackagesView = !bPackagesView; bCartView = false; LastPkgSig = -1; RefreshStore(); });
		FButtonStyle PS;
		PS.Normal = RoundedBrush(PkgCol, 9.f);
		PS.Hovered = RoundedBrush(PkgCol * 1.3f, 9.f);
		PS.Pressed = RoundedBrush(PkgCol * 0.8f, 9.f);
		PS.NormalPadding = FMargin(8.f, 4.f); PS.PressedPadding = FMargin(8.f, 4.f);
		StorePackagesToggle->SetStyle(PS);

		// Eén vast label dat we alleen van tekst veranderen + expliciet gecentreerd in de knop.
		StorePackagesLabel = MakeText(bPackagesView ? TEXT("Shop") : FString::Printf(TEXT("Packages (%d)"), PkgN), 11, FLinearColor::White, true);
		StorePackagesToggle->SetContent(StorePackagesLabel);
		if (UButtonSlot* BSlot = Cast<UButtonSlot>(StorePackagesLabel->Slot))
		{
			BSlot->SetHorizontalAlignment(HAlign_Center);
			BSlot->SetVerticalAlignment(VAlign_Center);
		}
		UHorizontalBoxSlot* PkgS = Header->AddChildToHorizontalBox(StorePackagesToggle);
		PkgS->SetVerticalAlignment(VAlign_Center);
	}
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
					FString NameStr = Name.ToString();
					if (NameStr.Len() > 22) { NameStr = NameStr.Left(21) + TEXT("."); }
					UTextBlock* T = MakeText(FString::Printf(TEXT("%s   EUR %.2f"), *NameStr, Cost / 100.f), 12,
						bPurchased ? FLinearColor::Gray : (bAvailable ? FLinearColor::White : FLinearColor(0.8f, 0.55f, 0.55f)));
					T->SetClipping(EWidgetClipping::ClipToBounds);
					UHorizontalBoxSlot* L = Row->AddChildToHorizontalBox(T);
					L->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					L->SetVerticalAlignment(VAlign_Center);
					L->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

					// Vaste-breedte rechtervak: Buy-knop of een status-label (overlapt nooit).
					USizeBox* RB = WidgetTree->ConstructWidget<USizeBox>();
					RB->SetWidthOverride(64.f);
					RB->SetHeightOverride(28.f);
					if (!bPurchased && bAvailable)
					{
						RB->SetContent(MakeButton(TEXT("Buy"), 3, idx, FLinearColor(0.2f, 0.5f, 0.28f)));
					}
					else
					{
						RB->SetContent(MakeText(bPurchased ? TEXT("owned") : TEXT("locked"), 11, FLinearColor::Gray, true));
					}
					UHorizontalBoxSlot* RS2 = Row->AddChildToHorizontalBox(RB);
					RS2->SetVerticalAlignment(VAlign_Center);

					UVerticalBoxSlot* RS = ContentBox->AddChildToVerticalBox(Row);
					RS->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f));
				}
				if (++idx >= 10) { break; }
			}
		}
	}
	else if (App == 1) // Suppliers -> webshop ín de telefoon
	{
		BuildStoreApp(ContentBox);
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
		BuildSettingsApp();
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
	case 0: Phone->OpenApp(Param); bCartView = false; break;
	case 1: Phone->GoHome(); bCartView = false; break;
	case 2: Phone->Toggle(); break;
	case 3: Phone->DoAction(Param); break;     // koop upgrade
	case 5: Phone->DoAction(0); break;         // accept bericht
	case 6: Phone->DoAction(1); break;         // decline bericht
	default: break;
	}
	// Geen volledige herbouw hier: dat gaf een flash. App-wissel/home herbouwt vanzelf via de
	// change-detectie in NativeTick; in-place acties (kopen/berichten) hoeven niet te herbouwen.
}

void UPhoneWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	if (!Phone.IsValid())
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// De widget zelf blijft altijd zichtbaar (zodat hij blijft ticken); we tonen/verbergen het frame.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	const bool bOpen = Phone->IsOpen();
	if (Frame) { Frame->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bOpen) { return; }

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (GS)
	{
		if (CashText && GS->GetEconomy())
		{
			CashText->SetText(FText::FromString(FString::Printf(TEXT("C %s  B %s"),
				*CompactEuros(GS->GetEconomy()->GetBalanceEuros()), *CompactEuros(GS->GetEconomy()->GetBankEuros()))));
		}
		if (TimeText && GS->GetDayCycle())
		{
			const float Hour = GS->GetDayCycle()->GetClockHour();
			const int32 H = FMath::Clamp((int32)Hour, 0, 23);
			const int32 M = FMath::Clamp((int32)((Hour - H) * 60.f), 0, 59);
			TimeText->SetText(FText::FromString(FString::Printf(TEXT("Day %d  %02d:%02d"),
				GS->GetDayCycle()->GetDayNumber(), H, M)));
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

	// Suppliers-app open: houd de Packages-tab levend (voortgang/ETA) en het knop-badge bij.
	if (!bHome && App == 1)
	{
		const int32 PkgN = Phone->GetPendingCount();
		if (StorePackagesLabel && !bPackagesView)
		{
			StorePackagesLabel->SetText(FText::FromString(FString::Printf(TEXT("Packages (%d)"), PkgN)));
		}
		if (bPackagesView)
		{
			const int32 Sig = PackagesSignature();
			if (Sig != LastPkgSig) { LastPkgSig = Sig; FillStoreList(); }
			else { UpdatePackagesLive(); }
		}
	}
}
