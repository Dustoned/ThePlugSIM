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
#include "World/CityGenerator.h"
#include "UI/MapWidget.h"
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
	UWidget* MakeRichBody(UWidgetTree* Tree, const FString& Body, int32 Size, const FLinearColor& Col, const FString& BoldExtra = FString())
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
			FString Run; bool bRunBold = false; int32 RunLen = 0;
			auto Flush = [&]()
			{
				if (Run.IsEmpty()) { return; }
				UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
				T->SetText(FText::FromString(Run));
				T->SetFont(FCoreStyle::GetDefaultFontStyle(bRunBold ? "Bold" : "Regular", Size));
				T->SetColorAndOpacity(FSlateColor(bRunBold ? FLinearColor(1.f, 1.f, 1.f) : Col));
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
				if (!Run.IsEmpty() && (bBold != bRunBold || RunLen + W.Len() + 1 > 16)) { Flush(); }
				if (Run.IsEmpty()) { bRunBold = bBold; }
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
	if (!ContentBox) { return; }

	// Categorie-knoppen. De 'Test'-tab (dag/nacht-switch e.d.) alleen in dev-modes (Sandbox/Testing), niet in een normaal spel.
	SettingsTabBtns.Reset();
	// Dev-tabs (Test/Rooms/Light/Spots) zijn verhuisd naar het losse DEV-MENU (F10). De telefoon toont alleen Status.
	SettingsCat = 0;
	static const TCHAR* CatNames[1] = { TEXT("Status") };
	const int32 NumTabs = 1;
	UHorizontalBox* Cats = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 i = 0; i < NumTabs; ++i)
	{
		const FLinearColor Col = (i == SettingsCat) ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
		UWeedActionButton* B = MakeActionBtn(CatNames[i], Col,
			[this, i]() { SettingsCat = i; bRebinding = false; RebindMsg.Reset(); RefreshSettingsTabs(); FillSettingsBody(); }, 10);
		UHorizontalBoxSlot* CS = Cats->AddChildToHorizontalBox(B);
		CS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS->SetPadding(FMargin(1.f, 0.f, 1.f, 0.f));
		SettingsTabBtns.Add(B);
	}
	ContentBox->AddChildToVerticalBox(Cats)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	// Body in een ScrollBox zodat lange lijsten netjes binnen de telefoon blijven (scrollen ipv overlopen).
	UScrollBox* BodyScroll = WidgetTree->ConstructWidget<UScrollBox>();
	SettingsBody = WidgetTree->ConstructWidget<UVerticalBox>();
	BodyScroll->AddChild(SettingsBody);
	UVerticalBoxSlot* ScrollSlot = ContentBox->AddChildToVerticalBox(BodyScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
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
	// Slider-pointers resetten: hun widgets zijn zojuist vernietigd; ApplyLightSliders mag er niet meer aan zitten.
	TimeSpeedSlider = nullptr; TimeSpeedV = nullptr;
	LMoon = LSun = LSkyN = LSkyD = LPitch = LLamp = LExp = nullptr;
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	auto BodyRow = [this](UWidget* W, const FMargin& Pad) { SettingsBody->AddChildToVerticalBox(W)->SetPadding(Pad); };

	if (SettingsCat == 1) // Test-tools, nu in nette secties
	{
		// --- Compacte helpers voor deze tab ---
		const FLinearColor CSave(0.26f, 0.42f, 0.32f), CClr(0.42f, 0.22f, 0.22f), CAim(0.28f, 0.34f, 0.48f), CKit(0.24f, 0.4f, 0.45f);
		auto Section = [&](const TCHAR* T) { BodyRow(MakeText(T, 11, FLinearColor(0.45f, 0.72f, 1.f)), FMargin(0.f, 8.f, 0.f, 2.f)); };
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
				GS->GetDayCycle()->IsNight() ? TEXT("night") : TEXT("day")), 11, FLinearColor(0.65f, 0.7f, 0.8f)), FMargin(0.f, 0.f, 0.f, 3.f));
		}
		Pair(TEXT("Set Day"), FLinearColor(0.85f, 0.7f, 0.2f), [this]() { if (Phone.IsValid()) { Phone->RequestSetDayNight(false); } },
			 TEXT("Set Night"), FLinearColor(0.25f, 0.3f, 0.55f), [this]() { if (Phone.IsValid()) { Phone->RequestSetDayNight(true); } });
		TimeSpeedSlider = nullptr; TimeSpeedV = nullptr;
		{
			const float CurDilation = GetWorld() ? UGameplayStatics::GetGlobalTimeDilation(GetWorld()) : 1.f;
			AddLightSlider(TEXT("Time speed"), (FMath::Clamp(CurDilation, 1.f, 8.f) - 1.f) / 7.f, TimeSpeedSlider, TimeSpeedV);
		}
		Pair(TEXT("Trigger Robbery"), FLinearColor(0.7f, 0.4f, 0.15f), [this]() { if (Phone.IsValid()) { Phone->RequestDevHeatEvent(false); } },
			 TEXT("Trigger Bust"), FLinearColor(0.6f, 0.2f, 0.2f), [this]() { if (Phone.IsValid()) { Phone->RequestDevHeatEvent(true); } });

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
			 TEXT("Hide paths"), FLinearColor(0.3f, 0.3f, 0.35f), [this]() { if (Phone.IsValid()) { Phone->HideAllPaths(); } });
		Single(TEXT("Delete path (aim at dot)"), CClr, [this]() { if (Phone.IsValid()) { Phone->DeletePathInCrosshair(); } });

		// === SHOPS ===
		Section(TEXT("SHOPS"));
		{
			static const TCHAR* KN[3] = { TEXT("Grow shop"), TEXT("Supplies"), TEXT("Furniture") };
			const int32 SelK = Phone.IsValid() ? FMath::Clamp(Phone->GetSelectedShopKind(), 0, 2) : 0;
			Single(FString::Printf(TEXT("Shop type: %s  (tap)"), KN[SelK]), FLinearColor(0.32f, 0.36f, 0.2f),
				[this]() { if (Phone.IsValid()) { Phone->CycleSelectedShopKind(); FillSettingsBody(); } });
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
		BodyRow(MakeText(TEXT("Room builder"), 14, FLinearColor(0.7f, 0.85f, 1.f)), FMargin(0.f, 0.f, 0.f, 2.f));
		// Kamer-job opslaan: huidige 3 markers worden permanent (RoomJobs.txt, elke sessie herbouwd).
		UWeedActionButton* JobB = MakeActionBtn(TEXT("Save room build (clears markers)"), FLinearColor(0.45f, 0.35f, 0.15f),
			[this]() { if (Phone.IsValid()) { Phone->SaveRoomJob(); } }, 13);
		BodyRow(JobB, FMargin(0.f, 0.f, 0.f, 8.f));

		// --- Room stamper: kamers als stempel plaatsen (deur snapt op deur, R = draaien) ---
		BodyRow(MakeText(TEXT("Room stamper"), 14, FLinearColor(0.7f, 0.85f, 1.f)), FMargin(0.f, 6.f, 0.f, 2.f));
		UWeedActionButton* TplB = MakeActionBtn(TEXT("Save room as template (2 markers)"), FLinearColor(0.25f, 0.4f, 0.5f),
			[this]() { if (Phone.IsValid()) { Phone->SaveRoomTemplateNow(); FillSettingsBody(); } }, 12);
		BodyRow(TplB, FMargin(0.f, 0.f, 0.f, 4.f));
		{
			const TArray<FString> Templates = ARoomStamper::ListTemplates();
			if (Templates.Num() == 0)
			{
				BodyRow(MakeText(TEXT("No templates yet - mark a room (2 corners) and save it."), 10, FLinearColor(0.6f, 0.64f, 0.74f)), FMargin(0.f, 0.f, 0.f, 4.f));
			}
			for (const FString& Tpl : Templates)
			{
				UHorizontalBox* TRow = WidgetTree->ConstructWidget<UHorizontalBox>();
				UWeedActionButton* StampB = MakeActionBtn(FString::Printf(TEXT("Stamp: %s"), *Tpl), FLinearColor(0.2f, 0.35f, 0.25f),
					[this, Tpl]() { if (Phone.IsValid()) { Phone->StartRoomStamp(Tpl); } }, 12);
				TRow->AddChildToHorizontalBox(StampB)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				TRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("X"), FLinearColor(0.45f, 0.2f, 0.2f),
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
				UWeedActionButton* UndoB = MakeActionBtn(TEXT("Undo last stamp"), FLinearColor(0.55f, 0.3f, 0.2f),
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
						11, FLinearColor(0.85f, 0.9f, 1.f)));
					SLab->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); SLab->SetVerticalAlignment(VAlign_Center);
					if (bHasPos)
					{
						SRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("TP"), FLinearColor(0.2f, 0.4f, 0.55f),
							[this, StampPos]()
							{
								if (APawn* Pn = GetOwningPlayerPawn()) { Pn->SetActorLocation(StampPos + FVector(0.f, 0.f, 120.f)); }
							}, 11))->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
					}
					SRow->AddChildToHorizontalBox(MakeActionBtn(TEXT("X"), FLinearColor(0.45f, 0.2f, 0.2f),
						[this, SLine]() { ARoomStamper::RemoveStamp(GetWorld(), SLine); FillSettingsBody(); }, 11))->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
					BodyRow(SRow, FMargin(0.f, 1.f, 0.f, 1.f));
				}
			}
		}

		// Deur op slot: vergrendeld zoals een bewoner-deur maar ZONDER naam (prompt "LOCKED").
		// Permanent via Saved/LockedDoors.txt - elke sessie opnieuw toegepast.
		UWeedActionButton* LockDoorB = MakeActionBtn(TEXT("Lock door in crosshair"), FLinearColor(0.35f, 0.3f, 0.2f),
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
		UWeedActionButton* LockDoorXB = MakeActionBtn(TEXT("Clear locked doors"), FLinearColor(0.4f, 0.22f, 0.22f),
			[this]()
			{
				WeedData::DeleteFile(TEXT("LockedDoors.txt"));
				if (Phone.IsValid()) { Phone->Toast(TEXT("Locked doors cleared (restart restores)"), FColor::Orange, 4.f); }
			}, 11);
		BodyRow(LockDoorXB, FMargin(0.f, 0.f, 0.f, 8.f));

		// Deur die naast z'n kozijn staat: richt erop + klik -> springt naar het dichtstbijzijnde deur-kozijn.
		// Permanent via Saved/DoorSnaps.txt (DoorRetrofitter zet 'm elke sessie terug op de juiste plek).
		UWeedActionButton* SnapDoorB = MakeActionBtn(TEXT("Snap door to frame"), FLinearColor(0.25f, 0.35f, 0.3f),
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
		UWeedActionButton* SnapDoorXB = MakeActionBtn(TEXT("Clear door snaps"), FLinearColor(0.4f, 0.22f, 0.22f),
			[this]()
			{
				WeedData::DeleteFile(TEXT("DoorSnaps.txt"));
				if (Phone.IsValid()) { Phone->Toast(TEXT("Door snaps cleared (restart restores)"), FColor::Orange, 4.f); }
			}, 11);
		BodyRow(SnapDoorXB, FMargin(0.f, 0.f, 0.f, 8.f));

		// Dev-opruimer: kijk naar een (zwevende of foute) deur, open de phone en klik - deur weg.
		UWeedActionButton* KillDoorB = MakeActionBtn(TEXT("Remove door in crosshair"), FLinearColor(0.4f, 0.22f, 0.22f),
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
		BodyRow(MakeText(TEXT("Lighting (live)"), 14, FLinearColor(0.7f, 0.85f, 1.f)), FMargin(0.f, 0.f, 0.f, 2.f));
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

		UWeedActionButton* SaveB = MakeActionBtn(TEXT("Save light config"), FLinearColor(0.22f, 0.5f, 0.32f),
			[this]() { if (ADayNightController* D = ADayNightController::GetLocal(GetWorld())) { D->SaveLightConfig(); } }, 12);
		BodyRow(SaveB, FMargin(0.f, 8.f, 0.f, 0.f));
	}
	else if (SettingsCat == 4) // Spots: F9-markers bekijken / teleporteren / verwijderen
	{
		{
			BodyRow(MakeText(TEXT("Marked spots"), 14, FLinearColor(0.7f, 0.85f, 1.f)), FMargin(0.f, 0.f, 0.f, 2.f));
			const FString SpotFile = FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt");
			TArray<FString> SpotLines;
			FFileHelper::LoadFileToStringArray(SpotLines, *SpotFile);
			SpotLines.RemoveAll([](const FString& L) { return L.TrimStartAndEnd().IsEmpty(); });
			if (SpotLines.Num() == 0)
			{
				BodyRow(MakeText(TEXT("No spots yet - press F9 in-game to mark one."), 11, FLinearColor(0.6f, 0.64f, 0.74f)), FMargin(0.f, 0.f, 0.f, 4.f));
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
					bSameMap ? FLinearColor(0.85f, 0.9f, 1.f) : FLinearColor(0.5f, 0.52f, 0.6f)));
				LS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LS2->SetVerticalAlignment(VAlign_Center);
				if (bHasPos && bSameMap)
				{
					RowB->AddChildToHorizontalBox(MakeActionBtn(TEXT("TP"), FLinearColor(0.2f, 0.4f, 0.55f),
						[this, SpotPos]()
						{
							if (APawn* Pn = GetOwningPlayerPawn()) { Pn->SetActorLocation(SpotPos + FVector(0.f, 0.f, 60.f)); }
						}, 11))->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
				}
				RowB->AddChildToHorizontalBox(MakeActionBtn(TEXT("X"), FLinearColor(0.45f, 0.2f, 0.2f),
					[this, SpotIdx]()
					{
						const FString F = FPaths::ProjectSavedDir() / TEXT("MarkedSpots.txt");
						TArray<FString> Ls;
						FFileHelper::LoadFileToStringArray(Ls, *F);
						Ls.RemoveAll([](const FString& L) { return L.TrimStartAndEnd().IsEmpty(); });
						if (Ls.IsValidIndex(SpotIdx)) { Ls.RemoveAt(SpotIdx); }
						FFileHelper::SaveStringToFile(FString::Join(Ls, TEXT("\n")) + (Ls.Num() ? TEXT("\n") : TEXT("")), *F);
						FillSettingsBody();
					}, 11))->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
				BodyRow(RowB, FMargin(0.f, 1.f, 0.f, 1.f));
			}
			if (SpotLines.Num() > MaxShow)
			{
				BodyRow(MakeText(FString::Printf(TEXT("(showing last %d of %d)"), MaxShow, SpotLines.Num()), 9, FLinearColor(0.55f, 0.58f, 0.68f)), FMargin(0.f, 2.f, 0.f, 0.f));
			}
		}
	}
	else // Status
	{
		// --- Character skin: kies man of vrouw (gerepliceerd zodat je co-op maat je skin ziet) ---
		{
			IPlayerNpcActions* Skn = Cast<IPlayerNpcActions>(GetOwningPlayerPawn());
			const uint8 Cur = Skn ? Skn->GetPlayerSkinIndex() : 0;
			BodyRow(MakeText(TEXT("Character"), 14, FLinearColor(0.8f, 0.85f, 1.f)), FMargin(0.f, 0.f, 0.f, 2.f));
			// Male = citizens Tony (skin 5). De oude Manny/Quinn-mannequins zijn weg.
			UHorizontalBox* GBtns = WidgetTree->ConstructWidget<UHorizontalBox>();
			UWeedActionButton* MaleB = MakeActionBtn(TEXT("Male"),
				(Cur == 5) ? FLinearColor(0.20f, 0.55f, 0.85f) : FLinearColor(0.15f, 0.16f, 0.21f),
				[this]() { if (IPlayerNpcActions* S = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { S->SetPlayerSkinIndex(5); } FillSettingsBody(); }, 13);
			GBtns->AddChildToHorizontalBox(MaleB)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			BodyRow(GBtns, FMargin(0.f, 0.f, 0.f, 4.f));
			// Female-keuzes: Casual-meisjes op skin 2/3/4.
			UHorizontalBox* CBtns = WidgetTree->ConstructWidget<UHorizontalBox>();
			const TCHAR* CasualLabels[3] = { TEXT("Girl 1"), TEXT("Girl 2"), TEXT("Girl 3") };
			for (int32 ci = 0; ci < 3; ++ci)
			{
				const uint8 SkinIdx = (uint8)(2 + ci);
				UWeedActionButton* CB = MakeActionBtn(CasualLabels[ci],
					(Cur == SkinIdx) ? FLinearColor(0.55f, 0.30f, 0.80f) : FLinearColor(0.15f, 0.16f, 0.21f),
					[this, SkinIdx]() { if (IPlayerNpcActions* S = Cast<IPlayerNpcActions>(GetOwningPlayerPawn())) { S->SetPlayerSkinIndex(SkinIdx); } FillSettingsBody(); }, 13);
				CBtns->AddChildToHorizontalBox(CB)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
			BodyRow(CBtns, FMargin(0.f, 0.f, 0.f, 10.f));
		}
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
	// Fase B: een live klant met een lopende afspraak -> z'n ApptTimeout-fractie (telt af tot 'ie opgeeft).
	for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
	{
		if (It->NpcId == ContactId && It->HasActiveAppointment())
		{
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
			// Fase 2: wacht op JOUW antwoord (accept/decline) - telt af tot de klant opgeeft (150s). EERST dit.
			const float Left = 150.f - (NowReal - M.SentRealTime);
			OutFrac = FMath::Clamp(Left / 150.f, 0.f, 1.f);
			OutSecsLeft = FMath::CeilToInt(FMath::Max(0.f, Left));
			OutPhase = 2;
			return true;
		}
		if (M.Status == 1 && Day)
		{
			// Fase 0: geaccepteerd, klant nog niet gespawnd -> tijd tot het afspraak-MOMENT (vast 240s-venster).
			float Remaining = M.AppointmentTimeOfDay - NowDay;
			if (Remaining < 0.f) { Remaining += Length; }
			OutFrac = FMath::Clamp(Remaining / 240.f, 0.f, 1.f);
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
	// In een open chat-thread tellen alleen de berichten van DAT contact mee, zodat een appje van een
	// ander contact je open thread niet herbouwt (geen flash). In de gesprekkenlijst telt alles.
	int32 Sig = 0, Cnt = 0;
	for (const FPhoneMessage& M : Msgs)
	{
		if (!IsMsgForLocal(M)) { continue; }
		if (!OpenChatContact.IsNone() && M.FromContactId != OpenChatContact) { continue; }
		++Cnt;
		Sig = Sig * 31 + (int32)M.Status + (M.bFromMe ? 5 : 0);
	}
	// GEEN urgentie-bucket meer: de balken + kleuren + sorteer-volgorde tijdens het aftellen werken live
	// (UpdateListBarsLive) zonder herbouw. Zo herbouwt de lijst alleen nog bij een ECHT bericht-verschil,
	// niet elke keer dat een afspraak-balk een kleur-drempel kruist (dat gaf onnodig geflits).
	return Sig * 1000003 + Cnt;
}

void UPhoneWidget::BuildChatApp()
{
	ApptBar = nullptr; ApptBarLabel = nullptr; ApptBarContact = NAME_None; // alleen geldig in een actieve-afspraak-thread
	WaitBar = nullptr; WaitBarLabel = nullptr; WaitBarSentTime = -1.f;     // wacht-balk voor een open deal-bericht
	ListApptBars.Reset(); ListCards.Reset(); ListPreviews.Reset();         // lijst-urgentie-widgets (live bijgewerkt)
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
		for (const FPhoneMessage& M : Msgs) { if (!IsMsgForLocal(M)) { continue; } Order.AddUnique(M.FromContactId); }
		if (Order.Num() == 0) { AddInfoRow(TEXT("No messages yet."), FLinearColor::Gray, 13); return; }
		// URGENTIE-sortering: contacten met een lopende afspraak bovenaan (meest urgent = kleinste fractie eerst),
		// daarna de rest in de bestaande nieuwste-eerst-volgorde (stabiel). ForPlayerId-filter blijft via Order.
		Order.StableSort([this](const FName& A, const FName& B)
		{
			float fa, fb; int32 sa, sb, pa, pb, ca, cb;
			const bool ua = GetApptUrgency(A, fa, sa, pa, ca);
			const bool ub = GetApptUrgency(B, fb, sb, pb, cb);
			if (ua != ub) { return ua; }   // afspraken boven gewone chats
			if (ua) { return fa < fb; }     // kleinere fractie = urgenter = hoger
			return false;                    // anders recentheid-volgorde behouden
		});

		UScrollBox* List = WidgetTree->ConstructWidget<UScrollBox>();
		UVerticalBoxSlot* LS = ContentBox->AddChildToVerticalBox(List);
		LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		for (const FName& Cid : Order)
		{
			// Laatste bericht + of er ONGELEZEN berichten van dit contact zijn (wist bij het openen van de chat).
			FString LastBody; FText Name; float LastClock = -1.f;
			for (const FPhoneMessage& M : Msgs)
			{
				if (!IsMsgForLocal(M)) { continue; }
				if (M.FromContactId != Cid) { continue; }
				if (LastBody.IsEmpty()) { LastBody = (M.bFromMe ? TEXT("You: ") : TEXT("")) + M.Body.ToString(); Name = M.SenderName; LastClock = M.SentClockHour; break; }
			}
			const bool bOpen = Phone.IsValid() && Phone->HasUnreadFrom(Cid);
			// Lopende afspraak? -> live preview ("Arrives in M:SS" / "At the door - M:SS left") + urgentie-kleur.
			float UFrac = 0.f; int32 USecs = 0, UPhase = 0, UClockM = 0;
			const bool bUrgent = GetApptUrgency(Cid, UFrac, USecs, UPhase, UClockM);
			if (bUrgent)
			{
				LastBody = (UPhase == 2) ? FString::Printf(TEXT("Reply needed - %d:%02d"), USecs / 60, USecs % 60)
					: (UPhase == 0) ? FString::Printf(TEXT("Coming - arrives at %02d:%02d"), (UClockM / 60) % 24, UClockM % 60)
					: FString::Printf(TEXT("At the door now - %d:%02d left"), USecs / 60, USecs % 60);
			}
			if (LastBody.Len() > 30) { LastBody = LastBody.Left(29) + TEXT("."); }

			const FLinearColor U = UrgencyColor(UFrac, UPhase == 2);
			UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
			Card->SetBrush(RoundedBrush(bUrgent ? FLinearColor(U.R * 0.28f, U.G * 0.28f, U.B * 0.28f, 0.97f)
				: (bOpen ? FLinearColor(0.12f, 0.17f, 0.13f, 0.97f) : FLinearColor(0.11f, 0.12f, 0.15f, 0.95f)), 8.f));
			ListCards.Add(Cid, Card);
			Card->SetPadding(FMargin(8.f, 6.f, 8.f, 6.f));
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			Card->SetContent(Row);
			UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
			Info->AddChildToVerticalBox(MakeText(Name.ToString(), 14, FLinearColor(0.95f, 0.97f, 1.f)));
			if (GS && GS->GetNpcRegistry())
			{
				const int32 Tr = GS->GetNpcRegistry()->GetCustomerTier(Cid);
				const FLinearColor TCol = (Tr >= 5) ? FLinearColor(1.f, 0.8f, 0.3f) : (Tr >= 4 ? FLinearColor(0.8f, 0.7f, 1.f) : FLinearColor(0.55f, 0.7f, 0.6f));
				Info->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%s customer"), *UNpcRegistryComponent::TierName(Tr)), 9, TCol));
			}
			UTextBlock* Prev = MakeText(LastBody, 10, FLinearColor(0.62f, 0.66f, 0.76f));
			Prev->SetClipping(EWidgetClipping::ClipToBounds);
			ListPreviews.Add(Cid, Prev);
			Info->AddChildToVerticalBox(Prev);
			if (bUrgent)
			{
				// Slanke aftelbalk onder de preview - in één oogopslag zie je hoeveel tijd je nog hebt.
				USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>();
				BarBox->SetHeightOverride(5.f);
				UProgressBar* LB = WidgetTree->ConstructWidget<UProgressBar>();
				LB->SetPercent(UFrac);
				LB->SetFillColorAndOpacity(U);
				LB->SetVisibility(ESlateVisibility::HitTestInvisible);
				BarBox->SetContent(LB);
				Info->AddChildToVerticalBox(BarBox)->SetPadding(FMargin(0.f, 3.f, 6.f, 0.f));
				ListApptBars.Add(Cid, LB);
			}
			UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(Info);
			IS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); IS->SetVerticalAlignment(VAlign_Center);
			// Tijdstempel van het laatste bericht (HH:MM, in-game klok) rechts in de rij.
			if (LastClock >= 0.f)
			{
				const int32 Hh = (int32)LastClock; const int32 Mm = (int32)((LastClock - Hh) * 60.f);
				UHorizontalBoxSlot* TsS = Row->AddChildToHorizontalBox(MakeText(FString::Printf(TEXT("%02d:%02d"), Hh, Mm), 9, FLinearColor(0.5f, 0.55f, 0.66f)));
				TsS->SetVerticalAlignment(VAlign_Top); TsS->SetPadding(FMargin(6.f, 1.f, 2.f, 0.f));
			}
			// Aantal-badge: groen pilletje met het aantal ongelezen berichten van dit contact (i.p.v. enkel "NEW").
			const int32 UnreadN = Phone.IsValid() ? Phone->GetUnreadCountFrom(Cid) : 0;
			if (UnreadN > 0)
			{
				UBorder* Badge = WidgetTree->ConstructWidget<UBorder>();
				Badge->SetBrush(RoundedBrush(FLinearColor(0.2f, 0.72f, 0.36f, 1.f), 9.f));
				Badge->SetPadding(FMargin(UnreadN > 9 ? 6.f : 8.f, 2.f, UnreadN > 9 ? 6.f : 8.f, 2.f));
				Badge->SetContent(MakeText(FString::Printf(TEXT("%d"), UnreadN), 12, FLinearColor(0.04f, 0.1f, 0.05f)));
				UHorizontalBoxSlot* BS = Row->AddChildToHorizontalBox(Badge);
				BS->SetVerticalAlignment(VAlign_Center); BS->SetPadding(FMargin(4.f, 0.f, 4.f, 0.f));
			}
			const FName Pick = Cid;
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Open"), FLinearColor(0.2f, 0.4f, 0.55f),
				[this, Pick]() { OpenChatContact = Pick; bOfferStrainView = false; ProposeMins = -1; MarkDirty(); }, 11))->SetVerticalAlignment(VAlign_Center);
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
		[this]() { OpenChatContact = NAME_None; bOfferStrainView = false; MarkDirty(); }, 11))->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* NS = Head->AddChildToHorizontalBox(MakeText(ContactName.ToString(), 15, FLinearColor(0.92f, 0.95f, 1.f)));
	NS->SetVerticalAlignment(VAlign_Center); NS->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
	ContentBox->AddChildToVerticalBox(Head)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// Klant-tier + XP-balk naar de volgende tier.
	if (GS && GS->GetNpcRegistry())
	{
		UNpcRegistryComponent* Reg = GS->GetNpcRegistry();
		const int32 Tier = Reg->GetCustomerTier(OpenChatContact);
		const float Frac = Reg->GetTierProgress01(OpenChatContact);
		UBorder* TB = WidgetTree->ConstructWidget<UBorder>();
		TB->SetBrush(RoundedBrush(FLinearColor(0.10f, 0.12f, 0.16f, 0.95f), 8.f));
		TB->SetPadding(FMargin(8.f, 5.f, 8.f, 6.f));
		UVerticalBox* TV = WidgetTree->ConstructWidget<UVerticalBox>();
		TB->SetContent(TV);
		const FString TLbl = (Tier >= 5)
			? FString::Printf(TEXT("Tier: %s  (max)"), *UNpcRegistryComponent::TierName(Tier))
			: FString::Printf(TEXT("Tier: %s  ->  %s"), *UNpcRegistryComponent::TierName(Tier), *UNpcRegistryComponent::TierName(Tier + 1));
		TV->AddChildToVerticalBox(MakeText(TLbl, 11, FLinearColor(0.85f, 0.9f, 1.f)));
		UProgressBar* TPB = WidgetTree->ConstructWidget<UProgressBar>();
		TPB->SetPercent(Frac);
		TPB->SetFillColorAndOpacity(FLinearColor(0.45f, 0.75f, 1.f));
		TV->AddChildToVerticalBox(TPB)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		ContentBox->AddChildToVerticalBox(TB)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	// Loopt er een afspraak met deze persoon? Aftellende balk: fase A = tijd tot 'ie KOMT (afspraak-moment),
	// fase B = tijd tot 'ie OPGEEFT. Werkt dus al vóór de klant fysiek is, en kleurt groen->geel->rood.
	{
		float TFrac = 0.f; int32 TSecs = 0, TPhase = 0, TClockM = 0;
		// Fase 2 (wacht-op-antwoord) toont de aparte WaitBar hieronder; hier alleen fase 0 (arrives) + 1 (at door).
		if (GetApptUrgency(OpenChatContact, TFrac, TSecs, TPhase, TClockM) && TPhase != 2)
		{
			UBorder* Box = WidgetTree->ConstructWidget<UBorder>();
			Box->SetBrush(RoundedBrush(FLinearColor(0.10f, 0.13f, 0.10f, 0.95f), 8.f));
			Box->SetPadding(FMargin(8.f, 5.f, 8.f, 6.f));
			UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
			Box->SetContent(VB);
			ApptBarLabel = MakeText(TEXT("Waiting..."), 11, FLinearColor(0.8f, 0.9f, 0.8f));
			VB->AddChildToVerticalBox(ApptBarLabel);
			ApptBar = WidgetTree->ConstructWidget<UProgressBar>();
			ApptBar->SetPercent(TFrac);
			ApptBar->SetFillColorAndOpacity(UrgencyColor(TFrac));
			VB->AddChildToVerticalBox(ApptBar)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
			ApptBarContact = OpenChatContact;
			ContentBox->AddChildToVerticalBox(Box)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		}
	}

	// Berichten chronologisch (oudste boven). Msgs is nieuwste-eerst -> achterstevoren itereren.
	UScrollBox* Thread = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* TS = ContentBox->AddChildToVerticalBox(Thread);
	TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	bool bAny = false; bool bHasOpen = false; float OpenSentTime = -1.f;
	for (int32 i = Msgs.Num() - 1; i >= 0; --i)
	{
		const FPhoneMessage& M = Msgs[i];
		if (!IsMsgForLocal(M)) { continue; }
		if (M.FromContactId != OpenChatContact) { continue; }
		bAny = true;
		if (M.Status == 0 && !M.bFromMe) { bHasOpen = true; OpenSentTime = M.SentRealTime; }

		FString Body = M.Body.ToString();
		if (!M.bFromMe && M.Status == 1) { Body += TEXT("  (accepted)"); }
		else if (!M.bFromMe && M.Status == 2) { Body += TEXT("  (declined)"); }

		UBorder* Bub = WidgetTree->ConstructWidget<UBorder>();
		Bub->SetBrush(RoundedBrush(M.bFromMe ? FLinearColor(0.16f, 0.35f, 0.22f, 0.97f) : FLinearColor(0.16f, 0.18f, 0.24f, 0.97f), 10.f));
		Bub->SetPadding(FMargin(9.f, 6.f, 9.f, 6.f));
		// Body met de belangrijke woorden vetgedrukt (grams/%/strain/product) i.p.v. één platte tekst.
		// Strain/product ook altijd vet: zelfde "pretty name minus bag" als waarmee de body is opgebouwd.
		FString BoldStrain;
		if (!M.WantProduct.IsNone()) { BoldStrain = WeedUI::PrettyItemName(M.WantProduct).Replace(TEXT(" bag"), TEXT(""), ESearchCase::IgnoreCase); }
		UWidget* BodyT = MakeRichBody(WidgetTree, Body, 12, FLinearColor(0.95f, 0.97f, 1.f), BoldStrain);
		// Tijdstempel (HH:MM, in-game klok) onder het bericht - zoals een normale berichten-app.
		UVerticalBox* BubVB = WidgetTree->ConstructWidget<UVerticalBox>();
		BubVB->AddChildToVerticalBox(BodyT);
		// Tijdstempel op ECHTE tijd ("Xm ago") i.p.v. de in-game klok (die liep te snel).
		if (M.SentRealTime >= 0.f)
		{
			const float RealNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			const int32 AgoSec = FMath::Max(0, (int32)(RealNow - M.SentRealTime));
			const FString Ago = (AgoSec < 60) ? FString(TEXT("just now")) : (AgoSec < 3600) ? FString::Printf(TEXT("%dm ago"), AgoSec / 60) : FString::Printf(TEXT("%dh ago"), AgoSec / 3600);
			UTextBlock* TimeT = MakeText(Ago, 8, FLinearColor(0.55f, 0.6f, 0.72f));
			UVerticalBoxSlot* TSl = BubVB->AddChildToVerticalBox(TimeT);
			TSl->SetHorizontalAlignment(M.bFromMe ? HAlign_Right : HAlign_Left);
			TSl->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		}
		Bub->SetContent(BubVB);

		USizeBox* Cap = WidgetTree->ConstructWidget<USizeBox>();
		Cap->SetMaxDesiredWidth(244.f);
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
		// Live wacht-balk: hoelang blijft de klant nog wachten op je antwoord voor 'ie opgeeft (150s).
		if (OpenSentTime >= 0.f)
		{
			UBorder* WBox = WidgetTree->ConstructWidget<UBorder>();
			WBox->SetBrush(RoundedBrush(FLinearColor(0.14f, 0.12f, 0.10f, 0.95f), 8.f));
			WBox->SetPadding(FMargin(8.f, 5.f, 8.f, 6.f));
			UVerticalBox* WVB = WidgetTree->ConstructWidget<UVerticalBox>();
			WBox->SetContent(WVB);
			WaitBarLabel = MakeText(TEXT("Waiting for your reply..."), 11, FLinearColor(0.88f, 0.82f, 0.72f));
			WVB->AddChildToVerticalBox(WaitBarLabel);
			WaitBar = WidgetTree->ConstructWidget<UProgressBar>();
			WaitBar->SetPercent(1.f);
			WVB->AddChildToVerticalBox(WaitBar)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
			WaitBarSentTime = OpenSentTime;
			ContentBox->AddChildToVerticalBox(WBox)->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
		}
		UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
		const FName Pick = OpenChatContact;
		UHorizontalBoxSlot* AS = Btns->AddChildToHorizontalBox(MakeActionBtn(TEXT("Accept"), FLinearColor(0.2f, 0.5f, 0.28f),
			[this, Pick]() { if (Phone.IsValid()) { Phone->RespondChat(Pick, true); } MarkDirty(); }, 13));
		AS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); AS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
		UHorizontalBoxSlot* DS = Btns->AddChildToHorizontalBox(MakeActionBtn(TEXT("Decline"), FLinearColor(0.5f, 0.28f, 0.2f),
			[this, Pick]() { if (Phone.IsValid()) { Phone->RespondChat(Pick, false); } MarkDirty(); }, 13));
		DS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); DS->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		ContentBox->AddChildToVerticalBox(Btns)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

		// --- "Offer instead..." : een ANDERE strain aanbieden (substituut) met stats + verschil + kans ---
		ContentBox->AddChildToVerticalBox(MakeActionBtn(bOfferStrainView ? TEXT("Hide alternatives") : TEXT("Offer instead..."),
			FLinearColor(0.25f, 0.35f, 0.5f), [this]() { bOfferStrainView = !bOfferStrainView; MarkDirty(); }, 12))
			->SetPadding(FMargin(0.f, 6.f, 0.f, 2.f));
		if (bOfferStrainView)
		{
			const FName ReqStrain = Con->GetRequestedStrain(OpenChatContact);
			float ExpThc = 15.f;
			if (GS && GS->GetStore()) { float t = 0.f, y = 0.f, g = 0.f; if (GS->GetStore()->GetStrainStats(ReqStrain, t, y, g) && t > 0.f) { ExpThc = t; } }
			ContentBox->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("They want %s (~%.0f%% THC). Your stock (incl. chests/shelves):"), *ReqStrain.ToString(), ExpThc), 10, FLinearColor(0.7f, 0.75f, 0.85f)))
				->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));

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

			int32 Shown = 0;
			for (const TPair<FName, FOfferAgg>& P : ByStrain)
			{
				const FName Strain = P.Key; const FOfferAgg& O = P.Value;
				const float Delta = O.Thc - ExpThc;
				const float Chance = Con->SubstituteAcceptChance(OpenChatContact, ReqStrain, Strain, O.Thc) * 100.f;
				const FString Lbl = FString::Printf(TEXT("%s   T%.0f%%  Q%.0f%%  %dg\n%+.0f%% THC vs ask   ~%.0f%% yes"),
					*Strain.ToString(), O.Thc, O.Qual, O.Qty, Delta, Chance);
				const FName SPick = Strain;
				ContentBox->AddChildToVerticalBox(MakeActionBtn(Lbl, (Delta >= 0.f ? FLinearColor(0.18f, 0.4f, 0.28f) : FLinearColor(0.36f, 0.3f, 0.2f)),
					[this, SPick]() { if (Phone.IsValid()) { Phone->ProposeChatStrain(OpenChatContact, SPick); } bOfferStrainView = false; MarkDirty(); }, 11))
					->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
				++Shown;
			}
			if (Shown == 0) { ContentBox->AddChildToVerticalBox(MakeText(TEXT("(no dried/bagged weed in your inventory or storages)"), 10, FLinearColor::Gray)); }
		}

		// Of kies zelf een tijd (per kwartier). Ze gaan altijd akkoord, geen nadeel.
		ContentBox->AddChildToVerticalBox(MakeText(TEXT("Can't make it? Pick a time:"), 11, FLinearColor(0.7f, 0.75f, 0.85f)))
			->SetPadding(FMargin(0.f, 6.f, 0.f, 2.f));

		// Huidige kloktijd (minuten) — de ondergrens loopt hiermee mee.
		const int32 NowMins = (GS && GS->GetDayCycle()) ? (((int32)(GS->GetDayCycle()->GetClockHour() * 60.f)) % 1440) : 0;

		// Startwaarde: de tijd die de KLANT voorstelde (dan klik je vandaaruit +kwartier/+uur). Geen
		// afspraaktijd bekend -> 1 uur vanaf nu. Afgerond op een kwartier.
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
		// Klem in "minuten-vooruit"-ruimte: minstens 30 min, hoogstens 23,5u vooruit (nooit de vorige dag).
		// Doordat NowMins meeloopt met de klok, schuift de ondergrens live mee.
		{
			int32 Gap = ((ProposeMins - NowMins) % 1440 + 1440) % 1440;
			Gap = FMath::Clamp(Gap, 30, 1410);
			ProposeMins = (NowMins + Gap) % 1440;
		}
		auto Step = [this, NowMins](int32 Delta)
		{
			int32 Gap = ((ProposeMins - NowMins) % 1440 + 1440) % 1440;
			Gap = FMath::Clamp(Gap + Delta, 30, 1410); // niet onder 30 min vooruit, niet naar gisteren
			ProposeMins = (NowMins + Gap) % 1440;
			// Alleen het klok-label bijwerken (geen rebuild -> geen flash).
			if (PickerClockText) { PickerClockText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), ProposeMins / 60, ProposeMins % 60))); }
		};

		UHorizontalBox* Stepper = WidgetTree->ConstructWidget<UHorizontalBox>();
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("-1h"), FLinearColor(0.22f, 0.28f, 0.4f), [Step]() { Step(-60); }, 13))->SetPadding(FMargin(0.f, 0.f, 3.f, 0.f));
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("-15m"), FLinearColor(0.22f, 0.28f, 0.4f), [Step]() { Step(-15); }, 13))->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		UTextBlock* Clock = MakeText(FString::Printf(TEXT("%02d:%02d"), ProposeMins / 60, ProposeMins % 60), 18, FLinearColor(0.95f, 0.97f, 1.f));
		PickerClockText = Clock;            // live bijwerken in NativeTick (geen rebuild -> geen flash)
		PickerContact = OpenChatContact;
		UHorizontalBoxSlot* CS2 = Stepper->AddChildToHorizontalBox(Clock);
		CS2->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS2->SetHorizontalAlignment(HAlign_Center); CS2->SetVerticalAlignment(VAlign_Center);
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("+15m"), FLinearColor(0.22f, 0.28f, 0.4f), [Step]() { Step(15); }, 13))->SetPadding(FMargin(6.f, 0.f, 3.f, 0.f));
		Stepper->AddChildToHorizontalBox(MakeActionBtn(TEXT("+1h"), FLinearColor(0.22f, 0.28f, 0.4f), [Step]() { Step(60); }, 13));
		ContentBox->AddChildToVerticalBox(Stepper)->SetPadding(FMargin(0.f, 2.f, 0.f, 4.f));

		// Knop leest de ACTUELE ProposeMins (member) bij klik, zodat live-bijwerken klopt.
		ContentBox->AddChildToVerticalBox(MakeActionBtn(TEXT("Propose this time"), FLinearColor(0.2f, 0.45f, 0.55f),
			[this, Pick]() { if (Phone.IsValid()) { Phone->ProposeChatTime(Pick, ProposeMins); } ProposeMins = -1; MarkDirty(); }, 13));
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
		Scroll->AddChild(MakeText(TEXT("No packages on the way."), 12, FLinearColor::Gray));
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

		// Status-regel: ETA + Cancel (onderweg), of "at the door" (aangekomen).
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
		UTextBlock* Desc = MakeText(TEXT("Upgrade to bank anywhere (no ATM)."), 12, FLinearColor(0.7f, 0.75f, 0.85f));
		Desc->SetAutoWrapText(true);
		AddRow(Desc);
		AddRow(MakeText(TEXT(""), 8, FLinearColor::Transparent));
		const int64 Cost = UPhoneClientComponent::PhoneUpgradeCostCents;
		AddRow(MakeActionBtn(FString::Printf(TEXT("Unlock  -  EUR %lld"), (long long)(WeedRoundEuros(Cost) / 100)),
			FLinearColor(0.16f, 0.5f, 0.85f), [this]() { if (Phone.IsValid()) { Phone->RequestBuyPhoneUpgrade(); } MarkDirty(); }, 14));
		return;
	}

	if (!Econ) { AddRow(MakeText(TEXT("Out of service."), 14, FLinearColor::Gray)); return; }

	// --- Balans-kaart (zoals een echte bank-app: groot saldo bovenaan) ---
	AddRow(MakeText(TEXT("BANK BALANCE"), 11, FLinearColor(0.5f, 0.7f, 0.95f), false));
	AddRow(MakeText(FString::Printf(TEXT("EUR %lld"), (long long)(WeedRoundEuros(Econ->GetBankCents()) / 100)), 26, FLinearColor(0.7f, 0.92f, 1.f), false));
	AddRow(MakeText(FString::Printf(TEXT("Cash to deposit:  EUR %lld"), (long long)(WeedRoundEuros(Econ->GetCashCents()) / 100)), 12, FLinearColor(0.7f, 0.72f, 0.78f)));
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
			const FLinearColor Col = (Cat == Ph->GetSupplierCat()) ? FLinearColor(0.20f, 0.78f, 0.45f) : FLinearColor(0.13f, 0.14f, 0.18f);
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
		const FLinearColor Col = (TabCat == Cat) ? FLinearColor(0.20f, 0.78f, 0.45f) : FLinearColor(0.13f, 0.14f, 0.18f);
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
		// Afspraak voorbij (deal gesloten of vertrokken) -> balk leeg + loslaten.
		ApptBar->SetPercent(0.f);
		if (ApptBarLabel) { ApptBarLabel->SetText(FText::FromString(TEXT("Done."))); }
		ApptBar = nullptr; ApptBarLabel = nullptr; ApptBarContact = NAME_None;
	}
}

void UPhoneWidget::UpdateListBarsLive()
{
	for (const TPair<FName, TObjectPtr<UProgressBar>>& KV : ListApptBars)
	{
		float Frac = 0.f; int32 Secs = 0, Phase = 0, ClockM = 0;
		const bool bActive = GetApptUrgency(KV.Key, Frac, Secs, Phase, ClockM);
		const FLinearColor U = UrgencyColor(Frac, Phase == 2);
		if (UProgressBar* Bar = KV.Value) { Bar->SetPercent(bActive ? Frac : 0.f); Bar->SetFillColorAndOpacity(U); }
		if (TObjectPtr<UBorder>* Card = ListCards.Find(KV.Key))
		{
			if (UBorder* C = *Card) { if (bActive) { C->SetBrushColor(FLinearColor(U.R * 0.28f, U.G * 0.28f, U.B * 0.28f, 0.97f)); } }
		}
		if (TObjectPtr<UTextBlock>* Prev = ListPreviews.Find(KV.Key))
		{
			if (UTextBlock* P = *Prev)
			{
				if (bActive) { P->SetText(FText::FromString(Phase == 2
					? FString::Printf(TEXT("Reply needed - %d:%02d"), Secs / 60, Secs % 60)
					: Phase == 0 ? FString::Printf(TEXT("Coming - arrives at %02d:%02d"), (ClockM / 60) % 24, ClockM % 60)
					: FString::Printf(TEXT("At the door now - %d:%02d left"), Secs / 60, Secs % 60))); }
			}
		}
	}
}

void UPhoneWidget::UpdateWaitBarLive()
{
	if (!WaitBar || WaitBarSentTime < 0.f || !GetWorld()) { return; }
	const float Total = 150.f; // GiveUpDelay in ContactsComponent: na 150s zonder antwoord geeft de klant op
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

	Scroll->AddChild(MakeText(TEXT("Pot upgrades stay with the pot."), 11, FLinearColor(0.6f, 0.66f, 0.76f)));

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
					RB->SetContent(MakeActionBtn(FString::Printf(TEXT("Buy  EUR %d"), (int32)(WeedRoundEuros((int64)Cost) / 100)), FLinearColor(0.2f, 0.5f, 0.28f),
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
		Scroll->AddChild(MakeText(TEXT("No pots placed yet."), 12, FLinearColor::Gray));
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
				bSell ? FString::Printf(TEXT("x%d   +EUR %d"), LQty, (int32)(WeedRoundEuros((int64)LineP) / 100)) : FString::Printf(TEXT("x%d   EUR %d"), LQty, (int32)(WeedRoundEuros((int64)LineP) / 100)),
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
			UTextBlock* TotT = MakeText(FString::Printf(TEXT("You receive: EUR %d"), (int32)(WeedRoundEuros((int64)SellSub) / 100)),
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
					const int32 OptFee = (int32)WeedRoundEuros((int64)FMath::RoundToInt(BuySub * UPhoneClientComponent::DeliveryFeePct(d)));
					const bool bSel = (d == DeliveryOpt);
					const FLinearColor Col = bSel ? FLinearColor(0.22f, 0.52f, 0.32f) : FLinearColor(0.15f, 0.16f, 0.21f);
					UWeedActionButton* OB = MakeActionBtn(TEXT(""), Col, [this, d]() { DeliveryOpt = d; RefreshStore(); }, 9);
					// Inhoud: PRIJS groot + leidend bovenaan, dan de naam, dan de tijd klein.
					UVerticalBox* OVB = WidgetTree->ConstructWidget<UVerticalBox>();
					FString PriceStr;
					if (OptFee <= 0) { PriceStr = TEXT("FREE"); }
					else { PriceStr = FString::Printf(TEXT("€%d"), (int32)(WeedRoundEuros((int64)OptFee) / 100)); }
					OVB->AddChildToVerticalBox(MakeText(PriceStr, 15, FLinearColor(1.f, 0.95f, 0.55f), true))->SetPadding(FMargin(0.f, 0.f, 0.f, 1.f));
					OVB->AddChildToVerticalBox(MakeText(UPhoneClientComponent::DeliveryName(d), 11, FLinearColor(0.95f, 0.97f, 1.f), true));
					OVB->AddChildToVerticalBox(MakeText(UPhoneClientComponent::DeliveryTimeText(d), 9, FLinearColor(0.65f, 0.7f, 0.8f), true));
					OB->SetContent(OVB);
					UHorizontalBoxSlot* OS = Opts->AddChildToHorizontalBox(OB);
					OS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); OS->SetPadding(FMargin(2.f, 0.f, 2.f, 0.f));
				}
				StoreFooter->AddChildToVerticalBox(Opts)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

				// Bezorg-adres: bij meerdere woningen kies je waar 't heen gaat. Standaard het huis waar je
				// nu binnen bent (groen). Anders je huidige/actieve woning.
				const TArray<int32>& OwnedH = Ph->GetOwnedHomes();
				if (OwnedH.Num() > 1)
				{
					const int32 CurTarget = Ph->ResolveDeliveryHome();
					StoreFooter->AddChildToVerticalBox(MakeText(TEXT("Deliver to"), 11, FLinearColor(0.75f, 0.8f, 0.95f)))->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
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

			// Opbouw-regel (kort): koop - verkoop - bezorging. Geen decimalen + alleen tonen wat van toepassing is.
			const int32 Fee = (int32)WeedRoundEuros((int64)FMath::RoundToInt(BuySub * UPhoneClientComponent::DeliveryFeePct(DeliveryOpt)));
			FString BD = FString::Printf(TEXT("Buy EUR %d"), (int32)(WeedRoundEuros((int64)BuySub) / 100));
			if (SellSub > 0) { BD += FString::Printf(TEXT("  -  Sell EUR %d"), (int32)(WeedRoundEuros((int64)SellSub) / 100)); }
			if (Fee > 0)     { BD += FString::Printf(TEXT("  +  Deliv EUR %d"), (int32)(WeedRoundEuros((int64)Fee) / 100)); }
			UTextBlock* Breakdown = MakeText(BD, 10, FLinearColor(0.68f, 0.72f, 0.82f));
			Breakdown->SetAutoWrapText(true);
			Breakdown->SetJustification(ETextJustify::Right);
			StoreFooter->AddChildToVerticalBox(Breakdown)->SetPadding(FMargin(0.f, 2.f, 4.f, 0.f));

			// Netto: positief = betalen, negatief = ontvangen.
			const int32 Net = Ph->GetCartNetCents(DeliveryOpt);
			UTextBlock* TotT = MakeText(Net >= 0 ? FString::Printf(TEXT("Total: EUR %d"), (int32)(WeedRoundEuros((int64)Net) / 100))
				: FString::Printf(TEXT("You receive: EUR %d"), (int32)(WeedRoundEuros((int64)(-Net)) / 100)),
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
			CardVB->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("You have %d   -   sells for EUR %d each"), Have, (int32)(WeedRoundEuros((int64)Val) / 100)), 10, FLinearColor(0.62f, 0.66f, 0.76f)));

			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UHorizontalBoxSlot* PT = Row->AddChildToHorizontalBox(MakeText(FString::Printf(TEXT("+EUR %d"), (int32)(WeedRoundEuros((int64)Val * Pend) / 100)), 13, FLinearColor(0.7f, 1.f, 0.7f)));
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
				TagPill->SetBrush(RoundedBrush(FLinearColor(0.10f, 0.42f, 0.20f, 0.96f), 5.f));
				TagPill->SetPadding(FMargin(4.f, 0.f, 4.f, 1.f));
				TagPill->SetContent(MakeText(STag, 8, FLinearColor(0.98f, 1.f, 0.99f)));
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
		UHorizontalBoxSlot* PT = Row->AddChildToHorizontalBox(MakeText(FString::Printf(TEXT("EUR %d"), (int32)(WeedRoundEuros((int64)Price) / 100)), 13, FLinearColor(0.7f, 1.f, 0.7f)));
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

	// Messages-app (index 3): rode ongelezen-badge rechtsboven op het app-icoon (refs bewaard -> live update).
	UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
	Ov->AddChildToOverlay(Sz);
	if (AppIndex == 3)
	{
		const int32 Unread = Phone.IsValid() ? Phone->GetUnreadMessageCount() : 0;
		MsgAppBadgeText = MakeText(Unread > 99 ? FString(TEXT("99+")) : FString::Printf(TEXT("%d"), FMath::Max(0, Unread)), 11, FLinearColor::White, true);
		MsgAppBadgePill = WidgetTree->ConstructWidget<UBorder>();
		MsgAppBadgePill->SetBrush(RoundedBrush(FLinearColor(0.90f, 0.16f, 0.16f, 0.98f), 9.f));
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
		GoalsAppBadgeText = MakeText(Claim > 99 ? FString(TEXT("99+")) : FString::Printf(TEXT("%d"), FMath::Max(0, Claim)), 11, FLinearColor(0.08f, 0.08f, 0.08f), true);
		GoalsAppBadgePill = WidgetTree->ConstructWidget<UBorder>();
		GoalsAppBadgePill->SetBrush(RoundedBrush(FLinearColor(0.95f, 0.78f, 0.20f, 0.98f), 9.f));
		GoalsAppBadgePill->SetPadding(FMargin(5.f, 0.f, 5.f, 0.f));
		GoalsAppBadgePill->SetContent(GoalsAppBadgeText);
		GoalsAppBadgePill->SetVisibility(Claim > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		UOverlaySlot* PS = Ov->AddChildToOverlay(GoalsAppBadgePill);
		PS->SetHorizontalAlignment(HAlign_Right); PS->SetVerticalAlignment(VAlign_Top);
		PS->SetPadding(FMargin(0.f, 2.f, 6.f, 0.f));
	}

	UVerticalBoxSlot* S1 = Cell->AddChildToVerticalBox(Ov);
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
	StatusSlot->SetPadding(FMargin(6.f, 4.f, 6.f, 4.f));

	// Level/XP-voortgangsbalk onder de statusbalk: zie hoeveel XP je nog nodig hebt voor het volgende level.
	{
		UHorizontalBox* XpRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		LevelXpBar = WidgetTree->ConstructWidget<UProgressBar>();
		LevelXpBar->SetFillColorAndOpacity(FLinearColor(0.45f, 0.85f, 0.5f));
		LevelXpBar->SetPercent(0.f);
		USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>(); BarSz->SetHeightOverride(10.f);
		BarSz->SetContent(LevelXpBar);
		UHorizontalBoxSlot* BS = XpRow->AddChildToHorizontalBox(BarSz);
		BS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BS->SetVerticalAlignment(VAlign_Center); BS->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		LevelXpText = MakeText(TEXT("0 / 0 XP"), 9, FLinearColor(0.78f, 0.96f, 0.82f));
		UHorizontalBoxSlot* TS = XpRow->AddChildToHorizontalBox(LevelXpText);
		TS->SetVerticalAlignment(VAlign_Center);
		UVerticalBoxSlot* XpSlot = VB->AddChildToVerticalBox(XpRow);
		XpSlot->SetPadding(FMargin(8.f, 0.f, 8.f, 8.f));
	}

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
	PickerClockText = nullptr; PickerProposeBtn = nullptr; PickerContact = NAME_None; // oude tijd-kiezer-refs vrijgeven
	MsgAppBadgePill = nullptr; MsgAppBadgeText = nullptr; // oude Messages-badge-refs vrijgeven (worden opnieuw gezet als 't home-rooster bouwt)
	GoalsAppBadgePill = nullptr; GoalsAppBadgeText = nullptr;

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	if (Phone->IsHomeScreen())
	{
		AddInfoRow(TEXT("Apps"), FLinearColor(0.6f, 1.f, 0.6f), 16);
		UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
		Grid->SetSlotPadding(FMargin(10.f));
		int32 Cell = 0;
		for (int32 oi = 0; oi < GNumApps; ++oi)
		{
			const int32 i = GHomeOrder[oi]; // logische volgorde i.p.v. ruwe app-index
			if (i == GStatsApp && !(GS && GS->IsCompetitive())) { continue; } // Leaderboard alleen in competitive
			if (i == GHashApp) { continue; } // Lab-app weg: alle processing zit nu in Supplies -> Kitchen
			UUniformGridSlot* GSlot = Grid->AddChildToUniformGrid(MakeAppCell(i, GAppName[i], GAppKey[i], GAppIcon[i], GAppCol[i]), Cell / 3, Cell % 3);
			GSlot->SetHorizontalAlignment(HAlign_Center);
			++Cell;
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
			ContentBox->AddChildToVerticalBox(MakeText(TEXT("Home"), 14, FLinearColor(0.7f, 0.9f, 1.f)))
				->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			TArray<FCityPropertyOffer> Offers;
			Phone->GetPropertyOffers(Offers);
			if (Offers.Num() == 0)
			{
				ContentBox->AddChildToVerticalBox(MakeText(TEXT("(city still loading...)"), 11, FLinearColor::Gray));
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
					? FString::Printf(TEXT("%s   EUR %lld"), *O.Sub, (long long)(WeedRoundEuros(O.PriceCents) / 100))
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
					RB->SetContent(MakeButton(TEXT("Buy"), 7, O.HomeIndex, FLinearColor(0.2f, 0.5f, 0.28f)));
				}
				else
				{
					// Eigen woning: teleport-knop (of "you are here") + verkoop-knop (~65%).
					const int32 SellVal = Phone->GetHomeSellValueCents(O.HomeIndex);
					UVerticalBox* OwnBox = WidgetTree->ConstructWidget<UVerticalBox>();
					if (bHereNow) { OwnBox->AddChildToVerticalBox(MakeText(TEXT("you are here"), 11, FLinearColor(0.6f, 1.f, 0.6f), true)); }
					else          { OwnBox->AddChildToVerticalBox(MakeButton(TEXT("Ga hierheen"), 8, O.HomeIndex, FLinearColor(0.25f, 0.45f, 0.6f))); }
					if (SellVal > 0)
					{
						OwnBox->AddChildToVerticalBox(MakeButton(*FString::Printf(TEXT("Sell EUR %d"), (int32)(WeedRoundEuros((int64)SellVal) / 100)), 9, O.HomeIndex, FLinearColor(0.5f, 0.32f, 0.2f)))
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
	else if (App == GSuppliesApp) // Supplies -> verwerken/verkopen/inrichten (papers, drogen, verpakken, meubels, keuken)
	{
		bSellApp = false;
		AppCats = { 4, 2, 3, 7, 11, 12 }; // Papers, Drying, Packing, Furniture, Kitchen (machines), Ingredients (boter etc.)
		if (!AppCats.Contains(Phone->GetSupplierCat())) { Phone->SetSupplierCat(AppCats[0]); }
		BuildStoreApp(ContentBox);
	}
	else if (App == GHashApp) // Lab -> de hasj-keten: machines (mesh/press) + machine-upgrades
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
	else if (App == GGoalsApp) // Goals -> milestone-doelen met rewards
	{
		BuildGoalsApp();
	}
	else if (App == GStatsApp) // Leaderboard (competitive stats)
	{
		BuildStatsApp();
	}
	else // Map
	{
		BuildMapApp();
	}
}

void UPhoneWidget::BuildStatsApp()
{
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	if (!GS || !GS->IsCompetitive()) { AddInfoRow(TEXT("Leaderboard is only for Competitive mode."), FLinearColor::Gray, 13); return; }

	AddInfoRow(TEXT("LEADERBOARD"), FLinearColor(1.f, 0.85f, 0.35f), 17);
	const TArray<FCompetitorScore>& St = GS->GetStandings();
	if (St.Num() == 0) { AddInfoRow(TEXT("No players yet."), FLinearColor::Gray, 13); return; }

	const APawn* Me = GetOwningPlayerPawn();
	const FString MyName = (Me && Me->GetPlayerState()) ? Me->GetPlayerState()->GetPlayerName() : FString();

	int32 Rank = 1;
	for (const FCompetitorScore& C : St)
	{
		const bool bMe = (!MyName.IsEmpty() && C.Name == MyName);
		const FLinearColor Col = (Rank == 1) ? FLinearColor(0.16f, 0.55f, 0.95f) : FLinearColor(0.10f, 0.11f, 0.16f);
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrush(WeedUI::Rounded(FLinearColor(Col.R, Col.G, Col.B, bMe ? 0.95f : 0.55f), 10.f));
		Card->SetPadding(FMargin(12.f, 9.f));
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(VB);

		const TCHAR* Medal = (Rank == 1) ? TEXT("1st") : (Rank == 2) ? TEXT("2nd") : (Rank == 3) ? TEXT("3rd") : TEXT("");
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("#%d  %s%s"), Rank, *C.Name, bMe ? TEXT("  (you)") : TEXT("")), 15,
			FLinearColor(1.f, 1.f, 1.f), true, true));
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("Net worth:  EUR %lld    %s"), (long long)(C.NetWorthCents / 100), Medal), 12,
			FLinearColor(0.85f, 1.f, 0.85f), false));
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("Cash EUR %lld   Bank EUR %lld"), (long long)(C.CashCents / 100), (long long)(C.BankCents / 100)), 11,
			FLinearColor(0.75f, 0.82f, 0.95f), false));
		VB->AddChildToVerticalBox(WeedUI::Text(WidgetTree,
			FString::Printf(TEXT("Earned EUR %lld   Customers %d"), (long long)(C.EarnedCents / 100), C.Customers), 11,
			FLinearColor(0.95f, 0.85f, 0.6f), false));

		UVerticalBoxSlot* CS = ContentBox->AddChildToVerticalBox(Card);
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
	if (!ContentBox) { return; }
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UGoalsComponent* GoalsC = GS ? GS->GetGoals() : nullptr;
	if (!GoalsC) { AddInfoRow(TEXT("Goals unavailable."), FLinearColor::Gray); return; }

	UScrollBox* List = WidgetTree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* LS = ContentBox->AddChildToVerticalBox(List);
	LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	const TArray<FGoalDef>& G = UGoalsComponent::Goals();
	for (int32 i = 0; i < G.Num(); ++i)
	{
		const FGoalDef& Gd = G[i];
		const int64 Prog = GoalsC->GetGoalProgress(i);
		const int64 Tgt = Gd.Target;
		const bool bDone = GoalsC->IsComplete(i);
		const bool bClaimed = GoalsC->IsClaimed(i);
		const float Frac = (Tgt > 0) ? FMath::Clamp((float)Prog / (float)Tgt, 0.f, 1.f) : 0.f;

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrush(RoundedBrush(bClaimed ? FLinearColor(0.10f, 0.13f, 0.10f, 0.95f) : FLinearColor(0.11f, 0.12f, 0.15f, 0.95f), 8.f));
		Card->SetPadding(FMargin(9.f, 7.f, 9.f, 7.f));
		UVerticalBox* CV = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(CV);

		CV->AddChildToVerticalBox(MakeText(Gd.Title, 13, bClaimed ? FLinearColor(0.6f, 0.8f, 0.6f) : FLinearColor(0.95f, 0.97f, 1.f)));

		const FString ProgStr = (Gd.Metric == 0)
			? FString::Printf(TEXT("EUR %lld / %lld"), (long long)(Prog / 100), (long long)(Tgt / 100))
			: FString::Printf(TEXT("%lld / %lld"), (long long)Prog, (long long)Tgt);
		CV->AddChildToVerticalBox(MakeText(ProgStr, 10, FLinearColor(0.62f, 0.66f, 0.76f)));

		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
		Bar->SetPercent(Frac);
		Bar->SetFillColorAndOpacity(bDone ? FLinearColor(0.4f, 0.95f, 0.5f) : FLinearColor(0.85f, 0.7f, 0.25f));
		USizeBox* BarSz = WidgetTree->ConstructWidget<USizeBox>();
		BarSz->SetHeightOverride(12.f); BarSz->SetContent(Bar);
		CV->AddChildToVerticalBox(BarSz)->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		FString RewardStr = TEXT("Reward:  ");
		if (Gd.RewardMoneyCents > 0) { RewardStr += FString::Printf(TEXT("EUR %lld"), (long long)(Gd.RewardMoneyCents / 100)); }
		if (!Gd.RewardItem.IsNone() && Gd.RewardQty > 0)
		{
			RewardStr += FString::Printf(TEXT("%s%dx %s"), Gd.RewardMoneyCents > 0 ? TEXT("  +  ") : TEXT(""), Gd.RewardQty, *WeedUI::PrettyItemName(Gd.RewardItem));
		}
		UHorizontalBoxSlot* RWS = Row->AddChildToHorizontalBox(MakeText(RewardStr, 10, FLinearColor(0.85f, 0.8f, 0.5f)));
		RWS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); RWS->SetVerticalAlignment(VAlign_Center);
		if (bClaimed)
		{
			Row->AddChildToHorizontalBox(MakeText(TEXT("Claimed"), 11, FLinearColor(0.5f, 0.8f, 0.5f)))->SetVerticalAlignment(VAlign_Center);
		}
		else if (bDone)
		{
			const int32 Idx = i;
			Row->AddChildToHorizontalBox(MakeActionBtn(TEXT("Claim"), FLinearColor(0.2f, 0.55f, 0.27f),
				[this, Idx]() { if (Phone.IsValid()) { Phone->ClaimGoal(Idx); } MarkDirty(); }, 11))->SetVerticalAlignment(VAlign_Center);
		}
		else
		{
			Row->AddChildToHorizontalBox(MakeText(TEXT("In progress"), 10, FLinearColor(0.5f, 0.52f, 0.58f)))->SetVerticalAlignment(VAlign_Center);
		}
		CV->AddChildToVerticalBox(Row);

		List->AddChild(Card);
		List->AddChild(MakeText(TEXT(""), 4, FLinearColor::Transparent));
	}
}

void UPhoneWidget::BuildMapApp()
{
	if (!ContentBox) { return; }

	// Knop naar de fullscreen-kaart (zelfde als M).
	UWeedActionButton* FB = MakeActionBtn(TEXT("Fullscreen"), FLinearColor(0.20f, 0.45f, 0.62f),
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
			const ULevelComponent* Lv = GS->GetLeveling();
			LevelText->SetText(FText::FromString(FString::Printf(TEXT("Lv %d"), Lv->GetLevel())));
			if (LevelXpBar) { LevelXpBar->SetPercent(Lv->GetLevelFraction()); }
			if (LevelXpText)
			{
				const int32 ToNext = Lv->GetXPToNext();
				LevelXpText->SetText(ToNext <= 0
					? FText::FromString(TEXT("MAX"))
					: FText::FromString(FString::Printf(TEXT("%d / %d XP"), Lv->GetCurrentXP(), ToNext)));
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
	if (bContentDirty || bHome != bLastHome || App != bLastApp)
	{
		bLastHome = bHome; bLastApp = App; bContentDirty = false;
		RefreshContent();
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

	// Berichten/Contacten open: bij een nieuw bericht of statuswijziging de inhoud herbouwen.
	if (!bHome && (App == 2 || App == 3))
	{
		const int32 Sig = MessagesSignature();
		if (Sig != LastMsgSig) { LastMsgSig = Sig; MarkDirty(); }
		else { UpdateApptBarLive(); UpdateWaitBarLive(); UpdateListBarsLive(); } // balken live bijwerken (zonder herbouw)
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
