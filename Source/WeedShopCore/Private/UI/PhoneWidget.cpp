#include "UI/PhoneWidget.h"
#include "WeedShopCore.h"

#include "Phone/PhoneClientComponent.h"
#include "Game/WeedShopGameState.h"
#include "Save/SaveGameSubsystem.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/PlayerNpcActions.h"
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
#include "World/StorageShelf.h"
#include "Phone/ContactsComponent.h"
#include "Input/ControlSettings.h"
#include "GameFramework/Pawn.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "InputCoreTypes.h"

#include "Blueprint/WidgetTree.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "World/RoomStamper.h"
#include "World/CityDoor.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
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
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Progression/GoalsComponent.h"
#include "Npc/NpcRegistryComponent.h"
#include "Components/Slider.h"
#include "World/DayNightController.h"
#include "World/CityTypes.h"
#include "UI/MapWidget.h"
#include "UI/WeedItemPickGrid.h" // "Offer instead"-lijst = icoon-grid (persistent, diff-SetItems -> geen flash)
#include "Phone/PhoneClientComponent.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "UI/WeedUiStyle.h"

namespace
{
	// (Bank-app op de telefoon komt pas terug na een telefoon-upgrade; bankieren gaat nu via de ATM.)
	constexpr int32 GNumApps = 13;
	const TCHAR* GAppName[GNumApps] = { TEXT("Upgrades"), TEXT("Grow shop"), TEXT("Contacts"), TEXT("Messages"), TEXT("Settings"), TEXT("Map"), TEXT("Sell"), TEXT("Supplies"), TEXT("Packages"), TEXT("Bank"), TEXT("Lab"), TEXT("Goals"), TEXT("Leaderboard") };
	const WeedUI::EIcon GAppIcon[GNumApps] = { WeedUI::EIcon::Upgrade, WeedUI::EIcon::Leaf, WeedUI::EIcon::Person, WeedUI::EIcon::Message, WeedUI::EIcon::Gear, WeedUI::EIcon::Map, WeedUI::EIcon::Coin, WeedUI::EIcon::Shop, WeedUI::EIcon::Shop, WeedUI::EIcon::Coin, WeedUI::EIcon::Flame, WeedUI::EIcon::Level, WeedUI::EIcon::Level };
	// Eigen icoon per app (PNG-sleutel); valt terug op GAppIcon als het PNG ontbreekt.
	const TCHAR* GAppKey[GNumApps] = { TEXT("ui_upgrade"), TEXT("ui_leaf"), TEXT("ui_person"), TEXT("ui_message"), TEXT("ui_gear"), TEXT("ui_map"), TEXT("ui_sell"), TEXT("ui_shop"), TEXT("ui_package"), TEXT("ui_bank"), TEXT("ui_hash"), TEXT("ui_goals"), TEXT("ui_goals") };
	const FLinearColor GAppCol[GNumApps] = {
		FLinearColor(0.45f, 0.35f, 0.85f), FLinearColor(0.18f, 0.55f, 0.30f), FLinearColor(0.20f, 0.50f, 0.80f),
		FLinearColor(0.90f, 0.55f, 0.20f), FLinearColor(0.40f, 0.42f, 0.48f), FLinearColor(0.18f, 0.62f, 0.58f),
		FLinearColor(0.85f, 0.65f, 0.20f), FLinearColor(0.30f, 0.50f, 0.70f), FLinearColor(0.55f, 0.40f, 0.80f),
		FLinearColor(0.16f, 0.55f, 0.95f), FLinearColor(0.80f, 0.45f, 0.20f), FLinearColor(0.90f, 0.75f, 0.25f),
		FLinearColor(0.95f, 0.78f, 0.25f),
	};
	constexpr int32 GStatsApp = 12;
	// Logische home-volgorde (groepjes per rij van 3): produceren -> dealen -> geld/uitbreiden -> voortgang -> systeem.
	const int32 GHomeOrder[GNumApps] = {
		1, 7, 10,   // Grow shop, Supplies, Lab        (produceren)
		6, 2, 3,    // Sell, Contacts, Messages        (dealen)
		9, 0, 8,    // Bank, Upgrades, Packages        (geld / uitbreiden)
		11, 12, 5,  // Goals, Leaderboard, Map         (voortgang / navigatie)
		4           // Settings                        (systeem)
	};
	constexpr int32 GGrowApp = 1;
	constexpr int32 GGoalsApp = 11;
	constexpr int32 GMapApp = 5;   // Map-app is van de telefoon af (de fullscreen-kaart zit op de M-toets); overgeslagen zoals Lab
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
		case 11: return TEXT("Kitchen");
		case 12: return TEXT("Ingredients");
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

	// Berichten-body als WrapBox van losse woorden, met de BELANGRIJKE woorden VETGEDRUKT: alles met een
	// cijfer (grams, % THC, prijs, tijd) en strain-/product-woorden. Zo springt eruit wát en hoeveel ze vragen.
	UWidget* MakeRichBody(UWidgetTree* Tree, const FString& Body, int32 Size, const FLinearColor& Col, const FString& BoldExtra = FString(), const FLinearColor& ExtraCol = FLinearColor(1.f, 1.f, 1.f))
	{
		// Per \n-regel een WRAPBOX (breekt lange regels netjes af BINNEN de bubbel i.p.v. eruit te lopen).
		// Belangrijke woorden VET: getallen/%/tijd (9g, 13%, 11:14), ALL-CAPS termen (VIP, THC), EN de strain/product
		// (via BoldExtra) zodat de klant z'n bestelling meteen ziet; rest gewoon.
		UVerticalBox* VB = Tree->ConstructWidget<UVerticalBox>();
		// Extra-vet woorden (de strain/product) als losse lowercase woorden voor de vergelijking.
		TSet<FString> ExtraWords;
		{
			TArray<FString> EW; BoldExtra.ToLower().ParseIntoArray(EW, TEXT(" "), true);
			for (const FString& E : EW) { if (E.Len() >= 2) { ExtraWords.Add(E); } }
		}
		TArray<FString> Lines;
		Body.ParseIntoArray(Lines, TEXT("\n"), false);
		for (const FString& Line : Lines)
		{
			UWrapBox* WB = Tree->ConstructWidget<UWrapBox>();
			WB->SetExplicitWrapSize(true);
			WB->SetWrapSize(222.f); // binnen de 244px-bubbel
			TArray<FString> Words;
			Line.ParseIntoArray(Words, TEXT(" "), true);
			// Woorden met dezelfde stijl samenvoegen tot 1 TextBlock (minder widgets = goedkopere telefoon-draw),
			// maar geknipt op ~16 tekens zodat de WrapBox lange regels nog steeds binnen de bubbel afbreekt.
			FString Run; bool bRunBold = false; bool bRunExtra = false; int32 RunLen = 0;
			auto Flush = [&]()
			{
				if (Run.IsEmpty()) { return; }
				UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
				T->SetText(FText::FromString(Run));
				T->SetFont(FCoreStyle::GetDefaultFontStyle(bRunBold ? "Bold" : "Regular", Size));
				// Strain/product-run: vet EN in de strain-tagkleur; ander vet = wit; rest = Col.
				T->SetColorAndOpacity(FSlateColor(bRunExtra ? ExtraCol : (bRunBold ? FLinearColor(1.f, 1.f, 1.f) : Col)));
				WB->AddChildToWrapBox(T);
				Run.Reset(); RunLen = 0;
			};
			for (const FString& W : Words)
			{
				bool bDigit = false, bAllUpper = (W.Len() >= 2);
				for (TCHAR c : W)
				{
					if (FChar::IsDigit(c) || c == TEXT('%')) { bDigit = true; }
					if (FChar::IsAlpha(c) && !FChar::IsUpper(c)) { bAllUpper = false; }
				}
				// Strain/product-woord? (kale alfa-kern vergelijken, dus "Haze?" matcht ook.)
				bool bExtra = false;
				if (ExtraWords.Num() > 0)
				{
					FString Core = W.ToLower();
					while (Core.Len() > 0 && !FChar::IsAlpha(Core[0])) { Core.RemoveAt(0); }
					while (Core.Len() > 0 && !FChar::IsAlpha(Core[Core.Len() - 1])) { Core.RemoveAt(Core.Len() - 1); }
					bExtra = Core.Len() >= 2 && ExtraWords.Contains(Core);
				}
				const bool bBold = bDigit || bAllUpper || bExtra;
				// Ook op bExtra breken: het gekleurde strain-woord mag NIET met omliggende vette getallen
				// in EEN TextBlock belanden (die zijn wit), anders krijgt de hele run de strain-kleur.
				if (!Run.IsEmpty() && (bBold != bRunBold || bExtra != bRunExtra || RunLen + W.Len() + 1 > 16)) { Flush(); }
				if (Run.IsEmpty()) { bRunBold = bBold; bRunExtra = bExtra; }
				Run += W; Run += TEXT(" "); RunLen += W.Len() + 1;
			}
			Flush();
			VB->AddChildToVerticalBox(WB);
		}
		return VB;
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
	if (!ActiveContent) { return; }

	// Categorie-knoppen. De 'Test'-tab (dag/nacht-switch e.d.) alleen in dev-modes (Sandbox/Testing), niet in een normaal spel.
	SettingsTabBtns.Reset();
	// Dev-tabs (Test/Rooms/Light/Spots) zijn verhuisd naar het losse DEV-MENU (F10). De telefoon toont alleen Status.
	SettingsCat = 0;
	static const TCHAR* CatNames[1] = { TEXT("Status") };
	const int32 NumTabs = 1;
	UHorizontalBox* Cats = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 i = 0; i < NumTabs; ++i)
	{
		const FLinearColor Col = (i == SettingsCat) ? WeedUI::ColAccentDim() : WeedUI::ColSlot();
		UWeedActionButton* B = MakeActionBtn(CatNames[i], Col,
			[this, i]() { SettingsCat = i; bRebinding = false; RebindMsg.Reset(); RefreshSettingsTabs(); FillSettingsBody(); }, 10);
		UHorizontalBoxSlot* CS = Cats->AddChildToHorizontalBox(B);
		CS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
		SettingsTabBtns.Add(B);
	}
	ActiveContent->AddChildToVerticalBox(Cats)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Body in een ScrollBox zodat lange lijsten netjes binnen de telefoon blijven (scrollen ipv overlopen).
	UScrollBox* BodyScroll = WidgetTree->ConstructWidget<UScrollBox>();
	SettingsBody = WidgetTree->ConstructWidget<UVerticalBox>();
	BodyScroll->AddChild(SettingsBody);
	UVerticalBoxSlot* ScrollSlot = ActiveContent->AddChildToVerticalBox(BodyScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	FillSettingsBody();
}

void UPhoneWidget::RefreshSettingsTabs()
{
	for (int32 i = 0; i < SettingsTabBtns.Num(); ++i)
	{
		if (!SettingsTabBtns[i]) { continue; }
		const FLinearColor Col = (i == SettingsCat) ? WeedUI::ColAccentDim() : WeedUI::ColSlot();
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
	// Slider-pointers resetten: hun widgets zijn zojuist vernietigd; ApplyLightSliders mag er niet meer aan zitten.
	TimeSpeedSlider = nullptr; TimeSpeedV = nullptr;
	LMoon = LSun = LSkyN = LSkyD = LPitch = LLamp = LExp = nullptr;
	SkinMaleBtn = nullptr; SkinFemaleBtn = nullptr; ShopTypeLabel = nullptr; // body-rebuild -> oude refs vrijgeven
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	auto BodyRow = [this](UWidget* W, const FMargin& Pad) { SettingsBody->AddChildToVerticalBox(W)->SetPadding(Pad); };

	if (SettingsCat == 1) // Test-tools, nu in nette secties
	{
		// --- Compacte helpers voor deze tab ---
		const FLinearColor CSave = WeedUI::ColGood(0.5f), CClr = WeedUI::ColWarn(0.5f), CAim = WeedUI::ColAccentDim(), CKit = WeedUI::ColAccentDim();
		auto Section = [&](const TCHAR* T) { BodyRow(MakeText(T, 11, WeedUI::ColAccent()), FMargin(0.f, 8.f, 0.f, 2.f)); };
		auto Single = [&](const FString& L, const FLinearColor& C, TFunction<void()> F) { BodyRow(MakeActionBtn(L, C, F, 11), FMargin(0.f, 0.f, 0.f, 3.f)); };
		auto Pair = [&](const FString& L1, const FLinearColor& C1, TFunction<void()> F1, const FString& L2, const FLinearColor& C2, TFunction<void()> F2)
		{
			UHorizontalBox* HB = WidgetTree->ConstructWidget<UHorizontalBox>();
			HB->AddChildToHorizontalBox(MakeActionBtn(L1, C1, F1, 11))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			if (UHorizontalBoxSlot* S2 = HB->AddChildToHorizontalBox(MakeActionBtn(L2, C2, F2, 11)))
			{
				S2->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				S2->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			}
			BodyRow(HB, FMargin(0.f, 0.f, 0.f, 3.f));
		};

		// === WORLD ===
		Section(TEXT("WORLD"));
		if (GS && GS->GetDayCycle())
		{
			const float H = GS->GetDayCycle()->GetClockHour();
			BodyRow(MakeText(FString::Printf(TEXT("%02d:%02d  (%s)"), (int32)H, (int32)((H - (int32)H) * 60.f),
				GS->GetDayCycle()->IsNight() ? TEXT("night") : TEXT("day")), 11, WeedUI::ColTextDim()), FMargin(0.f, 0.f, 0.f, 3.f));
		}
		Pair(TEXT("Set Day"), WeedUI::ColAccentDim(), [this]() { if (Phone.IsValid()) { Phone->RequestSetDayNight(false); } },
			 TEXT("Set Night"), WeedUI::ColInner(), [this]() { if (Phone.IsValid()) { Phone->RequestSetDayNight(true); } });
		TimeSpeedSlider = nullptr; TimeSpeedV = nullptr;
		{
			const float CurDilation = GetWorld() ? UGameplayStatics::GetGlobalTimeDilation(GetWorld()) : 1.f;
			AddLightSlider(TEXT("Time speed"), (FMath::Clamp(CurDilation, 1.f, 8.f) - 1.f) / 7.f, TimeSpeedSlider, TimeSpeedV);
		}
		Pair(TEXT("Trigger Robbery"), WeedUI::ColAccentDim(), [this]() { if (Phone.IsValid()) { Phone->RequestDevHeatEvent(false); } },
			 TEXT("Trigger Bust"), WeedUI::ColInner(), [this]() { if (Phone.IsValid()) { Phone->RequestDevHeatEvent(true); } });

		// === BUILD & FURNISH ===
		Section(TEXT("BUILD & FURNISH"));
		Pair(TEXT("Build kit"), CKit, [this]() { if (Phone.IsValid()) { Phone->RequestGiveBuildKit(); } },
			 TEXT("Furniture kit"), CKit, [this]() { if (Phone.IsValid()) { Phone->RequestGiveFurnitureKit(); } });
		Pair(TEXT("Save furniture"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveStarterFurniture(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearStarterFurniture(); } });
		// No-build-zone: markeer 2 hoeken (F9) van een muur/gebied -> knop zet ze vast (eigen bestand, blijft staan).
		Pair(TEXT("Save no-build zone (2 F9 corners)"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveNoBuildZone(); } },
			 TEXT("Clear no-build"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearNoBuildZone(); } });

		// === HOME ===
		Section(TEXT("HOME"));
		Single(TEXT("Save home spawn (stand here)"), CAim, [this]() { if (Phone.IsValid()) { Phone->SaveHomeSpawn(); } });

		// === NPC ROUTES & SPOTS ===
		Section(TEXT("NPC ROUTES & SPOTS"));
		Pair(TEXT("Save walk route"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveNpcRoute(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearNpcRoute(); } });
		Pair(TEXT("Save chill spots"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveChillSpots(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearChillSpots(); } });
		Pair(TEXT("Save stairs path"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveStairsPath(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearStairsPath(); } });
		Pair(TEXT("Show paths"), CAim, [this]() { if (Phone.IsValid()) { Phone->ShowAllPaths(); } },
			 TEXT("Hide paths"), WeedUI::ColSlot(), [this]() { if (Phone.IsValid()) { Phone->HideAllPaths(); } });
		Single(TEXT("Delete path (aim at dot)"), CClr, [this]() { if (Phone.IsValid()) { Phone->DeletePathInCrosshair(); } });

		// === SHOPS ===
		Section(TEXT("SHOPS"));
		{
			static const TCHAR* KN[3] = { TEXT("Grow shop"), TEXT("Supplies"), TEXT("Furniture") };
			const int32 SelK = Phone.IsValid() ? FMath::Clamp(Phone->GetSelectedShopKind(), 0, 2) : 0;
			// De knop-tekst wordt in-place ge-set bij het cyclen (geen body-herbouw -> geen flash).
			UWeedActionButton* ShopTypeBtn = MakeActionBtn(FString::Printf(TEXT("Shop type: %s  (tap)"), KN[SelK]), WeedUI::ColAccentDim(),
				[this]()
				{
					if (!Phone.IsValid()) { return; }
					Phone->CycleSelectedShopKind();
					static const TCHAR* KN2[3] = { TEXT("Grow shop"), TEXT("Supplies"), TEXT("Furniture") };
					const int32 NewK = FMath::Clamp(Phone->GetSelectedShopKind(), 0, 2);
					if (ShopTypeLabel) { ShopTypeLabel->SetText(FText::FromString(FString::Printf(TEXT("Shop type: %s  (tap)"), KN2[NewK]))); }
				}, 11);
			// Het tekstblok in de knop onthouden zodat we het label kunnen updaten.
			ShopTypeLabel = MakeText(FString::Printf(TEXT("Shop type: %s  (tap)"), KN[SelK]), 11, FLinearColor::White, true);
			ShopTypeBtn->SetContent(ShopTypeLabel);
			BodyRow(ShopTypeBtn, FMargin(0.f, 0.f, 0.f, 3.f));
		}
		Pair(TEXT("Save shops (at counter)"), CSave, [this]() { if (Phone.IsValid()) { Phone->SaveShopSpots(); } },
			 TEXT("Clear"), CClr, [this]() { if (Phone.IsValid()) { Phone->ClearShopSpots(); } });
		Single(TEXT("Set shop type (aim)"), CAim, [this]() { if (Phone.IsValid()) { Phone->SetShopTypeInCrosshair(); } });

		// === MAP FIXES ===
		Section(TEXT("MAP FIXES"));
		Pair(TEXT("Walk-through (aim)"), CKit, [this]()
			{
				APlayerController* PC = GetOwningPlayer();
				UWorld* DW = GetWorld();
				if (!PC || !DW || !Phone.IsValid()) { return; }
				FVector VL; FRotator VR;
				PC->GetPlayerViewPoint(VL, VR);
				FHitResult Hit;
				FCollisionQueryParams Q;
				Q.AddIgnoredActor(PC->GetPawn());
				if (!DW->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 2500.f, ECC_Pawn, Q) || !Hit.GetComponent())
				{
					Phone->Toast(TEXT("Nothing blocking in crosshair"), FColor::Orange, 2.5f);
					return;
				}
				if (FMath::Abs(Hit.ImpactNormal.Z) > 0.6f)
				{
					Phone->Toast(TEXT("Aim at a wall/object, not the floor"), FColor::Orange, 3.f);
					return;
				}
				UPrimitiveComponent* Comp = Hit.GetComponent();
				Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				FVector SaveLoc = Comp->GetComponentLocation();
				FString MeshNm = Comp->GetName();
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp))
				{
					if (SMC->GetStaticMesh()) { MeshNm = SMC->GetStaticMesh()->GetName(); }
				}
				if (UInstancedStaticMeshComponent* IC = Cast<UInstancedStaticMeshComponent>(Comp))
				{
					FTransform IT;
					if (Hit.Item >= 0 && IC->GetInstanceTransform(Hit.Item, IT, true)) { SaveLoc = IT.GetLocation(); }
				}
				FString Cur;
				FFileHelper::LoadFileToString(Cur, *(FPaths::ProjectSavedDir() / TEXT("NoCollide.txt")));
				Cur += FString::Printf(TEXT("%s|%.1f,%.1f,%.1f\n"), *MeshNm, SaveLoc.X, SaveLoc.Y, SaveLoc.Z);
				FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("NoCollide.txt")));
				Phone->Toast(FString::Printf(TEXT("Walk-through: %s (saved)"), *MeshNm), FColor::Cyan, 3.f);
			},
			TEXT("Clear"), CClr, [this]()
			{
				WeedData::DeleteFile(TEXT("NoCollide.txt"));
				if (Phone.IsValid()) { Phone->Toast(TEXT("Walk-throughs cleared (restart restores collision)"), FColor::Orange, 4.f); }
			});
	}
	else if (SettingsCat == 2) // Rooms: kamer-builds + stamper
	{
		BodyRow(MakeText(TEXT("Room builder"), 14, WeedUI::ColText()), FMargin(0.f, 0.f, 0.f, 2.f));
		// Kamer-job opslaan: huidige 3 markers worden permanent (RoomJobs.txt, elke sessie herbouwd).
		UWeedActionButton* JobB = MakeActionBtn(TEXT("Save room build (clears markers)"), WeedUI::ColGood(0.5f),
			[this]() { if (Phone.IsValid()) { Phone->SaveRoomJob(); } }, 13);
		BodyRow(JobB, FMargin(0.f, 0.f, 0.f, 8.f));

		// --- Room stamper: kamers als stempel plaatsen (deur snapt op deur, R = draaien) ---
		BodyRow(MakeText(TEXT("Room stamper"), 14, WeedUI::ColText()), FMargin(0.f, 6.f, 0.f, 2.f));
		UWeedActionButton* TplB = MakeActionBtn(TEXT("Save room as template (2 markers)"), WeedUI::ColAccentDim(),
			[this]() { if (Phone.IsValid()) { Phone->SaveRoomTemplateNow(); FillSettingsBody(); } }, 12);
		BodyRow(TplB, FMargin(0.f, 0.f, 0.f, 4.f));
		{
			const TArray<FString> Templates = ARoomStamper::ListTemplates();
			if (Templates.Num() == 0)
			{
				BodyRow(MakeText(TEXT("No templates yet - mark a room (2 corners) and save it."), 10, WeedUI::ColTextDim()), FMargin(0.f, 0.f, 0.f, 4.f));
			}
			for (const FString& Tpl : Templates)
			{
				UHorizontalBox* TRow = WidgetTree->ConstructWidget<UHorizontalBox>();
				UWeedActionButton* StampB = MakeActionBtn(FString::Printf(TEXT("Stamp: %s"), *Tpl), WeedUI::ColAccentDim(),
					[this, Tpl]() { if (Phone.IsValid()) { Phone->StartRoomStamp(Tpl); } }, 12);
				TRow->AddChildToHorizontalBox(StampB)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				TRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("X"), WeedUI::ColWarn(),
					[this, Tpl]()
					{
						WeedData::DeleteFile(FString(TEXT("RoomTemplates")) / (Tpl + TEXT(".txt")));
						FillSettingsBody();
					}, 11))->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
				BodyRow(TRow, FMargin(0.f, 1.f, 0.f, 1.f));
			}
		}
		// Geplaatste (nog niet gebakken) stempels: undo + per stuk verwijderen.
		{
			const TArray<FString> PlacedStamps = ARoomStamper::ListPlacedStamps(GetWorld());
			if (PlacedStamps.Num() > 0)
			{
				UWeedActionButton* UndoB = MakeActionBtn(TEXT("Undo last stamp"), WeedUI::ColWarn(),
					[this]() { FString Info; ARoomStamper::UndoLastStamp(GetWorld(), Info); FillSettingsBody(); }, 12);
				BodyRow(UndoB, FMargin(0.f, 3.f, 0.f, 2.f));
				for (const FString& SLine : PlacedStamps)
				{
					// Regel: template|x,y,z|yaw -> naam + positie, met TP- en verwijder-knop.
					TArray<FString> SParts;
					SLine.ParseIntoArray(SParts, TEXT("|"));
					FVector StampPos = FVector::ZeroVector; bool bHasPos = false;
					if (SParts.Num() > 1)
					{
						TArray<FString> PosParts;
						SParts[1].ParseIntoArray(PosParts, TEXT(","));
						if (PosParts.Num() >= 3)
						{
							StampPos = FVector(FCString::Atof(*PosParts[0]), FCString::Atof(*PosParts[1]), FCString::Atof(*PosParts[2]));
							bHasPos = true;
						}
					}
					UHorizontalBox* SRow = WidgetTree->ConstructWidget<UHorizontalBox>();
					UHorizontalBoxSlot* SLab = SRow->AddChildToHorizontalBox(MakeText(
						FString::Printf(TEXT("%s  (%.0f, %.0f)"), SParts.Num() > 0 ? *SParts[0] : *SLine, StampPos.X, StampPos.Y),
						11, WeedUI::ColText()));
					SLab->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); SLab->SetVerticalAlignment(VAlign_Center);
					if (bHasPos)
					{
						SRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("TP"), WeedUI::ColAccentDim(),
							[this, StampPos]()
							{
								if (APawn* Pn = GetOwningPlayerPawn()) { Pn->SetActorLocation(StampPos + FVector(0.f, 0.f, 120.f)); }
							}, 11))->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
					}
					SRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("X"), WeedUI::ColWarn(),
						[this, SLine]() { ARoomStamper::RemoveStamp(GetWorld(), SLine); FillSettingsBody(); }, 11))->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
					BodyRow(SRow, FMargin(0.f, 1.f, 0.f, 1.f));
				}
			}
		}

		// Deur op slot: vergrendeld zoals een bewoner-deur maar ZONDER naam (prompt "LOCKED").
		// Permanent via Saved/LockedDoors.txt - elke sessie opnieuw toegepast.
		UWeedActionButton* LockDoorB = MakeActionBtn(TEXT("Lock door in crosshair"), WeedUI::ColAccentDim(),
			[this]()
			{
				APlayerController* PC = GetOwningPlayer();
				UWorld* DW = GetWorld();
				if (!PC || !DW || !Phone.IsValid()) { return; }
				FVector VL; FRotator VR;
				PC->GetPlayerViewPoint(VL, VR);
				FHitResult Hit;
				FCollisionQueryParams Q;
				Q.AddIgnoredActor(PC->GetPawn());
				const bool bHit = DW->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 2500.f, ECC_Visibility, Q);
				const FVector Target = bHit ? Hit.ImpactPoint : VL + VR.Vector() * 600.f;
				ACityDoor* Best = nullptr;
				float BestD = 250.f;
				for (TActorIterator<ACityDoor> It(DW); It; ++It)
				{
					const float Dd = FVector::Dist(It->GetActorLocation(), Target);
					if (Dd < BestD) { BestD = Dd; Best = *It; }
				}
				if (!Best) { Phone->Toast(TEXT("No door in crosshair"), FColor::Orange, 2.5f); return; }
				Best->SetResident(FString());
				const FVector DLoc = Best->GetActorLocation();
				FString Cur;
				FFileHelper::LoadFileToString(Cur, *(FPaths::ProjectSavedDir() / TEXT("LockedDoors.txt")));
				Cur += FString::Printf(TEXT("%.1f,%.1f,%.1f\n"), DLoc.X, DLoc.Y, DLoc.Z);
				FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("LockedDoors.txt")));
				Phone->Toast(TEXT("Door locked (saved)"), FColor::Cyan, 3.f);
			}, 12);
		BodyRow(LockDoorB, FMargin(0.f, 6.f, 0.f, 2.f));
		UWeedActionButton* LockDoorXB = MakeActionBtn(TEXT("Clear locked doors"), WeedUI::ColWarn(),
			[this]()
			{
				WeedData::DeleteFile(TEXT("LockedDoors.txt"));
				if (Phone.IsValid()) { Phone->Toast(TEXT("Locked doors cleared (restart restores)"), FColor::Orange, 4.f); }
			}, 11);
		BodyRow(LockDoorXB, FMargin(0.f, 0.f, 0.f, 8.f));

		// Deur die naast z'n kozijn staat: richt erop + klik -> springt naar het dichtstbijzijnde deur-kozijn.
		// Permanent via Saved/DoorSnaps.txt (DoorRetrofitter zet 'm elke sessie terug op de juiste plek).
		UWeedActionButton* SnapDoorB = MakeActionBtn(TEXT("Snap door to frame"), WeedUI::ColAccentDim(),
			[this]()
			{
				APlayerController* PC = GetOwningPlayer();
				UWorld* DW = GetWorld();
				if (!PC || !DW || !Phone.IsValid()) { return; }
				FVector VL; FRotator VR; PC->GetPlayerViewPoint(VL, VR);
				FHitResult Hit; FCollisionQueryParams Q; Q.AddIgnoredActor(PC->GetPawn());
				const bool bHit = DW->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 2500.f, ECC_Visibility, Q);
				const FVector Aim = bHit ? Hit.ImpactPoint : VL + VR.Vector() * 600.f;
				ACityDoor* Best = nullptr; float BestD = 250.f;
				for (TActorIterator<ACityDoor> It(DW); It; ++It) { const float Dd = FVector::Dist(It->GetActorLocation(), Aim); if (Dd < BestD) { BestD = Dd; Best = *It; } }
				if (!Best) { Phone->Toast(TEXT("No door in crosshair"), FColor::Orange, 2.5f); return; }
				// Markeer de HUIDIGE (foute) plek -> opslaan, en snap meteen. De DoorRetrofitter snapt elke
				// sessie de deur bij deze positie automatisch in z'n kozijn (gedeelde helper -> zelfde resultaat).
				const FVector DLoc = Best->GetActorLocation();
				ACityDoor::SnapToNearestFrame(DW, Best);
				FString Cur;
				FFileHelper::LoadFileToString(Cur, *(FPaths::ProjectSavedDir() / TEXT("DoorSnaps.txt")));
				Cur += FString::Printf(TEXT("%.1f,%.1f,%.1f\n"), DLoc.X, DLoc.Y, DLoc.Z);
				FFileHelper::SaveStringToFile(Cur, *(FPaths::ProjectSavedDir() / TEXT("DoorSnaps.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				Phone->Toast(TEXT("Door snapped to frame (saved)"), FColor::Cyan, 3.f);
			}, 12);
		BodyRow(SnapDoorB, FMargin(0.f, 4.f, 0.f, 2.f));
		UWeedActionButton* SnapDoorXB = MakeActionBtn(TEXT("Clear door snaps"), WeedUI::ColWarn(),
			[this]()
			{
				WeedData::DeleteFile(TEXT("DoorSnaps.txt"));
				if (Phone.IsValid()) { Phone->Toast(TEXT("Door snaps cleared (restart restores)"), FColor::Orange, 4.f); }
			}, 11);
		BodyRow(SnapDoorXB, FMargin(0.f, 0.f, 0.f, 8.f));

		// Dev-opruimer: kijk naar een (zwevende of foute) deur, open de phone en klik - deur weg.
		UWeedActionButton* KillDoorB = MakeActionBtn(TEXT("Remove door in crosshair"), WeedUI::ColWarn(),
			[this]()
			{
				APlayerController* PC = GetOwningPlayer();
				UWorld* DW = GetWorld();
				if (!PC || !DW) { return; }
				FVector VL; FRotator VR;
				PC->GetPlayerViewPoint(VL, VR);
				FHitResult Hit;
				FCollisionQueryParams Q;
				Q.AddIgnoredActor(PC->GetPawn());
				const bool bHit = DW->LineTraceSingleByChannel(Hit, VL, VL + VR.Vector() * 2500.f, ECC_Visibility, Q);
				const FVector Target = bHit ? Hit.ImpactPoint : VL + VR.Vector() * 600.f;
				ACityDoor* Best = nullptr; float BestD = 250.f;
				for (TActorIterator<ACityDoor> It(DW); It; ++It)
				{
					if (!IsValid(*It)) { continue; }
					const float D = FVector::Dist(It->GetActorLocation(), Target);
					if (D < BestD) { BestD = D; Best = *It; }
				}
				if (Best) { Best->Destroy(); }
			}, 11);
		BodyRow(KillDoorB, FMargin(0.f, 6.f, 0.f, 0.f));
	}
	else if (SettingsCat == 3) // Light: live light-tuning (stuurt de lokale DayNightController direct aan)
	{
		BodyRow(MakeText(TEXT("Lighting (live)"), 14, WeedUI::ColText()), FMargin(0.f, 0.f, 0.f, 2.f));
		ADayNightController* DN = ADayNightController::GetLocal(GetWorld());
		const bool bPackL = DN && DN->IsPackMinimal();
		const float Moon = DN ? DN->MoonIntensity : 0.65f;
		const float Sun  = DN ? DN->SunIntensity  : 6.5f;
		const float Lmp  = DN ? DN->LampIntensity  : 28000.f;
		// Norm = (waarde - min) / (max - min) per regelaar.
		AddLightSlider(TEXT("Moon (night)"),  Moon / 3.f,            LMoon,  LMoonV);
		AddLightSlider(TEXT("Sun (day)"),     Sun / 12.f,            LSun,   LSunV);
		AddLightSlider(TEXT("Street lamps"),  Lmp / 80000.f,         LLamp,  LLampV);
		if (bPackL)
		{
			// Beach-map: de knoppen van het nieuwe dag/nacht-systeem.
			AddLightSlider(TEXT("Night dark"),     DN->NightGain,                   LSkyN,  LSkyNV);
			AddLightSlider(TEXT("Night exposure"), (DN->NightExposure + 4.f) / 4.f, LSkyD,  LSkyDV);
			AddLightSlider(TEXT("Day glow"),       DN->DayBloom / 1.5f,             LPitch, LPitchV);
			AddLightSlider(TEXT("Sun haze"),       DN->SunHaze / 0.008f,            LExp,   LExpV);
		}
		else
		{
			const float SkN  = DN ? DN->SkyNight       : 0.85f;
			const float SkD  = DN ? DN->SkyDay         : 1.0f;
			const float Pit  = DN ? DN->MoonPitch      : -52.f;
			const float Exp  = DN ? DN->ExposureBias   : 9.f;
			AddLightSlider(TEXT("Sky night"),     SkN / 2.f,             LSkyN,  LSkyNV);
			AddLightSlider(TEXT("Sky day"),       SkD / 2.f,             LSkyD,  LSkyDV);
			AddLightSlider(TEXT("Moon angle"),    (Pit + 90.f) / 90.f,   LPitch, LPitchV);
			AddLightSlider(TEXT("Exposure"),      Exp / 16.f,            LExp,   LExpV);
		}
		ApplyLightSliders(); // labels meteen vullen met de echte waardes

		UWeedActionButton* SaveB = MakeActionBtn(TEXT("Save light config"), WeedUI::ColGood(0.5f),
			[this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SaveLightConfig(); } }, 12);
		BodyRow(SaveB, FMargin(0.f, 8.f, 0.f, 0.f));
	}
	else if (SettingsCat == 4) // Spots: F9-markers bekijken / teleporteren / verwijderen
	{
		{
			BodyRow(MakeText(TEXT("Marked spots"), 14, WeedUI::ColText()), FMargin(0.f, 0.f, 0.f, 2.f));
			const FString SpotFile = FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt");
			TArray<FString> SpotLines;
			FFileHelper::LoadFileToStringArray(SpotLines, *SpotFile);
			SpotLines.RemoveAll([](const FString& L) { return L.TrimStartAndEnd().IsEmpty(); });
			if (SpotLines.Num() == 0)
			{
				BodyRow(MakeText(TEXT("No spots yet - press F9 in-game to mark one."), 11, WeedUI::ColTextDim()), FMargin(0.f, 0.f, 0.f, 4.f));
			}
			const int32 MaxShow = 20;
			for (int32 SpotIdx = FMath::Max(0, SpotLines.Num() - MaxShow); SpotIdx < SpotLines.Num(); ++SpotIdx)
			{
				const FString& Line = SpotLines[SpotIdx];
				// "label | map=/Game/... | pos=(x, y, z) | yaw=NN"
				FString Label = Line; FString PosStr;
				int32 Bar = INDEX_NONE;
				if (Label.FindChar(TEXT('|'), Bar)) { Label = Label.Left(Bar).TrimStartAndEnd(); }
				FVector SpotPos = FVector::ZeroVector; bool bHasPos = false;
				{
					const int32 PIdx = Line.Find(TEXT("pos=("));
					if (PIdx != INDEX_NONE)
					{
						PosStr = Line.Mid(PIdx + 5);
						int32 Close = INDEX_NONE;
						if (PosStr.FindChar(TEXT(')'), Close)) { PosStr = PosStr.Left(Close); }
						TArray<FString> Parts;
						PosStr.ParseIntoArray(Parts, TEXT(","));
						if (Parts.Num() >= 3)
						{
							SpotPos = FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
							bHasPos = true;
						}
					}
				}
				const FString CurMap = GetWorld() ? GetWorld()->GetOutermost()->GetName() : FString();
				const bool bSameMap = !Line.Contains(TEXT("map=")) || Line.Contains(CurMap);

				UHorizontalBox* RowB = WidgetTree->ConstructWidget<UHorizontalBox>();
				UHorizontalBoxSlot* LS2 = RowB->AddChildToHorizontalBox(MakeText(
					FString::Printf(TEXT("%s  (%.0f, %.0f)"), *Label, SpotPos.X, SpotPos.Y), 11,
					bSameMap ? WeedUI::ColText() : WeedUI::ColTextDim()));
				LS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS2->SetVerticalAlignment(VAlign_Center);
				if (bHasPos && bSameMap)
				{
					RowB->AddChildToHorizontalBox(MakeActionBtn(TEXT("TP"), WeedUI::ColAccentDim(),
						[this, SpotPos]()
						{
							if (APawn* Pn = GetOwningPlayerPawn()) { Pn->SetActorLocation(SpotPos + FVector(0.f, 0.f, 60.f)); }
						}, 11))->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
				}
				const FString RawLine = Line; // exacte regel-inhoud -> per-content matchen (stabiel na eerdere deletes)
				RowB->AddChildToHorizontalBox(MakeActionBtn(TEXT("X"), WeedUI::ColWarn(),
					[this, RawLine, RowB]()
					{
						const FString F = FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt");
						TArray<FString> Ls;
						FFileHelper::LoadFileToStringArray(Ls, *F);
						Ls.RemoveAll([](const FString& L) { return L.TrimStartAndEnd().IsEmpty(); });
						// Verwijder de EERSTE regel die exact overeenkomt (index-vrij -> ook correct na eerdere deletes).
						const int32 Found = Ls.IndexOfByKey(RawLine);
						if (Found != INDEX_NONE) { Ls.RemoveAt(Found); }
						FFileHelper::SaveStringToFile(FString::Join(Ls, TEXT("\n")) + (Ls.Num() ? TEXT("\n") : TEXT("")), *F);
						// Alleen DEZE regel uit de body halen (geen hele body-herbouw -> geen flash).
						if (RowB) { RowB->RemoveFromParent(); }
					}, 11))->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
				BodyRow(RowB, FMargin(0.f, 1.f, 0.f, 1.f));
			}
			if (SpotLines.Num() > MaxShow)
			{
				BodyRow(MakeText(FString::Printf(TEXT("(showing last %d of %d)"), MaxShow, SpotLines.Num()), 9, WeedUI::ColTextDim()), FMargin(0.f, 2.f, 0.f, 0.f));
			}
		}
	}
	else // Status
	{
		// --- Character skin: kies man of vrouw (gerepliceerd zodat je co-op maat je skin ziet) ---
		{
			IPlayerNpcActions* Skn = Cast<IPlayerNpcActions>(GetOwningPlayerPawn());
			const uint8 Cur = Skn ? Skn->GetPlayerSkinIndex() : 0;
			BodyRow(MakeText(TEXT("Character"), 14, WeedUI::ColText()), FMargin(0.f, 0.f, 0.f, 2.f));
			// Alleen Man/Vrouw hier; het EXACTE model (Casual/Gamer/School of Tony/Citizen) kies je in de Wardrobe.
			const bool bMale = (Cur == 5 || Cur == 6 || Cur == 0);
			UHorizontalBox* GBtns = WidgetTree->ConstructWidget<UHorizontalBox>();
			// Toggle-klik verandert alleen de 2 knop-kleuren in-place (mirror RefreshSettingsTabs) -> geen body-herbouw.
			auto RefreshSkin = [this]()
			{
				IPlayerNpcActions* S = Cast<IPlayerNpcActions>(GetOwningPlayerPawn());
				const uint8 c = S ? S->GetPlayerSkinIndex() : 0;
				const bool bM = (c == 5 || c == 6 || c == 0);
				auto Style = [this](UWeedActionButton* B, bool bSel)
				{
					if (!B) { return; }
					const FLinearColor Col = bSel ? WeedUI::ColAccentDim() : WeedUI::ColSlot();
					FButtonStyle St;
					St.Normal = RoundedBrush(Col, 8.f);
					St.Hovered = RoundedBrush(Col * 1.3f, 8.f);
					St.Pressed = RoundedBrush(Col * 0.8f, 8.f);
					St.NormalPadding = FMargin(6.f, 4.f); St.PressedPadding = FMargin(6.f, 4.f);
					B->SetStyle(St);
				};
				Style(SkinMaleBtn, bM); Style(SkinFemaleBtn, !bM);
			};
			SkinMaleBtn = MakeActionBtn(TEXT("Male"),
				bMale ? WeedUI::ColAccentDim() : WeedUI::ColSlot(),
				[this, RefreshSkin]() { if (IPlayerNpcActions* S = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { const uint8 c = S->GetPlayerSkinIndex(); if (c != 5 && c != 6) { S->SetPlayerSkinIndex(5); } } RefreshSkin(); }, 13);
			GBtns->AddChildToHorizontalBox(SkinMaleBtn)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			SkinFemaleBtn = MakeActionBtn(TEXT("Female"),
				!bMale ? WeedUI::ColAccentDim() : WeedUI::ColSlot(),
				[this, RefreshSkin]() { if (IPlayerNpcActions* S = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { const uint8 c = S->GetPlayerSkinIndex(); if (c < 2 || c > 4) { S->SetPlayerSkinIndex(2); } } RefreshSkin(); }, 13);
			GBtns->AddChildToHorizontalBox(SkinFemaleBtn)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			BodyRow(GBtns, FMargin(0.f, 0.f, 0.f, 4.f));
			BodyRow(MakeText(TEXT("Pick the exact model in the Wardrobe."), 10, WeedUI::ColTextDim()), FMargin(0.f, 0.f, 0.f, 10.f));
		}
		// Co-op: toon level/XP/heat van de LOKALE speler (eigenaar van deze widget), niet van de host.
		APawn* OwnerPawn = GetOwningPlayerPawn();
		if (GS && GS->GetLeveling())
		{
			ULevelComponent* Lv = GS->GetLeveling();
			BodyRow(MakeText(FString::Printf(TEXT("Level %d"), Lv->GetLevelFor(OwnerPawn)), 15, WeedUI::ColGood()), FMargin(0.f, 0.f, 0.f, 2.f));
			UProgressBar* XpBar = WidgetTree->ConstructWidget<UProgressBar>();
			XpBar->SetPercent(Lv->GetLevelFractionFor(OwnerPawn));
			XpBar->SetFillColorAndOpacity(WeedUI::ColAccent());
			BodyRow(XpBar, FMargin(0.f, 2.f, 0.f, 4.f));
			BodyRow(MakeText(Lv->GetLevelFor(OwnerPawn) >= ULevelComponent::MaxLevel ? TEXT("MAX")
				: *FString::Printf(TEXT("%d / %d XP"), Lv->GetCurrentXPFor(OwnerPawn), Lv->GetXPToNextFor(OwnerPawn)), 12, WeedUI::ColTextDim()), FMargin(0.f, 0.f, 0.f, 6.f));
		}
		if (GS && GS->GetHeat())
		{
			BodyRow(MakeText(TEXT("Heat"), 14, FLinearColor(1.f, 0.7f, 0.6f)), FMargin(0.f, 0.f, 0.f, 2.f));
			UProgressBar* HeatBar = WidgetTree->ConstructWidget<UProgressBar>();
			HeatBar->SetPercent(GS->GetHeat()->GetHeatFor(OwnerPawn) / 100.f);
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
	Slider->SetSliderBarColor(WeedUI::ColSlot());
	Slider->SetSliderHandleColor(WeedUI::ColAccent());
	OutS = Slider;

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(MakeText(Label, 12, WeedUI::ColTextDim()));
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	USizeBox* SSz = WidgetTree->ConstructWidget<USizeBox>();
	SSz->SetWidthOverride(150.f); SSz->SetHeightOverride(18.f); SSz->SetContent(Slider);
	Row->AddChildToHorizontalBox(SSz);
	OutV = MakeText(TEXT(""), 12, WeedUI::ColText(), true);
	USizeBox* VSz = WidgetTree->ConstructWidget<USizeBox>();
	VSz->SetWidthOverride(56.f); VSz->SetContent(OutV);
	Row->AddChildToHorizontalBox(VSz);

	if (SettingsBody) { SettingsBody->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f)); }
	return Slider;
}

void UPhoneWidget::ApplyLightSliders()
{
	// Tijd-versnelling (1x-8x) live toepassen terwijl je sleept.
	if (TimeSpeedSlider && GetWorld())
	{
		const float Speed = 1.f + TimeSpeedSlider->GetValue() * 7.f;
		if (FMath::Abs(Speed - UGameplayStatics::GetGlobalTimeDilation(GetWorld())) > 0.01f)
		{
			UGameplayStatics::SetGlobalTimeDilation(GetWorld(), Speed);
		}
		if (TimeSpeedV) { TimeSpeedV->SetText(FText::FromString(FString::Printf(TEXT("%.1fx"), Speed))); }
	}

	ADayNightController* DN = ADayNightController::GetLocal(GetWorld());
	if (!DN || !LMoon) { return; }
	DN->MoonIntensity = LMoon->GetValue()  * 3.f;
	DN->SunIntensity  = LSun  ? LSun->GetValue()  * 12.f    : DN->SunIntensity;
	DN->LampIntensity = LLamp ? LLamp->GetValue() * 80000.f : DN->LampIntensity;
	if (LMoonV)  { LMoonV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->MoonIntensity))); }
	if (LSunV)   { LSunV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->SunIntensity))); }
	if (LLampV)  { LLampV->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), DN->LampIntensity))); }

	if (DN->IsPackMinimal())
	{
		// Beach-map: nieuwe dag/nacht-knoppen.
		if (LSkyN)  { DN->NightGain     = LSkyN->GetValue(); }
		if (LSkyD)  { DN->NightExposure = LSkyD->GetValue() * 4.f - 4.f; }
		if (LPitch) { DN->DayBloom      = LPitch->GetValue() * 1.5f; }
		if (LExp)   { DN->SunHaze       = LExp->GetValue() * 0.008f; }
		if (LSkyNV)  { LSkyNV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->NightGain))); }
		if (LSkyDV)  { LSkyDV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->NightExposure))); }
		if (LPitchV) { LPitchV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->DayBloom))); }
		if (LExpV)   { LExpV->SetText(FText::FromString(FString::Printf(TEXT("%.4f"), DN->SunHaze))); }
	}
	else
	{
		DN->SkyNight      = LSkyN ? LSkyN->GetValue() * 2.f     : DN->SkyNight;
		DN->SkyDay        = LSkyD ? LSkyD->GetValue() * 2.f     : DN->SkyDay;
		DN->MoonPitch     = LPitch? LPitch->GetValue() * 90.f - 90.f : DN->MoonPitch;
		DN->ExposureBias  = LExp  ? LExp->GetValue()  * 16.f    : DN->ExposureBias;
		if (LSkyNV)  { LSkyNV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->SkyNight))); }
		if (LSkyDV)  { LSkyDV->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), DN->SkyDay))); }
		if (LPitchV) { LPitchV->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), DN->MoonPitch))); }
		if (LExpV)   { LExpV->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), DN->ExposureBias))); }
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
		if (bSellApp)
		{
			// Sell-app: puur de verkoopwaarde van de cart (altijd "+", je ontvangt geld).
			const int32 Sell = Phone->GetCartSellCents();
			StoreCartText->SetText(FText::FromString(FString::Printf(TEXT("Cart %d   +EUR %d"), Phone->GetCartNumLines(), (int32)(WeedRoundEuros((int64)Sell) / 100))));
			return;
		}
		// Toon het NETTO bedrag (koop + bezorging - verkoop). Negatief = je ontvangt geld (+).
		const int32 Net = Phone->GetCartNetCents(DeliveryOpt);
		const FString Amt = (Net < 0)
			? FString::Printf(TEXT("+EUR %d"), (int32)(WeedRoundEuros((int64)(-Net)) / 100))
			: FString::Printf(TEXT("EUR %d"), (int32)(WeedRoundEuros((int64)Net) / 100));
		StoreCartText->SetText(FText::FromString(FString::Printf(TEXT("Cart %d   %s"), Phone->GetCartNumLines(), *Amt)));
	}
}

bool UPhoneWidget::IsMsgForLocal(const FPhoneMessage& M) const
{
	// Leeg ForPlayerId = voor iedereen (co-op). In competitive zie je alleen je EIGEN afspraken.
	if (M.ForPlayerId.IsEmpty()) { return true; }
	const APawn* P = GetOwningPlayerPawn();
	return P && USaveGameSubsystem::StablePlayerId(P) == M.ForPlayerId;
}

FLinearColor UPhoneWidget::UrgencyColor(float Frac, bool bReply)
{
	if (Frac < 0.2f)  { return FLinearColor(0.92f, 0.28f, 0.22f); } // rood - bijna op
	if (Frac < 0.45f) { return FLinearColor(0.95f, 0.7f, 0.2f); }   // geel/oranje - loopt af
	// Vers: BLAUW als 't op JOU wacht (accept/decline), GROEN als de afspraak loopt -> direct onderscheid.
	return bReply ? FLinearColor(0.35f, 0.62f, 0.98f) : FLinearColor(0.35f, 0.8f, 0.45f);
}

bool UPhoneWidget::GetApptUrgency(FName ContactId, float& OutFrac, int32& OutSecsLeft, int32& OutPhase, int32& OutClockMins) const
{
	OutFrac = 0.f; OutSecsLeft = 0; OutPhase = 0; OutClockMins = 0;
	if (ContactId.IsNone() || !GetWorld()) { return false; }
	// Competitive: koppel de live-customer-match aan de LOKALE speler, anders toont de balk de afspraak-NPC
	// van de tegenstander (zelfde NpcId, andere eigenaar). ApptForPlayerId leeg = gedeeld/co-op -> altijd match.
	const AWeedShopGameState* GScomp = GetWorld()->GetGameState<AWeedShopGameState>();
	const bool bCompFilter = GScomp && GScomp->IsCompetitive();
	const APawn* MeP = GetOwningPlayerPawn();
	const FString MyId = (bCompFilter && MeP) ? USaveGameSubsystem::StablePlayerId(MeP) : FString();
	// Fase B: een live klant met een lopende afspraak -> z'n ApptTimeout-fractie (telt af tot 'ie opgeeft).
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		if (It->NpcId == ContactId && It->HasActiveAppointment())
		{
			// Afspraak-NPC van de tegenstander overslaan (leeg = gedeeld, altijd tonen).
			if (bCompFilter && !It->ApptForPlayerId.IsEmpty() && It->ApptForPlayerId != MyId) { continue; }
			OutFrac = It->GetApptFraction();
			OutSecsLeft = FMath::CeilToInt(It->GetApptTimeLeft());
			OutPhase = 1;
			return true;
		}
	}
	// Nog geen klant -> kijk naar het afspraak-bericht zelf.
	AWeedShopGameState* GS = GetWorld()->GetGameState<AWeedShopGameState>();
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	UDayCycleComponent* Day = GS ? GS->GetDayCycle() : nullptr;
	if (!Con) { return false; }
	const float NowReal = GetWorld()->GetTimeSeconds();
	const float NowDay = Day ? Day->GetTimeOfDaySeconds() : 0.f;
	const float Length = Day ? FMath::Max(1.f, Day->DayLengthSeconds + Day->NightLengthSeconds) : 1.f;
	for (const FPhoneMessage& M : Con->GetMessages())
	{
		if (!IsMsgForLocal(M) || M.FromContactId != ContactId || M.bFromMe) { continue; }
		if (M.AppointmentTimeOfDay < 0.f) { continue; } // geen afspraak-bericht -> geen balk
		if (M.Status == 0)
		{
			// Fase 2: wacht op JOUW antwoord (accept/decline) - telt af tot de klant opgeeft. Leest de
			// gedeelde ContactsComponent-constante zodat de balk NOOIT uiteenloopt met de echte cancel (D.13).
			const float Left = UContactsComponent::ResponseWindowSec - (NowReal - M.SentRealTime);
			OutFrac = FMath::Clamp(Left / UContactsComponent::ResponseWindowSec, 0.f, 1.f);
			OutSecsLeft = FMath::CeilToInt(FMath::Max(0.f, Left));
			OutPhase = 2;
			return true;
		}
		if (M.Status == 1 && Day)
		{
			// Fase 0: geaccepteerd, klant nog niet gespawnd -> tijd tot het afspraak-MOMENT. Deler = de max
			// afspraak-offset (gedeelde constante) zodat de balk-schaal meeloopt met de echte offset-range (D.13).
			float Remaining = M.AppointmentTimeOfDay - NowDay;
			if (Remaining < 0.f) { Remaining += Length; }
			OutFrac = FMath::Clamp(Remaining / UContactsComponent::ApptOffsetMaxSec, 0.f, 1.f);
			OutSecsLeft = FMath::CeilToInt(Remaining);
			OutClockMins = Con->ClockMinutesOf(M.AppointmentTimeOfDay); // kloktijd van de afspraak (bv. 11:00)
			OutPhase = 0;
			return true;
		}
	}
	return false;
}

int32 UPhoneWidget::MessagesSignature() const
{
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return 0; }
	const TArray<FPhoneMessage>& Msgs = Con->GetMessages();

	if (OpenChatContact.IsNone())
	{
		// GESPREKKENLIJST: STRUCTURELE sig = welke contacten er zijn + hun afspraak-fase. NIET het bericht-aantal,
		// de inhoud, tijdstempels of ongelezen-aantallen -> die werkt UpdateListBarsLive live in-place bij
		// (preview/badge/timestamp/kleur). Zo herbouwt de lijst NIET bij elk nieuw bericht van een BESTAAND contact
		// (dat gaf constant geflits); alleen een NIEUW/weg contact of een afspraak-fase-overgang herbouwt + hersorteert.
		int32 Sig = 0, N = 0;
		TArray<FName> Seen;
		for (const FPhoneMessage& M : Msgs)
		{
			if (!IsMsgForLocal(M) || Seen.Contains(M.FromContactId)) { continue; }
			Seen.Add(M.FromContactId);
			float f; int32 s, p, c; const bool bU = GetApptUrgency(M.FromContactId, f, s, p, c);
			Sig = Sig * 31 + (int32)GetTypeHash(M.FromContactId) + (bU ? (p + 1) * 101 : 0);
			++N;
		}
		return Sig * 13 + N;
	}

	// OPEN THREAD: berichten van DIT contact (een nieuw bericht in de open chat voegt wél een bubble toe).
	int32 Sig = 0, Cnt = 0;
	for (const FPhoneMessage& M : Msgs)
	{
		if (!IsMsgForLocal(M) || M.FromContactId != OpenChatContact) { continue; }
		++Cnt;
		Sig = Sig * 31 + (int32)M.Status + (M.bFromMe ? 5 : 0);
	}
	return Sig * 1000003 + Cnt;
}

int32 UPhoneWidget::ContactsSignature() const
{
	// STRUCTURELE sig van de Contacts-app: ALLEEN de contact-SET (aantal + hash van de contact-ids). NIET de
	// relationship-waarde, afspraak-fase of berichten -> een nieuw bericht/afspraak van een BESTAAND contact
	// herbouwt de Contacts-lijst niet (dat gaf flits via de gedeelde MessagesSignature). Alleen een NIEUW/weg
	// contact wijzigt deze sig -> dan herbouwt de lijst één keer.
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return 0; }
	int32 Sig = 0, N = 0;
	for (const FPhoneContact& C : Con->GetContacts())
	{
		Sig = Sig * 31 + (int32)GetTypeHash(C.ContactId);
		++N;
	}
	return Sig * 13 + N;
}

int32 UPhoneWidget::UpgradesSignature() const
{
	// STRUCTURELE sig van de Upgrades-app (app 0): woning-bezit (welke panden van jou zijn) + de backpack-tier
	// van de LOKALE speler. Wijzigt na een woning-koop/verkoop of een backpack-upgrade -> paneel herbouwt één keer
	// (MarkDirty), zodat de knoppen/tekst meteen kloppen zonder de app opnieuw te openen. Geen live-drift erin.
	if (!Phone.IsValid()) { return -1; }
	int32 Sig = 0;
	TArray<FCityPropertyOffer> Offers;
	Phone->GetPropertyOffers(Offers);
	for (const FCityPropertyOffer& O : Offers)
	{
		Sig = Sig * 31 + O.HomeIndex * 2 + (Phone->IsPropertyOwned(O.HomeIndex) ? 1 : 0);
	}
	int32 Tier = 0;
	if (APawn* P = GetOwningPlayerPawn())
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>()) { Tier = Inv->GetBackpackTier(); }
	}
	// + Horloge-bezit (gedeelde upgrade, gerepliceerd): na de koop herbouwt het paneel een keer -> "owned".
	int32 Watch = 0;
	if (const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr)
	{
		if (GS->GetUpgrades() && GS->GetUpgrades()->HasWatch()) { Watch = 1; }
	}
	return (Sig * 17 + Tier) * 2 + Watch;
}

int32 UPhoneWidget::BankUnlockSignature() const
{
	// MINIMALE structurele sig: ALLEEN de mobile-banking/ATM-beschikbaarheid. Flipt deze staat, dan wisselt de
	// upgrade-prompt <-> bank-kaart en mag het bank-paneel één keer herbouwen. Saldo/cash/transfers zitten er
	// bewust NIET in -> die werken in-place bij (geen flash).
	if (!Phone.IsValid()) { return -1; }
	return (Phone->IsBankAppUnlocked() || Phone->IsBankViaAtm()) ? 1 : 0;
}

// Bouwt de Berichten-app als een PERSISTENTE shell in ActiveContent: één lijst-scroll + één thread-root met
// alle vaste sub-secties. Dit draait maar één keer per paneel (bij bNew/prewarm-rebuild); daarna verversen
// RefreshChatViews/List/Thread alles in-place, zodat GEEN enkele chat-interactie of binnenkomend bericht nog
// het paneel via MarkDirty herbouwt (geen flash).
void UPhoneWidget::BuildChatApp()
{
	// Alle chat-refs nullen: een verse BuildChatApp hoort bij een net-geleegd paneel (RefreshContent ClearChildren).
	ApptBar = nullptr; ApptBarLabel = nullptr; ApptBarContact = NAME_None; // alleen geldig in een actieve-afspraak-thread
	WaitBar = nullptr; WaitBarLabel = nullptr; WaitBarSentTime = -1.f;     // wacht-balk voor een open deal-bericht
	OfferBox = nullptr; OfferToggleBtn = nullptr;                          // "Offer instead"-box (gevuld bij open thread)
	ListApptBars.Reset(); ListCards.Reset(); ListPreviews.Reset();         // lijst-urgentie-widgets (live bijgewerkt)
	ListBadges.Reset(); ListBadgeTexts.Reset(); ListStamps.Reset();        // lijst-badge/timestamp per contact (live bijgewerkt)
	ChatBubblePool.Reset(); ChatBubbleSigs.Reset();                        // bubbel-pool (verse scroll)
	ChatListScroll = nullptr; ChatThreadRoot = nullptr;
	ChatHeaderName = nullptr; ChatTierBox = nullptr; ChatTierLabel = nullptr; ChatTierBar = nullptr;
	ChatApptBox = nullptr; ChatBubbleScroll = nullptr; ChatNoMsgText = nullptr; ChatWaitBox = nullptr;
	ChatRespondRow = nullptr; ChatOfferSection = nullptr; ChatPickerPrompt = nullptr; ChatProposeBtn = nullptr;
	PickerClockText = nullptr; PickerContact = NAME_None;
	if (!Phone.IsValid() || !ActiveContent) { return; }
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { AddInfoRow(TEXT("No messages."), WeedUI::ColTextDim()); return; }

	// ---- Lijst + thread stapelen in een OVERLAY (zelfde ruimte) zodat de INACTIEVE view op HIDDEN kan (blijft
	// gearrangeerd + gerealiseerd, alleen niet getekend) i.p.v. Collapsed. Zo is switchen list<->thread én naar
	// Messages toe instant, zonder her-layout-flikker. In een VerticalBox zou Hidden allebei ruimte laten innemen. ----
	UOverlay* ChatOv = WidgetTree->ConstructWidget<UOverlay>();
	UVerticalBoxSlot* COS = ActiveContent->AddChildToVerticalBox(ChatOv);
	COS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// ---- Gesprekkenlijst-container (persistent; kaarten reconciliëren in RefreshChatList) ----
	ChatListScroll = WidgetTree->ConstructWidget<UScrollBox>();
	UOverlaySlot* LS = ChatOv->AddChildToOverlay(ChatListScroll);
	LS->SetHorizontalAlignment(HAlign_Fill); LS->SetVerticalAlignment(VAlign_Fill);

	// ---- Thread-container (persistent; alle sub-secties vast, in-place bijgewerkt in RefreshChatThread) ----
	ChatThreadRoot = WidgetTree->ConstructWidget<UVerticalBox>();
	UOverlaySlot* TRS = ChatOv->AddChildToOverlay(ChatThreadRoot);
	TRS->SetHorizontalAlignment(HAlign_Fill); TRS->SetVerticalAlignment(VAlign_Fill);

	// Header: < Chats-knop + contact-naam (naam-tekst in-place bijgewerkt).
	{
		UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
		Head->AddChildToHorizontalBox(MakeActionBtn(TEXT("< Chats"), WeedUI::ColAccentDim(),
			[this]() { OpenChatContact = NAME_None; bOfferStrainView = false; RefreshChatViews(); }, 11))->SetVerticalAlignment(VAlign_Center);
		ChatHeaderName = MakeText(TEXT(""), 15, WeedUI::ColText());
		UHorizontalBoxSlot* NS = Head->AddChildToHorizontalBox(ChatHeaderName);
		NS->SetVerticalAlignment(VAlign_Center); NS->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
		ChatThreadRoot->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	// Klant-tier + XP-balk naar de volgende tier (getoond/verborgen + tekst/percent in-place).
	{
		ChatTierBox = WidgetTree->ConstructWidget<UBorder>();
		ChatTierBox->SetBrush(RoundedBrush(WeedUI::ColInner(0.95f), 8.f));
		ChatTierBox->SetPadding(FMargin(8.f, 5.f, 8.f, 6.f));
		UVerticalBox* TV = WidgetTree->ConstructWidget<UVerticalBox>();
		ChatTierBox->SetContent(TV);
		ChatTierLabel = MakeText(TEXT(""), 11, WeedUI::ColText());
		TV->AddChildToVerticalBox(ChatTierLabel);
		ChatTierBar = WidgetTree->ConstructWidget<UProgressBar>();
		ChatTierBar->SetFillColorAndOpacity(FLinearColor(0.45f, 0.75f, 1.f));
		TV->AddChildToVerticalBox(ChatTierBar)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		ChatThreadRoot->AddChildToVerticalBox(ChatTierBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	// Afspraak-balk (fase 0=arrives / 1=at door). Box + label + bar persistent; ApptBar wordt hier gezet en
	// door UpdateApptBarLive live bijgewerkt (de box wordt getoond/verborgen in RefreshChatThread).
	{
		ChatApptBox = WidgetTree->ConstructWidget<UBorder>();
		ChatApptBox->SetBrush(RoundedBrush(WeedUI::ColInner(0.95f), 8.f));
		ChatApptBox->SetPadding(FMargin(8.f, 5.f, 8.f, 6.f));
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		ChatApptBox->SetContent(VB);
		ApptBarLabel = MakeText(TEXT("Waiting..."), 11, WeedUI::ColTextDim());
		VB->AddChildToVerticalBox(ApptBarLabel);
		ApptBar = WidgetTree->ConstructWidget<UProgressBar>();
		VB->AddChildToVerticalBox(ApptBar)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		ChatThreadRoot->AddChildToVerticalBox(ChatApptBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	// Berichten-scroll (bubbel-pool) + "geen berichten"-tekst.
	{
		ChatBubbleScroll = WidgetTree->ConstructWidget<UScrollBox>();
		UVerticalBoxSlot* TS = ChatThreadRoot->AddChildToVerticalBox(ChatBubbleScroll);
		TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ChatNoMsgText = MakeText(TEXT("No messages with this contact yet."), 12, WeedUI::ColTextDim());
		ChatThreadRoot->AddChildToVerticalBox(ChatNoMsgText);
	}

	// Wacht-balk (onder een open deal-bericht): box + label + bar persistent; live in UpdateWaitBarLive.
	{
		ChatWaitBox = WidgetTree->ConstructWidget<UBorder>();
		ChatWaitBox->SetBrush(RoundedBrush(WeedUI::ColInner(0.95f), 8.f));
		ChatWaitBox->SetPadding(FMargin(8.f, 5.f, 8.f, 6.f));
		UVerticalBox* WVB = WidgetTree->ConstructWidget<UVerticalBox>();
		ChatWaitBox->SetContent(WVB);
		WaitBarLabel = MakeText(TEXT("Waiting for your reply..."), 11, WeedUI::ColTextDim());
		WVB->AddChildToVerticalBox(WaitBarLabel);
		WaitBar = WidgetTree->ConstructWidget<UProgressBar>();
		WaitBar->SetPercent(1.f);
		WVB->AddChildToVerticalBox(WaitBar)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		ChatThreadRoot->AddChildToVerticalBox(ChatWaitBox)->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
	}

	// Accept/Decline-rij (alleen bij een open afspraak getoond).
	{
		ChatRespondRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* AS = ChatRespondRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("Accept"), WeedUI::ColAccentDim(),
			[this]() { if (Phone.IsValid() && !OpenChatContact.IsNone()) { Phone->RespondChat(OpenChatContact, true); } RefreshChatThread(); }, 13));
		AS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); AS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
		UHorizontalBoxSlot* DS = ChatRespondRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("Decline"), WeedUI::ColWarn(),
			[this]() { if (Phone.IsValid() && !OpenChatContact.IsNone()) { Phone->RespondChat(OpenChatContact, false); } RefreshChatThread(); }, 13));
		DS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); DS->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		ChatThreadRoot->AddChildToVerticalBox(ChatRespondRow)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}

	// Offer/tijd-kiezer-sectie (alleen bij een open afspraak getoond). Toggle + OfferBox + "pick a time" + stepper +
	// "Propose this time" leven allemaal in ChatOfferSection; de OfferBox-INHOUD wordt bij RefreshChatThread gevuld.
	{
		ChatOfferSection = WidgetTree->ConstructWidget<UVerticalBox>();

		OfferToggleBtn = MakeActionBtn(TEXT("Offer instead..."), WeedUI::ColAccentDim(), [this]()
		{
			bOfferStrainView = !bOfferStrainView;
			if (OfferBox) { OfferBox->SetVisibility(bOfferStrainView ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
			if (OfferToggleBtn) { OfferToggleBtn->SetContent(MakeText(bOfferStrainView ? TEXT("Hide alternatives") : TEXT("Offer instead..."), 12, FLinearColor::White, true)); }
		}, 12);
		ChatOfferSection->AddChildToVerticalBox(OfferToggleBtn)->SetPadding(FMargin(0.f, 6.f, 0.f, 2.f));

		OfferBox = WidgetTree->ConstructWidget<UVerticalBox>();
		OfferBox->SetVisibility(ESlateVisibility::Collapsed);
		ChatOfferSection->AddChildToVerticalBox(OfferBox);
		// Binnenwerk EENMALIG: header-tekst + icoon-grid + "geen voorraad"-tekst. RefreshChatThread werkt ze
		// alleen in-place bij (SetText / SetItems-diff / visibility) -> nooit meer ClearChildren -> geen flash.
		{
			OfferHeadText = MakeText(TEXT(""), 10, WeedUI::ColTextDim());
			OfferHeadText->SetAutoWrapText(true);
			OfferBox->AddChildToVerticalBox(OfferHeadText)->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));

			OfferGrid = WidgetTree->ConstructWidget<UWeedItemPickGrid>();
			OfferGrid->CellSize = 86.f;
			OfferGrid->MaxVisibleRows = 2;
			OfferGrid->bShowSelection = false;
			OfferGrid->OnPick = [this](FName Id, int32 /*P*/)
			{
				if (Phone.IsValid() && !OpenChatContact.IsNone()) { Phone->ProposeChatStrain(OpenChatContact, Id); }
				bOfferStrainView = false;
				RefreshChatThread();
			};
			OfferBox->AddChildToVerticalBox(OfferGrid);

			OfferEmptyText = MakeText(TEXT("(no dried/bagged weed in your inventory or storages)"), 10, WeedUI::ColTextDim());
			OfferEmptyText->SetVisibility(ESlateVisibility::Collapsed);
			OfferBox->AddChildToVerticalBox(OfferEmptyText)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		}

		ChatPickerPrompt = MakeText(TEXT("Can't make it? Pick a time:"), 11, WeedUI::ColTextDim());
		ChatOfferSection->AddChildToVerticalBox(ChatPickerPrompt)->SetPadding(FMargin(0.f, 6.f, 0.f, 2.f));

		// Klok-label wordt live bijgewerkt (Step-lambda's + NativeTick); de step-knoppen lezen de klok bij klik.
		UHorizontalBox* Stepper = WidgetTree->ConstructWidget<UHorizontalBox>();
		auto Step = [this](int32 Delta)
		{
			AWeedShopGameState* SGS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
			const int32 NowMins = (SGS && SGS->GetDayCycle()) ? (((int32)(SGS->GetDayCycle()->GetClockHour() * 60.f)) % 1440) : 0;
			int32 Gap = ((ProposeMins - NowMins) % 1440 + 1440) % 1440;
			Gap = FMath::Clamp(Gap + Delta, 30, 1410); // niet onder 30 min vooruit, niet naar gisteren
			ProposeMins = (NowMins + Gap) % 1440;
			if (PickerClockText) { PickerClockText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), ProposeMins / 60, ProposeMins % 60))); }
		};
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("-1h"), WeedUI::ColSlot(), [Step]() { Step(-60); }, 13))->SetPadding(FMargin(0.f, 0.f, 3.f, 0.f));
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("-15m"), WeedUI::ColSlot(), [Step]() { Step(-15); }, 13))->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		UTextBlock* Clock = MakeText(TEXT("00:00"), 18, WeedUI::ColText());
		PickerClockText = Clock;            // live bijwerken in NativeTick (geen rebuild -> geen flash)
		UHorizontalBoxSlot* CS2 = Stepper->AddChildToHorizontalBox(Clock);
		CS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS2->SetHorizontalAlignment(HAlign_Center); CS2->SetVerticalAlignment(VAlign_Center);
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("+15m"), WeedUI::ColSlot(), [Step]() { Step(15); }, 13))->SetPadding(FMargin(6.f, 0.f, 3.f, 0.f));
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("+1h"), WeedUI::ColSlot(), [Step]() { Step(60); }, 13));
		ChatOfferSection->AddChildToVerticalBox(Stepper)->SetPadding(FMargin(0.f, 2.f, 0.f, 4.f));

		// Knop leest de ACTUELE ProposeMins (member) bij klik, zodat live-bijwerken klopt.
		ChatProposeBtn = MakeActionBtn(TEXT("Propose this time"), WeedUI::ColAccentDim(),
			[this]() { if (Phone.IsValid() && !OpenChatContact.IsNone()) { Phone->ProposeChatTime(OpenChatContact, ProposeMins); } ProposeMins = -1; RefreshChatThread(); }, 13);
		ChatOfferSection->AddChildToVerticalBox(ChatProposeBtn);

		ChatThreadRoot->AddChildToVerticalBox(ChatOfferSection);
	}

	// Eerste keer meteen vullen (lijst of thread, afhankelijk van OpenChatContact).
	RefreshChatViews();
}

// Toggelt lijst vs thread (puur zichtbaarheid, geen rebuild) en ververst de zichtbare kant in-place.
void UPhoneWidget::RefreshChatViews()
{
	if (!ChatListScroll || !ChatThreadRoot) { return; }
	const bool bThread = !OpenChatContact.IsNone();
	// Inactieve view op HIDDEN (blijft gearrangeerd -> geen her-layout-flikker bij switchen), niet Collapsed.
	ChatListScroll->SetVisibility(bThread ? ESlateVisibility::Hidden : ESlateVisibility::SelfHitTestInvisible);
	ChatThreadRoot->SetVisibility(bThread ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	if (bThread) { RefreshChatThread(); }
	else { ProposeMins = -1; RefreshChatList(); } // tijd-keuze reset zodra je de thread verlaat
}

// Reconcilieert de gesprekkenlijst-kaarten IN-PLACE: nieuwe contacten onderaan toevoegen, verdwenen contacten
// verwijderen. NOOIT ClearChildren. De inhoud (preview/badge/timestamp/kleur/afspraak-balk) doet UpdateListBarsLive.
// (Hersorteren op urgentie slaan we over: een nieuw contact onderaan toevoegen is prima -> geen flash.)
void UPhoneWidget::RefreshChatList()
{
	if (!ChatListScroll || !Phone.IsValid()) { return; }
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return; }
	const TArray<FPhoneMessage>& Msgs = Con->GetMessages(); // nieuwste eerst

	// Huidige contact-set (nieuwste-eerst volgorde).
	TArray<FName> Order;
	for (const FPhoneMessage& M : Msgs) { if (!IsMsgForLocal(M)) { continue; } Order.AddUnique(M.FromContactId); }

	// Verdwenen contacten: kaart weghalen (RemoveFromParent) + uit alle lijst-maps.
	TArray<FName> Gone;
	for (const TPair<FName, TObjectPtr<UBorder>>& CK : ListCards) { if (!Order.Contains(CK.Key)) { Gone.Add(CK.Key); } }
	for (const FName& Cid : Gone)
	{
		if (TObjectPtr<UBorder>* C = ListCards.Find(Cid)) { if (*C) { (*C)->RemoveFromParent(); } }
		ListCards.Remove(Cid); ListPreviews.Remove(Cid); ListApptBars.Remove(Cid);
		ListBadges.Remove(Cid); ListBadgeTexts.Remove(Cid); ListStamps.Remove(Cid);
	}

	// Nieuwe contacten: bouw een kaart (met een ALTIJD-aanwezige afspraak-balk zodat urgentie later live kan
	// aan/uit zonder herbouw) en voeg 'm onderaan toe. UpdateListBarsLive doet daarna alle inhoud/kleur.
	for (const FName& Cid : Order)
	{
		if (ListCards.Contains(Cid)) { continue; }

		FText Name = FText::FromName(Cid);
		for (const FPhoneMessage& M : Msgs) { if (IsMsgForLocal(M) && M.FromContactId == Cid) { Name = M.SenderName; break; } }

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrush(RoundedBrush(WeedUI::ColInner(0.95f), 8.f));
		Card->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
		ListCards.Add(Cid, Card);
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Card->SetContent(Row);
		UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
		Info->AddChildToVerticalBox(MakeText(Name.ToString(), 14, WeedUI::ColText()));
		if (GS && GS->GetNpcRegistry())
		{
			// Per-speler tier (competitive): de contact-kaart toont MIJN relatie-tier met dit contact.
			const int32 Tr = GS->GetNpcRegistry()->GetCustomerTier(Cid, USaveGameSubsystem::StablePlayerId(GetOwningPlayerPawn()));
			const FLinearColor TCol = (Tr >= 5) ? FLinearColor(1.f, 0.8f, 0.3f) : (Tr >= 4 ? FLinearColor(0.8f, 0.7f, 1.f) : FLinearColor(0.55f, 0.7f, 0.6f));
			Info->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%s customer"), *UNpcRegistryComponent::TierName(Tr)), 9, TCol));
		}
		UTextBlock* Prev = MakeText(TEXT(""), 10, WeedUI::ColTextDim());
		Prev->SetClipping(EWidgetClipping::ClipToBounds);
		ListPreviews.Add(Cid, Prev);
		Info->AddChildToVerticalBox(Prev);
		// Slanke aftelbalk: ALTIJD gebouwd (percent 0 = onzichtbaar-leeg) + opgeslagen -> live aan/uit zonder herbouw.
		{
			USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>();
			BarBox->SetHeightOverride(5.f);
			UProgressBar* LB = WidgetTree->ConstructWidget<UProgressBar>();
			LB->SetPercent(0.f);
			LB->SetVisibility(ESlateVisibility::HitTestInvisible);
			BarBox->SetContent(LB);
			Info->AddChildToVerticalBox(BarBox)->SetPadding(FMargin(0.f, 3.f, 6.f, 0.f));
			ListApptBars.Add(Cid, LB);
		}
		UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(Info);
		IS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); IS->SetVerticalAlignment(VAlign_Center);
		// Tijdstempel (HH:MM) rechts — ALTIJD gebouwd + opgeslagen (live via UpdateListBarsLive).
		{
			UTextBlock* Stamp = MakeText(TEXT(""), 9, WeedUI::ColTextDim());
			ListStamps.Add(Cid, Stamp);
			UHorizontalBoxSlot* TsS = Row->AddChildToHorizontalBox(Stamp);
			TsS->SetVerticalAlignment(VAlign_Top); TsS->SetPadding(FMargin(6.f, 1.f, 2.f, 0.f));
		}
		// Ongelezen-badge: ALTIJD gebouwd (verborgen bij 0) + opgeslagen -> live togglebaar zonder herbouw.
		{
			UBorder* Badge = WidgetTree->ConstructWidget<UBorder>();
			Badge->SetBrush(RoundedBrush(WeedUI::ColGood(1.f), 9.f));
			Badge->SetPadding(FMargin(7.f, 2.f, 7.f, 2.f));
			UTextBlock* BadgeT = MakeText(TEXT("0"), 12, WeedUI::ColBg());
			Badge->SetContent(BadgeT);
			Badge->SetVisibility(ESlateVisibility::Collapsed);
			ListBadges.Add(Cid, Badge); ListBadgeTexts.Add(Cid, BadgeT);
			UHorizontalBoxSlot* BS = Row->AddChildToHorizontalBox(Badge);
			BS->SetVerticalAlignment(VAlign_Center); BS->SetPadding(FMargin(4.f, 0.f, 4.f, 0.f));
		}
		const FName Pick = Cid;
		Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Open"), WeedUI::ColAccentDim(),
			[this, Pick]() { OpenChatContact = Pick; bOfferStrainView = false; ProposeMins = -1; RefreshChatViews(); }, 11))->SetVerticalAlignment(VAlign_Center);
		ChatListScroll->AddChild(Card);
		ChatListScroll->AddChild(MakeText(TEXT(""), 4, FLinearColor::Transparent));
	}

	// Inhoud (preview/badge/timestamp/kleur/afspraak-balk) van ALLE kaarten bijwerken.
	UpdateListBarsLive();
}

// Werkt de open thread voor OpenChatContact IN-PLACE bij (NOOIT ClearChildren op ChatThreadRoot):
// header-naam, tier, afspraak-balk, bubbels (pool), wacht-balk, accept/decline, offer + tijd-kiezer.
void UPhoneWidget::RefreshChatThread()
{
	if (!ChatThreadRoot || OpenChatContact.IsNone() || !Phone.IsValid()) { return; }
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return; }
	const TArray<FPhoneMessage>& Msgs = Con->GetMessages(); // nieuwste eerst

	Phone->MarkChatSeen(OpenChatContact); // berichten van deze persoon zijn nu gelezen -> notificatie-bubble weg

	// Header-naam.
	FText ContactName = FText::FromName(OpenChatContact);
	for (const FPhoneContact& C : Con->GetContacts()) { if (C.ContactId == OpenChatContact) { ContactName = C.DisplayName; break; } }
	if (ChatHeaderName) { ChatHeaderName->SetText(ContactName); }

	// Klant-tier + XP-balk (box tonen/verbergen op basis van registry).
	if (ChatTierBox)
	{
		if (GS && GS->GetNpcRegistry())
		{
			UNpcRegistryComponent* Reg = GS->GetNpcRegistry();
			// Per-speler tier + voortgang (competitive): de chat toont MIJN relatie, niet de gedeelde base.
			const FString MyPid = USaveGameSubsystem::StablePlayerId(GetOwningPlayerPawn());
			const int32 Tier = Reg->GetCustomerTier(OpenChatContact, MyPid);
			const float Frac = Reg->GetTierProgress01(OpenChatContact, MyPid);
			const FString TLbl = (Tier >= 5)
				? FString::Printf(TEXT("Tier: %s  (max)"), *UNpcRegistryComponent::TierName(Tier))
				: FString::Printf(TEXT("Tier: %s  ->  %s"), *UNpcRegistryComponent::TierName(Tier), *UNpcRegistryComponent::TierName(Tier + 1));
			if (ChatTierLabel) { ChatTierLabel->SetText(FText::FromString(TLbl)); }
			if (ChatTierBar) { ChatTierBar->SetPercent(Frac); }
			ChatTierBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else { ChatTierBox->SetVisibility(ESlateVisibility::Collapsed); }
	}

	// Afspraak-balk (fase 0=arrives / 1=at door; fase 2 gebruikt de wacht-balk hieronder).
	{
		float TFrac = 0.f; int32 TSecs = 0, TPhase = 0, TClockM = 0;
		const bool bShow = GetApptUrgency(OpenChatContact, TFrac, TSecs, TPhase, TClockM) && TPhase != 2;
		if (ChatApptBox) { ChatApptBox->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
		if (bShow)
		{
			if (ApptBar) { ApptBar->SetPercent(TFrac); ApptBar->SetFillColorAndOpacity(UrgencyColor(TFrac)); }
			ApptBarContact = OpenChatContact; // UpdateApptBarLive werkt label/percent verder live bij
		}
		else { ApptBarContact = NAME_None; }
	}

	// --- Bubbels via een POOL (patroon van FillStoreListRows) ---
	// Sig per bericht = body + status + bFromMe + "ago"-emmer. Alleen een gewijzigde bubbel krijgt nieuwe inhoud.
	bool bAny = false; bool bHasOpen = false; float OpenSentTime = -1.f;
	TArray<FString> BubSigs;
	TArray<TFunction<UWidget*()>> BubBuilders;
	const float RealNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (int32 i = Msgs.Num() - 1; i >= 0; --i) // oudste boven (Msgs is nieuwste-eerst)
	{
		const FPhoneMessage& M = Msgs[i];
		if (!IsMsgForLocal(M)) { continue; }
		if (M.FromContactId != OpenChatContact) { continue; }
		bAny = true;
		if (M.Status == 0 && !M.bFromMe) { bHasOpen = true; OpenSentTime = M.SentRealTime; }

		FString Body = M.Body.ToString();
		if (!M.bFromMe && M.Status == 1) { Body += TEXT("  (accepted)"); }
		else if (!M.bFromMe && M.Status == 2) { Body += TEXT("  (declined)"); }

		// "Xm ago"-emmer voor de signatuur (zodat de bubbel alleen herbouwt als de weergegeven tekst wijzigt).
		const int32 AgoSecRaw = (M.SentRealTime >= 0.f) ? FMath::Max(0, (int32)(RealNow - M.SentRealTime)) : -1;
		const int32 AgoBucket = (AgoSecRaw < 0) ? -1 : (AgoSecRaw < 60 ? 0 : (AgoSecRaw < 3600 ? (1000 + AgoSecRaw / 60) : (100000 + AgoSecRaw / 3600)));
		BubSigs.Add(FString::Printf(TEXT("%d|%d|%s|%d"), M.bFromMe ? 1 : 0, (int32)M.Status, *Body, AgoBucket));

		const bool bFromMe = M.bFromMe;
		const FName WantProduct = M.WantProduct;
		const float SentReal = M.SentRealTime;
		BubBuilders.Add([this, Body, bFromMe, WantProduct, SentReal]() -> UWidget*
		{
			UBorder* Bub = WidgetTree->ConstructWidget<UBorder>();
			Bub->SetBrush(RoundedBrush(bFromMe ? WeedUI::ColAccentDim(0.97f) : WeedUI::ColInner(0.97f), 10.f));
			Bub->SetPadding(FMargin(9.f, 6.f, 9.f, 6.f));
			// Body met de belangrijke woorden vetgedrukt (grams/%/strain/product) i.p.v. één platte tekst.
			// Strain/product ook altijd vet: zelfde "pretty name minus bag" als waarmee de body is opgebouwd.
			FString BoldStrain;
			if (!WantProduct.IsNone()) { BoldStrain = WeedUI::PrettyItemName(WantProduct).Replace(TEXT(" bag"), TEXT(""), ESearchCase::IgnoreCase); }
			// D.1 - het strain-woord vet EN in de strain-tagkleur; overige vette woorden blijven wit.
			const FLinearColor StrainCol = WantProduct.IsNone() ? FLinearColor(1.f, 1.f, 1.f) : WeedUI::TagColorForItem(WantProduct, 0.85f, 0.75f);
			UWidget* BodyT = MakeRichBody(WidgetTree, Body, 12, WeedUI::ColText(), BoldStrain, StrainCol);
			UVerticalBox* BubVB = WidgetTree->ConstructWidget<UVerticalBox>();
			BubVB->AddChildToVerticalBox(BodyT);
			// Tijdstempel op ECHTE tijd ("Xm ago") i.p.v. de in-game klok (die liep te snel).
			if (SentReal >= 0.f)
			{
				const float Now2 = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
				const int32 AgoSec = FMath::Max(0, (int32)(Now2 - SentReal));
				const FString Ago = (AgoSec < 60) ? FString(TEXT("just now")) : (AgoSec < 3600) ? FString::Printf(TEXT("%dm ago"), AgoSec / 60) : FString::Printf(TEXT("%dh ago"), AgoSec / 3600);
				UTextBlock* TimeT = MakeText(Ago, 8, WeedUI::ColTextDim());
				UVerticalBoxSlot* TSl = BubVB->AddChildToVerticalBox(TimeT);
				TSl->SetHorizontalAlignment(bFromMe ? HAlign_Right : HAlign_Left);
				TSl->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
			}
			Bub->SetContent(BubVB);

			USizeBox* Cap = WidgetTree->ConstructWidget<USizeBox>();
			Cap->SetMaxDesiredWidth(244.f);
			Cap->SetContent(Bub);

			UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
			if (bFromMe)
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
			return Line;
		});
	}

	// Bubbel-pool op maat brengen (persistent) + per-bubbel diff -> alleen gewijzigde bubbel krijgt nieuwe inhoud.
	if (ChatBubbleScroll)
	{
		const int32 N = BubSigs.Num();
		while (ChatBubblePool.Num() < N)
		{
			UBorder* SlotBox = WidgetTree->ConstructWidget<UBorder>();
			SlotBox->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f)); // transparant wrapper (geen extra rand)
			SlotBox->SetPadding(FMargin(0.f));
			ChatBubbleScroll->AddChild(SlotBox);
			ChatBubblePool.Add(SlotBox); ChatBubbleSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
		}
		while (ChatBubblePool.Num() > N)
		{
			const int32 Last = ChatBubblePool.Num() - 1;
			if (ChatBubblePool[Last]) { ChatBubblePool[Last]->RemoveFromParent(); }
			ChatBubblePool.RemoveAt(Last); ChatBubbleSigs.RemoveAt(Last);
		}
		for (int32 i = 0; i < N; ++i)
		{
			if (!ChatBubblePool.IsValidIndex(i) || !ChatBubblePool[i]) { continue; }
			if (ChatBubbleSigs.IsValidIndex(i) && BubSigs[i] == ChatBubbleSigs[i]) { continue; }
			ChatBubbleSigs[i] = BubSigs[i];
			// Bubbel + gefoldede 4px-gap eronder (zoals de losse spacer-tekst in de oude bouw).
			UVerticalBox* Holder = WidgetTree->ConstructWidget<UVerticalBox>();
			Holder->AddChildToVerticalBox(BubBuilders[i]());
			UBorder* Gap = WidgetTree->ConstructWidget<UBorder>();
			Gap->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
			Gap->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
			Holder->AddChildToVerticalBox(Gap);
			ChatBubblePool[i]->SetContent(Holder);
		}
	}
	if (ChatNoMsgText) { ChatNoMsgText->SetVisibility(bAny ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }

	// Wacht-balk + Accept/Decline + offer/tijd-kiezer alleen bij een open afspraak (Status==0 && !bFromMe).
	if (ChatWaitBox) { ChatWaitBox->SetVisibility((bHasOpen && OpenSentTime >= 0.f) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	WaitBarSentTime = (bHasOpen && OpenSentTime >= 0.f) ? OpenSentTime : -1.f; // basis voor UpdateWaitBarLive
	if (bHasOpen && OpenSentTime >= 0.f && WaitBar) { WaitBar->SetPercent(1.f); }
	if (ChatRespondRow) { ChatRespondRow->SetVisibility(bHasOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }

	if (ChatOfferSection)
	{
		ChatOfferSection->SetVisibility(bHasOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (bHasOpen)
		{
			// Toggle-knop + OfferBox-zichtbaarheid synchroon met bOfferStrainView.
			if (OfferToggleBtn) { OfferToggleBtn->SetContent(MakeText(bOfferStrainView ? TEXT("Hide alternatives") : TEXT("Offer instead..."), 12, FLinearColor::White, true)); }
			if (OfferBox)
			{
				OfferBox->SetVisibility(bOfferStrainView ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
				// OfferBox-inhoud IN-PLACE bijwerken: header-tekst + grid-diff (geen ClearChildren -> geen flash).
				const FName ReqStrain = Con->GetRequestedStrain(OpenChatContact);
				float ExpThc = 15.f;
				if (GS && GS->GetStore()) { float t = 0.f, y = 0.f, g = 0.f; if (GS->GetStore()->GetStrainStats(ReqStrain, t, y, g) && t > 0.f) { ExpThc = t; } }
				if (OfferHeadText)
				{
					OfferHeadText->SetText(FText::FromString(FString::Printf(
						TEXT("They want %s (~%.0f%% THC). Your stock (incl. chests/shelves):"), *ReqStrain.ToString(), ExpThc)));
				}

				// Strain uit een wiet-item halen (Bag_X_<g> of Bud_X); nat (WetBud_) telt NIET mee.
				auto StrainOf = [](FName Id) -> FName
				{
					const FString S = Id.ToString();
					if (S.StartsWith(TEXT("WetBud_"))) { return NAME_None; }
					if (UInventoryComponent::IsBag(Id)) { return UInventoryComponent::BagStrain(Id); }
					if (S.StartsWith(TEXT("Bud_"))) { return FName(*S.RightChop(4)); }
					return NAME_None;
				};
				// Aggregeer per strain over inventory + ALLE chests/shelves (beste THC representatief).
				struct FOfferAgg { float Thc = 0.f; float Qual = 0.f; int32 Qty = 0; };
				TMap<FName, FOfferAgg> ByStrain;
				auto Consider = [&](FName Id, int32 Q, float Thc, float Ql)
				{
					const FName Strain = StrainOf(Id);
					if (Strain.IsNone() || Q <= 0) { return; }
					// Tel in GRAMMEN: los gedroogd (Bud_) = 1g per stuk; zakjes (Bag_) = aantal x gram-per-zakje.
					const int32 Grams = UInventoryComponent::IsBag(Id) ? Q * FMath::Max(1, UInventoryComponent::BagGrams(Id)) : Q;
					FOfferAgg& O = ByStrain.FindOrAdd(Strain);
					O.Qty += Grams;
					if (Thc > O.Thc) { O.Thc = Thc; O.Qual = Ql; }
				};
				if (UInventoryComponent* Inv = GetOwningPlayerPawn() ? GetOwningPlayerPawn()->FindComponentByClass<UInventoryComponent>() : nullptr)
				{
					for (const FInventoryStack& St : Inv->GetStacks()) { Consider(St.ItemId, St.Quantity, St.Quality, St.QualityPct); }
				}
				for (TActorIterator<AStorageShelf> It(GetWorld()); It; ++It)
				{
					for (const FShelfStack& C : It->Contents) { Consider(C.ItemId, C.Quantity, C.Thc, C.QualityPct); }
				}

				// Naar grid-items: icoon = Bud_<strain>, badge = grammen, subline = THC-delta + kans (kleur per teken),
				// tooltip = de oude 2-regel-tekst. SetItems doet de diff -> geen flash bij een gewone thread-refresh.
				TArray<FWeedPickItem> Items;
				for (const TPair<FName, FOfferAgg>& P : ByStrain)
				{
					const FName Strain = P.Key; const FOfferAgg& O = P.Value;
					const float Delta = O.Thc - ExpThc;
					const float Chance = Con->SubstituteAcceptChance(OpenChatContact, ReqStrain, Strain, O.Thc) * 100.f;
					FWeedPickItem It;
					It.Id = Strain;
					It.IconId = FName(*(FString(TEXT("Bud_")) + Strain.ToString()));
					It.Badge = FString::Printf(TEXT("%dg"), O.Qty);
					It.SubLine = FString::Printf(TEXT("%+.0f%% ~%.0f%%"), Delta, Chance);
					It.SubCol = (Delta >= 0.f) ? WeedUI::ColGood() : WeedUI::ColWarn();
					It.Tooltip = FString::Printf(TEXT("%s   T%.0f%%  Q%.0f%%  %dg\n%+.0f%% THC vs ask   ~%.0f%% yes"),
						*Strain.ToString(), O.Thc, O.Qual, O.Qty, Delta, Chance);
					Items.Add(It);
				}
				if (OfferGrid) { OfferGrid->SetItems(Items); }
				if (OfferEmptyText) { OfferEmptyText->SetVisibility(Items.Num() == 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
				if (OfferGrid) { OfferGrid->SetVisibility(Items.Num() == 0 ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }
			}

			// Tijd-kiezer: startwaarde + clamping (30 min..1410 min, kwartier). PickerContact stelt de live NativeTick-
			// clamp scherp; PickerClockText krijgt de tekst.
			const int32 NowMins = (GS && GS->GetDayCycle()) ? (((int32)(GS->GetDayCycle()->GetClockHour() * 60.f)) % 1440) : 0;
			if (ProposeMins < 0)
			{
				float ApptTod = -1.f;
				for (const FPhoneMessage& M : Msgs)
				{
					if (!IsMsgForLocal(M)) { continue; }
					if (M.FromContactId == OpenChatContact && M.Status == 0 && !M.bFromMe && M.AppointmentTimeOfDay >= 0.f)
					{ ApptTod = M.AppointmentTimeOfDay; break; }
				}
				const int32 Mins = (ApptTod >= 0.f) ? Con->ClockMinutesOf(ApptTod) : ((NowMins + 60) % 1440);
				ProposeMins = ((FMath::RoundToInt(Mins / 15.f) * 15) % 1440 + 1440) % 1440;
			}
			{
				int32 Gap = ((ProposeMins - NowMins) % 1440 + 1440) % 1440;
				Gap = FMath::Clamp(Gap, 30, 1410);
				ProposeMins = (NowMins + Gap) % 1440;
			}
			if (PickerClockText) { PickerClockText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), ProposeMins / 60, ProposeMins % 60))); }
			PickerContact = OpenChatContact;
		}
		else { PickerContact = NAME_None; }
	}

	// Live-elementen meteen bijwerken (label/percent van appt- en wacht-balk).
	UpdateApptBarLive();
	UpdateWaitBarLive();
}

void UPhoneWidget::FillPackagesInto(UScrollBox* Scroll)
{
	if (!Scroll || !Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();

	// Wisselt de doel-scroll (store-packages-view <-> losse Packages-app)? Dan zijn de oude kaart-refs voor een
	// andere (mogelijk vernietigde) scroll -> map/placeholder resetten zodat we vers opbouwen (geen dangling wrap).
	if (PkgScrollOwner != Scroll)
	{
		PkgCards.Reset();
		PkgEmptyRow = nullptr;
		PkgDeliveredBox = nullptr; PkgDeliveredSig = -1; // hoort bij de oude (mogelijk vernietigde) scroll -> vers opbouwen
		PkgScrollOwner = Scroll;
	}

	// Bouwt/ververst de "Delivered"-sectie (kop + compacte inner-cards) onder de pending-kaarten. De container-widget
	// blijft persistent (Scroll->AddChild eenmalig); alleen de INHOUD herbouwt als de historie-lengte wijzigt (sig).
	auto FillDelivered = [this, Scroll, Ph]()
	{
		const TArray<UPhoneClientComponent::FDeliveredRecord>& Hist = Ph->GetDeliveredHistory();
		if (!PkgDeliveredBox)
		{
			PkgDeliveredBox = WidgetTree->ConstructWidget<UBorder>();
			PkgDeliveredBox->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
			PkgDeliveredBox->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
			Scroll->AddChild(PkgDeliveredBox);
			PkgDeliveredSig = -1; // forceer eerste vulling
		}
		// Sig = lengte + nieuwste OrderId: de historie is gecapt (20), dus alleen-lengte zou na de cap bevriezen.
		const int32 Sig = Hist.Num() * 131 + (Hist.Num() > 0 ? Hist[0].OrderId : 0);
		if (Sig == PkgDeliveredSig) { return; }
		PkgDeliveredSig = Sig;
		PkgDeliveredBox->SetVisibility(Hist.Num() == 0 ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		if (Hist.Num() == 0) { PkgDeliveredBox->SetContent(nullptr); return; }

		UVerticalBox* DVB = WidgetTree->ConstructWidget<UVerticalBox>();
		DVB->AddChildToVerticalBox(MakeText(TEXT("Delivered"), 12, WeedUI::ColTextDim()))->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		// Nieuwste bovenaan (de historie staat al nieuwste-voorop, Insert op 0).
		for (const UPhoneClientComponent::FDeliveredRecord& R : Hist)
		{
			UBorder* Inner = WidgetTree->ConstructWidget<UBorder>();
			Inner->SetBrush(RoundedBrush(WeedUI::ColInner(0.95f), 8.f));
			Inner->SetPadding(FMargin(8.f, 5.f, 8.f, 5.f));
			UVerticalBox* IVB = WidgetTree->ConstructWidget<UVerticalBox>();
			Inner->SetContent(IVB);
			const int32 N = FMath::Min(R.Ids.Num(), R.Qtys.Num());
			for (int32 i = 0; i < N; ++i)
			{
				UTextBlock* LineT = MakeText(FString::Printf(TEXT("%dx %s"), R.Qtys[i], *WeedUI::PrettyItemName(R.Ids[i])), 10, WeedUI::ColTextDim());
				LineT->SetAutoWrapText(true);
				IVB->AddChildToVerticalBox(LineT);
			}
			IVB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("Paid EUR %lld  -  fee EUR %lld"),
				(long long)(WeedRoundEuros(R.PaidCents) / 100), (long long)(WeedRoundEuros(R.FeeCents) / 100)), 10, WeedUI::ColGood()))
				->SetPadding(FMargin(0.f, 1.f, 0.f, 0.f));
			DVB->AddChildToVerticalBox(Inner)->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		}
		PkgDeliveredBox->SetContent(DVB);
	};

	// Bouwt de kaart-inhoud (border) voor één bestelling. De per-OrderId-kaart-widget zelf blijft persistent
	// in PkgCards -> Cancel haalt alleen die ene kaart weg (RemoveFromParent), geen ClearChildren.
	auto BuildCard = [this, Ph](const UPhoneClientComponent::FPendingDelivery& D) -> UBorder*
	{
		const int32 OrderId = D.OrderId;
		UBorder* CardB = WidgetTree->ConstructWidget<UBorder>();
		CardB->SetBrush(RoundedBrush(WeedUI::ColInner(0.95f), 8.f));
		CardB->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
		UVerticalBox* CVB = WidgetTree->ConstructWidget<UVerticalBox>();
		CardB->SetContent(CVB);

		// Titel-rij: bezorgnaam + aantal stuks.
		CVB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%s delivery   -   %d item(s)"),
			*UPhoneClientComponent::DeliveryName(D.DeliveryOpt), D.ItemCount), 13, WeedUI::ColText()));
		// Inhoud: per-item regels ("Nx <naam>") uit de parallelle Ids/Qtys.
		{
			const int32 N = FMath::Min(D.Ids.Num(), D.Qtys.Num());
			for (int32 i = 0; i < N; ++i)
			{
				UTextBlock* LineT = MakeText(FString::Printf(TEXT("%dx %s"), D.Qtys[i], *WeedUI::PrettyItemName(D.Ids[i])), 10, WeedUI::ColTextDim());
				LineT->SetAutoWrapText(true);
				CVB->AddChildToVerticalBox(LineT);
			}
			// Betaald + fee + bezorgnaam.
			CVB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("Paid EUR %lld  -  fee EUR %lld  -  %s"),
				(long long)(WeedRoundEuros(D.PaidCents) / 100), (long long)(WeedRoundEuros(D.FeeCents) / 100),
				*UPhoneClientComponent::DeliveryName(D.DeliveryOpt)), 10, WeedUI::ColTextDim()))
				->SetPadding(FMargin(0.f, 1.f, 0.f, 0.f));
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

		// Status-regel: ETA + Cancel (onderweg), of "at the door" (aangekomen).
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		if (bArrived)
		{
			UTextBlock* AtDoor = MakeText(TEXT("At the door - go pick it up"), 12, WeedUI::ColGood());
			Row->AddChildToHorizontalBox(AtDoor)->SetVerticalAlignment(VAlign_Center);
		}
		else
		{
			const int32 Left = FMath::CeilToInt(Ph->GetDeliverySecondsLeft(D));
			UTextBlock* EtaT = MakeText(FString::Printf(TEXT("Drone on the way - %d:%02d"), Left / 60, Left % 60), 12, WeedUI::ColTextDim());
			UHorizontalBoxSlot* ES = Row->AddChildToHorizontalBox(EtaT);
			ES->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); ES->SetVerticalAlignment(VAlign_Center);
			PkgEtas.Add(OrderId, EtaT);
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Cancel"), WeedUI::ColWarn(),
				[this, Ph, OrderId]()
				{
					Ph->CancelDelivery(OrderId);
					// Alleen DEZE kaart weg (geen hele lijst herbouwen -> geen flash). De sig-gate in NativeTick
					// merkt de verandering en laat de rest live meelopen; LastPkgSig gelijkzetten zodat 't niet
					// alsnog een volledige herbouw triggert.
					if (TObjectPtr<UBorder>* Card = PkgCards.Find(OrderId)) { if (*Card) { (*Card)->RemoveFromParent(); } }
					PkgCards.Remove(OrderId); PkgBars.Remove(OrderId); PkgEtas.Remove(OrderId);
					LastPkgSig = PackagesSignature();
				}, 11));
		}
		CVB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 5.f, 0.f, 0.f));
		return CardB;
	};

	const TArray<UPhoneClientComponent::FPendingDelivery>& Pend = Ph->GetPendingDeliveries();

	// Bars/ETA's worden per (her)bouw opnieuw verzameld; kaart-widgets zelf blijven persistent in PkgCards.
	PkgBars.Reset();
	PkgEtas.Reset();

	// Verzamel de OrderIds die er nu zijn -> verwijder kaarten van bestellingen die weg zijn (aangekomen/geannuleerd).
	TSet<int32> Live;
	for (const UPhoneClientComponent::FPendingDelivery& D : Pend) { Live.Add(D.OrderId); }
	for (auto It = PkgCards.CreateIterator(); It; ++It)
	{
		if (!Live.Contains(It.Key())) { if (It.Value()) { It.Value()->RemoveFromParent(); } It.RemoveCurrent(); }
	}

	// "Geen pakketten"-placeholder: apart bijhouden zodat we 'm gericht kunnen tonen/verbergen (geen ClearChildren).
	if (Pend.Num() == 0)
	{
		if (PkgCards.Num() == 0 && !PkgEmptyRow)
		{
			PkgEmptyRow = WidgetTree->ConstructWidget<UBorder>();
			PkgEmptyRow->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
			PkgEmptyRow->SetPadding(FMargin(0.f));
			PkgEmptyRow->SetContent(MakeText(TEXT("No packages on the way."), 12, WeedUI::ColTextDim()));
			Scroll->AddChild(PkgEmptyRow);
		}
		FillDelivered(); // geleverd-lijstje ook zonder pending tonen (onder de placeholder)
		return;
	}
	if (PkgEmptyRow) { PkgEmptyRow->RemoveFromParent(); PkgEmptyRow = nullptr; }

	// Wrapper (kaart + 3px-gap) als vaste holder per bestelling; alleen de inhoud wordt ververst.
	auto WrapCard = [this](UBorder* CardInner) -> UWidget*
	{
		UVerticalBox* Holder = WidgetTree->ConstructWidget<UVerticalBox>();
		Holder->AddChildToVerticalBox(CardInner);
		UBorder* Gap = WidgetTree->ConstructWidget<UBorder>();
		Gap->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
		Gap->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		Holder->AddChildToVerticalBox(Gap);
		return Holder;
	};

	// Bestaande kaarten hun inhoud verversen (bv. onderweg -> aan de deur), nieuwe kaarten toevoegen.
	bool bAddedPending = false;
	for (const UPhoneClientComponent::FPendingDelivery& D : Pend)
	{
		const int32 OrderId = D.OrderId;
		if (TObjectPtr<UBorder>* Existing = PkgCards.Find(OrderId))
		{
			if (*Existing) { (*Existing)->SetContent(WrapCard(BuildCard(D))); }
			continue;
		}
		// Nieuwe bestelling: wrapper-border (kaart + gap) toevoegen aan het einde.
		UBorder* Wrap = WidgetTree->ConstructWidget<UBorder>();
		Wrap->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
		Wrap->SetPadding(FMargin(0.f));
		Wrap->SetContent(WrapCard(BuildCard(D)));
		Scroll->AddChild(Wrap);
		PkgCards.Add(OrderId, Wrap);
		bAddedPending = true;
	}

	// Delivered-sectie ONDER de pending-kaarten houden: een net toegevoegde pending-kaart landt na de (al bestaande)
	// delivered-box, dus re-anchoren we die naar het einde (AddChild verplaatst 'm). Anders alleen inhoud verversen.
	if (bAddedPending && PkgDeliveredBox) { Scroll->AddChild(PkgDeliveredBox); }
	FillDelivered();
}

void UPhoneWidget::BuildPackagesApp()
{
	PackagesScroll = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* PS = ActiveContent->AddChildToVerticalBox(PackagesScroll);
	PS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LastPkgSig = PackagesSignature();
	FillPackagesInto(PackagesScroll);
}

void UPhoneWidget::BuildBankApp()
{
	// Bank-app is PERSISTENT: bouw ZOWEL de upgrade-prompt (BankLockedBox) als de bank-kaart (BankUnlockedBox) één
	// keer; NativeTick toggelt welke zichtbaar is op de live unlock/ATM-staat. Zo wisselt het KOPEN van de upgrade
	// gewoon van box (geen ClearChildren/rebuild -> geen flash), en werken saldo + cash-to-deposit in-place bij.
	BankBalanceText = nullptr; BankCashText = nullptr; BankLockedBox = nullptr; BankUnlockedBox = nullptr;
	BankSendBox = nullptr; BankSendLabel = nullptr;
	if (!ActiveContent || !Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;
	const bool bUnlocked = Ph->IsBankAppUnlocked() || Ph->IsBankViaAtm();

	// Beide boxen stapelen in een OVERLAY (zelfde ruimte) -> de inactieve box kan op HIDDEN (blijft gearrangeerd,
	// alleen niet getekend) i.p.v. Collapsed, dus de unlock-wissel bij het kopen is instant zonder her-layout-flikker.
	UOverlay* BankOv = WidgetTree->ConstructWidget<UOverlay>();
	UVerticalBoxSlot* SS = ActiveContent->AddChildToVerticalBox(BankOv);
	SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// --- Upgrade-prompt (geblokkeerd; alleen relevant zonder ATM). ---
	BankLockedBox = WidgetTree->ConstructWidget<UVerticalBox>();
	{ UOverlaySlot* LKS = BankOv->AddChildToOverlay(BankLockedBox); LKS->SetHorizontalAlignment(HAlign_Fill); LKS->SetVerticalAlignment(VAlign_Top); }
	{
		BankLockedBox->AddChildToVerticalBox(MakeText(TEXT("Mobile banking"), 16, WeedUI::ColText(), false));
		UTextBlock* Desc = MakeText(TEXT("Upgrade to bank anywhere (no ATM)."), 12, WeedUI::ColTextDim());
		Desc->SetAutoWrapText(true);
		BankLockedBox->AddChildToVerticalBox(Desc);
		BankLockedBox->AddChildToVerticalBox(MakeText(TEXT(""), 8, FLinearColor::Transparent));
		const int64 Cost = UPhoneClientComponent::PhoneUpgradeCostCents;
		// GEEN MarkDirty: NativeTick toggelt naar de bank-kaart zodra de unlock repliceert -> geen rebuild/flash.
		BankLockedBox->AddChildToVerticalBox(MakeActionBtn(FString::Printf(TEXT("Unlock  -  EUR %lld"), (long long)(WeedRoundEuros(Cost) / 100)),
			WeedUI::ColAccentDim(), [this]() { if (Phone.IsValid()) { Phone->RequestBuyPhoneUpgrade(); } }, 14));
	}

	// --- Bank-kaart (ontgrendeld / via ATM). ---
	BankUnlockedBox = WidgetTree->ConstructWidget<UVerticalBox>();
	{ UOverlaySlot* UKS = BankOv->AddChildToOverlay(BankUnlockedBox); UKS->SetHorizontalAlignment(HAlign_Fill); UKS->SetVerticalAlignment(VAlign_Top); }
	auto AddU = [this](UWidget* W) { BankUnlockedBox->AddChildToVerticalBox(W); };
	{
		const float DepTax = Econ ? Econ->DepositTaxPct : 0.f;
		const float TrFee = Econ ? Econ->TransferFeePct : 0.f;
		const int64 BankC = Econ ? Econ->GetBankCents() : 0;
		const int64 CashC = Econ ? Econ->GetCashCents() : 0;
		// Saldo + cash-to-deposit = de ENIGE dynamische teksten; ref bewaren -> NativeTick werkt ze IN-PLACE bij.
		AddU(MakeText(TEXT("BANK BALANCE"), 11, WeedUI::ColTextDim(), false));
		BankBalanceText = MakeText(FString::Printf(TEXT("EUR %lld"), (long long)(WeedRoundEuros(BankC) / 100)), 26, WeedUI::ColGood(), false);
		AddU(BankBalanceText);
		BankCashText = MakeText(FString::Printf(TEXT("Cash to deposit:  EUR %lld"), (long long)(WeedRoundEuros(CashC) / 100)), 12, WeedUI::ColTextDim());
		AddU(BankCashText);
		AddU(MakeText(TEXT(""), 10, FLinearColor::Transparent));

		// --- Storten (cash -> bank), bescheiden presets + max ---
		AddU(MakeText(FString::Printf(TEXT("Deposit   (%.0f%% tax)"), DepTax * 100.f), 13, WeedUI::ColGood()));
		{
			const int64 Amts[3] = { 10000, 50000, 100000 }; // EUR 100 / 500 / 1000
			UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
			for (int32 i = 0; i < 3; ++i)
			{
				const int64 A = Amts[i];
				UWeedActionButton* B = MakeActionBtn(FString::Printf(TEXT("%lld"), (long long)(A / 100)), WeedUI::ColAccentDim(),
					[this, A]() { if (Phone.IsValid()) { Phone->RequestDeposit(A); } }, 13);
				UHorizontalBoxSlot* BS = Btns->AddChildToHorizontalBox(B);
				BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
			}
			UWeedActionButton* Mx = MakeActionBtn(TEXT("Max"), WeedUI::ColAccentDim(),
				[this]() { if (Phone.IsValid()) { Phone->RequestDeposit(-1); } }, 13);
			UHorizontalBoxSlot* MS = Btns->AddChildToHorizontalBox(Mx);
			MS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); MS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
			AddU(Btns);
		}
		// --- Sturen naar co-op vriend, bescheiden presets ---
		// Eigen sub-box (incl. de spacer erboven): ALLEEN zichtbaar als er echt een 2e speler in de sessie zit.
		// NativeTick toggelt Collapsed<->zichtbaar en zet de naam van de vriend in-place in het label
		// ("Send to <naam>"); join/leave is zeldzaam, dus die toggle flitst niet.
		BankSendBox = WidgetTree->ConstructWidget<UVerticalBox>();
		{
			BankSendBox->AddChildToVerticalBox(MakeText(TEXT(""), 10, FLinearColor::Transparent));
			BankSendLabel = MakeText(FString::Printf(TEXT("Send to a friend   (%.0f%% fee)"), TrFee * 100.f), 13, WeedUI::ColTextDim());
			BankSendBox->AddChildToVerticalBox(BankSendLabel);
			const int64 Amts[3] = { 10000, 25000, 50000 }; // EUR 100 / 250 / 500
			UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
			for (int32 i = 0; i < 3; ++i)
			{
				const int64 A = Amts[i];
				UWeedActionButton* B = MakeActionBtn(FString::Printf(TEXT("%lld"), (long long)(A / 100)),
					WeedUI::ColAccentDim(), [this, A]() { if (Phone.IsValid()) { Phone->RequestTransfer(A); } }, 13);
				UHorizontalBoxSlot* BS = Btns->AddChildToHorizontalBox(B);
				BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
			}
			BankSendBox->AddChildToVerticalBox(Btns);
			// Start verborgen (singleplayer-default); NativeTick toont 'm zodra er een vriend online is.
			BankSendBox->SetVisibility(ESlateVisibility::Collapsed);
		}
		AddU(BankSendBox);
	}

	// Init-zichtbaarheid (NativeTick houdt 'm daarna live in sync met de unlock/ATM-staat). Inactieve box op HIDDEN.
	BankLockedBox->SetVisibility(bUnlocked ? ESlateVisibility::Hidden : ESlateVisibility::SelfHitTestInvisible);
	BankUnlockedBox->SetVisibility(bUnlocked ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
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
			const FLinearColor Col = (Cat == Ph->GetSupplierCat()) ? WeedUI::ColAccentDim() : WeedUI::ColSlot();
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
	StoreCartText = MakeText(TEXT("Cart 0"), 13, WeedUI::ColGood());
	StoreCartText->SetClipping(EWidgetClipping::ClipToBounds);
	UHorizontalBoxSlot* CL = CartBar->AddChildToHorizontalBox(StoreCartText);
	CL->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CL->SetVerticalAlignment(VAlign_Center);
	UpdateStoreCartText();
	StoreCartToggle = MakeActionBtn(bCartView ? TEXT("Shop") : TEXT("View cart"), WeedUI::ColAccentDim(), [this]() { bPackagesView = false; bCartView = !bCartView; RefreshStore(); }, 11);
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
		const FLinearColor Col = (TabCat == Cat) ? WeedUI::ColAccentDim() : WeedUI::ColSlot();
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

void UPhoneWidget::SaveStoreRefs(int32 App)
{
	// De huidige gedeelde winkel-refs horen bij App -> snapshot in de cache (incl. de gekozen categorie).
	FPhoneStoreRefs& R = StoreRefsByApp.FindOrAdd(App);
	R.Scroll = StoreScroll; R.Footer = StoreFooter;
	R.CartText = StoreCartText; R.CartToggle = StoreCartToggle; R.PackagesLabel = StorePackagesLabel;
	R.RowPool = StoreRowPool; R.RowSigs = StoreRowSigs;
	R.FooterPool = StoreFooterPool; R.FooterSigs = StoreFooterSigs;
	R.TabBtns = StoreTabBtns; R.QtyTexts = StoreQtyTexts;
	R.Cats = AppCats; R.bSell = bSellApp;
	R.LastCat = Phone.IsValid() ? Phone->GetSupplierCat() : -1;
}

void UPhoneWidget::RestoreStoreRefs(int32 App)
{
	// Gedeelde winkel-refs terugzetten naar App's gecachte paneel (geen herbouw). De widgets leven nog in het
	// AppPanels[App]-paneel; we wijzen de refs er weer naartoe zodat FillStoreList/RefreshStore dat paneel raken.
	const FPhoneStoreRefs* R = StoreRefsByApp.Find(App);
	if (!R) { return; }
	StoreScroll = R->Scroll; StoreFooter = R->Footer;
	StoreCartText = R->CartText; StoreCartToggle = R->CartToggle; StorePackagesLabel = R->PackagesLabel;
	StoreRowPool = R->RowPool; StoreRowSigs = R->RowSigs;
	StoreFooterPool = R->FooterPool; StoreFooterSigs = R->FooterSigs;
	StoreTabBtns = R->TabBtns; StoreQtyTexts = R->QtyTexts;
	AppCats = R->Cats; bSellApp = R->bSell;
	bCartView = false; bPackagesView = false; // net als (her)openen via BuildStoreApp: terug in de shop-catalogus
	if (Phone.IsValid() && R->LastCat >= 0) { Phone->SetSupplierCat(R->LastCat); }
}

int32 UPhoneWidget::PackagesSignature() const
{
	if (!Phone.IsValid()) { return 0; }
	const TArray<UPhoneClientComponent::FPendingDelivery>& Pend = Phone->GetPendingDeliveries();
	int32 Sig = Pend.Num() * 1000003;
	for (const UPhoneClientComponent::FPendingDelivery& D : Pend) { Sig = Sig * 31 + D.OrderId + (D.bArrived ? 7 : 0); }
	// Geleverd-historie mee-signeren: na een pickup schuift een pending naar de historie -> lijst moet verversen.
	// Lengte + nieuwste OrderId: de historie is gecapt (20), dus alleen-lengte zou na de cap niet meer wijzigen.
	const TArray<UPhoneClientComponent::FDeliveredRecord>& Hist = Phone->GetDeliveredHistory();
	Sig = Sig * 31 + Hist.Num();
	if (Hist.Num() > 0) { Sig = Sig * 31 + Hist[0].OrderId; }
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
	float Frac = 0.f; int32 Secs = 0, Phase = 0, ClockM = 0;
	if (GetApptUrgency(ApptBarContact, Frac, Secs, Phase, ClockM))
	{
		ApptBar->SetPercent(Frac);
		ApptBar->SetFillColorAndOpacity(UrgencyColor(Frac, Phase == 2));
		if (ApptBarLabel)
		{
			ApptBarLabel->SetText(FText::FromString(Phase == 0
				? FString::Printf(TEXT("Coming - arrives at %02d:%02d"), (ClockM / 60) % 24, ClockM % 60)
				: FString::Printf(TEXT("At the door now - leaves in %d:%02d"), Secs / 60, Secs % 60)));
		}
	}
	else
	{
		// Afspraak voorbij (deal gesloten of vertrokken) -> balk-box verbergen + loslaten. ApptBar/ApptBarLabel zijn
		// nu PERSISTENTE thread-widgets (in ChatApptBox): niet nullen (dan zou een volgende afspraak ze niet vinden),
		// alleen de box verbergen en het contact loslaten. RefreshChatThread toont 'm weer bij een nieuwe afspraak.
		ApptBar->SetPercent(0.f);
		if (ApptBarLabel) { ApptBarLabel->SetText(FText::FromString(TEXT("Done."))); }
		if (ChatApptBox) { ChatApptBox->SetVisibility(ESlateVisibility::Collapsed); }
		ApptBarContact = NAME_None;
	}
}

void UPhoneWidget::UpdateListBarsLive()
{
	// Werkt de HELE gesprekkenlijst in-place bij (preview + tijdstempel + ongelezen-badge + kleur + afspraak-balk)
	// zodat een nieuw bericht van een bestaand contact GEEN lijst-herbouw nodig heeft -> geen flash.
	if (ListCards.Num() == 0) { return; }
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UContactsComponent* Con = GS ? GS->GetContacts() : nullptr;
	if (!Con) { return; }
	const TArray<FPhoneMessage>& Msgs = Con->GetMessages(); // nieuwste eerst
	for (const TPair<FName, TObjectPtr<UBorder>>& CK : ListCards)
	{
		const FName Cid = CK.Key;
		float Frac = 0.f; int32 Secs = 0, Phase = 0, ClockM = 0;
		const bool bActive = GetApptUrgency(Cid, Frac, Secs, Phase, ClockM);
		const FLinearColor U = UrgencyColor(Frac, Phase == 2);
		const bool bUnread = Phone.IsValid() && Phone->HasUnreadFrom(Cid);

		// Kaart-tint (urgent > ongelezen > normaal) — nu ook terug naar normaal als de urgentie voorbij is.
		if (UBorder* C = CK.Value)
		{
			C->SetBrushColor(bActive ? FLinearColor(U.R * 0.28f, U.G * 0.28f, U.B * 0.28f, 0.97f)
				: (bUnread ? WeedUI::ColAccentDim(0.97f) : WeedUI::ColInner(0.95f)));
		}
		// Afspraak-balk (alleen aanwezig als de kaart urgent gebouwd is).
		if (TObjectPtr<UProgressBar>* Bar = ListApptBars.Find(Cid)) { if (UProgressBar* B = *Bar) { B->SetPercent(bActive ? Frac : 0.f); B->SetFillColorAndOpacity(U); } }

		// Preview: afspraak-tekst indien urgent, anders het laatste bericht van dit contact.
		FString Body; float LastClock = -1.f;
		for (const FPhoneMessage& M : Msgs)
		{
			if (!IsMsgForLocal(M) || M.FromContactId != Cid) { continue; }
			Body = (M.bFromMe ? TEXT("You: ") : TEXT("")) + M.Body.ToString(); LastClock = M.SentClockHour; break;
		}
		if (bActive) { Body = (Phase == 2) ? FString::Printf(TEXT("Reply needed - %d:%02d"), Secs / 60, Secs % 60)
			: (Phase == 0) ? FString::Printf(TEXT("Coming - arrives at %02d:%02d"), (ClockM / 60) % 24, ClockM % 60)
			: FString::Printf(TEXT("At the door now - %d:%02d left"), Secs / 60, Secs % 60); }
		if (Body.Len() > 30) { Body = Body.Left(29) + TEXT("."); }
		if (TObjectPtr<UTextBlock>* Prev = ListPreviews.Find(Cid)) { if (UTextBlock* P = *Prev) { P->SetText(FText::FromString(Body)); } }

		// Tijdstempel laatste bericht.
		if (TObjectPtr<UTextBlock>* St = ListStamps.Find(Cid))
		{
			if (UTextBlock* S = *St)
			{
				if (LastClock >= 0.f) { const int32 Hh = (int32)LastClock; const int32 Mm = (int32)((LastClock - Hh) * 60.f); S->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), Hh, Mm))); }
				else { S->SetText(FText::GetEmpty()); }
			}
		}

		// Ongelezen-badge (aantal + zichtbaarheid).
		const int32 UnreadN = Phone.IsValid() ? Phone->GetUnreadCountFrom(Cid) : 0;
		if (TObjectPtr<UBorder>* Bd = ListBadges.Find(Cid)) { if (UBorder* B = *Bd) { B->SetVisibility(UnreadN > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); } }
		if (TObjectPtr<UTextBlock>* BT = ListBadgeTexts.Find(Cid)) { if (UTextBlock* T = *BT) { T->SetText(FText::FromString(FString::Printf(TEXT("%d"), UnreadN))); } }
	}
}

void UPhoneWidget::UpdateWaitBarLive()
{
	if (!WaitBar || WaitBarSentTime < 0.f || !GetWorld()) { return; }
	const float Total = UContactsComponent::ResponseWindowSec; // GiveUpDelay in ContactsComponent: na deze real-sec geeft de klant op
	const float Elapsed = GetWorld()->GetTimeSeconds() - WaitBarSentTime;
	const float Frac = FMath::Clamp(1.f - Elapsed / Total, 0.f, 1.f);
	WaitBar->SetPercent(Frac);
	WaitBar->SetFillColorAndOpacity(Frac < 0.2f ? FLinearColor(0.9f, 0.3f, 0.25f) : (Frac < 0.45f ? FLinearColor(0.9f, 0.7f, 0.25f) : FLinearColor(0.4f, 0.7f, 0.95f)));
	if (WaitBarLabel)
	{
		const int32 Left = FMath::CeilToInt(FMath::Max(0.f, Total - Elapsed));
		WaitBarLabel->SetText(FText::FromString(Left > 0
			? FString::Printf(TEXT("Waiting for your reply - gives up in %d:%02d"), Left / 60, Left % 60)
			: FString(TEXT("They gave up waiting..."))));
	}
}

void UPhoneWidget::FillPotUpgradesInto(UScrollBox* Scroll)
{
	if (!Scroll || !Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();
	Scroll->ClearChildren();

	Scroll->AddChild(MakeText(TEXT("Pot upgrades stay with the pot."), 11, WeedUI::ColTextDim()));

	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	// Co-op: level-gate op de LOKALE speler (eigenaar van deze widget), niet op de host.
	const int32 PlayerLvl = (GS && GS->GetLeveling()) ? GS->GetLeveling()->GetLevelFor(GetOwningPlayerPawn()) : 1;

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
			Card->SetBrush(WeedUI::Rounded(WeedUI::ColInner(0.95f), 8.f));
			Card->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
			UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
			Card->SetContent(VB);
			VB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("Pot %d   -   %s"), PotNum, *PotName), 14, WeedUI::ColText()));

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
				const FLinearColor TxtCol = bOwned ? WeedUI::ColGood() : (bBuyable ? WeedUI::ColText() : WeedUI::ColTextDim());
				UTextBlock* T = MakeText(Label, 11, TxtCol);
				T->SetClipping(EWidgetClipping::ClipToBounds);
				UHorizontalBoxSlot* L = Row->AddChildToHorizontalBox(T);
				L->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); L->SetVerticalAlignment(VAlign_Center); L->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

				USizeBox* RB = WidgetTree->ConstructWidget<USizeBox>();
				RB->SetWidthOverride(124.f); RB->SetHeightOverride(26.f);
				if (bOwned)
				{
					RB->SetContent(MakeText(TEXT("installed"), 11, WeedUI::ColGood(), true));
				}
				else if (!bTierOk)
				{
					RB->SetContent(MakeText(TEXT("needs better pot"), 10, WeedUI::ColWarn(), true));
				}
				else if (!bPrereqOk)
				{
					RB->SetContent(MakeText(TEXT("prev. tier first"), 10, WeedUI::ColWarn(), true));
				}
				else if (!bLevelOk)
				{
					RB->SetContent(MakeText(FString::Printf(TEXT("Lvl %d"), Ups[i].MinPlayerLevel), 10, WeedUI::ColWarn(), true));
				}
				else
				{
					RB->SetContent(MakeActionBtn(FString::Printf(TEXT("Buy  EUR %d"), (int32)(WeedRoundEuros((int64)Cost) / 100)), WeedUI::ColAccentDim(),
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
		Scroll->AddChild(MakeText(TEXT("No pots placed yet."), 12, WeedUI::ColTextDim()));
	}
}

void UPhoneWidget::FillStoreList()
{
	if (!StoreScroll || !Phone.IsValid()) { return; }
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return; }
	UPhoneClientComponent* Ph = Phone.Get();

	const int32 Cat = Ph->GetSupplierCat();

	// LET OP: StoreQtyTexts NIET resetten. De qty-tekst zit in de persistente pool-kaart en wordt in-place
	// bijgewerkt door de -/+ knoppen; alleen bij een kaart-herbouw overschrijft de builder de ref. Zo blijft
	// de in-place update ook werken voor rijen die NIET herbouwd zijn (anders zou de ref na een refresh weg zijn).

	// Helper: leeg de persistente pools (alleen bij een ECHTE view-wissel, bv. packages<->shop).
	auto DropPools = [this]()
	{
		for (const TObjectPtr<UBorder>& B : StoreRowPool) { if (B) { B->RemoveFromParent(); } }
		StoreRowPool.Reset(); StoreRowSigs.Reset();
		for (const TObjectPtr<UBorder>& B : StoreFooterPool) { if (B) { B->RemoveFromParent(); } }
		StoreFooterPool.Reset(); StoreFooterSigs.Reset();
	};

	// --- Packages-view is een echt àndere schermweergave (eigen per-OrderId-kaarten in dezelfde scroll).
	if (bPackagesView)
	{
		DropPools();
		StoreScroll->ClearChildren(); // whole-screen change: packages-lijst i.p.v. de winkel-pool
		FillPackagesInto(StoreScroll);
		return;
	}
	// Uit packages terug in de winkel: de losse package-kaarten + delivered-box (buiten de pool om toegevoegd) opruimen.
	if (PkgScrollOwner == StoreScroll && (PkgCards.Num() > 0 || PkgDeliveredBox || PkgEmptyRow))
	{
		StoreScroll->ClearChildren();
		StoreRowPool.Reset(); StoreRowSigs.Reset(); // pool-widgets zijn net mee-geleegd -> refs vrijgeven
		PkgCards.Reset(); PkgBars.Reset(); PkgEtas.Reset(); PkgEmptyRow = nullptr; PkgScrollOwner = nullptr;
		PkgDeliveredBox = nullptr; PkgDeliveredSig = -1;
	}

	// Verzamel eerst de rij-inhoud: per logische rij een signatuur + een bouw-lambda die de kaart maakt.
	// (Zo herbouwen we alleen de rij die ECHT wijzigde, net als StoreWidget::FillBody.)
	TArray<FString> Sigs;
	TArray<TFunction<UWidget*()>> Builders;
	auto AddRow = [&](const FString& Sig, TFunction<UWidget*()> Build) { Sigs.Add(Sig); Builders.Add(MoveTemp(Build)); };

	// Pot-gear (cat 8) wordt nu een NORMALE koop-categorie: fysieke accessoires die je naast je pot zet.
	if (bCartView)
	{
		const int32 Lines = Ph->GetCartNumLines();
		if (Lines == 0)
		{
			AddRow(TEXT("cart-empty"), [this]() -> UWidget* { return MakeText(TEXT("Cart is empty."), 13, WeedUI::ColTextDim()); });
		}
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

			const FString Sig = FString::Printf(TEXT("cart|%d|%s|%d|%d|%d"), Idx, *LId.ToString(), LQty, bSell ? 1 : 0, LineP);
			AddRow(Sig, [this, Ph, LName, LQty, bSell, LineP, Idx]() -> UWidget*
			{
				UBorder* CardB = WidgetTree->ConstructWidget<UBorder>();
				CardB->SetBrush(RoundedBrush(WeedUI::ColInner(0.95f), 8.f));
				CardB->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
				UVerticalBox* CVB = WidgetTree->ConstructWidget<UVerticalBox>();
				CardB->SetContent(CVB);
				UTextBlock* NameT = MakeText(LName, 12, bSell ? WeedUI::ColGood() : WeedUI::ColText());
				NameT->SetClipping(EWidgetClipping::ClipToBounds);
				CVB->AddChildToVerticalBox(NameT);
				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				UHorizontalBoxSlot* T = Row->AddChildToHorizontalBox(MakeText(
					bSell ? FString::Printf(TEXT("x%d   +EUR %d"), LQty, (int32)(WeedRoundEuros((int64)LineP) / 100)) : FString::Printf(TEXT("x%d   EUR %d"), LQty, (int32)(WeedRoundEuros((int64)LineP) / 100)),
					12, WeedUI::ColGood()));
				T->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); T->SetVerticalAlignment(VAlign_Center);
				Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("-"), WeedUI::ColSlot(), [this, Ph, Idx]() { Ph->AdjustCartLine(Idx, -1); RefreshStore(); }));
				Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("+"), WeedUI::ColSlot(), [this, Ph, Idx]() { Ph->AdjustCartLine(Idx, +1); RefreshStore(); }));
				Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("x"), WeedUI::ColWarn(), [this, Ph, Idx]() { Ph->AdjustCartLine(Idx, -100000); RefreshStore(); }));
				CVB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
				return CardB;
			});
		}
		// De voettekst wordt via een eigen pool (StoreFooterPool) gebouwd: bij bezorgoptie/deliver-to-home
		// wijzigt alleen deze footer-handtekening -> de scroll-lijst hierboven blijft ongemoeid (geen scroll-sprong).
		FillStoreListRows(MoveTemp(Sigs), MoveTemp(Builders));
		FillStoreFooter();
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
		if (Order.Num() == 0)
		{
			AddRow(TEXT("nothing-sellable"), [this]() -> UWidget* { return MakeText(TEXT("(nothing sellable)"), 13, WeedUI::ColTextDim()); });
			FillStoreListRows(MoveTemp(Sigs), MoveTemp(Builders));
			FillStoreFooter();
			return;
		}

		for (const FName& Id : Order)
		{
			const int32 Have = Totals[Id];
			const int32 Val = Store->GetSellValueCents(Id);
			const int32 Pend = Ph->GetPendingSellQty(Id);
			const FName PickId = Id;
			// GEEN Pend in de signatuur: de qty-tekst wordt in-place via StoreQtyTexts bijgewerkt (geen kaart-herbouw).
			const FString Sig = FString::Printf(TEXT("sell|%s|%d|%d"), *Id.ToString(), Have, Val);
			AddRow(Sig, [this, Ph, PickId, Have, Val, Pend]() -> UWidget*
			{
				UBorder* CardB = WidgetTree->ConstructWidget<UBorder>();
				CardB->SetBrush(RoundedBrush(WeedUI::ColInner(0.95f), 8.f));
				CardB->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
				UVerticalBox* CardVB = WidgetTree->ConstructWidget<UVerticalBox>();
				CardB->SetContent(CardVB);
				CardVB->AddChildToVerticalBox(MakeText(WeedUI::PrettyItemName(PickId), 14, WeedUI::ColText()));
				CardVB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("You have %d   -   sells for EUR %d each"), Have, (int32)(WeedRoundEuros((int64)Val) / 100)), 10, WeedUI::ColTextDim()));

				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				UHorizontalBoxSlot* PT = Row->AddChildToHorizontalBox(MakeText(FString::Printf(TEXT("+EUR %d"), (int32)(WeedRoundEuros((int64)Val * Pend) / 100)), 13, WeedUI::ColGood()));
				PT->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); PT->SetVerticalAlignment(VAlign_Center);
				UTextBlock* QtyT = MakeText(FString::Printf(TEXT("  %d  "), Pend), 13, WeedUI::ColText());
				StoreQtyTexts.Add(PickId, QtyT);
				Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("-"), WeedUI::ColSlot(), [this, Ph, PickId]() { Ph->AdjustPendingSellQty(PickId, -1); if (TObjectPtr<UTextBlock>* X = StoreQtyTexts.Find(PickId)) { (*X)->SetText(FText::FromString(FString::Printf(TEXT("  %d  "), Ph->GetPendingSellQty(PickId)))); } }));
				Row->AddChildToHorizontalBox(QtyT)->SetVerticalAlignment(VAlign_Center);
				Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("+"), WeedUI::ColSlot(), [this, Ph, PickId]() { Ph->AdjustPendingSellQty(PickId, +1); if (TObjectPtr<UTextBlock>* X = StoreQtyTexts.Find(PickId)) { (*X)->SetText(FText::FromString(FString::Printf(TEXT("  %d  "), Ph->GetPendingSellQty(PickId)))); } }));
				Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Add"), WeedUI::ColAccentDim(), [this, Ph, PickId]() { Ph->AddSellToCart(PickId); UpdateStoreCartText(); }))->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
				CardVB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
				return CardB;
			});
		}
		FillStoreListRows(MoveTemp(Sigs), MoveTemp(Builders));
		FillStoreFooter();
		return;
	}

	// --- Catalogus (kopen) ---
	// Co-op: level-gate op de LOKALE speler (eigenaar van deze widget), niet op de host.
	const int32 CatPlayerLvl = (GS && GS->GetLeveling()) ? GS->GetLeveling()->GetLevelFor(GetOwningPlayerPawn()) : 1;
	for (const FName& Id : Store->GetSupplierCategory(Cat))
	{
		const int32 Price = Store->GetCatalogPriceCents(Id);
		const int32 Pend = Ph->GetPendingQty(Id);
		const int32 ReqLvl = Store->RequiredLevelFor(Id);
		const bool bLocked = ReqLvl > CatPlayerLvl;
		const FName PickId = Id;
		const FName IconId = UStoreComponent::IsSeedCategory(Cat) ? UStoreComponent::SeedItemId(Id) : Id;
		const FString NameStr = Store->GetCatalogName(Id).ToString();
		const FString DescStr = Store->GetCatalogDesc(Id).ToString();
		// GEEN Pend in de signatuur: de qty-tekst wordt in-place via StoreQtyTexts bijgewerkt (geen kaart-herbouw).
		const FString Sig = FString::Printf(TEXT("cat|%s|%d|%d|%d"), *Id.ToString(), Price, ReqLvl, bLocked ? 1 : 0);
		AddRow(Sig, [this, Ph, PickId, IconId, NameStr, DescStr, Price, ReqLvl, bLocked, Pend]() -> UWidget*
		{
			UBorder* CardB = WidgetTree->ConstructWidget<UBorder>();
			CardB->SetBrush(RoundedBrush(bLocked ? WeedUI::ColSlotEmpty(0.95f) : WeedUI::ColInner(0.95f), 8.f));
			CardB->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));

			// Icoon links + tekstkolom rechts (seeds: icoon van het zaad-item).
			UHorizontalBox* CardHB = WidgetTree->ConstructWidget<UHorizontalBox>();
			CardB->SetContent(CardHB);
			USizeBox* IcoSz = WidgetTree->ConstructWidget<USizeBox>();
			IcoSz->SetWidthOverride(52.f); IcoSz->SetHeightOverride(52.f);
			{
				// Icoon + tag-bubble (OG, GSC, II, 100g, ...) onderaan, net als in de hotbar/inventory.
				UOverlay* IcoOv = WidgetTree->ConstructWidget<UOverlay>();
				UOverlaySlot* IconOS = IcoOv->AddChildToOverlay(WeedUI::ItemIcon(WidgetTree, IconId, 52.f));
				IconOS->SetHorizontalAlignment(HAlign_Fill); IconOS->SetVerticalAlignment(VAlign_Fill);
				const FString STag = WeedUI::ItemTagShort(IconId);
				if (!STag.IsEmpty())
				{
					UBorder* TagPill = WidgetTree->ConstructWidget<UBorder>();
					TagPill->SetBrush(WeedUI::ItemTagPillBrush(IconId, 6.f));
					TagPill->SetPadding(FMargin(6.f, 0.f, 6.f, 2.f));
					UTextBlock* TagT = MakeText(STag, 11, FLinearColor::White, true);
					TagT->SetFont(WeedUI::ItemTagFont(11));
					TagT->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
					TagT->SetShadowOffset(FVector2D(1.f, 1.f));
					TagPill->SetContent(TagT);
					UOverlaySlot* TagOS = IcoOv->AddChildToOverlay(TagPill);
					TagOS->SetHorizontalAlignment(HAlign_Center); TagOS->SetVerticalAlignment(VAlign_Bottom);
				}
				IcoSz->SetContent(IcoOv);
			}
			UHorizontalBoxSlot* IcoSlot = CardHB->AddChildToHorizontalBox(IcoSz);
			IcoSlot->SetVerticalAlignment(VAlign_Center); IcoSlot->SetPadding(FMargin(0.f, 0.f, 9.f, 0.f));

			UVerticalBox* CardVB = WidgetTree->ConstructWidget<UVerticalBox>();
			UHorizontalBoxSlot* VBSlot = CardHB->AddChildToHorizontalBox(CardVB);
			VBSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); VBSlot->SetVerticalAlignment(VAlign_Center);
			// Titel (kort) + beschrijving eronder.
			CardVB->AddChildToVerticalBox(MakeText(NameStr, 14, bLocked ? WeedUI::ColTextDim() : WeedUI::ColText()));
			if (!DescStr.IsEmpty())
			{
				UTextBlock* Desc = MakeText(DescStr, 10, WeedUI::ColTextDim());
				Desc->SetAutoWrapText(true);
				CardVB->AddChildToVerticalBox(Desc);
			}
			if (ReqLvl > 0)
			{
				CardVB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("Unlocks at level %d"), ReqLvl), 10,
					bLocked ? WeedUI::ColWarn() : WeedUI::ColGood()));
			}

			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UHorizontalBoxSlot* PT = Row->AddChildToHorizontalBox(MakeText(FString::Printf(TEXT("EUR %d"), (int32)(WeedRoundEuros((int64)Price) / 100)), 13, WeedUI::ColGood()));
			PT->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); PT->SetVerticalAlignment(VAlign_Center);
			UTextBlock* QtyT = MakeText(FString::Printf(TEXT("  %d  "), Pend), 13, WeedUI::ColText());
			StoreQtyTexts.Add(PickId, QtyT);
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("-"), WeedUI::ColSlot(), [this, Ph, PickId]() { Ph->AdjustPendingQty(PickId, -1); if (TObjectPtr<UTextBlock>* X = StoreQtyTexts.Find(PickId)) { (*X)->SetText(FText::FromString(FString::Printf(TEXT("  %d  "), Ph->GetPendingQty(PickId)))); } }));
			Row->AddChildToHorizontalBox(QtyT)->SetVerticalAlignment(VAlign_Center);
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("+"), WeedUI::ColSlot(), [this, Ph, PickId]() { Ph->AdjustPendingQty(PickId, +1); if (TObjectPtr<UTextBlock>* X = StoreQtyTexts.Find(PickId)) { (*X)->SetText(FText::FromString(FString::Printf(TEXT("  %d  "), Ph->GetPendingQty(PickId)))); } }));
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Add"), WeedUI::ColAccentDim(), [this, Ph, PickId]() { Ph->AddToCart(PickId); UpdateStoreCartText(); }))->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
			CardVB->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
			return CardB;
		});
	}

	// Winkel-view: geen footer (die is alleen in de cart). Leeg de footer-pool.
	FillStoreListRows(MoveTemp(Sigs), MoveTemp(Builders));
	FillStoreFooter();
}

void UPhoneWidget::FillStoreListRows(TArray<FString> Sigs, TArray<TFunction<UWidget*()>> Builders)
{
	if (!StoreScroll) { return; }
	const int32 N = Sigs.Num();

	// Pool op maat brengen (persistent) -> geen ClearChildren, dus geen flash/scroll-sprong.
	while (StoreRowPool.Num() < N)
	{
		UBorder* SlotBox = WidgetTree->ConstructWidget<UBorder>();
		SlotBox->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f)); // transparant wrapper (geen extra rand)
		SlotBox->SetPadding(FMargin(0.f));
		StoreScroll->AddChild(SlotBox);
		StoreRowPool.Add(SlotBox); StoreRowSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
	}
	while (StoreRowPool.Num() > N)
	{
		const int32 Last = StoreRowPool.Num() - 1;
		if (StoreRowPool[Last]) { StoreRowPool[Last]->RemoveFromParent(); }
		StoreRowPool.RemoveAt(Last); StoreRowSigs.RemoveAt(Last);
	}

	// Per-rij diff: alleen een rij die ECHT wijzigde krijgt nieuwe inhoud (kaart + gefoldede 3px-gap eronder).
	for (int32 i = 0; i < N; ++i)
	{
		if (!StoreRowPool.IsValidIndex(i) || !StoreRowPool[i]) { continue; }
		if (StoreRowSigs.IsValidIndex(i) && Sigs[i] == StoreRowSigs[i]) { continue; }
		StoreRowSigs[i] = Sigs[i];

		UVerticalBox* Holder = WidgetTree->ConstructWidget<UVerticalBox>();
		Holder->AddChildToVerticalBox(Builders[i]());
		UBorder* Gap = WidgetTree->ConstructWidget<UBorder>();
		Gap->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
		Gap->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		Holder->AddChildToVerticalBox(Gap);
		StoreRowPool[i]->SetContent(Holder);
	}
}

void UPhoneWidget::FillStoreFooter()
{
	if (!StoreFooter || !Phone.IsValid()) { return; }
	UPhoneClientComponent* Ph = Phone.Get();

	// Bouw de voettekst als ÉÉN gepoolde entry: alleen zichtbaar in de cart. Zo blijft de scroll-lijst ongemoeid
	// bij bezorgoptie/deliver-to-home (geen scroll-sprong) en flitst er niets.
	FString Sig;
	TFunction<UWidget*()> Build;

	if (bCartView && bSellApp)
	{
		const int32 SellSub = Ph->GetCartSellCents();
		Sig = FString::Printf(TEXT("sellfoot|%d"), SellSub);
		Build = [this, Ph, SellSub]() -> UWidget*
		{
			UVerticalBox* F = WidgetTree->ConstructWidget<UVerticalBox>();
			UTextBlock* TotT = MakeText(FString::Printf(TEXT("You receive: EUR %d"), (int32)(WeedRoundEuros((int64)SellSub) / 100)),
				15, WeedUI::ColGood());
			TotT->SetJustification(ETextJustify::Right);
			F->AddChildToVerticalBox(TotT)->SetPadding(FMargin(0.f, 6.f, 4.f, 4.f));
			F->AddChildToVerticalBox(MakeActionBtn(TEXT("SELL"), WeedUI::ColAccentDim(),
				[this, Ph]() { Ph->Checkout(0); bCartView = false; RefreshStore(); }, 14));
			return F;
		};
	}
	else if (bCartView)
	{
		const int32 BuySub = Ph->GetCartBuyCents();
		const int32 SellSub = Ph->GetCartSellCents();
		const int32 Net = Ph->GetCartNetCents(DeliveryOpt);
		const int32 CurTarget = Ph->ResolveDeliveryHome();
		const TArray<int32> OwnedH = Ph->GetOwnedHomes();
		FString HomesSig; for (int32 H : OwnedH) { HomesSig += FString::Printf(TEXT("%d,"), H); }
		Sig = FString::Printf(TEXT("buyfoot|%d|%d|%d|%d|%d|%s"), BuySub, SellSub, DeliveryOpt, Net, CurTarget, *HomesSig);
		Build = [this, Ph, BuySub, SellSub, Net, CurTarget, OwnedH]() -> UWidget*
		{
			UVerticalBox* F = WidgetTree->ConstructWidget<UVerticalBox>();

			// Bezorgopties (alleen relevant als je koopt). Fee = % van het koop-subtotaal.
			F->AddChildToVerticalBox(MakeText(BuySub > 0 ? TEXT("Delivery") : TEXT("Selling only - no delivery needed"), 11, WeedUI::ColTextDim()))->SetPadding(FMargin(0.f, 4.f, 0.f, 2.f));
			if (BuySub > 0)
			{
				UHorizontalBox* Opts = WidgetTree->ConstructWidget<UHorizontalBox>();
				for (int32 d = 0; d < 3; ++d)
				{
					const int32 OptFee = (int32)WeedRoundEuros((int64)FMath::RoundToInt(BuySub * UPhoneClientComponent::DeliveryFeePct(d)));
					const bool bSel = (d == DeliveryOpt);
					const FLinearColor Col = bSel ? WeedUI::ColAccentDim() : WeedUI::ColSlot();
					UWeedActionButton* OB = MakeActionBtn(TEXT(""), Col, [this, d]() { DeliveryOpt = d; RefreshStore(); }, 9);
					// Inhoud: PRIJS groot + leidend bovenaan, dan de naam, dan de tijd klein.
					UVerticalBox* OVB = WidgetTree->ConstructWidget<UVerticalBox>();
					FString PriceStr;
					if (OptFee <= 0) { PriceStr = TEXT("FREE"); }
					else { PriceStr = FString::Printf(TEXT("€%d"), (int32)(WeedRoundEuros((int64)OptFee) / 100)); }
					OVB->AddChildToVerticalBox(MakeText(PriceStr, 15, WeedUI::ColGood(), true))->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
					OVB->AddChildToVerticalBox(MakeText(UPhoneClientComponent::DeliveryName(d), 11, WeedUI::ColText(), true));
					OVB->AddChildToVerticalBox(MakeText(UPhoneClientComponent::DeliveryTimeText(d), 9, WeedUI::ColTextDim(), true));
					OB->SetContent(OVB);
					UHorizontalBoxSlot* OS = Opts->AddChildToHorizontalBox(OB);
					OS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); OS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
				}
				F->AddChildToVerticalBox(Opts)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

				// Bezorg-adres: bij meerdere woningen kies je waar 't heen gaat. Standaard het huis waar je
				// nu binnen bent (groen). Anders je huidige/actieve woning.
				if (OwnedH.Num() > 1)
				{
					F->AddChildToVerticalBox(MakeText(TEXT("Deliver to"), 11, WeedUI::ColTextDim()))->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
					UHorizontalBox* HomesRow = WidgetTree->ConstructWidget<UHorizontalBox>();
					for (int32 HIdx : OwnedH)
					{
						const FLinearColor Col = (HIdx == CurTarget) ? WeedUI::ColAccentDim() : WeedUI::ColSlot();
						UWeedActionButton* HB = MakeActionBtn(Ph->GetHomeLabel(HIdx), Col,
							[this, HIdx]() { if (Phone.IsValid()) { Phone->SetDeliveryHome(HIdx); } RefreshStore(); }, 9);
						UHorizontalBoxSlot* HS = HomesRow->AddChildToHorizontalBox(HB);
						HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); HS->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
					}
					F->AddChildToVerticalBox(HomesRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
				}
			}

			// Opbouw-regel (kort): koop - verkoop - bezorging. Geen decimalen + alleen tonen wat van toepassing is.
			const int32 Fee = (int32)WeedRoundEuros((int64)FMath::RoundToInt(BuySub * UPhoneClientComponent::DeliveryFeePct(DeliveryOpt)));
			FString BD = FString::Printf(TEXT("Buy EUR %d"), (int32)(WeedRoundEuros((int64)BuySub) / 100));
			if (SellSub > 0) { BD += FString::Printf(TEXT("  -  Sell EUR %d"), (int32)(WeedRoundEuros((int64)SellSub) / 100)); }
			if (Fee > 0)     { BD += FString::Printf(TEXT("  +  Deliv EUR %d"), (int32)(WeedRoundEuros((int64)Fee) / 100)); }
			UTextBlock* Breakdown = MakeText(BD, 10, WeedUI::ColTextDim());
			Breakdown->SetAutoWrapText(true);
			Breakdown->SetJustification(ETextJustify::Right);
			F->AddChildToVerticalBox(Breakdown)->SetPadding(FMargin(0.f, 2.f, 4.f, 0.f));

			// Netto: positief = betalen, negatief = ontvangen.
			UTextBlock* TotT = MakeText(Net >= 0 ? FString::Printf(TEXT("Total: EUR %d"), (int32)(WeedRoundEuros((int64)Net) / 100))
				: FString::Printf(TEXT("You receive: EUR %d"), (int32)(WeedRoundEuros((int64)(-Net)) / 100)),
				15, WeedUI::ColGood());
			TotT->SetJustification(ETextJustify::Right);
			F->AddChildToVerticalBox(TotT)->SetPadding(FMargin(0.f, 2.f, 4.f, 4.f));

			F->AddChildToVerticalBox(MakeActionBtn(TEXT("CHECKOUT"), WeedUI::ColAccentDim(),
				[this, Ph]() { Ph->Checkout(DeliveryOpt); bCartView = false; RefreshStore(); }, 14));
			return F;
		};
	}
	else
	{
		Sig = TEXT("nofoot"); // geen footer buiten de cart
	}

	// Footer-pool: 0 of 1 entry. Diff op signatuur -> alleen herbouwen als de footer-inhoud wijzigt.
	const int32 Want = Build ? 1 : 0;
	while (StoreFooterPool.Num() < Want)
	{
		UBorder* SlotBox = WidgetTree->ConstructWidget<UBorder>();
		SlotBox->SetBrush(WeedUI::Rounded(FLinearColor(0, 0, 0, 0), 0.f));
		SlotBox->SetPadding(FMargin(0.f));
		StoreFooter->AddChildToVerticalBox(SlotBox);
		StoreFooterPool.Add(SlotBox); StoreFooterSigs.Add(TEXT("\x01"));
	}
	while (StoreFooterPool.Num() > Want)
	{
		const int32 Last = StoreFooterPool.Num() - 1;
		if (StoreFooterPool[Last]) { StoreFooterPool[Last]->RemoveFromParent(); }
		StoreFooterPool.RemoveAt(Last); StoreFooterSigs.RemoveAt(Last);
	}
	if (Want == 1 && StoreFooterPool[0])
	{
		if (!StoreFooterSigs.IsValidIndex(0) || Sig != StoreFooterSigs[0])
		{
			StoreFooterSigs[0] = Sig;
			StoreFooterPool[0]->SetContent(Build());
		}
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

	// Messages-app (index 3): rode ongelezen-badge rechtsboven op het app-icoon (refs bewaard -> live update).
	UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
	Ov->AddChildToOverlay(Sz);
	if (AppIndex == 3)
	{
		const int32 Unread = Phone.IsValid() ? Phone->GetUnreadMessageCount() : 0;
		MsgAppBadgeText = MakeText(Unread > 99 ? FString(TEXT("99+")) : FString::Printf(TEXT("%d"), FMath::Max(0, Unread)), 11, FLinearColor::White, true);
		MsgAppBadgePill = WidgetTree->ConstructWidget<UBorder>();
		MsgAppBadgePill->SetBrush(RoundedBrush(WeedUI::ColWarn(0.98f), 9.f));
		MsgAppBadgePill->SetPadding(FMargin(5.f, 0.f, 5.f, 0.f));
		MsgAppBadgePill->SetContent(MsgAppBadgeText);
		MsgAppBadgePill->SetVisibility(Unread > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		UOverlaySlot* PS = Ov->AddChildToOverlay(MsgAppBadgePill);
		PS->SetHorizontalAlignment(HAlign_Right); PS->SetVerticalAlignment(VAlign_Top);
		PS->SetPadding(FMargin(0.f, 2.f, 6.f, 0.f));
	}
	else if (AppIndex == 11) // Goals-app: gele badge met aantal claimbare doelen (live, geen rebuild)
	{
		const int32 Claim = GetClaimableGoals();
		GoalsAppBadgeText = MakeText(Claim > 99 ? FString(TEXT("99+")) : FString::Printf(TEXT("%d"), FMath::Max(0, Claim)), 11, WeedUI::ColBg(), true);
		GoalsAppBadgePill = WidgetTree->ConstructWidget<UBorder>();
		GoalsAppBadgePill->SetBrush(RoundedBrush(WeedUI::ColGood(0.98f), 9.f));
		GoalsAppBadgePill->SetPadding(FMargin(5.f, 0.f, 5.f, 0.f));
		GoalsAppBadgePill->SetContent(GoalsAppBadgeText);
		GoalsAppBadgePill->SetVisibility(Claim > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		UOverlaySlot* PS = Ov->AddChildToOverlay(GoalsAppBadgePill);
		PS->SetHorizontalAlignment(HAlign_Right); PS->SetVerticalAlignment(VAlign_Top);
		PS->SetPadding(FMargin(0.f, 2.f, 6.f, 0.f));
	}

	UVerticalBoxSlot* S1 = Cell->AddChildToVerticalBox(Ov);
	S1->SetHorizontalAlignment(HAlign_Center);

	UVerticalBoxSlot* S2 = Cell->AddChildToVerticalBox(MakeText(Name, 11, WeedUI::ColTextDim(), true));
	S2->SetHorizontalAlignment(HAlign_Center);
	S2->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	return Cell;
}

void UPhoneWidget::AddInfoRow(const FString& Txt, const FLinearColor& Col, int32 Size)
{
	if (!ActiveContent) { return; } // loopt tijdens een build -> ActiveContent = het paneel dat nu bouwt
	UVerticalBoxSlot* RowSlot = ActiveContent->AddChildToVerticalBox(MakeText(Txt, Size, Col));
	RowSlot->SetPadding(FMargin(2.f, 3.f, 2.f, 3.f));
}

void UPhoneWidget::BuildShell(UCanvasPanel* Root)
{
	// Lege ruimte naast de telefoon mag klikken doorlaten naar de canvas-winkel.
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Telefoon-frame: afgerond, donker, rechts in beeld.
	Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Frame"));
	{
		FSlateBrush FrBr = WeedUI::Rounded(WeedUI::ColPanel(0.99f), 38.f);
		FrBr.OutlineSettings.Width = 1.f;
		FrBr.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.6f));
		Frame->SetBrush(FrBr);
	}
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
	TimeText = MakeText(TEXT("Day 00:00"), 12, WeedUI::ColTextDim());
	LevelText = MakeText(TEXT("Lv 1"), 12, WeedUI::ColGood(), true);
	CashText = MakeText(TEXT("EUR 0"), 12, WeedUI::ColGood());
	TimeText->SetClipping(EWidgetClipping::ClipToBounds);
	LevelText->SetClipping(EWidgetClipping::ClipToBounds);
	CashText->SetClipping(EWidgetClipping::ClipToBounds);
	{
		UHorizontalBoxSlot* H1 = Status->AddChildToHorizontalBox(TimeText);  H1->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); H1->SetHorizontalAlignment(HAlign_Left);
		UHorizontalBoxSlot* H2 = Status->AddChildToHorizontalBox(LevelText); H2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); H2->SetHorizontalAlignment(HAlign_Center);
		UHorizontalBoxSlot* H3 = Status->AddChildToHorizontalBox(CashText);  H3->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); H3->SetHorizontalAlignment(HAlign_Right);
	}
	UVerticalBoxSlot* StatusSlot = VB->AddChildToVerticalBox(Status);
	StatusSlot->SetPadding(FMargin(6.f, 4.f, 6.f, 4.f));

	// Level/XP-voortgangsbalk onder de statusbalk: zie hoeveel XP je nog nodig hebt voor het volgende level.
	{
		UHorizontalBox* XpRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		LevelXpBar = WidgetTree->ConstructWidget<UProgressBar>();
		LevelXpBar->SetFillColorAndOpacity(WeedUI::ColAccent());
		LevelXpBar->SetPercent(0.f);
		USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>(); BarSz->SetHeightOverride(10.f);
		BarSz->SetContent(LevelXpBar);
		UHorizontalBoxSlot* BS = XpRow->AddChildToHorizontalBox(BarSz);
		BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetVerticalAlignment(VAlign_Center); BS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		LevelXpText = MakeText(TEXT("0 / 0 XP"), 9, WeedUI::ColTextDim());
		UHorizontalBoxSlot* TS = XpRow->AddChildToHorizontalBox(LevelXpText);
		TS->SetVerticalAlignment(VAlign_Center);
		UVerticalBoxSlot* XpSlot = VB->AddChildToVerticalBox(XpRow);
		XpSlot->SetPadding(FMargin(8.f, 0.f, 8.f, 8.f));
	}

	// Scherm-vlak met de app-inhoud.
	UBorder* Screen = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Screen"));
	Screen->SetBrush(RoundedBrush(WeedUI::ColWell(1.f), 26.f));
	Screen->SetPadding(FMargin(12.f));
	UVerticalBoxSlot* ScreenSlot = VB->AddChildToVerticalBox(Screen);
	ScreenSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Overlay i.p.v. VerticalBox: de per-app panelen stapelen op elkaar. Zo kunnen inactieve panelen op Hidden
	// staan (blijven gearrangeerd, niet getekend) i.p.v. Collapsed -> een app-open is puur een teken-toggle
	// zonder dat Slate het paneel opnieuw moet layouten (die her-layout gaf de 1-frame-flikker bij elke app-open).
	ContentBox = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ContentBox"));
	Screen->SetContent(ContentBox);

	// Ronde zwarte home-knop onderaan (zoals een telefoon-home-knop).
	UWeedActionButton* HomeBtn = WidgetTree->ConstructWidget<UWeedActionButton>();
	HomeBtn->OnClicked.AddDynamic(HomeBtn, &UWeedActionButton::Handle);
	// Home: vanuit een app -> terug naar het homescreen; al op het homescreen -> telefoon sluiten.
	HomeBtn->OnAction.BindLambda([this](int32, int32) { if (Phone.IsValid()) { if (Phone->IsHomeScreen()) { Phone->Toggle(); } else { Phone->GoHome(); } } });
	{
		FButtonStyle HS;
		HS.Normal = RoundedBrush(WeedUI::ColBg(1.f), 24.f);
		HS.Hovered = RoundedBrush(WeedUI::ColInner(1.f), 24.f);
		HS.Pressed = RoundedBrush(WeedUI::ColBg(1.f), 24.f);
		HS.Normal.OutlineSettings.Color = FSlateColor(WeedUI::ColStroke(0.9f));
		HS.Normal.OutlineSettings.Width = 2.f;
		HS.Hovered.OutlineSettings = HS.Normal.OutlineSettings;
		HS.Pressed.OutlineSettings = HS.Normal.OutlineSettings;
		HomeBtn->SetStyle(HS);
	}
	// Klein huisje in de knop.
	{
		USizeBox* Hi = WidgetTree->ConstructWidget<USizeBox>();
		Hi->SetWidthOverride(20.f); Hi->SetHeightOverride(20.f);
		Hi->SetContent(WeedUI::Icon(WidgetTree, WeedUI::EIcon::Home, 20.f, WeedUI::ColText()));
		HomeBtn->SetContent(Hi);
	}
	USizeBox* HbSz = WidgetTree->ConstructWidget<USizeBox>();
	HbSz->SetWidthOverride(48.f); HbSz->SetHeightOverride(48.f);
	HbSz->SetContent(HomeBtn);
	UVerticalBoxSlot* HbSlot = VB->AddChildToVerticalBox(HbSz);
	HbSlot->SetHorizontalAlignment(HAlign_Center);
	HbSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));
}

void UPhoneWidget::PrewarmStep()
{
	if (bPrewarmDone || !ContentBox || !Phone.IsValid()) { return; }

	// Prewarm-key-volgorde: eerst home (cursor 0), daarna de app-tabs 0..GNumApps-1 (cursor 1..N), waarbij de
	// verwijderde Map- en Lab-apps (GMapApp/GHashApp) worden overgeslagen. Eén stap per tick -> geen hitch;
	// loopt terwijl de telefoon dicht is, dus de per-paneel visibility-toggles zijn onzichtbaar.
	// Aantal echte app-stappen na home = GNumApps - 2 (Map + Lab eruit).
	const int32 NumSteps = 1 + (GNumApps - 2); // home + alle apps behalve Map en Lab

	// Snapshot de winkel-categorie vóór de eerste stap (winkel-panelen kunnen SetSupplierCat muteren);
	// na de laatste stap zetten we 'm terug zodat prewarm géén netto neveneffect op de telefoon-status heeft.
	if (PrewarmCursor == 0) { SavedSupplierCat = Phone->GetSupplierCat(); }

	if (PrewarmCursor < NumSteps)
	{
		// Bepaal de key voor deze stap. Cursor 0 = home; cursor>=1 = app-tab (Lab overslaan).
		bPrewarming = true;
		if (PrewarmCursor == 0)
		{
			bPrewarmHome = true; PrewarmApp = 0;
		}
		else
		{
			bPrewarmHome = false;
			// App-index = (cursor-1), maar met Map en Lab overgeslagen: schuif één op vanaf elke
			// verwijderde app-index (oplopend: eerst GMapApp=5, daarna GHashApp=10).
			int32 AppIdx = PrewarmCursor - 1;
			if (AppIdx >= GMapApp) { ++AppIdx; }
			if (AppIdx >= GHashApp) { ++AppIdx; }
			PrewarmApp = AppIdx;
		}
		RefreshContent(); // bouwt dit ENE paneel (bNew -> bouwt sowieso), rest blijft Collapsed
		bPrewarming = false;
		++PrewarmCursor;
		return; // één paneel deze tick — de rest volgende ticks
	}

	// --- Finaliseren (na de laatste stap) ---
	Phone->SetSupplierCat(SavedSupplierCat); // winkel-categorie herstellen (prewarm mag niets muteren)

	// Het ECHTE huidige paneel opnieuw bouwen zodat de gedeelde single-value refs (MsgAppBadgePill, StoreScroll,
	// PackagesScroll, OfferBox, PickerClockText, ...) bij dat paneel horen — de prewarm-loop liet ze bij het
	// LAATST gebouwde paneel achter. bContentDirty forceert de rebuild-tak (i.p.v. de pure-switch early-return);
	// daarna staat ActiveContent op de huidige app (zichtbaar) en is de rest Collapsed. Dit is normaal onzichtbaar
	// want de telefoon is nog dicht (Frame Collapsed).
	bContentDirty = true;
	RefreshContent();
	bContentDirty = false; // vers gebouwd -> geen directe her-refresh nodig (zou de flash terugbrengen)
	// Change-detectie-baseline gelijk aan het net gebouwde huidige paneel, zodat de eerste OPEN geen
	// (weliswaar flash-vrije, maar overbodige) pure-switch-refresh triggert.
	bLastHome = Phone->IsHomeScreen();
	bLastApp = Phone->GetTab();

	// Sig-baselines gelijkzetten aan de HUIDIGE waarden zodat NativeTick de vers-gebouwde panelen als up-to-date
	// beschouwt: de eerste ECHTE open triggert dan geen MarkDirty+herbouw (dat zou de flash juist terugbrengen).
	// Formules exact gelijk aan die in NativeTick.
	LastMsgSig = MessagesSignature();
	LastContactsSig = ContactsSignature();
	LastPkgSig = PackagesSignature();
	LastBankSig = BankUnlockSignature();
	// (Goals heeft geen sig-baseline meer: de kaart-pool is net vers gevuld door BuildGoalsApp en
	//  updatet zichzelf in-place via RefreshGoalsApp zolang de app open is.)
	if (GetWorld()) { LastStatsRefresh = GetWorld()->GetTimeSeconds(); }

	bPrewarmDone = true;
}

void UPhoneWidget::RefreshContent()
{
	if (!ContentBox || !Phone.IsValid()) { return; }

	// --- Persistente per-app panelen (cache-and-toggle) ---
	// Home = -1, elke app = z'n tab-index. Elk paneel blijft in ContentBox hangen; een gewone app-wissel is
	// puur een zichtbaarheid-toggle (geen ClearChildren -> geen flash). Alleen een ECHTE data-wijziging
	// (bContentDirty, gezet door MarkDirty) herbouwt het panel van die ene app.
	// Tijdens pre-warm sturen bPrewarmHome/PrewarmApp welk paneel we bouwen (i.p.v. de live Phone-status).
	const bool bHome = bPrewarming ? bPrewarmHome : Phone->IsHomeScreen();
	const int32 App = bPrewarming ? PrewarmApp : FMath::Clamp(Phone->GetTab(), 0, GNumApps - 1);
	const int32 Key = bHome ? -1 : App;
	// Store-apps (Grow/Supplies/Sell/Hash) delen één set member-refs (StoreScroll/StoreTabBtns/AppCats/...).
	// Met meerdere gecachte store-panelen wijzen die naar het láátst-gebouwde paneel; herbouw dit paneel als
	// de refs nu bij een ANDER store-paneel horen, anders vult category-switch/FillStoreList het verkeerde
	// (verborgen) paneel -> "grow alles op 1 page / supplies 1 categorie".
	const bool bStoreApp = !bHome && (App == GGrowApp || App == GSuppliesApp || App == GSellApp || App == GHashApp);
	const bool bStoreRefsStale = bStoreApp && (StoreMembersOwnerApp != App);
	bool bNew = false;
	TObjectPtr<UVerticalBox>* Found = AppPanels.Find(Key);
	UVerticalBox* Panel = nullptr;
	if (Found) { Panel = *Found; }
	else
	{
		Panel = WidgetTree->ConstructWidget<UVerticalBox>();
		// Paneel vult het hele scherm-vlak (Overlay-Fill), zodat de Fill-kinderen (scroll-lijsten in
		// chat/contacts/store/goals/bank) hun volledige hoogte houden.
		UOverlaySlot* PanelSlot = ContentBox->AddChildToOverlay(Panel);
		PanelSlot->SetHorizontalAlignment(HAlign_Fill);
		PanelSlot->SetVerticalAlignment(VAlign_Fill);
		AppPanels.Add(Key, Panel);
		bNew = true;
	}
	ActiveContent = Panel;
	// Alleen het actieve paneel tonen; de rest op HIDDEN (blijft gearrangeerd + gerealiseerd, alleen niet getekend/
	// klikbaar). Zo hoeft Slate het paneel bij een app-open niet opnieuw te layouten -> geen 1-frame-flikker.
	for (auto& KV : AppPanels)
	{
		if (KV.Value)
		{
			KV.Value->SetVisibility(KV.Key == Key ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
		}
	}
	// Winkel-app-switch naar een ANDER (al gecacht) winkel-paneel: refs HERSTELLEN uit de per-app cache i.p.v.
	// het paneel herbouwen -> geen ClearChildren, geen flash. De inhoud staat al in het paneel; RefreshStore
	// ververst daarna alleen gewijzigde rijen/prijzen + de tab-highlight in-place.
	{
		auto IsStoreIdx = [](int32 A) { return A == GGrowApp || A == GSuppliesApp || A == GSellApp || A == GHashApp; };
		if (bStoreRefsStale && !bNew && !bContentDirty && StoreRefsByApp.Contains(App))
		{
			if (IsStoreIdx(StoreMembersOwnerApp) && StoreMembersOwnerApp != App) { SaveStoreRefs(StoreMembersOwnerApp); }
			RestoreStoreRefs(App);
			StoreMembersOwnerApp = App;
			RefreshStore();
			return;
		}
	}
	// PURE SWITCH: bestaand paneel + geen data-wijziging + winkel-refs kloppen -> niets herbouwen -> geen flash.
	if (!bNew && !bContentDirty && !bStoreRefsStale) { return; }

	// --- Rebuild-pad: alleen DIT paneel herbouwen ---
	Panel->ClearChildren();
	// ClearChildren() sloopte ALLEEN dit paneel z'n widgets -> geef alleen de member-refs vrij die BIJ DIT paneel
	// horen. Refs van ANDERE (nog gecachte) panelen blijven geldig; die hier nullen zou hun in-place updates
	// bevriezen (bv. home-badges na één app-bezoek, of de chat-picker/offer na weg-en-terug navigeren).
	if (Key == -1) // Home-rooster (badge-refs worden opnieuw gezet door MakeAppCell)
	{
		MsgAppBadgePill = nullptr; MsgAppBadgeText = nullptr;
		GoalsAppBadgePill = nullptr; GoalsAppBadgeText = nullptr;
	}
	if (Key == 3) // Messages/chat: hele persistente chat-shell (lijst-scroll + thread-root + sub-secties + bubbel-pool)
	{
		// Het paneel is net geleegd (ClearChildren) -> alle chat-widget-refs wijzen naar dode widgets. BuildChatApp
		// nult ze zelf ook aan het begin en bouwt ze opnieuw; hier expliciet nullen zodat een BuildChatApp-early-return
		// (bv. geen ContactsComponent) geen dangling refs in de live-updates achterlaat.
		PickerClockText = nullptr; PickerProposeBtn = nullptr; PickerContact = NAME_None;
		OfferBox = nullptr; OfferToggleBtn = nullptr;
		OfferGrid = nullptr; OfferHeadText = nullptr; OfferEmptyText = nullptr;
		ChatListScroll = nullptr; ChatThreadRoot = nullptr;
		ChatHeaderName = nullptr; ChatTierBox = nullptr; ChatTierLabel = nullptr; ChatTierBar = nullptr;
		ChatApptBox = nullptr; ChatBubbleScroll = nullptr; ChatNoMsgText = nullptr; ChatWaitBox = nullptr;
		ChatRespondRow = nullptr; ChatOfferSection = nullptr; ChatPickerPrompt = nullptr; ChatProposeBtn = nullptr;
		ChatBubblePool.Reset(); ChatBubbleSigs.Reset();
		ApptBar = nullptr; ApptBarLabel = nullptr; ApptBarContact = NAME_None;
		WaitBar = nullptr; WaitBarLabel = nullptr; WaitBarSentTime = -1.f;
		ListApptBars.Reset(); ListCards.Reset(); ListPreviews.Reset();
		ListBadges.Reset(); ListBadgeTexts.Reset(); ListStamps.Reset();
	}
	if (bStoreApp) // Grow/Supplies/Sell/Hash delen de winkel-pools; FillStoreList vult ze vers in de nieuwe StoreScroll
	{
		StoreRowPool.Reset(); StoreRowSigs.Reset();
		StoreFooterPool.Reset(); StoreFooterSigs.Reset();
		StoreQtyTexts.Reset();
	}
	if (Key == GPackagesApp) // Packages-kaarten (map + placeholder + delivered-box + scroll-eigenaar)
	{
		PkgCards.Reset(); PkgBars.Reset(); PkgEtas.Reset(); PkgEmptyRow = nullptr; PkgScrollOwner = nullptr;
		PkgDeliveredBox = nullptr; PkgDeliveredSig = -1;
	}
	if (Key == GBankApp) // Bank: de in-place saldo/cash-tekst-refs wijzen na ClearChildren naar dode widgets
	{
		BankBalanceText = nullptr; BankCashText = nullptr; // BuildBankApp zet ze in de ontgrendelde tak opnieuw
		BankLockedBox = nullptr; BankUnlockedBox = nullptr; // vaste bank-boxen (persistent, getoggled)
		BankSendBox = nullptr; BankSendLabel = nullptr;     // "Send to <vriend>"-sectie (co-op-only, getoggled)
		// Changed-check-caches resetten: de nieuwe widgets moeten de eerste tick vers gezet worden.
		LastBankUnlocked = -1; LastBankBalShown = -1; LastBankCashShown = -1;
		LastBankSendVis = -1; LastBankSendLabel.Reset();
	}

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	if (bHome)
	{
		AddInfoRow(TEXT("Apps"), WeedUI::ColText(), 16);
		UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
		Grid->SetSlotPadding(FMargin(10.f));
		int32 Cell = 0;
		for (int32 oi = 0; oi < GNumApps; ++oi)
		{
			const int32 i = GHomeOrder[oi]; // logische volgorde i.p.v. ruwe app-index
			if (i == GStatsApp && !(GS && GS->IsCompetitive())) { continue; } // Leaderboard alleen in competitive
			if (i == GHashApp) { continue; } // Lab-app weg: alle processing zit nu in Supplies -> Kitchen
			if (i == GMapApp) { continue; } // Map-app weg: de fullscreen-kaart zit op de M-toets
			UUniformGridSlot* GSlot = Grid->AddChildToUniformGrid(MakeAppCell(i, GAppName[i], GAppKey[i], GAppIcon[i], GAppCol[i]), Cell / 3, Cell % 3);
			GSlot->SetHorizontalAlignment(HAlign_Center);
			++Cell;
		}
		UVerticalBoxSlot* GridSlot = ActiveContent->AddChildToVerticalBox(Grid);
		GridSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		return;
	}

	// App-header: back-knop + titel. (Packages is nu een losse app, geen knop meer in de winkel.)
	bPackagesView = false;
	StorePackagesToggle = nullptr;
	StorePackagesLabel = nullptr;
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	// Linksboven: altijd een Back-knop (terug naar het home-scherm).
	Header->AddChildToHorizontalBox(MakeButton(TEXT("< Back"), 1, 0, WeedUI::ColAccentDim()));
	UTextBlock* TitleText = MakeText(GAppName[App], 15, WeedUI::ColText());
	TitleText->SetClipping(EWidgetClipping::ClipToBounds);
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(TitleText);
	TitleSlot->SetPadding(FMargin(10.f, 4.f, 6.f, 0.f));
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	UVerticalBoxSlot* HeaderSlot = ActiveContent->AddChildToVerticalBox(Header);
	HeaderSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	if (App == 0) // Upgrades
	{
		// --- Woning: 3 koopbare panden (het starter-flatje is al van jou) ---
		{
			ActiveContent->AddChildToVerticalBox(MakeText(TEXT("Home"), 14, WeedUI::ColText()))
				->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			TArray<FCityPropertyOffer> Offers;
			Phone->GetPropertyOffers(Offers);
			if (Offers.Num() == 0)
			{
				ActiveContent->AddChildToVerticalBox(MakeText(TEXT("(city still loading...)"), 11, WeedUI::ColTextDim()));
			}
			for (const FCityPropertyOffer& O : Offers)
			{
				const bool bOwned = Phone->IsPropertyOwned(O.HomeIndex);
				const bool bHereNow = bOwned && (Phone->GetHomePlayerIsInside() == O.HomeIndex);
				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
				Info->AddChildToVerticalBox(MakeText(O.Title, 12, bOwned ? WeedUI::ColGood() : WeedUI::ColText()));
				// Woning-info: type + huisnummer + verdieping.
				const FString InfoLine = Phone->GetHomeInfoLine(O.HomeIndex);
				if (!InfoLine.IsEmpty())
				{
					Info->AddChildToVerticalBox(MakeText(InfoLine, 10, WeedUI::ColTextDim()));
				}
				const FString PriceStr = (O.PriceCents > 0)
					? FString::Printf(TEXT("%s   EUR %lld"), *O.Sub, (long long)(WeedRoundEuros(O.PriceCents) / 100))
					: (bOwned ? FString::Printf(TEXT("%s   (in bezit)"), *O.Sub) : FString::Printf(TEXT("%s   (starter)"), *O.Sub));
				Info->AddChildToVerticalBox(MakeText(PriceStr, 10, WeedUI::ColTextDim()));
				UHorizontalBoxSlot* IL = Row->AddChildToHorizontalBox(Info);
				IL->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				IL->SetVerticalAlignment(VAlign_Center);
				IL->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

				USizeBox* RB = WidgetTree->ConstructWidget<USizeBox>();
				RB->SetWidthOverride(124.f);
				if (!bOwned)
				{
					RB->SetHeightOverride(28.f);
					RB->SetContent(MakeButton(TEXT("Buy"), 7, O.HomeIndex, WeedUI::ColAccentDim()));
				}
				else
				{
					// Eigen woning: teleport-knop (of "you are here") + verkoop-knop (~65%).
					const int32 SellVal = Phone->GetHomeSellValueCents(O.HomeIndex);
					UVerticalBox* OwnBox = WidgetTree->ConstructWidget<UVerticalBox>();
					if (bHereNow) { OwnBox->AddChildToVerticalBox(MakeText(TEXT("you are here"), 11, WeedUI::ColGood(), true)); }
					else          { OwnBox->AddChildToVerticalBox(MakeButton(TEXT("Ga hierheen"), 8, O.HomeIndex, WeedUI::ColAccentDim())); }
					if (SellVal > 0)
					{
						OwnBox->AddChildToVerticalBox(MakeButton(*FString::Printf(TEXT("Sell EUR %d"), (int32)(WeedRoundEuros((int64)SellVal) / 100)), 9, O.HomeIndex, WeedUI::ColAccentDim()))
							->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
					}
					RB->SetHeightOverride(SellVal > 0 ? 58.f : 28.f);
					RB->SetContent(OwnBox);
				}
				UHorizontalBoxSlot* RS = Row->AddChildToHorizontalBox(RB);
				RS->SetVerticalAlignment(VAlign_Center);
				ActiveContent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f));
			}
		}

		// --- Backpack: draagcapaciteit-upgrade (per speler, puur geld). Tier bepaalt slots + draaggewicht. ---
		{
			ActiveContent->AddChildToVerticalBox(MakeText(TEXT("Backpack"), 14, WeedUI::ColText()))
				->SetPadding(FMargin(0.f, 10.f, 0.f, 4.f));

			// Lees de tier van de LOKALE speler-pawn (de backpack-component zit op de pawn = per speler).
			int32 Tier = 0;
			bool bHaveInv = false;
			if (APawn* LP = GetOwningPlayerPawn())
			{
				if (const UInventoryComponent* LInv = LP->FindComponentByClass<UInventoryComponent>())
				{
					Tier = LInv->GetBackpackTier();
					bHaveInv = true;
				}
			}

			// ALLE tiers als eigen regel (slots/kg + prijs) zodat je ziet wat elke level geeft; je huidige level is
			// gemarkeerd en de eerstvolgende tier krijgt de koop-knop. Herbouwt alleen bij tier-wijziging (signature).
			for (int32 T = 0; T <= UInventoryComponent::BackpackMaxTier; ++T)
			{
				int32 MaxStacks = 0; float MaxWeight = 0.f;
				UInventoryComponent::GetBackpackTierCaps(T, MaxStacks, MaxWeight);
				const bool bOwned = (T <= Tier);
				const bool bNext = (T == Tier + 1);

				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
				const FLinearColor LvCol = (T == Tier) ? WeedUI::ColGood() : (bOwned ? WeedUI::ColTextDim() : WeedUI::ColText());
				const FString LvLabel = (T == Tier) ? FString::Printf(TEXT("Lv %d  (current)"), T + 1) : FString::Printf(TEXT("Lv %d"), T + 1);
				Info->AddChildToVerticalBox(MakeText(LvLabel, 12, LvCol));
				Info->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%d slots / %.0f kg"), MaxStacks, MaxWeight), 10, WeedUI::ColTextDim()));
				UHorizontalBoxSlot* IL = Row->AddChildToHorizontalBox(Info);
				IL->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				IL->SetVerticalAlignment(VAlign_Center);
				IL->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

				if (bNext && bHaveInv)
				{
					// Eerstvolgende tier: koop-knop met prijs (server-authoritative: eigen cash, tier++).
					const int64 Cost = UInventoryComponent::BackpackUpgradeCostCents(Tier);
					const FString BtnLabel = FString::Printf(TEXT("Upgrade  -  EUR %lld"), (long long)(Cost / 100));
					USizeBox* RB = WidgetTree->ConstructWidget<USizeBox>();
					RB->SetWidthOverride(150.f); RB->SetHeightOverride(28.f);
					RB->SetContent(MakeActionBtn(BtnLabel, WeedUI::ColAccentDim(), [this]()
					{
						if (APawn* P = GetOwningPlayerPawn())
						{
							if (UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>()) { Inv->ServerBuyBackpackUpgrade(); }
						}
					}, 11));
					UHorizontalBoxSlot* RS = Row->AddChildToHorizontalBox(RB);
					RS->SetVerticalAlignment(VAlign_Center);
				}
				else if (!bOwned)
				{
					// Latere (nog niet koopbare) tier: prijs als preview.
					const int64 Cost = UInventoryComponent::BackpackUpgradeCostCents(T - 1);
					Row->AddChildToHorizontalBox(MakeText(FString::Printf(TEXT("EUR %lld"), (long long)(Cost / 100)), 11, WeedUI::ColTextDim()))
						->SetVerticalAlignment(VAlign_Center);
				}
				else if (T < Tier)
				{
					Row->AddChildToHorizontalBox(MakeText(TEXT("owned"), 10, WeedUI::ColTextDim()))
						->SetVerticalAlignment(VAlign_Center);
				}

				ActiveContent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f));
			}
		}

		// --- Horloge (ND7.16): gedeelde upgrade (UpgradeComponent op de GameState). Zonder horloge
		// geen klok linksboven in de HUD; de telefoon-statusbalk toont de tijd altijd. Koop loopt via
		// de bestaande upgrade-route: catalogus-index -> DoAction (Tab 0) -> ServerBuyUpgrade. ---
		if (GS && GS->GetUpgrades())
		{
			FText WName; int32 WCost = 0; bool bWOwned = false, bWAvail = false;
			if (GS->GetUpgrades()->GetUpgradeDisplay(UUpgradeComponent::WatchUpgradeId, WName, WCost, bWOwned, bWAvail))
			{
				ActiveContent->AddChildToVerticalBox(MakeText(TEXT("Watch"), 14, WeedUI::ColText()))
					->SetPadding(FMargin(0.f, 10.f, 0.f, 4.f));

				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
				Info->AddChildToVerticalBox(MakeText(WName.ToString(), 12, bWOwned ? WeedUI::ColGood() : WeedUI::ColText()));
				Info->AddChildToVerticalBox(MakeText(TEXT("Shows the clock top-left in your HUD"), 10, WeedUI::ColTextDim()));
				UHorizontalBoxSlot* IL = Row->AddChildToHorizontalBox(Info);
				IL->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				IL->SetVerticalAlignment(VAlign_Center);
				IL->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

				if (bWOwned)
				{
					Row->AddChildToHorizontalBox(MakeText(TEXT("owned"), 10, WeedUI::ColTextDim()))
						->SetVerticalAlignment(VAlign_Center);
				}
				else
				{
					// Koper betaalt met EIGEN bankgeld (BuyUpgrade; co-op: toast landt op de koper-client).
					const int32 WatchIdx = GS->GetUpgrades()->GetAllUpgradeIds().IndexOfByKey(UUpgradeComponent::WatchUpgradeId);
					const FString BtnLabel = FString::Printf(TEXT("Buy  -  EUR %d"), WCost / 100);
					USizeBox* RB = WidgetTree->ConstructWidget<USizeBox>();
					RB->SetWidthOverride(150.f); RB->SetHeightOverride(28.f);
					RB->SetContent(MakeActionBtn(BtnLabel, WeedUI::ColAccentDim(), [this, WatchIdx]()
					{
						if (Phone.IsValid() && WatchIdx >= 0) { Phone->DoAction(WatchIdx); }
					}, 11));
					UHorizontalBoxSlot* RS = Row->AddChildToHorizontalBox(RB);
					RS->SetVerticalAlignment(VAlign_Center);
				}
				ActiveContent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f));
			}
		}
	}
	else if (App == GGrowApp) // Grow shop -> ALLES om te kweken (zaad, pot, aarde, water, upgrades, verzorging)
	{
		bSellApp = false;
		AppCats = { 0, 1, 5, 2, 8, 9 }; // Seeds, Pots, Soil, Drying, Grow Upg., Care
		if (!AppCats.Contains(Phone->GetSupplierCat())) { Phone->SetSupplierCat(AppCats[0]); }
		BuildStoreApp(ActiveContent);
	}
	else if (App == GSuppliesApp) // Supplies -> verwerken/verkopen/inrichten (papers, drogen, verpakken, meubels, keuken)
	{
		bSellApp = false;
		AppCats = { 4, 6, 3, 7, 11, 12 }; // Papers, Water, Packing, Furniture, Kitchen (machines), Ingredients (boter etc.)
		if (!AppCats.Contains(Phone->GetSupplierCat())) { Phone->SetSupplierCat(AppCats[0]); }
		BuildStoreApp(ActiveContent);
	}
	else if (App == GHashApp) // Lab -> de hasj-keten: machines (mesh/press) + machine-upgrades
	{
		bSellApp = false;
		AppCats = { 10 }; // Hash (Mesh/Press + ProcUp-upgrades)
		if (!AppCats.Contains(Phone->GetSupplierCat())) { Phone->SetSupplierCat(AppCats[0]); }
		BuildStoreApp(ActiveContent);
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
			AddInfoRow(TEXT("No contacts yet."), WeedUI::ColTextDim(), 13);
			AddInfoRow(TEXT("Deal with customers to get"), WeedUI::ColTextDim());
			AddInfoRow(TEXT("their number."), WeedUI::ColTextDim());
		}
		else
		{
			UScrollBox* List = WidgetTree->ConstructWidget<UScrollBox>();
			UVerticalBoxSlot* LS = ActiveContent->AddChildToVerticalBox(List);
			LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			// COMPETITIVE: toon alleen EIGEN contacten (leeg OwnerPlayerId = gedeeld/co-op, dan altijd tonen).
			// Zelfde eigenaarschaps-filter als IsMsgForLocal voor de Messages-app; in normale co-op verandert
			// er niks (alle OwnerPlayerId's leeg). Voorkomt dat de relatie%-waarden van de TEGENSTANDER lekken.
			const bool bCompContacts = GS && GS->IsCompetitive();
			const APawn* LocalPawn = GetOwningPlayerPawn();
			const FString LocalPid = LocalPawn ? USaveGameSubsystem::StablePlayerId(LocalPawn) : FString();
			for (const FPhoneContact& C : Con->GetContacts())
			{
				if (bCompContacts && !C.OwnerPlayerId.IsEmpty() && C.OwnerPlayerId != LocalPid) { continue; }
				const FName Cid = C.ContactId;
				UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
				Card->SetBrush(RoundedBrush(WeedUI::ColInner(0.95f), 8.f));
				Card->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				Card->SetContent(Row);
				UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
				Info->AddChildToVerticalBox(MakeText(C.DisplayName.ToString(), 14, WeedUI::ColText()));
				Info->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("Relationship %.0f%%"), C.Relationship), 10, WeedUI::ColTextDim()));
				UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(Info);
				IS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); IS->SetVerticalAlignment(VAlign_Center);
				Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Message"), WeedUI::ColAccentDim(),
					// Zet het te openen contact + wissel naar de Messages-app. De app-open zelf gaat via de bestaande
					// persistente-paneel-toggle; RefreshChatViews (aangeroepen bij het togglen naar app 3 in NativeTick)
					// zorgt dat de juiste thread meteen klopt. GEEN MarkDirty (dat zou het chat-paneel herbouwen -> flash).
					[this, Cid]() { OpenChatContact = Cid; bOfferStrainView = false; ProposeMins = -1; if (Phone.IsValid()) { Phone->OpenApp(3); } }, 11))->SetVerticalAlignment(VAlign_Center);
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
		BuildStoreApp(ActiveContent);
	}
	else if (App == GGoalsApp) // Goals -> milestone-doelen met rewards
	{
		BuildGoalsApp();
	}
	else if (App == GStatsApp) // Leaderboard (competitive stats)
	{
		BuildStatsApp();
	}
	else if (App == GMapApp) // Map-app weg (dode tak, zoals Lab): uit home-rooster + prewarm; M-toets = fullscreen-kaart
	{
		BuildMapApp();
	}

	// Onthoud welk store-paneel de gedeelde winkel-member-refs nu beschrijven (voor de stale-check bovenaan).
	if (bStoreApp) { StoreMembersOwnerApp = App; SaveStoreRefs(App); } // vers gebouwd -> cache seeden voor latere flash-vrije restores
}

void UPhoneWidget::BuildStatsApp()
{
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (!GS || !GS->IsCompetitive()) { AddInfoRow(TEXT("Leaderboard is only for Competitive mode."), WeedUI::ColTextDim(), 13); return; }

	AddInfoRow(TEXT("LEADERBOARD"), WeedUI::ColText(), 17);
	const TArray<FCompetitorScore>& St = GS->GetStandings();
	if (St.Num() == 0) { AddInfoRow(TEXT("No players yet."), WeedUI::ColTextDim(), 13); return; }

	const APawn* Me = GetOwningPlayerPawn();
	const FString MyName = (Me && Me->GetPlayerState()) ? Me->GetPlayerState()->GetPlayerName() : FString();

	int32 Rank = 1;
	for (const FCompetitorScore& C : St)
	{
		const bool bMe = (!MyName.IsEmpty() && C.Name == MyName);
		const FLinearColor Col = (Rank == 1) ? WeedUI::ColAccentDim() : WeedUI::ColInner();
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrush(WeedUI::Rounded(FLinearColor(Col.R, Col.G, Col.B, bMe ? 0.95f : 0.55f), 10.f));
		Card->SetPadding(FMargin(12.f, 9.f));
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(VB);

		const TCHAR* Medal = (Rank == 1) ? TEXT("1st") : (Rank == 2) ? TEXT("2nd") : (Rank == 3) ? TEXT("3rd") : TEXT("");
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("#%d  %s%s"), Rank, *C.Name, bMe ? TEXT("  (you)") : TEXT("")), 15,
			WeedUI::ColText(), true, true));
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("Net worth:  EUR %lld    %s"), (long long)(C.NetWorthCents / 100), Medal), 12,
			WeedUI::ColTextDim(), false));
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("Cash EUR %lld   Bank EUR %lld"), (long long)(C.CashCents / 100), (long long)(C.BankCents / 100)), 11,
			WeedUI::ColTextDim(), false));
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("Earned EUR %lld   Customers %d"), (long long)(C.EarnedCents / 100), C.Customers), 11,
			WeedUI::ColTextDim(), false));

		UVerticalBoxSlot* CS = ActiveContent->AddChildToVerticalBox(Card);
		CS->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
		++Rank;
	}
}

int32 UPhoneWidget::GetClaimableGoals() const
{
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const UGoalsComponent* GoalsC = GS ? GS->GetGoals() : nullptr;
	return GoalsC ? GoalsC->GetClaimableCount() : 0;
}

void UPhoneWidget::BuildGoalsApp()
{
	if (!ActiveContent) { return; }

	// Dit paneel is zojuist geleegd (ClearChildren in RefreshContent) -> alle oude kaart-refs zijn dood.
	// Pool altijd resetten, ook op het "unavailable"-pad, zodat RefreshGoalsApp nooit aan dode widgets zit.
	GoalCards.Reset(); GoalTitles.Reset(); GoalProgTexts.Reset(); GoalBars.Reset();
	GoalRewardTexts.Reset(); GoalClaimBtns.Reset(); GoalStatusTexts.Reset();
	GoalCardGoalIdx.Reset(); GoalCardSigs.Reset(); GoalCardProgStrs.Reset();

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UGoalsComponent* GoalsC = GS ? GS->GetGoals() : nullptr;
	if (!GoalsC) { AddInfoRow(TEXT("Goals unavailable."), WeedUI::ColTextDim()); return; }

	UScrollBox* List = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* LS = ActiveContent->AddChildToVerticalBox(List);
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// PERSISTENTE kaart-pool: de goals-set is statisch per sessie, dus N lege kaart-shells één keer bouwen.
	// RefreshGoalsApp (hieronder + live in NativeTick) wijst per POSITIE een goal toe en vult/styled in-place;
	// dit paneel wordt daarna NOOIT meer herbouwd -> geen flash, scroll-positie blijft staan.
	const int32 N = UGoalsComponent::Goals().Num();
	for (int32 p = 0; p < N; ++p)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetPadding(FMargin(9.f, 7.f, 9.f, 7.f));
		UVerticalBox* CV = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(CV);

		UTextBlock* TitleT = MakeText(FString(), 13, WeedUI::ColText());
		CV->AddChildToVerticalBox(TitleT);
		UTextBlock* ProgT = MakeText(FString(), 10, WeedUI::ColTextDim());
		CV->AddChildToVerticalBox(ProgT);

		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
		USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>();
		BarSz->SetHeightOverride(12.f); BarSz->SetContent(Bar);
		CV->AddChildToVerticalBox(BarSz)->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* RewardT = MakeText(FString(), 10, WeedUI::ColTextDim());
		UHorizontalBoxSlot* RWS = Row->AddChildToHorizontalBox(RewardT);
		RWS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); RWS->SetVerticalAlignment(VAlign_Center);

		// Claim-knop + status-label ("Claimed"/"In progress") samen in een Overlay; togglen = Hidden (geen
		// Collapsed -> geen her-layout-flits, de rij houdt een vaste breedte). De klik leest de goal-index van
		// deze POSITIE pas op het klik-moment (de sortering kan de toewijzing verschuiven) -> altijd de juiste
		// goal zonder de delegate te herbinden.
		const int32 Pos = p;
		UOverlay* ActionOv = WidgetTree->ConstructWidget<UOverlay>();
		UWeedActionButton* ClaimB = MakeActionBtn(TEXT("Claim"), WeedUI::ColGood(0.55f),
			[this, Pos]()
			{
				if (!Phone.IsValid() || !GoalCardGoalIdx.IsValidIndex(Pos) || GoalCardGoalIdx[Pos] < 0) { return; }
				Phone->ClaimGoal(GoalCardGoalIdx[Pos]);
				// Direct hersorteren: op de host is de claim meteen verwerkt (kaart zakt gedimd naar onderen);
				// op een joiner pakt de eerstvolgende live-refresh de gerepliceerde claim-staat op.
				RefreshGoalsApp();
			}, 12);
		UOverlaySlot* OS1 = ActionOv->AddChildToOverlay(ClaimB);
		OS1->SetHorizontalAlignment(HAlign_Fill); OS1->SetVerticalAlignment(VAlign_Center);
		UTextBlock* StatusT = MakeText(FString(), 10, WeedUI::ColTextDim());
		UOverlaySlot* OS2 = ActionOv->AddChildToOverlay(StatusT);
		OS2->SetHorizontalAlignment(HAlign_Center); OS2->SetVerticalAlignment(VAlign_Center);
		Row->AddChildToHorizontalBox(ActionOv)->SetVerticalAlignment(VAlign_Center);
		CV->AddChildToVerticalBox(Row);

		if (UScrollBoxSlot* SS = Cast<UScrollBoxSlot>(List->AddChild(Card)))
		{
			SS->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
		}

		GoalCards.Add(Card); GoalTitles.Add(TitleT); GoalProgTexts.Add(ProgT); GoalBars.Add(Bar);
		GoalRewardTexts.Add(RewardT); GoalClaimBtns.Add(ClaimB); GoalStatusTexts.Add(StatusT);
		GoalCardGoalIdx.Add(-1); GoalCardSigs.Add(-1); GoalCardProgStrs.Add(FString());
	}

	// Meteen vers vullen (sigs staan op -1 -> elke positie krijgt z'n eerste toewijzing + stijl).
	RefreshGoalsApp();
}

// Sorteert de doelen (claimbaar -> in-progress bijna-klaar-eerst -> claimed) en werkt de persistente kaart-pool
// in-place bij. Restyle (brush/kleuren/knop-toggle/vaste teksten) alleen op posities waarvan de sig (goal-index +
// status) wijzigt; de voortgangstekst is changed-checked en de balk loopt altijd live mee. NOOIT ClearChildren.
void UPhoneWidget::RefreshGoalsApp()
{
	const AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const UGoalsComponent* GoalsC = GS ? GS->GetGoals() : nullptr;
	const TArray<FGoalDef>& G = UGoalsComponent::Goals();
	if (!GoalsC || GoalCards.Num() != G.Num()) { return; } // pool (nog) niet gebouwd of goals onbeschikbaar

	// --- Sortering: status (0=in-progress, 1=claimbaar, 2=claimed) + voortgangs-fractie per goal ---
	struct FGoalEntry { int32 Idx = 0; int32 Status = 0; float Frac = 0.f; };
	TArray<FGoalEntry> Order; Order.Reserve(G.Num());
	for (int32 i = 0; i < G.Num(); ++i)
	{
		const int64 Tgt = G[i].Target;
		FGoalEntry E;
		E.Idx = i;
		E.Status = GoalsC->IsClaimed(i) ? 2 : (GoalsC->IsComplete(i) ? 1 : 0);
		E.Frac = (Tgt > 0) ? FMath::Clamp((float)GoalsC->GetGoalProgress(i) / (float)Tgt, 0.f, 1.f) : 0.f;
		Order.Add(E);
	}
	// Claimbaar bovenaan, dan in-progress op voortgang aflopend (bijna-klaar eerst), claimed onderaan.
	// Index als tiebreaker -> stabiel binnen gelijke groepen (definitie-volgorde).
	Order.Sort([](const FGoalEntry& A, const FGoalEntry& B)
	{
		auto Rank = [](int32 S) { return S == 1 ? 0 : (S == 0 ? 1 : 2); };
		if (Rank(A.Status) != Rank(B.Status)) { return Rank(A.Status) < Rank(B.Status); }
		if (A.Status == 0 && A.Frac != B.Frac) { return A.Frac > B.Frac; }
		return A.Idx < B.Idx;
	});

	for (int32 p = 0; p < Order.Num(); ++p)
	{
		const FGoalEntry& E = Order[p];
		const FGoalDef& Gd = G[E.Idx];
		const bool bClaimed = (E.Status == 2);
		const bool bClaimable = (E.Status == 1);

		// --- Toewijzing/status van deze positie gewijzigd -> eenmalig hervullen + restylen ---
		const int32 Sig = E.Idx * 4 + E.Status;
		if (GoalCardSigs[p] != Sig)
		{
			GoalCardSigs[p] = Sig;
			GoalCardGoalIdx[p] = E.Idx;
			GoalCardProgStrs[p].Reset(); // andere goal/status -> voortgangstekst hieronder sowieso vers zetten

			// Kaart-stijl: claimbaar = subtiel gouden randje (zelfde goud als de balk), claimed = grijs + gedimd.
			FSlateBrush CardBr = RoundedBrush(bClaimed ? WeedUI::ColSlotEmpty(0.95f) : WeedUI::ColInner(0.95f), 8.f);
			if (bClaimable)
			{
				CardBr.OutlineSettings.Width = 1.5f;
				CardBr.OutlineSettings.Color = FSlateColor(FLinearColor(0.85f, 0.7f, 0.25f, 0.9f));
			}
			if (GoalCards[p])
			{
				GoalCards[p]->SetBrush(CardBr);
				// Gedimde claimed-kaart: RenderOpacity is hier veilig — alleen de claimbare brush heeft een
				// outline (de bekende spook-kader-valkuil speelt dus niet op de claimed-kaart).
				GoalCards[p]->SetRenderOpacity(bClaimed ? 0.55f : 1.f);
			}
			if (GoalTitles[p])
			{
				GoalTitles[p]->SetText(FText::FromString(Gd.Title));
				GoalTitles[p]->SetColorAndOpacity(FSlateColor(bClaimed ? WeedUI::ColGood() : WeedUI::ColText()));
			}
			if (GoalRewardTexts[p])
			{
				FString RewardStr = TEXT("Reward:  ");
				if (Gd.RewardMoneyCents > 0) { RewardStr += FString::Printf(TEXT("EUR %lld"), (long long)(Gd.RewardMoneyCents / 100)); }
				if (!Gd.RewardItem.IsNone() && Gd.RewardQty > 0)
				{
					RewardStr += FString::Printf(TEXT("%s%dx %s"), Gd.RewardMoneyCents > 0 ? TEXT("  +  ") : TEXT(""), Gd.RewardQty, *WeedUI::PrettyItemName(Gd.RewardItem));
				}
				GoalRewardTexts[p]->SetText(FText::FromString(RewardStr));
			}
			// Claim-knop vs status-label: Hidden-toggle in de Overlay (ruimte blijft staan, geen layout-sprong;
			// Hidden is ook niet klikbaar, dus een verborgen Claim-knop kan niet per ongeluk claimen).
			if (GoalClaimBtns[p])
			{
				GoalClaimBtns[p]->SetVisibility(bClaimable ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
			}
			if (GoalStatusTexts[p])
			{
				GoalStatusTexts[p]->SetVisibility(bClaimable ? ESlateVisibility::Hidden : ESlateVisibility::SelfHitTestInvisible);
				GoalStatusTexts[p]->SetText(FText::FromString(bClaimed ? TEXT("Claimed") : TEXT("In progress")));
				GoalStatusTexts[p]->SetColorAndOpacity(FSlateColor(bClaimed ? WeedUI::ColGood() : WeedUI::ColTextDim()));
			}
			// Balk-kleur per status: goud onderweg, groen zodra het doel af is (claimbaar of geclaimd).
			if (GoalBars[p])
			{
				GoalBars[p]->SetFillColorAndOpacity(E.Status == 0 ? FLinearColor(0.85f, 0.7f, 0.25f) : FLinearColor(0.4f, 0.95f, 0.5f));
			}
		}

		// --- Live waarden (elke refresh; de sig dekt voortgang bewust NIET, dus balken lopen nu WEL live) ---
		const int64 Prog = GoalsC->GetGoalProgress(E.Idx);
		const int64 Tgt = Gd.Target;
		const FString ProgStr = (Gd.Metric == 0)
			? FString::Printf(TEXT("EUR %lld / %lld"), (long long)(Prog / 100), (long long)(Tgt / 100))
			: FString::Printf(TEXT("%lld / %lld"), (long long)Prog, (long long)Tgt);
		if (GoalProgTexts[p] && ProgStr != GoalCardProgStrs[p])
		{
			GoalCardProgStrs[p] = ProgStr;
			GoalProgTexts[p]->SetText(FText::FromString(ProgStr));
		}
		if (GoalBars[p]) { GoalBars[p]->SetPercent(E.Frac); }
	}
}

void UPhoneWidget::BuildMapApp()
{
	// (DOOD, zoals de Lab-app) De Map-app is van de telefoon af: uit het home-rooster en de prewarm-loop,
	// dus deze bouw wordt nooit meer aangeroepen. De fullscreen-kaart op de M-toets staat hier los van.
	if (!ActiveContent) { return; }

	// Knop naar de fullscreen-kaart (zelfde als M).
	UWeedActionButton* FB = MakeActionBtn(TEXT("Fullscreen"), WeedUI::ColAccentDim(),
		[this]() { if (Phone.IsValid()) { Phone->ToggleMapOverlay(); } }, 13);
	ActiveContent->AddChildToVerticalBox(FB)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Ingebedde mini-kaart (toont winkels, huisnummers, jouw positie + de NPC's).
	if (UMapWidget* MW = CreateWidget<UMapWidget>(GetOwningPlayer(), UMapWidget::StaticClass()))
	{
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetHeightOverride(300.f);
		Box->SetContent(MW);
		ActiveContent->AddChildToVerticalBox(Box);
	}
}

void UPhoneWidget::HandlePhoneButton(int32 Action, int32 Param)
{
	if (!Phone.IsValid()) { return; }
	switch (Action)
	{
	case 0: Phone->OpenApp(Param); bCartView = false; if (Param == 3) { OpenChatContact = NAME_None; bOfferStrainView = false; ProposeMins = -1; } break; // Messages-open: het chat-paneel is persistent, dus geen MarkDirty; de app-switch-toggle in NativeTick roept RefreshChatViews aan (toont de lijst)
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

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	// Pre-warm de app-panelen GESPREID (1 per tick) terwijl de telefoon DICHT is (vlak na de load, achter het
	// boot-scherm): geen runtime-hitch, en tegen de tijd dat de speler de telefoon opent zijn alle panelen al
	// gebouwd -> pure toggle -> geen flash. Alleen stappen terwijl gesloten: een prewarm-stap togglet de
	// panel-visibility op de PREWARM-key, wat een geopend paneel zou verbergen (flash). Opent de speler mid-
	// prewarm (zeldzaam, eerste ~0.2s), dan bouwt de lazy pad het huidige scherm on-demand (één flash) en hervat
	// de spreiding zodra de telefoon weer dicht is. Wacht op de game-state (echte data).
	if (!bPrewarmDone && GS && !bOpen)
	{
		PrewarmStep();
		// Prewarm-versneller: zolang de cover/bootfase nog op beeld staat (crowd nog niet gespawnd) kan er
		// een TWEEDE stap per tick bij — de panelen zijn toch onzichtbaar, de speler ziet er niets van.
		if (!bPrewarmDone && !WeedShop_IsCrowdSpawned()) { PrewarmStep(); }
	}

	if (!bOpen) { return; }

	if (GS)
	{
		// Statusbalk: changed-checks -> alleen formatteren + SetText als de waarde echt wijzigde.
		if (CashText && GS->GetEconomy())
		{
			const double CashE = GS->GetEconomy()->GetBalanceEuros();
			const double BankE = GS->GetEconomy()->GetBankEuros();
			if (CashE != LastPhoneCash || BankE != LastPhoneBank)
			{
				LastPhoneCash = CashE; LastPhoneBank = BankE;
				CashText->SetText(FText::FromString(FString::Printf(TEXT("C %s  B %s"),
					*CompactEuros(CashE), *CompactEuros(BankE))));
			}
		}
		if (TimeText && GS->GetDayCycle())
		{
			const float Hour = GS->GetDayCycle()->GetClockHour();
			const int32 H = FMath::Clamp((int32)Hour, 0, 23);
			const int32 M = FMath::Clamp((int32)((Hour - H) * 60.f), 0, 59);
			const int32 TimeKey = GS->GetDayCycle()->GetDayNumber() * 10000 + H * 60 + M;
			if (TimeKey != LastPhoneTimeKey)
			{
				LastPhoneTimeKey = TimeKey;
				TimeText->SetText(FText::FromString(FString::Printf(TEXT("Day %d  %02d:%02d"),
					GS->GetDayCycle()->GetDayNumber(), H, M)));
			}
		}
		if (LevelText && GS->GetLeveling())
		{
			const ULevelComponent* Lv = GS->GetLeveling();
			// Co-op: header toont het level/XP van de LOKALE speler (eigenaar van deze widget), niet van de host.
			APawn* OwnerPawn = GetOwningPlayerPawn();
			const int32 Level = Lv->GetLevelFor(OwnerPawn);
			if (Level != LastPhoneLevel)
			{
				LastPhoneLevel = Level;
				LevelText->SetText(FText::FromString(FString::Printf(TEXT("Lv %d"), Level)));
			}
			const int32 Xp = Lv->GetCurrentXPFor(OwnerPawn);
			const int32 ToNext = Lv->GetXPToNextFor(OwnerPawn);
			if (Xp != LastPhoneXp || ToNext != LastPhoneXpNext)
			{
				LastPhoneXp = Xp; LastPhoneXpNext = ToNext;
				if (LevelXpBar) { LevelXpBar->SetPercent(Lv->GetLevelFractionFor(OwnerPawn)); }
				if (LevelXpText)
				{
					LevelXpText->SetText(ToNext <= 0
						? FText::FromString(TEXT("MAX"))
						: FText::FromString(FString::Printf(TEXT("%d / %d XP"), Xp, ToNext)));
				}
			}
		}
	}

	const bool bHome = Phone->IsHomeScreen();
	const int32 App = Phone->GetTab();

	// Leaderboard live houden: forceer ~elke 2s een rebuild zolang de stats-app open is (standen verversen).
	if (!bHome && App == GStatsApp && GetWorld())
	{
		const float NowT = GetWorld()->GetTimeSeconds();
		if (NowT - LastStatsRefresh >= 2.f) { LastStatsRefresh = NowT; bContentDirty = true; }
	}
	// (Geen per-bericht home-herbouw meer: dat gaf geflits. De Messages-badge ververst bij openen/navigeren;
	//  de live notificatie zie je sowieso op het hotbar-telefoon-icoon.)
	const bool bSwitchedToChat = (App == 3) && !bHome && (bHome != bLastHome || App != bLastApp); // net naar Messages
	if (bContentDirty || bHome != bLastHome || App != bLastApp)
	{
		bLastHome = bHome; bLastApp = App;
		RefreshContent();      // moet bContentDirty ZIEN om het paneel te herbouwen
		bContentDirty = false; // pas NA de (eventuele) rebuild wissen
		// Chat-paneel is PERSISTENT: bij een pure toggle naar Messages (geen rebuild) synct RefreshChatViews de
		// juiste lijst/thread naar de actuele OpenChatContact (bv. na Message in Contacts of gewoon de app openen).
		if (bSwitchedToChat) { RefreshChatViews(); }
	}

	// Messages-app badge op het home-rooster LIVE bijwerken (zonder rebuild -> geen flash).
	if (bHome && MsgAppBadgePill && MsgAppBadgeText)
	{
		const int32 Unread = Phone->GetUnreadMessageCount();
		MsgAppBadgePill->SetVisibility(Unread > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (Unread > 0)
		{
			MsgAppBadgeText->SetText(FText::FromString(Unread > 99 ? FString(TEXT("99+")) : FString::Printf(TEXT("%d"), Unread)));
		}
	}
	// Goals-app badge op het home-rooster LIVE bijwerken (aantal claimbare doelen) - geen rebuild/flash.
	if (bHome && GoalsAppBadgePill && GoalsAppBadgeText)
	{
		const int32 Claim = GetClaimableGoals();
		GoalsAppBadgePill->SetVisibility(Claim > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (Claim > 0) { GoalsAppBadgeText->SetText(FText::FromString(Claim > 99 ? FString(TEXT("99+")) : FString::Printf(TEXT("%d"), Claim))); }
	}

	// Settings/Test: light-sliders live toepassen terwijl je sleept (geen restart nodig).
	// Sinds de tab-splitsing: time-speed zit in Test (cat 1), licht-sliders in Light (cat 3).
	if (!bHome && App == 4 && ((SettingsCat == 1 && TimeSpeedSlider) || (SettingsCat == 3 && LMoon)))
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

	// Contacts open: EIGEN structurele sig = alleen de contact-SET. Zo herbouwt Contacts NIET bij een nieuw
	// bericht/afspraak-fase van een BESTAAND contact (dat gaf flits via de gedeelde MessagesSignature); alleen een
	// NIEUW/weg contact (zeldzaam) herbouwt de lijst één keer. (De relationship-drift laten we met rust — geen flash.)
	if (!bHome && App == 2)
	{
		const int32 CSig = ContactsSignature();
		if (CSig != LastContactsSig) { LastContactsSig = CSig; MarkDirty(); }
	}

	// Upgrades open (app 0): bij een woning-koop/verkoop of een backpack-upgrade herbouwt het paneel één keer
	// (MarkDirty) zodat de knoppen/tekst kloppen zonder de app opnieuw te openen. Structurele sig -> geen live-drift.
	if (!bHome && App == 0)
	{
		const int32 USig = UpgradesSignature();
		if (USig != LastUpgradesSig) { LastUpgradesSig = USig; MarkDirty(); }
	}

	// Berichten open: bij een nieuw bericht of statuswijziging de inhoud verversen (App 2/Contacts zit hier NIET
	// meer in -> die heeft z'n eigen contact-SET-sig hierboven).
	if (!bHome && App == 3)
	{
		const int32 Sig = MessagesSignature();
		if (Sig != LastMsgSig)
		{
			LastMsgSig = Sig;
			// App 3 (Messages) is PERSISTENT: een structurele wijziging (nieuw/weg contact of thread-bericht-verschil)
			// werkt IN-PLACE bij via RefreshChatViews -> GEEN paneel-herbouw, geen flash.
			RefreshChatViews();
		}
		else { UpdateApptBarLive(); UpdateWaitBarLive(); UpdateListBarsLive(); } // lijst/threads live bijwerken (zonder herbouw)
		// Tijd-kiezer-ondergrens LIVE laten meelopen zonder rebuild (geen flash): klem ProposeMins op
		// 30 min..23,5u vooruit met de huidige klok en werk alleen het klok-label bij.
		if (App == 3 && PickerClockText && !PickerContact.IsNone() && GS && GS->GetDayCycle())
		{
			const int32 NowMins = (static_cast<int32>(GS->GetDayCycle()->GetClockHour() * 60.f)) % 1440;
			int32 Gap = ((ProposeMins - NowMins) % 1440 + 1440) % 1440;
			const int32 Clamped = FMath::Clamp(Gap, 30, 1410);
			if (Clamped != Gap)
			{
				ProposeMins = (NowMins + Clamped) % 1440;
				PickerClockText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), ProposeMins / 60, ProposeMins % 60)));
			}
		}
	}

	// Bank-app open: het paneel is VOLLEDIG PERSISTENT (upgrade-prompt + bank-kaart allebei gebouwd). Toggle welke
	// box zichtbaar is op de live unlock/ATM-staat (kopen van de upgrade = box-wissel, GEEN rebuild/flash), en werk
	// saldo + cash-to-deposit IN-PLACE bij. De tax/fee-labels + presets zijn constant -> niet bijwerken.
	if (!bHome && App == GBankApp && GS && GS->GetEconomy())
	{
		const bool bBankUnlocked = Phone->IsBankAppUnlocked() || Phone->IsBankViaAtm();
		// Box-toggle alleen bij een unlock-wissel (changed-check; cache reset bij panel-rebuild).
		if (LastBankUnlocked != (bBankUnlocked ? 1 : 0))
		{
			LastBankUnlocked = bBankUnlocked ? 1 : 0;
			if (BankLockedBox)   { BankLockedBox->SetVisibility(bBankUnlocked ? ESlateVisibility::Hidden : ESlateVisibility::SelfHitTestInvisible); }
			if (BankUnlockedBox) { BankUnlockedBox->SetVisibility(bBankUnlocked ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden); }
		}
		if (bBankUnlocked)
		{
			const UEconomyComponent* E = GS->GetEconomy();
			const long long BalShown = (long long)(WeedRoundEuros(E->GetBankCents()) / 100);
			if (BankBalanceText && BalShown != LastBankBalShown)
			{
				LastBankBalShown = BalShown;
				BankBalanceText->SetText(FText::FromString(FString::Printf(TEXT("EUR %lld"), BalShown)));
			}
			const long long CashShown = (long long)(WeedRoundEuros(E->GetCashCents()) / 100);
			if (BankCashText && CashShown != LastBankCashShown)
			{
				LastBankCashShown = CashShown;
				BankCashText->SetText(FText::FromString(FString::Printf(TEXT("Cash to deposit:  EUR %lld"), CashShown)));
			}
			// "Send to <vriend>": alleen tonen als er echt een 2e speler in de sessie zit (co-op), mét diens naam
			// in het label. In-place toggle + tekst-update (geen rebuild); join/leave is zeldzaam dus geen flash.
			if (BankSendBox)
			{
				const APlayerController* PC = GetOwningPlayer();
				APlayerState* FriendPS = nullptr;
				for (APlayerState* PS : GS->PlayerArray)
				{
					// De eerste ANDERE speler (bij >2 spelers pakken we die als weergegeven ontvanger).
					if (PS && PC && PS != PC->PlayerState) { FriendPS = PS; break; }
				}
				const int32 SendVis = FriendPS ? 1 : 0;
				if (SendVis != LastBankSendVis)
				{
					LastBankSendVis = SendVis;
					BankSendBox->SetVisibility(FriendPS ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
				}
				if (FriendPS && BankSendLabel)
				{
					FString Lbl = FString::Printf(TEXT("Send to %s   (%.0f%% fee)"),
						*FriendPS->GetPlayerName(), E->TransferFeePct * 100.f);
					if (Lbl != LastBankSendLabel)
					{
						LastBankSendLabel = MoveTemp(Lbl);
						BankSendLabel->SetText(FText::FromString(LastBankSendLabel));
					}
				}
			}
		}
	}

	// Goals-app open: de persistente kaart-pool live bijwerken (~4x/s) — teksten/balken in-place, hersorteren
	// zodra een status wijzigt (claimbaar naar boven, claimed gedimd naar onderen). Geen MarkDirty/paneel-herbouw
	// meer voor deze app -> geen flash, scroll-positie blijft staan. App-openen is altijd vers: zolang de app
	// dicht was liep deze throttle niet, dus de eerste tick met de app open refresht meteen.
	if (!bHome && App == GGoalsApp && GS && GS->GetGoals() && GetWorld())
	{
		if (GoalCards.Num() != UGoalsComponent::Goals().Num())
		{
			// Zelfherstel: het paneel is ooit zonder GoalsComponent gebouwd ("Goals unavailable.") — herbouw
			// eenmalig nu de component er wel is; daarna doet de pool alles in-place.
			MarkDirty();
		}
		else
		{
			const float NowG = GetWorld()->GetTimeSeconds();
			if (NowG >= NextGoalsLiveRefresh)
			{
				NextGoalsLiveRefresh = NowG + 0.25f;
				RefreshGoalsApp();
			}
		}
	}
}
