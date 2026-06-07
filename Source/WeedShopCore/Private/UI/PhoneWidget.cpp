#include "UI/PhoneWidget.h"

#include "Phone/PhoneClientComponent.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"
#include "World/HeatComponent.h"
#include "Progression/LevelComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Cultivation/GrowPlant.h"
#include "Cultivation/PotTypes.h"
#include "EngineUtils.h"
#include "Customer/CustomerBase.h" // afspraak-balk in de chat
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
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "World/DayNightController.h"
#include "World/CityGenerator.h"
#include "UI/MapWidget.h"
#include "Phone/PhoneClientComponent.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "UI/WeedUiStyle.h"

namespace
{
	// (Bank-app op de telefoon komt pas terug na een telefoon-upgrade; bankieren gaat nu via de ATM.)
	constexpr int32 GNumApps = 11;
	const TCHAR* GAppName[GNumApps] = { TEXT("Upgrades"), TEXT("Grow shop"), TEXT("Contacts"), TEXT("Messages"), TEXT("Settings"), TEXT("Map"), TEXT("Sell"), TEXT("Supplies"), TEXT("Packages"), TEXT("Bank"), TEXT("Hash Lab") };
	const WeedUI::EIcon GAppIcon[GNumApps] = { WeedUI::EIcon::Upgrade, WeedUI::EIcon::Leaf, WeedUI::EIcon::Person, WeedUI::EIcon::Message, WeedUI::EIcon::Gear, WeedUI::EIcon::Map, WeedUI::EIcon::Coin, WeedUI::EIcon::Shop, WeedUI::EIcon::Shop, WeedUI::EIcon::Coin, WeedUI::EIcon::Flame };
	// Eigen icoon per app (PNG-sleutel); valt terug op GAppIcon als het PNG ontbreekt.
	const TCHAR* GAppKey[GNumApps] = { TEXT("ui_upgrade"), TEXT("ui_leaf"), TEXT("ui_person"), TEXT("ui_message"), TEXT("ui_gear"), TEXT("ui_map"), TEXT("ui_sell"), TEXT("ui_shop"), TEXT("ui_package"), TEXT("ui_bank"), TEXT("ui_hash") };
	const FLinearColor GAppCol[GNumApps] = {
		FLinearColor(0.45f, 0.35f, 0.85f), FLinearColor(0.18f, 0.55f, 0.30f), FLinearColor(0.20f, 0.50f, 0.80f),
		FLinearColor(0.90f, 0.55f, 0.20f), FLinearColor(0.40f, 0.42f, 0.48f), FLinearColor(0.18f, 0.62f, 0.58f),
		FLinearColor(0.85f, 0.65f, 0.20f), FLinearColor(0.30f, 0.50f, 0.70f), FLinearColor(0.55f, 0.40f, 0.80f),
		FLinearColor(0.16f, 0.55f, 0.95f), FLinearColor(0.80f, 0.45f, 0.20f),
	};
	constexpr int32 GGrowApp = 1;
	constexpr int32 GSellApp = 6;
	constexpr int32 GSuppliesApp = 7;
	constexpr int32 GPackagesApp = 8;
	constexpr int32 GHashApp = 10;
	constexpr int32 GBankApp = 9;

	// Korte naam per categorie (0..6).
	const TCHAR* CatName(int32 Cat)
	{
		switch (Cat)
		{
		case 0: return TEXT("Seeds");
		case 1: return TEXT("Pots");
		case 2: return TEXT("Drying");
		case 3: return TEXT("Packing");
		case 4: return TEXT("Papers");
		case 5: return TEXT("Soil");
		case 6: return TEXT("Water");
		case 7: return TEXT("Furniture");
		case 8: return TEXT("Grow Upg.");
		case 9: return TEXT("Care");
		case 10: return TEXT("Hash");
		default: return TEXT("?");
		}
	}

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

	// Categorie-knoppen. De 'Test'-tab (dag/nacht-switch e.d.) alleen in dev-modes (Sandbox/Testing), niet in een normaal spel.
	SettingsTabBtns.Reset();
	const AWeedShopGameState* GSt = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const bool bDevMode = GSt && GSt->IsFreeBuild();
	if (!bDevMode && SettingsCat != 0) { SettingsCat = 0; }
	static const TCHAR* CatNames[2] = { TEXT("Status"), TEXT("Test") };
	const int32 NumTabs = bDevMode ? 2 : 1;
	UHorizontalBox* Cats = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 i = 0; i < NumTabs; ++i)
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

	if (SettingsCat == 1) // Test-tools (dag/nacht snel wisselen voor testing)
	{
		BodyRow(MakeText(TEXT("Testing"), 14, FLinearColor(0.7f, 0.85f, 1.f)), FMargin(0.f, 0.f, 0.f, 2.f));
		if (GS && GS->GetDayCycle())
		{
			const float H = GS->GetDayCycle()->GetClockHour();
			BodyRow(MakeText(FString::Printf(TEXT("Now: %02d:%02d  (%s)"), (int32)H, (int32)((H - (int32)H) * 60.f),
				GS->GetDayCycle()->IsNight() ? TEXT("night") : TEXT("day")), 11, FLinearColor(0.65f, 0.7f, 0.8f)), FMargin(0.f, 0.f, 0.f, 8.f));
		}
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		UWeedActionButton* DayB = MakeActionBtn(TEXT("Set Day"), FLinearColor(0.85f, 0.7f, 0.2f),
			[this]() { if (Phone.IsValid()) { Phone->RequestSetDayNight(false); } }, 13);
		UWeedActionButton* NightB = MakeActionBtn(TEXT("Set Night"), FLinearColor(0.25f, 0.3f, 0.55f),
			[this]() { if (Phone.IsValid()) { Phone->RequestSetDayNight(true); } }, 13);
		Btns->AddChildToHorizontalBox(DayB)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Btns->AddChildToHorizontalBox(NightB)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BodyRow(Btns, FMargin(0.f, 0.f, 0.f, 8.f));

		// --- Live light-tuning (stuurt de lokale DayNightController direct aan; geen restart nodig) ---
		LMoon = LSun = LSkyN = LSkyD = LPitch = LLamp = LExp = nullptr;
		BodyRow(MakeText(TEXT("Lighting (live)"), 14, FLinearColor(0.7f, 0.85f, 1.f)), FMargin(0.f, 0.f, 0.f, 2.f));
		ADayNightController* DN = ADayNightController::GetLocal(GetWorld());
		const float Moon = DN ? DN->MoonIntensity : 0.65f;
		const float Sun  = DN ? DN->SunIntensity  : 6.5f;
		const float SkN  = DN ? DN->SkyNight       : 0.85f;
		const float SkD  = DN ? DN->SkyDay         : 1.0f;
		const float Pit  = DN ? DN->MoonPitch      : -52.f;
		const float Lmp  = DN ? DN->LampIntensity  : 28000.f;
		const float Exp  = DN ? DN->ExposureBias   : 9.f;
		// Norm = (waarde - min) / (max - min) per regelaar.
		AddLightSlider(TEXT("Moon (night)"),  Moon / 3.f,            LMoon,  LMoonV);
		AddLightSlider(TEXT("Sun (day)"),     Sun / 12.f,            LSun,   LSunV);
		AddLightSlider(TEXT("Sky night"),     SkN / 2.f,             LSkyN,  LSkyNV);
		AddLightSlider(TEXT("Sky day"),       SkD / 2.f,             LSkyD,  LSkyDV);
		AddLightSlider(TEXT("Moon angle"),    (Pit + 90.f) / 90.f,   LPitch, LPitchV);
		AddLightSlider(TEXT("Street lamps"),  Lmp / 80000.f,         LLamp,  LLampV);
		AddLightSlider(TEXT("Exposure"),      Exp / 16.f,            LExp,   LExpV);
		ApplyLightSliders(); // labels meteen vullen met de echte waardes

		UWeedActionButton* SaveB = MakeActionBtn(TEXT("Save light config"), FLinearColor(0.22f, 0.5f, 0.32f),
			[this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SaveLightConfig(); } }, 12);
		BodyRow(SaveB, FMargin(0.f, 8.f, 0.f, 0.f));
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

USlider* UPhoneWidget::AddLightSlider(const FString& Label, float Norm, TObjectPtr<USlider>& OutS, TObjectPtr<UTextBlock>& OutV)
{
	USlider* Slider = WidgetTree->ConstructWidget<USlider>();
	Slider->SetMinValue(0.f); Slider->SetMaxValue(1.f);
	Slider->SetValue(FMath::Clamp(Norm, 0.f, 1.f));
	Slider->SetSliderBarColor(FLinearColor(0.18f, 0.2f, 0.27f));
	Slider->SetSliderHandleColor(FLinearColor(0.55f, 0.8f, 1.f));
	OutS = Slider;

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(MakeText(Label, 12, FLinearColor(0.82f, 0.86f, 0.95f)));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	USizeBox* SSz = WidgetTree->ConstructWidget<USizeBox>();
	SSz->SetWidthOverride(150.f); SSz->SetHeightOverride(18.f); SSz->SetContent(Slider);
	Row->AddChildToHorizontalBox(SSz);
	OutV = MakeText(TEXT(""), 12, FLinearColor::White, true);
	USizeBox* VSz = WidgetTree->ConstructWidget<USizeBox>();
	VSz->SetWidthOverride(56.f); VSz->SetContent(OutV);
	Row->AddChildToHorizontalBox(VSz);

	if (SettingsBody) { SettingsBody->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f)); }
	return Slider;
}

void UPhoneWidget::ApplyLightSliders()
{
	ADayNightController* DN = ADayNightController::GetLocal(GetWorld());
	if (!DN || !LMoon) { return; }
	DN->MoonIntensity = LMoon->GetValue()  * 3.f;
	DN->SunIntensity  = LSun  ? LSun->GetValue()  * 12.f    : DN->SunIntensity;
	DN->SkyNight      = LSkyN ? LSkyN->GetValue() * 2.f     : DN->SkyNight;
	DN->SkyDay        = LSkyD ? LSkyD->GetValue() * 2.f     : DN->SkyDay;
	DN->MoonPitch     = LPitch? LPitch->GetValue() * 90.f - 90.f : DN->MoonPitch;
	DN->LampIntensity = LLamp ? LLamp->GetValue() * 80000.f : DN->LampIntensity;
	DN->ExposureBias  = LExp  ? LExp->GetValue()  * 16.f    : DN->ExposureBias;

	if (LMoonV)  { LMoonV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->MoonIntensity))); }
	if (LSunV)   { LSunV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->SunIntensity))); }
	if (LSkyNV)  { LSkyNV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->SkyNight))); }
	if (LSkyDV)  { LSkyDV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->SkyDay))); }
	if (LPitchV) { LPitchV->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), DN->MoonPitch))); }
	if (LLampV)  { LLampV->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), DN->LampIntensity))); }
	if (LExpV)   { LExpV->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), DN->ExposureBias))); }
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
		if (bSellApp)
		{
			// Sell-app: puur de verkoopwaarde van de cart (altijd "+", je ontvangt geld).
			const int32 Sell = Phone->GetCartSellCents();
			StoreCartText->SetText(FText::FromString(FString::Printf(TEXT("Cart %d   +EUR %.2f"), Phone->GetCartNumLines(), Sell / 100.f)));
			return;
		}
		// Toon het NETTO bedrag (koop + bezorging - verkoop). Negatief = je ontvangt geld (+).
		const int32 Net = Phone->GetCartNetCents(DeliveryOpt);
		const FString Amt = (Net < 0)
			? FString::Printf(TEXT("+EUR %.2f"), -Net / 100.f)
			: FString::Printf(TEXT("EUR %.2f"), Net / 100.f);
		StoreCartText->SetText(FText::FromString(FString::Printf(TEXT("Cart %d   %s"), Phone->GetCartNumLines(), *Amt)));
	}
}

int32 UPhoneWidget::MessagesSignature() const
{
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return 0; }
	const TArray<FPhoneMessage>& Msgs = Con->GetMessages();
	int32 Sig = Msgs.Num() * 1000003;
	for (const FPhoneMessage& M : Msgs) { Sig = Sig * 31 + (int32)M.Status + (M.bFromMe ? 5 : 0); }
	return Sig;
}

void UPhoneWidget::BuildChatApp()
{
	ApptBar = nullptr; ApptBarLabel = nullptr; ApptBarContact = NAME_None; // alleen geldig in een actieve-afspraak-thread
	if (!Phone.IsValid()) { return; }
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { AddInfoRow(TEXT("No messages."), FLinearColor::Gray); return; }
	const TArray<FPhoneMessage>& Msgs = Con->GetMessages(); // nieuwste eerst

	// ---- Gesprekkenlijst (geen chat open) ----
	if (OpenChatContact.IsNone())
	{
		ProposeMins = -1; // tijd-keuze reset zodra je de thread verlaat
		// Unieke contacten in volgorde van nieuwste bericht.
		TArray<FName> Order;
		for (const FPhoneMessage& M : Msgs) { Order.AddUnique(M.FromContactId); }
		if (Order.Num() == 0) { AddInfoRow(TEXT("No messages yet."), FLinearColor::Gray, 13); return; }

		UScrollBox* List = WidgetTree->ConstructWidget<UScrollBox>();
		UVerticalBoxSlot* LS = ContentBox->AddChildToVerticalBox(List);
		LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		for (const FName& Cid : Order)
		{
			// Laatste bericht + of er een open afspraak is.
			FString LastBody; FText Name; bool bOpen = false;
			for (const FPhoneMessage& M : Msgs)
			{
				if (M.FromContactId != Cid) { continue; }
				if (LastBody.IsEmpty()) { LastBody = (M.bFromMe ? TEXT("You: ") : TEXT("")) + M.Body.ToString(); Name = M.SenderName; }
				if (M.Status == 0 && !M.bFromMe) { bOpen = true; }
			}
			if (LastBody.Len() > 30) { LastBody = LastBody.Left(29) + TEXT("."); }

			UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
			Card->SetBrush(RoundedBrush(bOpen ? FLinearColor(0.12f, 0.17f, 0.13f, 0.97f) : FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 8.f));
			Card->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			Card->SetContent(Row);
			UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
			Info->AddChildToVerticalBox(MakeText(Name.ToString(), 14, FLinearColor(0.95f, 0.97f, 1.f)));
			UTextBlock* Prev = MakeText(LastBody, 10, FLinearColor(0.62f, 0.66f, 0.76f));
			Prev->SetClipping(EWidgetClipping::ClipToBounds);
			Info->AddChildToVerticalBox(Prev);
			UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(Info);
			IS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); IS->SetVerticalAlignment(VAlign_Center);
			if (bOpen) { Row->AddChildToHorizontalBox(MakeText(TEXT("NEW"), 11, FLinearColor(0.5f, 1.f, 0.6f)))->SetVerticalAlignment(VAlign_Center); }
			const FName Pick = Cid;
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Open"), FLinearColor(0.2f, 0.4f, 0.55f),
				[this, Pick]() { OpenChatContact = Pick; MarkDirty(); }, 11))->SetVerticalAlignment(VAlign_Center);
			List->AddChild(Card);
			List->AddChild(MakeText(TEXT(""), 4, FLinearColor::Transparent));
		}
		return;
	}

	// ---- Open chat-thread ----
	Phone->MarkChatSeen(OpenChatContact); // berichten van deze persoon zijn nu gelezen -> notificatie-bubble weg
	FText ContactName = FText::FromName(OpenChatContact);
	for (const FPhoneContact& C : Con->GetContacts()) { if (C.ContactId == OpenChatContact) { ContactName = C.DisplayName; break; } }

	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	Head->AddChildToHorizontalBox(MakeActionBtn(TEXT("< Chats"), FLinearColor(0.2f, 0.3f, 0.45f),
		[this]() { OpenChatContact = NAME_None; MarkDirty(); }, 11))->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* NS = Head->AddChildToHorizontalBox(MakeText(ContactName.ToString(), 15, FLinearColor(0.92f, 0.95f, 1.f)));
	NS->SetVerticalAlignment(VAlign_Center); NS->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
	ContentBox->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Loopt er een afspraak met deze persoon? Toon een aftellende balk (loopt naar 0 tot 'ie opgeeft).
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		if (It->NpcId != OpenChatContact || !It->HasActiveAppointment()) { continue; }
		UBorder* Box = WidgetTree->ConstructWidget<UBorder>();
		Box->SetBrush(RoundedBrush(FLinearColor(0.10f, 0.13f, 0.10f, 0.95f), 8.f));
		Box->SetPadding(FMargin(8.f, 5.f, 8.f, 6.f));
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		Box->SetContent(VB);
		ApptBarLabel = MakeText(TEXT("Waiting..."), 11, FLinearColor(0.8f, 0.9f, 0.8f));
		VB->AddChildToVerticalBox(ApptBarLabel);
		ApptBar = WidgetTree->ConstructWidget<UProgressBar>();
		ApptBar->SetPercent(It->GetApptFraction());
		VB->AddChildToVerticalBox(ApptBar)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		ApptBarContact = OpenChatContact;
		ContentBox->AddChildToVerticalBox(Box)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		break;
	}

	// Berichten chronologisch (oudste boven). Msgs is nieuwste-eerst -> achterstevoren itereren.
	UScrollBox* Thread = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* TS = ContentBox->AddChildToVerticalBox(Thread);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	bool bAny = false; bool bHasOpen = false;
	for (int32 i = Msgs.Num() - 1; i >= 0; --i)
	{
		const FPhoneMessage& M = Msgs[i];
		if (M.FromContactId != OpenChatContact) { continue; }
		bAny = true;
		if (M.Status == 0 && !M.bFromMe) { bHasOpen = true; }

		FString Body = M.Body.ToString();
		if (!M.bFromMe && M.Status == 1) { Body += TEXT("  (accepted)"); }
		else if (!M.bFromMe && M.Status == 2) { Body += TEXT("  (declined)"); }

		UBorder* Bub = WidgetTree->ConstructWidget<UBorder>();
		Bub->SetBrush(RoundedBrush(M.bFromMe ? FLinearColor(0.16f, 0.35f, 0.22f, 0.97f) : FLinearColor(0.16f, 0.18f, 0.24f, 0.97f), 10.f));
		Bub->SetPadding(FMargin(9.f, 6.f, 9.f, 6.f));
		UTextBlock* BodyT = MakeText(Body, 12, FLinearColor(0.95f, 0.97f, 1.f));
		BodyT->SetAutoWrapText(true);
		Bub->SetContent(BodyT);

		USizeBox* Cap = WidgetTree->ConstructWidget<USizeBox>();
		Cap->SetMaxDesiredWidth(210.f);
		Cap->SetContent(Bub);

		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (M.bFromMe)
		{
			UHorizontalBoxSlot* Sp = Line->AddChildToHorizontalBox(MakeText(TEXT(""), 1, FLinearColor::Transparent));
			Sp->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Line->AddChildToHorizontalBox(Cap);
		}
		else
		{
			Line->AddChildToHorizontalBox(Cap);
			UHorizontalBoxSlot* Sp = Line->AddChildToHorizontalBox(MakeText(TEXT(""), 1, FLinearColor::Transparent));
			Sp->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		Thread->AddChild(Line);
		Thread->AddChild(MakeText(TEXT(""), 4, FLinearColor::Transparent));
	}
	if (!bAny) { Thread->AddChild(MakeText(TEXT("No messages with this contact yet."), 12, FLinearColor::Gray)); }

	// Open afspraak? -> Accept/Decline onderaan.
	if (bHasOpen)
	{
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		const FName Pick = OpenChatContact;
		UHorizontalBoxSlot* AS = Btns->AddChildToHorizontalBox(MakeActionBtn(TEXT("Accept"), FLinearColor(0.2f, 0.5f, 0.28f),
			[this, Pick]() { if (Phone.IsValid()) { Phone->RespondChat(Pick, true); } MarkDirty(); }, 13));
		AS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); AS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
		UHorizontalBoxSlot* DS = Btns->AddChildToHorizontalBox(MakeActionBtn(TEXT("Decline"), FLinearColor(0.5f, 0.28f, 0.2f),
			[this, Pick]() { if (Phone.IsValid()) { Phone->RespondChat(Pick, false); } MarkDirty(); }, 13));
		DS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); DS->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		ContentBox->AddChildToVerticalBox(Btns)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

		// Of kies zelf een tijd (per kwartier). Ze gaan altijd akkoord, geen nadeel.
		ContentBox->AddChildToVerticalBox(MakeText(TEXT("Can't make it? Pick a time:"), 11, FLinearColor(0.7f, 0.75f, 0.85f)))
			->SetPadding(FMargin(0.f, 6.f, 0.f, 2.f));

		// Startwaarde: de tijd die de KLANT voorstelde (dan klik je vandaaruit +kwartier/+uur). Geen
		// afspraaktijd bekend -> de huidige kloktijd. Afgerond op een kwartier.
		if (ProposeMins < 0)
		{
			float ApptTod = -1.f;
			for (const FPhoneMessage& M : Msgs)
			{
				if (M.FromContactId == OpenChatContact && M.Status == 0 && !M.bFromMe && M.AppointmentTimeOfDay >= 0.f)
				{ ApptTod = M.AppointmentTimeOfDay; break; }
			}
			float Now = 0.f, Length = 1800.f; Con->GetCycleTime(Now, Length); if (Length <= 0.f) { Length = 1800.f; }
			const float Src = (ApptTod >= 0.f) ? ApptTod : Now;
			const float Frac = FMath::Fmod(FMath::Max(0.f, Src), Length) / Length;
			ProposeMins = (FMath::RoundToInt(Frac * 1440.f / 15.f) * 15) % 1440;
		}
		auto Step = [this](int32 Delta) { ProposeMins = ((ProposeMins + Delta) % 1440 + 1440) % 1440; MarkDirty(); };

		UHorizontalBox* Stepper = WidgetTree->ConstructWidget<UHorizontalBox>();
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("-1h"), FLinearColor(0.22f, 0.28f, 0.4f), [Step]() { Step(-60); }, 13))->SetPadding(FMargin(0.f, 0.f, 3.f, 0.f));
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("-15m"), FLinearColor(0.22f, 0.28f, 0.4f), [Step]() { Step(-15); }, 13))->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		UTextBlock* Clock = MakeText(FString::Printf(TEXT("%02d:%02d"), ProposeMins / 60, ProposeMins % 60), 18, FLinearColor(0.95f, 0.97f, 1.f));
		UHorizontalBoxSlot* CS2 = Stepper->AddChildToHorizontalBox(Clock);
		CS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS2->SetHorizontalAlignment(HAlign_Center); CS2->SetVerticalAlignment(VAlign_Center);
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("+15m"), FLinearColor(0.22f, 0.28f, 0.4f), [Step]() { Step(15); }, 13))->SetPadding(FMargin(6.f, 0.f, 3.f, 0.f));
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("+1h"), FLinearColor(0.22f, 0.28f, 0.4f), [Step]() { Step(60); }, 13));
		ContentBox->AddChildToVerticalBox(Stepper)->SetPadding(FMargin(0.f, 2.f, 0.f, 4.f));

		const int32 Mins = ProposeMins;
		ContentBox->AddChildToVerticalBox(MakeActionBtn(FString::Printf(TEXT("Propose %02d:%02d"), Mins / 60, Mins % 60), FLinearColor(0.2f, 0.45f, 0.55f),
			[this, Pick, Mins]() { if (Phone.IsValid()) { Phone->ProposeChatTime(Pick, Mins); } ProposeMins = -1; MarkDirty(); }, 13));
	}
}

void UPhoneWidget::FillPackagesInto(UScrollBox* Scroll)
{
	if (!Scroll || !Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();
	Scroll->ClearChildren();
	PkgBars.Reset();
	PkgEtas.Reset();

	auto AddGap = [this, Scroll]() {
		UBorder* Gap = WidgetTree->ConstructWidget<UBorder>();
		Gap->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
		Gap->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		Scroll->AddChild(Gap);
	};

	const TArray<UPhoneClientComponent::FPendingDelivery>& Pend = Ph->GetPendingDeliveries();
	if (Pend.Num() == 0)
	{
		Scroll->AddChild(MakeText(TEXT("No packages on the way. Order from the Grow shop or Supplies and pick a delivery speed."), 12, FLinearColor::Gray));
		return;
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
				[this, Ph, OrderId]() { Ph->CancelDelivery(OrderId); LastPkgSig = -1; if (PackagesScroll) { FillPackagesInto(PackagesScroll); } }, 11));
		}
		CVB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 5.f, 0.f, 0.f));

		Scroll->AddChild(CardB);
		AddGap();
	}
}

void UPhoneWidget::BuildPackagesApp()
{
	PackagesScroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* PS = ContentBox->AddChildToVerticalBox(PackagesScroll);
	PS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LastPkgSig = PackagesSignature();
	FillPackagesInto(PackagesScroll);
}

void UPhoneWidget::BuildBankApp()
{
	if (!ContentBox || !Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* SS = ContentBox->AddChildToVerticalBox(Scroll);
	SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	auto AddRow = [this, Scroll](UWidget* W) { Scroll->AddChild(W); };

	// Nog niet ontgrendeld -> alleen de koop-knop voor de telefoon-upgrade. (Bij een fysieke ATM
	// mag je altijd bankieren, ook zonder upgrade.)
	if (!Ph->IsBankAppUnlocked() && !Ph->IsBankViaAtm())
	{
		AddRow(MakeText(TEXT("Mobile banking"), 16, FLinearColor(0.95f, 0.8f, 0.5f), false));
		UTextBlock* Desc = MakeText(TEXT("Upgrade your phone to bank anywhere - no ATM needed."), 12, FLinearColor(0.7f, 0.75f, 0.85f));
		Desc->SetAutoWrapText(true);
		AddRow(Desc);
		AddRow(MakeText(TEXT(""), 8, FLinearColor::Transparent));
		const int64 Cost = UPhoneClientComponent::PhoneUpgradeCostCents;
		AddRow(MakeActionBtn(FString::Printf(TEXT("Unlock  -  EUR %.0f"), Cost / 100.f),
			FLinearColor(0.16f, 0.5f, 0.85f), [this]() { if (Phone.IsValid()) { Phone->RequestBuyPhoneUpgrade(); } MarkDirty(); }, 14));
		return;
	}

	if (!Econ) { AddRow(MakeText(TEXT("Out of service."), 14, FLinearColor::Gray)); return; }

	// --- Balans-kaart (zoals een echte bank-app: groot saldo bovenaan) ---
	AddRow(MakeText(TEXT("BANK BALANCE"), 11, FLinearColor(0.5f, 0.7f, 0.95f), false));
	AddRow(MakeText(FString::Printf(TEXT("EUR %.2f"), Econ->GetBankEuros()), 26, FLinearColor(0.7f, 0.92f, 1.f), false));
	AddRow(MakeText(FString::Printf(TEXT("Cash to deposit:  EUR %.2f"), Econ->GetBalanceEuros()), 12, FLinearColor(0.7f, 0.72f, 0.78f)));
	AddRow(MakeText(TEXT(""), 10, FLinearColor::Transparent));

	// --- Storten (cash -> bank), bescheiden presets + max ---
	AddRow(MakeText(FString::Printf(TEXT("Deposit   (%.0f%% tax)"), Econ->DepositTaxPct * 100.f), 13, FLinearColor(0.7f, 1.f, 0.75f)));
	{
		const int64 Amts[3] = { 10000, 50000, 100000 }; // EUR 100 / 500 / 1000
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < 3; ++i)
		{
			const int64 A = Amts[i];
			UWeedActionButton* B = MakeActionBtn(FString::Printf(TEXT("%lld"), (long long)(A / 100)), FLinearColor(0.18f, 0.42f, 0.30f),
				[this, A]() { if (Phone.IsValid()) { Phone->RequestDeposit(A); } }, 13);
			UHorizontalBoxSlot* BS = Btns->AddChildToHorizontalBox(B);
			BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
		}
		UWeedActionButton* Mx = MakeActionBtn(TEXT("Max"), FLinearColor(0.2f, 0.5f, 0.34f),
			[this]() { if (Phone.IsValid()) { Phone->RequestDeposit(-1); } }, 13);
		UHorizontalBoxSlot* MS = Btns->AddChildToHorizontalBox(Mx);
		MS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); MS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
		AddRow(Btns);
	}
	AddRow(MakeText(TEXT(""), 10, FLinearColor::Transparent));

	// --- Sturen naar co-op vriend, bescheiden presets ---
	AddRow(MakeText(FString::Printf(TEXT("Send to a friend   (%.0f%% fee)"), Econ->TransferFeePct * 100.f), 13, FLinearColor(0.7f, 0.85f, 1.f)));
	{
		const int64 Amts[3] = { 10000, 25000, 50000 }; // EUR 100 / 250 / 500
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 i = 0; i < 3; ++i)
		{
			const int64 A = Amts[i];
			UWeedActionButton* B = MakeActionBtn(FString::Printf(TEXT("%lld"), (long long)(A / 100)),
				FLinearColor(0.2f, 0.34f, 0.5f), [this, A]() { if (Phone.IsValid()) { Phone->RequestTransfer(A); } }, 13);
			UHorizontalBoxSlot* BS = Btns->AddChildToHorizontalBox(B);
			BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
		}
		AddRow(Btns);
	}
}

void UPhoneWidget::BuildStoreApp(UVerticalBox* Into)
{
	if (!Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();
	// Bij (her)openen van de Suppliers-app altijd standaard op de Shop-catalogus starten.
	bCartView = false;
	bPackagesView = false;
	StoreQtyTexts.Reset();
	StoreTabBtns.Reset();

	// Koop-categorieën (alleen de tabs van deze winkel-app). De Sell-app heeft géén koop-tabs.
	if (!bSellApp)
	{
		// Uniform grid: alle tabs even breed en netjes uitgelijnd (3 kolommen, dus 6 cats = 2 nette
		// rijen; 4 cats = 1 rij van 4). Geen "los hangende" tweede rij meer.
		const int32 Cols = (AppCats.Num() <= 4) ? FMath::Max(1, AppCats.Num()) : 3;
		UUniformGridPanel* Tabs = WidgetTree->ConstructWidget<UUniformGridPanel>();
		Tabs->SetSlotPadding(FMargin(3.f));
		for (int32 Idx = 0; Idx < AppCats.Num(); ++Idx)
		{
			const int32 Cat = AppCats[Idx];
			const FLinearColor Col = (Cat == Ph->GetSupplierCat()) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
			UWeedActionButton* Pill = MakeActionBtn(CatName(Cat), Col, [this, Ph, Cat]() { Ph->SetSupplierCat(Cat); bCartView = false; RefreshStore(); }, 11);
			UUniformGridSlot* GSlot = Tabs->AddChildToUniformGrid(Pill, Idx / Cols, Idx % Cols);
			GSlot->SetHorizontalAlignment(HAlign_Fill);
			GSlot->SetVerticalAlignment(VAlign_Fill);
			StoreTabBtns.Add(Pill);
		}
		Into->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

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
		const int32 TabCat = AppCats.IsValidIndex(i) ? AppCats[i] : i;
		const FLinearColor Col = (TabCat == Cat) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
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

void UPhoneWidget::UpdateApptBarLive()
{
	if (!ApptBar || ApptBarContact.IsNone() || !GetWorld()) { return; }
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		if (It->NpcId != ApptBarContact) { continue; }
		if (It->HasActiveAppointment())
		{
			const float Frac = It->GetApptFraction();
			ApptBar->SetPercent(Frac);
			ApptBar->SetFillColorAndOpacity(Frac < 0.2f ? FLinearColor(0.9f, 0.3f, 0.25f) : (Frac < 0.45f ? FLinearColor(0.9f, 0.7f, 0.25f) : FLinearColor(0.35f, 0.8f, 0.45f)));
			if (ApptBarLabel)
			{
				const int32 Left = FMath::CeilToInt(It->GetApptTimeLeft());
				ApptBarLabel->SetText(FText::FromString(FString::Printf(TEXT("Waiting at the door - leaves in %d:%02d"), Left / 60, Left % 60)));
			}
		}
		else
		{
			// Afspraak voorbij (deal of vertrokken) -> balk weghalen.
			ApptBar->SetPercent(0.f);
			if (ApptBarLabel) { ApptBarLabel->SetText(FText::FromString(TEXT("Gone."))); }
			ApptBar = nullptr; ApptBarLabel = nullptr; ApptBarContact = NAME_None;
		}
		return;
	}
	// NPC niet meer gevonden -> balk loslaten.
	ApptBar = nullptr; ApptBarLabel = nullptr; ApptBarContact = NAME_None;
}

void UPhoneWidget::FillPotUpgradesInto(UScrollBox* Scroll)
{
	if (!Scroll || !Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();
	Scroll->ClearChildren();

	Scroll->AddChild(MakeText(TEXT("Upgrade your placed pots. Upgrades stay with the pot (gone if you sell it)."), 11, FLinearColor(0.6f, 0.66f, 0.76f)));

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const int32 PlayerLvl = (GS && GS->GetLeveling()) ? GS->GetLeveling()->GetLevel() : 1;

	int32 PotCount = 0;
	if (UWorld* W = GetWorld())
	{
		const TArray<FPotUpgradeDef>& Ups = GetPotUpgrades();
		int32 PotNum = 0;
		for (TActorIterator<AGrowPlant> It(W); It; ++It)
		{
			AGrowPlant* Pot = *It;
			if (!Pot) { continue; }
			++PotNum; ++PotCount;
			TWeakObjectPtr<AGrowPlant> WPot = Pot;
			const FName Tier = Pot->GetPotTier();
			FPotDef Pd; const FString PotName = GetPotDef(Tier, Pd) ? Pd.DisplayName : TEXT("Pot");

			UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
			Card->SetBrush(WeedUI::Rounded(FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 8.f));
			Card->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
			UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
			Card->SetContent(VB);
			VB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("Pot %d   -   %s"), PotNum, *PotName), 14, FLinearColor(0.7f, 1.f, 0.7f)));

			for (int32 i = 0; i < Ups.Num(); ++i)
			{
				const int32 ui = i;
				const bool bOwned = Pot->HasPotUpgrade(i);
				const bool bTierOk = IsPotUpgradeAllowed(i, Tier);
				const bool bPrereqOk = (Ups[i].PrereqIndex < 0) || Pot->HasPotUpgrade(Ups[i].PrereqIndex);
				const bool bLevelOk = PlayerLvl >= Ups[i].MinPlayerLevel;
				const bool bBuyable = bTierOk && bPrereqOk && bLevelOk && !bOwned;
				const int32 Cost = GetPotUpgradeCost(i, Tier);

				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				const FString Label = FString::Printf(TEXT("%s  -  %s"), *Ups[i].DisplayName, *Ups[i].Desc);
				const FLinearColor TxtCol = bOwned ? FLinearColor(0.5f, 1.f, 0.5f) : (bBuyable ? FLinearColor(0.9f, 0.92f, 1.f) : FLinearColor(0.72f, 0.6f, 0.6f));
				UTextBlock* T = MakeText(Label, 11, TxtCol);
				T->SetClipping(EWidgetClipping::ClipToBounds);
				UHorizontalBoxSlot* L = Row->AddChildToHorizontalBox(T);
				L->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); L->SetVerticalAlignment(VAlign_Center); L->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

				USizeBox* RB = WidgetTree->ConstructWidget<USizeBox>();
				RB->SetWidthOverride(124.f); RB->SetHeightOverride(26.f);
				if (bOwned)
				{
					RB->SetContent(MakeText(TEXT("installed"), 11, FLinearColor(0.5f, 1.f, 0.5f), true));
				}
				else if (!bTierOk)
				{
					RB->SetContent(MakeText(TEXT("needs better pot"), 10, FLinearColor(1.f, 0.6f, 0.5f), true));
				}
				else if (!bPrereqOk)
				{
					RB->SetContent(MakeText(TEXT("prev. tier first"), 10, FLinearColor(1.f, 0.7f, 0.5f), true));
				}
				else if (!bLevelOk)
				{
					RB->SetContent(MakeText(FString::Printf(TEXT("Lvl %d"), Ups[i].MinPlayerLevel), 10, FLinearColor(1.f, 0.6f, 0.5f), true));
				}
				else
				{
					RB->SetContent(MakeActionBtn(FString::Printf(TEXT("Buy  EUR %.2f"), Cost / 100.f), FLinearColor(0.2f, 0.5f, 0.28f),
						[this, Ph, WPot, ui]() { if (AGrowPlant* P = WPot.Get()) { Ph->RequestPotUpgradeFor(P, ui); RefreshStore(); } }, 10));
				}
				UHorizontalBoxSlot* RS2 = Row->AddChildToHorizontalBox(RB); RS2->SetVerticalAlignment(VAlign_Center);
				VB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
			}

			Scroll->AddChild(Card);
			UBorder* Gap = WidgetTree->ConstructWidget<UBorder>();
			Gap->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
			Gap->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
			Scroll->AddChild(Gap);
		}
	}
	if (PotCount == 0)
	{
		Scroll->AddChild(MakeText(TEXT("No pots placed yet. Buy a pot (Pots tab) and place it first."), 12, FLinearColor::Gray));
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
		FillPackagesInto(StoreScroll);
		return;
	}

	// Pot-gear (cat 8) wordt nu een NORMALE koop-categorie: fysieke accessoires die je naast je pot zet.
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
		// Vaste voettekst. In de Sell-app: puur verkopen (geen bezorging/koop-opbouw).
		if (StoreFooter && bSellApp)
		{
			const int32 SellSub = Ph->GetCartSellCents();
			UTextBlock* TotT = MakeText(FString::Printf(TEXT("You receive: EUR %.2f"), SellSub / 100.f),
				15, FLinearColor(0.6f, 1.f, 0.65f));
			TotT->SetJustification(ETextJustify::Right);
			StoreFooter->AddChildToVerticalBox(TotT)->SetPadding(FMargin(0.f, 6.f, 4.f, 4.f));

			StoreFooter->AddChildToVerticalBox(MakeActionBtn(TEXT("SELL"), FLinearColor(0.2f, 0.55f, 0.27f),
				[this, Ph]() { Ph->Checkout(0); bCartView = false; RefreshStore(); }, 14));
		}
		// Koop-app cart: bezorgopties (op koopdeel) + netto-totaal (rechtsonder) + checkout.
		else if (StoreFooter)
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

				// Bezorg-adres: bij meerdere woningen kies je waar 't heen gaat. Standaard het huis waar je
				// nu binnen bent (groen). Anders je huidige/actieve woning.
				const TArray<int32>& OwnedH = Ph->GetOwnedHomes();
				if (OwnedH.Num() > 1)
				{
					const int32 CurTarget = Ph->ResolveDeliveryHome();
					StoreFooter->AddChildToVerticalBox(MakeText(TEXT("Bezorgen bij"), 11, FLinearColor(0.75f, 0.8f, 0.95f)))->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
					UHorizontalBox* HomesRow = WidgetTree->ConstructWidget<UHorizontalBox>();
					for (int32 HIdx : OwnedH)
					{
						const FLinearColor Col = (HIdx == CurTarget) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
						UWeedActionButton* HB = MakeActionBtn(Ph->GetHomeLabel(HIdx), Col,
							[this, HIdx]() { if (Phone.IsValid()) { Phone->SetDeliveryHome(HIdx); } RefreshStore(); }, 9);
						UHorizontalBoxSlot* HS = HomesRow->AddChildToHorizontalBox(HB);
						HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); HS->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
					}
					StoreFooter->AddChildToVerticalBox(HomesRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
				}
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

	if (bSellApp) // Sell-app — verkooplijst: aantal kiezen + Add naar de winkelwagen.
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
		const int32 ReqLvl = Store->RequiredLevelFor(Id);
		const int32 PlayerLvl = (GS && GS->GetLeveling()) ? GS->GetLeveling()->GetLevel() : 1;
		const bool bLocked = ReqLvl > PlayerLvl;

		UBorder* CardB = WidgetTree->ConstructWidget<UBorder>();
		CardB->SetBrush(RoundedBrush(bLocked ? FLinearColor(0.10f, 0.09f, 0.09f, 0.95f) : FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 8.f));
		CardB->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));

		// Icoon links + tekstkolom rechts (seeds: icoon van het zaad-item).
		UHorizontalBox* CardHB = WidgetTree->ConstructWidget<UHorizontalBox>();
		CardB->SetContent(CardHB);
		const FName IconId = UStoreComponent::IsSeedCategory(Cat) ? UStoreComponent::SeedItemId(Id) : Id;
		USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>();
		IcoSz->SetWidthOverride(40.f); IcoSz->SetHeightOverride(40.f);
		IcoSz->SetContent(WeedUI::ItemIcon(WidgetTree, IconId, 40.f));
		UHorizontalBoxSlot* IcoSlot = CardHB->AddChildToHorizontalBox(IcoSz);
		IcoSlot->SetVerticalAlignment(VAlign_Center); IcoSlot->SetPadding(FMargin(0.f, 0.f, 9.f, 0.f));

		UVerticalBox* CardVB = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBoxSlot* VBSlot = CardHB->AddChildToHorizontalBox(CardVB);
		VBSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); VBSlot->SetVerticalAlignment(VAlign_Center);
		// Titel (kort) + beschrijving eronder.
		CardVB->AddChildToVerticalBox(MakeText(Store->GetCatalogName(Id).ToString(), 14, bLocked ? FLinearColor(0.6f, 0.6f, 0.62f) : FLinearColor(0.95f, 0.97f, 1.f)));
		const FString DescStr = Store->GetCatalogDesc(Id).ToString();
		if (!DescStr.IsEmpty())
		{
			UTextBlock* Desc = MakeText(DescStr, 10, FLinearColor(0.62f, 0.66f, 0.76f));
			Desc->SetAutoWrapText(true);
			CardVB->AddChildToVerticalBox(Desc);
		}
		if (ReqLvl > 0)
		{
			CardVB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("Unlocks at level %d"), ReqLvl), 10,
				bLocked ? FLinearColor(1.f, 0.5f, 0.45f) : FLinearColor(0.5f, 0.8f, 0.55f)));
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

UWidget* UPhoneWidget::MakeAppCell(int32 AppIndex, const FString& Name, const FString& IconKey, WeedUI::EIcon IconFallback, const FLinearColor& Col)
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
	IcoSz->SetContent(WeedUI::UiGlyph(WidgetTree, IconKey, 34.f, FLinearColor::White, IconFallback));
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
	// Home: vanuit een app -> terug naar het homescreen; al op het homescreen -> telefoon sluiten.
	HomeBtn->OnAction.BindLambda([this](int32, int32) { if (Phone.IsValid()) { if (Phone->IsHomeScreen()) { Phone->Toggle(); } else { Phone->GoHome(); } } });
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
			UUniformGridSlot* GSlot = Grid->AddChildToUniformGrid(MakeAppCell(i, GAppName[i], GAppKey[i], GAppIcon[i], GAppCol[i]), i / 3, i % 3);
			GSlot->SetHorizontalAlignment(HAlign_Center);
		}
		UVerticalBoxSlot* GridSlot = ContentBox->AddChildToVerticalBox(Grid);
		GridSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		return;
	}

	const int32 App = FMath::Clamp(Phone->GetTab(), 0, GNumApps - 1);

	// App-header: back-knop + titel. (Packages is nu een losse app, geen knop meer in de winkel.)
	bPackagesView = false;
	StorePackagesToggle = nullptr;
	StorePackagesLabel = nullptr;
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	// Linksboven: altijd een Back-knop (terug naar het home-scherm).
	Header->AddChildToHorizontalBox(MakeButton(TEXT("< Back"), 1, 0, FLinearColor(0.2f, 0.3f, 0.45f)));
	UTextBlock* TitleText = MakeText(GAppName[App], 15, FLinearColor(0.9f, 0.95f, 1.f));
	TitleText->SetClipping(EWidgetClipping::ClipToBounds);
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(TitleText);
	TitleSlot->SetPadding(FMargin(10.f, 4.f, 6.f, 0.f));
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	UVerticalBoxSlot* HeaderSlot = ContentBox->AddChildToVerticalBox(Header);
	HeaderSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	if (App == 0) // Upgrades
	{
		// --- Woning: 3 koopbare panden (het starter-flatje is al van jou) ---
		{
			ContentBox->AddChildToVerticalBox(MakeText(TEXT("Woning"), 14, FLinearColor(0.7f, 0.9f, 1.f)))
				->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			TArray<FCityPropertyOffer> Offers;
			Phone->GetPropertyOffers(Offers);
			if (Offers.Num() == 0)
			{
				ContentBox->AddChildToVerticalBox(MakeText(TEXT("(stad laadt nog...)"), 11, FLinearColor::Gray));
			}
			for (const FCityPropertyOffer& O : Offers)
			{
				const bool bOwned = Phone->IsPropertyOwned(O.HomeIndex);
				const bool bHereNow = bOwned && (Phone->GetHomePlayerIsInside() == O.HomeIndex);
				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
				Info->AddChildToVerticalBox(MakeText(O.Title, 12, bOwned ? FLinearColor(0.7f, 1.f, 0.7f) : FLinearColor::White));
				// Woning-info: type + huisnummer + verdieping.
				const FString InfoLine = Phone->GetHomeInfoLine(O.HomeIndex);
				if (!InfoLine.IsEmpty())
				{
					Info->AddChildToVerticalBox(MakeText(InfoLine, 10, FLinearColor(0.62f, 0.78f, 0.92f)));
				}
				const FString PriceStr = (O.PriceCents > 0)
					? FString::Printf(TEXT("%s   EUR %.0f"), *O.Sub, O.PriceCents / 100.f)
					: (bOwned ? FString::Printf(TEXT("%s   (in bezit)"), *O.Sub) : FString::Printf(TEXT("%s   (starter)"), *O.Sub));
				Info->AddChildToVerticalBox(MakeText(PriceStr, 10, FLinearColor(0.65f, 0.7f, 0.8f)));
				UHorizontalBoxSlot* IL = Row->AddChildToHorizontalBox(Info);
				IL->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				IL->SetVerticalAlignment(VAlign_Center);
				IL->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

				USizeBox* RB = WidgetTree->ConstructWidget<USizeBox>();
				RB->SetWidthOverride(124.f);
				if (!bOwned)
				{
					RB->SetHeightOverride(28.f);
					RB->SetContent(MakeButton(TEXT("Koop"), 7, O.HomeIndex, FLinearColor(0.2f, 0.5f, 0.28f)));
				}
				else
				{
					// Eigen woning: teleport-knop (of "je bent hier") + verkoop-knop (~65%).
					const int32 SellVal = Phone->GetHomeSellValueCents(O.HomeIndex);
					UVerticalBox* OwnBox = WidgetTree->ConstructWidget<UVerticalBox>();
					if (bHereNow) { OwnBox->AddChildToVerticalBox(MakeText(TEXT("je bent hier"), 11, FLinearColor(0.6f, 1.f, 0.6f), true)); }
					else          { OwnBox->AddChildToVerticalBox(MakeButton(TEXT("Ga hierheen"), 8, O.HomeIndex, FLinearColor(0.25f, 0.45f, 0.6f))); }
					if (SellVal > 0)
					{
						OwnBox->AddChildToVerticalBox(MakeButton(*FString::Printf(TEXT("Verkoop EUR %.0f"), SellVal / 100.f), 9, O.HomeIndex, FLinearColor(0.5f, 0.32f, 0.2f)))
							->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
					}
					RB->SetHeightOverride(SellVal > 0 ? 58.f : 28.f);
					RB->SetContent(OwnBox);
				}
				UHorizontalBoxSlot* RS = Row->AddChildToHorizontalBox(RB);
				RS->SetVerticalAlignment(VAlign_Center);
				ContentBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f));
			}
		}

	}
	else if (App == GGrowApp) // Grow shop -> ALLES om te kweken (zaad, pot, aarde, water, upgrades, verzorging)
	{
		bSellApp = false;
		AppCats = { 0, 1, 5, 6, 8, 9 }; // Seeds, Pots, Soil, Water, Grow Upg., Care
		if (!AppCats.Contains(Phone->GetSupplierCat())) { Phone->SetSupplierCat(AppCats[0]); }
		BuildStoreApp(ContentBox);
	}
	else if (App == GSuppliesApp) // Supplies -> verwerken/verkopen/inrichten (papers, drogen, verpakken, meubels)
	{
		bSellApp = false;
		AppCats = { 4, 2, 3, 7 }; // Papers, Drying, Packing, Furniture
		if (!AppCats.Contains(Phone->GetSupplierCat())) { Phone->SetSupplierCat(AppCats[0]); }
		BuildStoreApp(ContentBox);
	}
	else if (App == GHashApp) // Hash Lab -> de hasj-keten: machines (mesh/press) + machine-upgrades
	{
		bSellApp = false;
		AppCats = { 10 }; // Hash (Mesh/Press + ProcUp-upgrades)
		if (!AppCats.Contains(Phone->GetSupplierCat())) { Phone->SetSupplierCat(AppCats[0]); }
		BuildStoreApp(ContentBox);
	}
	else if (App == GPackagesApp) // Packages -> losse app met onderweg zijnde bestellingen
	{
		BuildPackagesApp();
	}
	else if (App == GBankApp) // Bank -> storten + sturen (ontgrendeld na de telefoon-upgrade)
	{
		BuildBankApp();
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
			UScrollBox* List = WidgetTree->ConstructWidget<UScrollBox>();
			UVerticalBoxSlot* LS = ContentBox->AddChildToVerticalBox(List);
			LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			for (const FPhoneContact& C : Con->GetContacts())
			{
				const FName Cid = C.ContactId;
				UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
				Card->SetBrush(RoundedBrush(FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 8.f));
				Card->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				Card->SetContent(Row);
				UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
				Info->AddChildToVerticalBox(MakeText(C.DisplayName.ToString(), 14, FLinearColor(0.95f, 0.97f, 1.f)));
				Info->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("Relationship %.0f%%"), C.Relationship), 10, FLinearColor(0.62f, 0.66f, 0.76f)));
				UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(Info);
				IS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); IS->SetVerticalAlignment(VAlign_Center);
				Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Message"), FLinearColor(0.2f, 0.4f, 0.55f),
					[this, Cid]() { OpenChatContact = Cid; if (Phone.IsValid()) { Phone->OpenApp(3); } MarkDirty(); }, 11))->SetVerticalAlignment(VAlign_Center);
				List->AddChild(Card);
				UTextBlock* Gap = MakeText(TEXT(""), 4, FLinearColor::Transparent);
				List->AddChild(Gap);
			}
		}
	}
	else if (App == 3) // Messages (echte chat)
	{
		BuildChatApp();
	}
	else if (App == 4) // Settings
	{
		BuildSettingsApp();
	}
	else if (App == GSellApp) // Sell -> aparte verkoop-app (zelfde store-UI, alleen verkopen)
	{
		bSellApp = true;
		BuildStoreApp(ContentBox);
	}
	else // Map
	{
		BuildMapApp();
	}
}

void UPhoneWidget::BuildMapApp()
{
	if (!ContentBox) { return; }

	// Knop naar de fullscreen-kaart (zelfde als M).
	UWeedActionButton* FB = MakeActionBtn(TEXT("Fullscreen (M)"), FLinearColor(0.20f, 0.45f, 0.62f),
		[this]() { if (Phone.IsValid()) { Phone->ToggleMapOverlay(); } }, 13);
	ContentBox->AddChildToVerticalBox(FB)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Ingebedde mini-kaart (toont winkels, huisnummers, jouw positie + de NPC's).
	if (UMapWidget* MW = CreateWidget<UMapWidget>(GetOwningPlayer(), UMapWidget::StaticClass()))
	{
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetHeightOverride(300.f);
		Box->SetContent(MW);
		ContentBox->AddChildToVerticalBox(Box);
	}
}

void UPhoneWidget::HandlePhoneButton(int32 Action, int32 Param)
{
	if (!Phone.IsValid()) { return; }
	switch (Action)
	{
	case 0: Phone->OpenApp(Param); bCartView = false; if (Param == 3) { OpenChatContact = NAME_None; } break;
	case 1: if (Phone->IsHomeScreen()) { Phone->Toggle(); } else { Phone->GoHome(); } bCartView = false; OpenChatContact = NAME_None; break;
	case 2: Phone->Toggle(); break;
	case 3: Phone->DoAction(Param); break;     // koop upgrade
	case 5: Phone->DoAction(0); break;         // accept bericht
	case 6: Phone->DoAction(1); break;         // decline bericht
	case 7: Phone->BuyProperty(Param); break;  // koop woning
	case 8: Phone->SetActiveHome(Param); break;// teleport naar deze woning
	case 9: Phone->SellProperty(Param); break; // verkoop woning (~65%)
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

	// Settings/Test: light-sliders live toepassen terwijl je sleept (geen restart nodig).
	if (!bHome && App == 4 && SettingsCat == 1 && LMoon)
	{
		ApplyLightSliders();
	}

	// Packages-app open: bij een nieuwe/gewijzigde bestelling de lijst herbouwen, anders
	// de bars/ETA's live bijwerken (geen herbouw -> geen flash).
	if (!bHome && App == GPackagesApp && PackagesScroll)
	{
		const int32 Sig = PackagesSignature();
		if (Sig != LastPkgSig) { LastPkgSig = Sig; FillPackagesInto(PackagesScroll); }
		else { UpdatePackagesLive(); }
	}

	// Berichten/Contacten open: bij een nieuw bericht of statuswijziging de inhoud herbouwen.
	if (!bHome && (App == 2 || App == 3))
	{
		const int32 Sig = MessagesSignature();
		if (Sig != LastMsgSig) { LastMsgSig = Sig; MarkDirty(); }
		else { UpdateApptBarLive(); } // aftellende afspraak-balk live bijwerken (zonder herbouw)
	}

	// Bank-app open: bij saldo-/limiet-/unlock-wijziging de inhoud herbouwen.
	if (!bHome && App == GBankApp && GS && GS->GetEconomy())
	{
		const UEconomyComponent* E = GS->GetEconomy();
		const int32 Sig = (int32)(E->GetCashCents() / 100) * 7 + (int32)(E->GetBankCents() / 100) * 13
			+ E->GetTransfersToday() * 101 + (int32)(E->GetDepositedTodayCents() / 100) * 3
			+ (Phone->IsBankAppUnlocked() ? 1000003 : 0);
		if (Sig != LastBankSig) { LastBankSig = Sig; MarkDirty(); }
	}
}
