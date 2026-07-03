#include "UI/WeedUiStyle.h"

#include "Inventory/InventoryComponent.h" // bag-helpers (IsBag/BagGrams/BagStrain) voor naam + badge
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "Cultivation/WaterCanComponent.h"
#include "Cultivation/BottleTypes.h" // fles-vulling (Fill x/y) in de quick-view-body
#include "Cultivation/SoilTypes.h"   // oogsten-per-soil in de quick-view-body
#include "Phone/PhoneClientComponent.h" // geladen-vloei-status voor het paper-icoon + paper-capaciteit/joint-sterkte
#include "Game/WeedShopGameState.h"
#include "Progression/StoreComponent.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include <initializer_list>

namespace
{
	// Eén vorm (UImage met afgeronde brush) op een canvas plaatsen; optioneel geroteerd.
	UImage* AddShape(UWidgetTree* Tree, UCanvasPanel* Canvas, float X, float Y, float W, float H,
		const FLinearColor& Col, float CornerRadius, float AngleDeg = 0.f)
	{
		UImage* Img = Tree->ConstructWidget<UImage>();
		Img->SetBrush(WeedUI::Rounded(Col, CornerRadius));
		UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Img);
		S->SetAutoSize(false);
		S->SetPosition(FVector2D(X, Y));
		S->SetSize(FVector2D(W, H));
		if (AngleDeg != 0.f)
		{
			Img->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			Img->SetRenderTransformAngle(AngleDeg);
		}
		return Img;
	}
}

namespace WeedUI
{
	FSlateBrush Rounded(const FLinearColor& Color, float Radius)
	{
		FSlateBrush B;
		B.DrawAs = ESlateBrushDrawType::RoundedBox;
		B.TintColor = FSlateColor(Color);
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		B.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		return B;
	}

	FSlateFontInfo Font(int32 Size, bool bBold)
	{
		// Project-font = Exo (uit de Minimalist GUI-kit). Eenmalig laden + ge-root; valt terug op de
		// engine-default als het asset er niet is, zodat de UI nooit zonder font komt te zitten.
		static const TCHAR* Base = TEXT("/Game/minimalist_gui/fonts/");
		static UObject* Reg = nullptr; static UObject* Bld = nullptr; static bool bInit = false;
		if (!bInit)
		{
			bInit = true;
			Reg = LoadObject<UObject>(nullptr, *(FString(Base) + TEXT("Exo-Regular_Font.Exo-Regular_Font")));
			Bld = LoadObject<UObject>(nullptr, *(FString(Base) + TEXT("Exo-SemiBold_Font.Exo-SemiBold_Font")));
			if (Reg) { Reg->AddToRoot(); }
			if (Bld) { Bld->AddToRoot(); }
		}
		UObject* F = bBold ? (Bld ? Bld : Reg) : (Reg ? Reg : Bld);
		if (F) { return FSlateFontInfo(F, Size); }
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	UTextBlock* Text(UWidgetTree* Tree, const FString& Txt, int32 Size, const FLinearColor& Col, bool bCenter, bool bBold)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Txt));
		T->SetFont(Font(Size, bBold));
		T->SetColorAndOpacity(FSlateColor(Col));
		if (bCenter) { T->SetJustification(ETextJustify::Center); }
		return T;
	}

	FString ItemInfoBody(FName ItemId, int32 Qty, float Thc, float QualPct)
	{
		// Quick-view-body ZONDER naam-regel (het details-paneel toont de naam al groot erboven).
		// Compact: type-regel -> kern-stats -> hoeveelheid alleen waar de cel-badge dat niet al toont.
		const FString S = ItemId.ToString();
		FString Out;
		auto Add = [&Out](const FString& L) { if (!Out.IsEmpty()) { Out += TEXT("\n"); } Out += L; };

		const bool bWet = S.StartsWith(TEXT("WetBud_"));
		const bool bBud = S.StartsWith(TEXT("Bud_"));
		const bool bBag = UInventoryComponent::IsBag(ItemId);
		const bool bJoint = S.StartsWith(TEXT("Joint_"));
		const bool bCrystal = S.StartsWith(TEXT("Crystal_"));
		const bool bHash = S.StartsWith(TEXT("Hash_"));
		const bool bMoon = S.StartsWith(TEXT("Moonrock_"));
		const bool bRosin = S.StartsWith(TEXT("Rosin_"));
		const bool bBubble = S.StartsWith(TEXT("Bubble_"));
		const bool bConc = bMoon || bRosin || bBubble;
		const bool bSeed = S.StartsWith(TEXT("Seed_"));
		const bool bBottle = S.StartsWith(TEXT("WaterBottle"));
		const bool bSoil = S.StartsWith(TEXT("Soil_"));
		const bool bPapers = S.StartsWith(TEXT("Papers_"));

		FString Type;
		if (bWet)            { Type = TEXT("Wet weed - dry it first"); }
		else if (bBud)       { Type = TEXT("Dried weed"); }
		else if (bBag)       { Type = TEXT("Bagged weed"); }
		else if (bJoint)     { Type = TEXT("Joint"); }
		else if (bCrystal)   { Type = TEXT("THC-crystals"); }
		else if (bHash)      { Type = TEXT("Hasj"); }
		else if (bMoon)      { Type = TEXT("Moonrocks"); }
		else if (bRosin)     { Type = TEXT("Rosin"); }
		else if (bBubble)    { Type = TEXT("Bubble hash"); }
		else if (bSeed)      { Type = TEXT("Seed"); }
		else if (bBottle)    { Type = TEXT("Water bottle"); }
		else if (bSoil)      { Type = TEXT("Soil"); }
		else if (bPapers)    { Type = TEXT("Rolling papers"); }
		else if (S.StartsWith(TEXT("Cont_")))        { Type = TEXT("Packaging"); }
		// (Cash: geen type-regel - de naam "Cash" zegt het al.)
		if (!Type.IsEmpty()) { Add(Type); }

		const bool bWeed = bWet || bBud || bBag || bJoint || bCrystal || bHash || bConc;
		if (bWeed && Thc > 0.f) { Add(FString::Printf(TEXT("THC %.0f%%   Quality %.0f%%"), Thc, QualPct)); }

		// Winkel-catalogus voor strain-stats/omschrijvingen; zelfde GWorld-idioom als PrettyItemName hierboven.
		UStoreComponent* Store = nullptr;
		if (GWorld)
		{
			if (const AWeedShopGameState* GS = GWorld->GetGameState<AWeedShopGameState>()) { Store = GS->GetStore(); }
		}

		if (bJoint)
		{
			// Sterkte = kwaliteit x gram (zelfde formule als roken/verkoop); strain + gram staan al in de naam.
			const int32 JG = UInventoryComponent::JointGrams(ItemId);
			const float Strength = UPhoneClientComponent::JointIntensity(JG, Thc, QualPct) * 100.f;
			Add(FString::Printf(TEXT("Strength %.0f%%  (%dg rolled)"), Strength, JG));
		}
		else if (bSeed)
		{
			// Strain-stats: wat deze seed kan opleveren (zelfde bron als de hand-preview/StoreWidget).
			float SThc = 0.f, SYield = 0.f, SGrow = 0.f;
			if (Store && Store->GetStrainStats(UStoreComponent::StrainFromSeedItem(ItemId), SThc, SYield, SGrow))
			{
				Add(FString::Printf(TEXT("THC up to %.0f%%"), SThc));
				Add(FString::Printf(TEXT("Yield ~%.0fg   Grow ~%.0f min"), SYield, SGrow));
			}
		}
		else if (bBottle)
		{
			// Vulling van DEZE fles: het water zit in het Quality-veld van de stack (zie WaterCanComponent).
			FBottleDef Bd;
			if (GetBottleDef(ItemId, Bd))
			{
				const int32 Fill = FMath::Clamp(FMath::RoundToInt(Thc), 0, Bd.Charges);
				Add(FString::Printf(TEXT("Fill %d / %d waterings"), Fill, Bd.Charges));
				if (Fill <= 0) { Add(TEXT("Empty - refill at a sink")); }
			}
		}
		else if (bSoil)
		{
			// Oogsten-per-toepassing + bonus (zelfde def als de plant-kaart z'n "(%d harvests left)").
			FSoilDef Sd;
			if (GetSoilDef(ItemId, Sd))
			{
				Add(FString::Printf(TEXT("Lasts %d harvests per use"), Sd.Harvests));
				FString Bonus;
				if (Sd.YieldMult > 1.001f)   { Bonus += FString::Printf(TEXT("Yield +%.0f%%"), (Sd.YieldMult - 1.f) * 100.f); }
				if (Sd.QualityMult > 1.001f) { Bonus += FString::Printf(TEXT("%sQuality +%.0f%%"), Bonus.IsEmpty() ? TEXT("") : TEXT("   "), (Sd.QualityMult - 1.f) * 100.f); }
				if (!Bonus.IsEmpty()) { Add(Bonus); }
			}
		}
		else if (bPapers)
		{
			// Welke joint-maat deze papers toestaan (zelfde tabel als GetMaxJointGrams).
			const int32 Cap = UPhoneClientComponent::PaperCapacity(ItemId);
			if (Cap > 0) { Add(FString::Printf(TEXT("Rolls joints up to %dg"), Cap)); }
		}
		else if (!bWeed && ItemId != TEXT("Cash"))
		{
			// Overige items (mest/sprays/gear/machines/verpakking/meubels): de winkel-omschrijving heeft
			// de concrete werking ("+15% yield this harvest") - zelfde patroon als HandInfoWidget.
			if (Store)
			{
				const FString Desc = Store->GetCatalogDesc(ItemId).ToString();
				if (!Desc.IsEmpty()) { Add(Desc); }
			}
		}

		// Hoeveelheid alleen waar de cel-badge dat niet al glashelder toont: zakjes krijgen de totaal-som;
		// losse grammen ("12g") en "xN"-stapels staan al op de badge en dubbelen we niet nogmaals.
		if (bBag)
		{
			const int32 G = FMath::Max(1, UInventoryComponent::BagGrams(ItemId));
			Add(FString::Printf(TEXT("%d bag(s) x %dg  =  %dg"), Qty, G, Qty * G));
		}
		else if (S == TEXT("Cash")) { Add(FString::Printf(TEXT("€%d in cash"), Qty)); }
		return Out;
	}

	FString ItemTooltip(FName ItemId, int32 Qty, float Thc, float QualPct)
	{
		// Naam + body: voor zwevende tooltips die los van het details-paneel staan (hotbar e.d.).
		const FString Body = ItemInfoBody(ItemId, Qty, Thc, QualPct);
		FString Out = PrettyItemName(ItemId);
		if (!Body.IsEmpty()) { Out += TEXT("\n") + Body; }
		return Out;
	}

	FString ItemQtyBadge(FName ItemId, int32 Qty)
	{
		if (UInventoryComponent::IsBag(ItemId))
		{
			const int32 G = UInventoryComponent::BagGrams(ItemId);
			return (Qty > 1) ? FString::Printf(TEXT("%dx %dg"), Qty, G) : FString::Printf(TEXT("%dg"), G);
		}
		const FString S = ItemId.ToString();
		if (S == TEXT("Cash")) { return FString::Printf(TEXT("€%d"), Qty); } // briefgeld: toon het bedrag (Qty = euro's)
		if (S.StartsWith(TEXT("Bud_")) || S.StartsWith(TEXT("WetBud_")) || S.StartsWith(TEXT("Crystal_")) || S.StartsWith(TEXT("Hash_"))
			|| S.StartsWith(TEXT("Baked_")) || S.StartsWith(TEXT("ButterMix_")) || S.StartsWith(TEXT("Edible_"))
			|| S.StartsWith(TEXT("Moonrock_")) || S.StartsWith(TEXT("Rosin_")) || S.StartsWith(TEXT("Bubble_"))) { return FString::Printf(TEXT("%dg"), Qty); }
		return (Qty > 1) ? FString::Printf(TEXT("x%d"), Qty) : FString();
	}

	// Splits camelCase strain-namen netjes: "SilverHaze" -> "Silver Haze", "WeddingCake" -> "Wedding Cake".
	static FString NiceName(const FString& In)
	{
		FString Out;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (i > 0 && FChar::IsUpper(C) && !FChar::IsUpper(In[i - 1]) && In[i - 1] != TEXT(' ')) { Out.AppendChar(TEXT(' ')); }
			Out.AppendChar(C);
		}
		return Out;
	}

	FString PrettyItemName(FName ItemId)
	{
		FString S = ItemId.ToString();
		if (S == TEXT("Cash"))                { return TEXT("Cash"); }
		if (S.StartsWith(TEXT("WetBud_")))    { return NiceName(S.RightChop(7)) + TEXT(" (wet)"); }
		if (S.StartsWith(TEXT("Bag_")))       { return NiceName(UInventoryComponent::BagStrain(ItemId).ToString()) + TEXT(" bag"); }
		if (S.StartsWith(TEXT("DryRack_")))   { return S.RightChop(8) + TEXT(" rack"); }
		if (S == TEXT("Bench_Pack"))          { return TEXT("Packing bench"); }
		if (S == TEXT("Cont_Bag2"))           { return TEXT("Small baggies"); }
		if (S == TEXT("Cont_Bag5"))           { return TEXT("Big baggies"); }
		if (S == TEXT("Cont_Jar10"))          { return TEXT("Small jars"); }
		if (S == TEXT("Cont_Jar15"))          { return TEXT("Jars"); }
		if (S == TEXT("Cont_Block100"))       { return TEXT("Press blocks"); }
		if (S == TEXT("Cont_Garbage500"))     { return TEXT("Garbage bags"); }
		if (S == TEXT("DryUp_FanSmall"))      { return TEXT("Small drying fan"); }
		if (S == TEXT("DryUp_Fan"))           { return TEXT("Drying fan"); }
		if (S == TEXT("DryUp_Seal"))          { return TEXT("Humidity sealer"); }
		if (S == TEXT("ProcUp_Motor"))        { return TEXT("Power motor"); }
		if (S == TEXT("ProcUp_Yield"))        { return TEXT("Fine filter"); }
		if (S == TEXT("Oven_Std"))            { return TEXT("Oven / stove"); }
		if (S == TEXT("Pan_Std"))             { return TEXT("Cooking pan"); }
		if (S == TEXT("Fridge_Std"))          { return TEXT("Fridge conversion"); }
		if (S == TEXT("Oven_Pro"))            { return TEXT("Pro range cooker"); }
		if (S == TEXT("Pan_Pro"))             { return TEXT("Pro cooktop"); }
		if (S == TEXT("Fridge_Pro"))          { return TEXT("Walk-in fridge"); }
		if (S == TEXT("Butter"))              { return TEXT("Butter"); }
		if (S.StartsWith(TEXT("Crystal_")))   { return NiceName(S.RightChop(8)) + TEXT(" crystals"); }
		if (S.StartsWith(TEXT("Hash_")))      { return NiceName(S.RightChop(5)) + TEXT(" hash"); }
		if (S.StartsWith(TEXT("Baked_")))     { return TEXT("Baked ") + NiceName(S.RightChop(6)); }
		if (S.StartsWith(TEXT("ButterMix_"))) { return NiceName(S.RightChop(10)) + TEXT(" butter mix"); }
		if (S.StartsWith(TEXT("Edible_")))    { return NiceName(S.RightChop(7)) + TEXT(" cannabutter"); }
		if (S.StartsWith(TEXT("Cookie_")))    { return NiceName(S.RightChop(7)) + TEXT(" cookies"); }
		if (S.StartsWith(TEXT("Gummy_")))     { return NiceName(S.RightChop(6)) + TEXT(" gummies"); }
		if (S.StartsWith(TEXT("Moonrock_")))  { return NiceName(S.RightChop(9)) + TEXT(" moonrocks"); }
		if (S.StartsWith(TEXT("Rosin_")))     { return NiceName(S.RightChop(6)) + TEXT(" rosin"); }
		if (S.StartsWith(TEXT("Bubble_")))    { return NiceName(S.RightChop(7)) + TEXT(" bubble hash"); }
		if (S.StartsWith(TEXT("Oil_")))       { return NiceName(S.RightChop(4)) + TEXT(" oil"); }
		if (S.StartsWith(TEXT("Bud_")))       { return NiceName(S.RightChop(4)); }
		if (S.StartsWith(TEXT("Seed_")))      { return NiceName(S.RightChop(5)) + TEXT(" seed"); }
		if (S.StartsWith(TEXT("Joint_")))
		{
			// Nieuw: Joint_<Strain>_<G>g -> "Silver Haze 3g joint". Oud: Joint_<G>g -> "3g joint".
			const FName JStrain = UInventoryComponent::JointStrain(ItemId);
			const int32 JG = UInventoryComponent::JointGrams(ItemId);
			if (!JStrain.IsNone()) { return NiceName(JStrain.ToString()) + FString::Printf(TEXT(" %dg joint"), JG); }
			return FString::Printf(TEXT("%dg joint"), JG);
		}
		if (S.StartsWith(TEXT("Papers_")))    { return S.RightChop(7) + TEXT(" papers"); }
		if (S.StartsWith(TEXT("Soil_")))      { return S.RightChop(5) + TEXT(" soil"); }
		if (S.StartsWith(TEXT("WaterBottle_"))) { return S.RightChop(12) + TEXT(" bottle"); }
		if (S.StartsWith(TEXT("Pot_")))       { return S.RightChop(4) + TEXT(" pot"); }

		// Onbekende id (Gear_/Mesh_/Press_ e.d.) -> pak de nette naam uit de winkel-catalogus.
		if (GWorld)
		{
			if (const AWeedShopGameState* GS = GWorld->GetGameState<AWeedShopGameState>())
			{
				if (UStoreComponent* St = GS->GetStore())
				{
					const FString Nm = St->GetCatalogName(ItemId).ToString();
					if (!Nm.IsEmpty()) { return Nm; }
				}
			}
		}
		return NiceName(S);
	}

	// Korte, duidelijke TAG op het icoon (strain voor wiet/seeds, variant voor soil/pot/etc.) zodat je
	// items met hetzelfde icoon uit elkaar houdt: OG seeds vs Silver Haze seeds, Basic vs Premium soil.
	// Leeg voor items met een uniek icoon (die hebben geen tag nodig).
	FString ItemTag(FName ItemId)
	{
		const FString S = ItemId.ToString();
		auto After = [&S](int32 N) { return NiceName(S.RightChop(N)); };
		if (S.StartsWith(TEXT("Seed_")))        { return After(5); }
		if (S.StartsWith(TEXT("WetBud_")))      { return After(7); }
		if (S.StartsWith(TEXT("Bud_")))         { return After(4); }
		if (S.StartsWith(TEXT("Bag_")))         { return NiceName(UInventoryComponent::BagStrain(ItemId).ToString()); }
		if (S.StartsWith(TEXT("Joint_")))       { const FName JS = UInventoryComponent::JointStrain(ItemId); return JS.IsNone() ? FString::Printf(TEXT("%dg"), UInventoryComponent::JointGrams(ItemId)) : NiceName(JS.ToString()); }
		if (S.StartsWith(TEXT("Crystal_")))     { return After(8); }
		if (S.StartsWith(TEXT("Hash_")))        { return After(5); }
		if (S.StartsWith(TEXT("Baked_")))       { return After(6); }
		if (S.StartsWith(TEXT("ButterMix_")))   { return After(10); }
		if (S.StartsWith(TEXT("Edible_")))      { return After(7); }
		if (S.StartsWith(TEXT("Cookie_")))      { return After(7); }
		if (S.StartsWith(TEXT("Gummy_")))       { return After(6); }
		if (S.StartsWith(TEXT("Rosin_")))       { return After(6); }
		if (S.StartsWith(TEXT("Bubble_")))      { return After(7); }
		if (S.StartsWith(TEXT("Moonrock_")))    { return After(9); }
		if (S.StartsWith(TEXT("Oil_")))         { return After(4); }
		if (S.StartsWith(TEXT("Soil_")))        { return After(5); }
		if (S.StartsWith(TEXT("Pot_")))         { return After(4); }
		if (S.StartsWith(TEXT("WaterBottle_"))) { return After(12); }
		if (S.StartsWith(TEXT("Fertilizer_")))  { return After(11); }
		if (S.StartsWith(TEXT("Spray_")))       { return After(6); }
		if (S.StartsWith(TEXT("Papers_")))      { return After(7); }
		if (S.StartsWith(TEXT("DryRack_")))     { return After(8); }
		return FString();
	}

	// KORTE bubble-code (UPPERCASE, ~2-4 tekens) op het icoon. Zie header.
	FString ItemTagShort(FName ItemId)
	{
		const FString S = ItemId.ToString();
		auto Suf = [&S](int32 N) { return S.RightChop(N); };

		// Strain -> canonieke wiet-afkorting (fallback: hoofdletters/cijfers, anders eerste 3 letters).
		auto StrainCode = [](const FString& St) -> FString
		{
			static const TMap<FString, FString> M = {
				{TEXT("Streetweed"),TEXT("STR")}, {TEXT("CriticalMass"),TEXT("CM")}, {TEXT("SilverHaze"),TEXT("SH")},
				{TEXT("BlueDream"),TEXT("BD")}, {TEXT("NorthernLights"),TEXT("NL")}, {TEXT("BigBud"),TEXT("BB")},
				{TEXT("WhiteWidow"),TEXT("WW")}, {TEXT("JackHerer"),TEXT("JH")}, {TEXT("SourDiesel"),TEXT("SD")},
				{TEXT("PineappleExpress"),TEXT("PE")}, {TEXT("AmnesiaHaze"),TEXT("AH")}, {TEXT("OGKush"),TEXT("OG")},
				{TEXT("BubbaKush"),TEXT("BK")}, {TEXT("DurbanPoison"),TEXT("DP")}, {TEXT("GorillaGlue"),TEXT("GG")},
				{TEXT("PurpleHaze"),TEXT("PH")}, {TEXT("GirlScoutCookies"),TEXT("GSC")}, {TEXT("CookiesCream"),TEXT("CC")},
				{TEXT("WeddingCake"),TEXT("WC")}, {TEXT("Gelato"),TEXT("GEL")}, {TEXT("Mimosa"),TEXT("MIM")},
				{TEXT("Runtz"),TEXT("RTZ")}, {TEXT("AppleFritter"),TEXT("AF")}, {TEXT("Zkittlez"),TEXT("ZKZ")},
				{TEXT("GaryPayton"),TEXT("GP")},
			};
			if (const FString* F = M.Find(St)) { return *F; }
			FString Caps;
			for (TCHAR c : St) { if (FChar::IsUpper(c) || FChar::IsDigit(c)) { Caps.AppendChar(c); } }
			return (Caps.Len() >= 2) ? Caps.Left(3) : St.Left(3).ToUpper();
		};
		// Tier-woord -> rank.
		auto Tier = [](const FString& V) -> FString
		{
			if (V == TEXT("Cheap") || V == TEXT("Basic"))   { return TEXT("I"); }
			if (V == TEXT("Std")   || V == TEXT("Rich"))    { return TEXT("II"); }
			if (V == TEXT("Pro")   || V == TEXT("Premium")) { return TEXT("III"); }
			return FString();
		};
		auto IsTierWord = [](const FString& V)
		{
			return V == TEXT("Cheap") || V == TEXT("Std") || V == TEXT("Pro")
				|| V == TEXT("Basic") || V == TEXT("Rich") || V == TEXT("Premium");
		};
		auto Map4 = [](const FString& V, const TCHAR* a, const TCHAR* ca, const TCHAR* b, const TCHAR* cb,
			const TCHAR* c, const TCHAR* cc, const TCHAR* d, const TCHAR* cd) -> FString
		{
			if (V == a) return ca; if (V == b) return cb; if (V == c) return cc; if (V == d) return cd;
			return FString();
		};

		// --- Wiet-producten: strain-code ---
		if (S.StartsWith(TEXT("Seed_")))      { return StrainCode(Suf(5)); }
		if (S.StartsWith(TEXT("WetBud_")))    { return StrainCode(Suf(7)); }
		if (S.StartsWith(TEXT("Bud_")))       { return StrainCode(Suf(4)); }
		if (S.StartsWith(TEXT("Bag_")))       { return StrainCode(UInventoryComponent::BagStrain(ItemId).ToString()); }
		if (S.StartsWith(TEXT("Joint_")))     { const FName JS = UInventoryComponent::JointStrain(ItemId); return JS.IsNone() ? FString::Printf(TEXT("%dg"), UInventoryComponent::JointGrams(ItemId)) : StrainCode(JS.ToString()); }
		if (S.StartsWith(TEXT("Crystal_")))   { return StrainCode(Suf(8)); }
		if (S.StartsWith(TEXT("Hash_")))      { return StrainCode(Suf(5)); }
		if (S.StartsWith(TEXT("Baked_")))     { return StrainCode(Suf(6)); }
		if (S.StartsWith(TEXT("ButterMix_"))) { return StrainCode(Suf(10)); }
		if (S.StartsWith(TEXT("Edible_")))    { return StrainCode(Suf(7)); }
		if (S.StartsWith(TEXT("Cookie_")))    { return StrainCode(Suf(7)); }
		if (S.StartsWith(TEXT("Gummy_")))     { return StrainCode(Suf(6)); }
		if (S.StartsWith(TEXT("Moonrock_")))  { return StrainCode(Suf(9)); }
		if (S.StartsWith(TEXT("Bubble_")))    { return StrainCode(Suf(7)); }
		// Rosin_/Oil_ zijn BEIDE product (strain) en machine (tier): onderscheid via het tier-woord.
		if (S.StartsWith(TEXT("Rosin_")))     { return IsTierWord(Suf(6)) ? Tier(Suf(6)) : StrainCode(Suf(6)); }
		if (S.StartsWith(TEXT("Oil_")))       { return IsTierWord(Suf(4)) ? Tier(Suf(4)) : StrainCode(Suf(4)); }

		// --- Teelt-varianten ---
		if (S.StartsWith(TEXT("Soil_")))        { return Tier(Suf(5)); }
		if (S.StartsWith(TEXT("Pot_")))         { return Map4(Suf(4), TEXT("Broken"),TEXT("BRK"), TEXT("Clay"),TEXT("CLY"), TEXT("Plastic"),TEXT("PLA"), TEXT("Fabric"),TEXT("FAB")); }
		if (S.StartsWith(TEXT("WaterBottle_"))) { return Map4(Suf(12), TEXT("Plastic"),TEXT("PLA"), TEXT("Steel"),TEXT("STL"), TEXT("Jerrycan"),TEXT("JRY"), TEXT("Tank"),TEXT("TNK")); }
		if (S.StartsWith(TEXT("Fertilizer_")))  { const FString V = Suf(11); return V==TEXT("Basic") ? FString(TEXT("BSC")) : (V==TEXT("Bloom") ? FString(TEXT("BLM")) : FString()); }
		if (S.StartsWith(TEXT("Spray_")))       { const FString V = Suf(6); return V==TEXT("Fungicide")?FString(TEXT("FNG")):(V==TEXT("Pesticide")?FString(TEXT("PST")):(V==TEXT("Broad")?FString(TEXT("BRD")):FString())); }

		// --- Consumables / opslag ---
		if (S.StartsWith(TEXT("Papers_")))      { return Map4(Suf(7), TEXT("Small"),TEXT("SM"), TEXT("Big"),TEXT("BIG"), TEXT("Blunt"),TEXT("BLT"), TEXT("Backwoods"),TEXT("BWD")); }
		if (S.StartsWith(TEXT("Safe_")))        { return Map4(Suf(5), TEXT("Small"),TEXT("S"), TEXT("Medium"),TEXT("M"), TEXT("Large"),TEXT("L"), TEXT("Vault"),TEXT("VLT")); }
		if (S.StartsWith(TEXT("Cont_")))
		{
			const FString V = Suf(5);
			if (V==TEXT("Bag2")) return TEXT("2g");   if (V==TEXT("Bag5")) return TEXT("5g");
			if (V==TEXT("Jar10")) return TEXT("10g"); if (V==TEXT("Jar15")) return TEXT("15g");
			if (V==TEXT("Block100")) return TEXT("100g"); if (V==TEXT("Garbage500")) return TEXT("500g");
			return FString();
		}
		if (S.StartsWith(TEXT("DryRack_")))     { return Tier(Suf(8)); }

		// --- Pot-gear upgrades ---
		if (S.StartsWith(TEXT("Gear_")))
		{
			const FString V = Suf(5);
			if (V==TEXT("Drainage")) return TEXT("DRN"); if (V==TEXT("Insulation")) return TEXT("INS"); if (V==TEXT("Bloom")) return TEXT("BLM");
			if (V.StartsWith(TEXT("Lamp")))  return TEXT("L") + V.RightChop(4);
			if (V.StartsWith(TEXT("Tent")))  return TEXT("T") + V.RightChop(4);
			if (V.StartsWith(TEXT("Water"))) return TEXT("W") + V.RightChop(5);
			return FString();
		}
		if (S.StartsWith(TEXT("DryUp_")))       { const FString V = Suf(6); return V==TEXT("Fan")?FString(TEXT("FAN")):(V==TEXT("Seal")?FString(TEXT("SEL")):FString()); }
		if (S.StartsWith(TEXT("ProcUp_")))      { const FString V = Suf(7); return V==TEXT("Motor")?FString(TEXT("MOT")):(V==TEXT("Yield")?FString(TEXT("YLD")):FString()); }

		// --- Machine-tiers (Cheap/Std/Pro of Std/Pro) -> rank ---
		if (S.StartsWith(TEXT("Mesh_")))   { return Tier(Suf(5)); }
		if (S.StartsWith(TEXT("Press_")))  { return Tier(Suf(6)); }
		if (S.StartsWith(TEXT("Oven_")))   { return Tier(Suf(5)); }
		if (S.StartsWith(TEXT("Pan_")))    { return Tier(Suf(4)); }
		if (S.StartsWith(TEXT("Fridge_"))) { return Tier(Suf(7)); }
		if (S.StartsWith(TEXT("Iso_")))    { return Tier(Suf(4)); }
		if (S.StartsWith(TEXT("Moon_")))   { return Tier(Suf(5)); }

		// --- Packing-bench tiers ---
		if (S == TEXT("Bench_Pack"))  { return TEXT("I"); }
		if (S == TEXT("Bench_Pack2")) { return TEXT("II"); }
		if (S == TEXT("Bench_Pack3")) { return TEXT("III"); }

		// --- Bouw-stukken ---
		if (S.StartsWith(TEXT("Struct_")))
		{
			const FString V = Suf(7);
			if (V==TEXT("Wall4m")) return TEXT("W4"); if (V==TEXT("Wall2m")) return TEXT("W2"); if (V==TEXT("Wall1m")) return TEXT("W1");
			if (V==TEXT("WallDoor4m")) return TEXT("D4"); if (V==TEXT("WallDoor3m")) return TEXT("D3");
			if (V==TEXT("Floor4x4")) return TEXT("F4"); if (V==TEXT("Floor1x1")) return TEXT("F1");
			if (V==TEXT("Ceil4x4")) return TEXT("C4"); if (V==TEXT("Ceil1x1")) return TEXT("C1");
			if (V==TEXT("CeilLamp")) return TEXT("LMP"); if (V==TEXT("Door")) return TEXT("DR");
			return FString();
		}

		return FString();
	}

	// Het uit vormen opgebouwde icoon (fallback als er geen PNG is).
	static UWidget* IconShape(UWidgetTree* Tree, EIcon Type, float Size, const FLinearColor& C)
	{
		UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::HitTestInvisible);
		const float S = Size;
		const float R = S * 0.5f; // volledige cirkel-radius
		const FLinearColor Dim(C.R * 0.6f, C.G * 0.6f, C.B * 0.6f, C.A);

		switch (Type)
		{
		case EIcon::Coin:
			AddShape(Tree, Canvas, 0, 0, S, S, C, R);                                  // buitenrand
			AddShape(Tree, Canvas, S * 0.18f, S * 0.18f, S * 0.64f, S * 0.64f, Dim, S * 0.32f); // binnenvlak
			AddShape(Tree, Canvas, S * 0.44f, S * 0.30f, S * 0.12f, S * 0.40f, C, S * 0.06f);    // "munt-streep"
			break;
		case EIcon::Clock:
			AddShape(Tree, Canvas, 0, 0, S, S, C, R);
			AddShape(Tree, Canvas, S * 0.16f, S * 0.16f, S * 0.68f, S * 0.68f, Dim, R);
			AddShape(Tree, Canvas, S * 0.47f, S * 0.28f, S * 0.06f, S * 0.26f, C, S * 0.03f);     // minuutwijzer
			AddShape(Tree, Canvas, S * 0.50f, S * 0.47f, S * 0.22f, S * 0.06f, C, S * 0.03f);     // uurwijzer
			break;
		case EIcon::Flame:
			AddShape(Tree, Canvas, S * 0.22f, S * 0.22f, S * 0.56f, S * 0.56f, C, S * 0.16f, 45.f); // ruit
			AddShape(Tree, Canvas, S * 0.34f, S * 0.34f, S * 0.32f, S * 0.32f, FLinearColor(1.f, 0.9f, 0.6f, C.A), S * 0.10f, 45.f);
			break;
		case EIcon::Level:
		case EIcon::Upgrade:
			// Chevron omhoog uit twee schuine balkjes.
			AddShape(Tree, Canvas, S * 0.16f, S * 0.42f, S * 0.42f, S * 0.16f, C, S * 0.08f, -45.f);
			AddShape(Tree, Canvas, S * 0.42f, S * 0.42f, S * 0.42f, S * 0.16f, C, S * 0.08f, 45.f);
			break;
		case EIcon::Leaf:
			AddShape(Tree, Canvas, S * 0.20f, S * 0.20f, S * 0.60f, S * 0.60f, C, S * 0.30f, 45.f); // blad/ruit
			AddShape(Tree, Canvas, S * 0.46f, S * 0.30f, S * 0.08f, S * 0.45f, Dim, S * 0.04f);      // nerf
			break;
		case EIcon::Shop:
			AddShape(Tree, Canvas, S * 0.30f, S * 0.14f, S * 0.40f, S * 0.16f, C, S * 0.08f);        // handvat
			AddShape(Tree, Canvas, S * 0.34f, S * 0.16f, S * 0.32f, S * 0.12f, Dim, S * 0.06f);
			AddShape(Tree, Canvas, S * 0.20f, S * 0.30f, S * 0.60f, S * 0.54f, C, S * 0.12f);        // tas
			break;
		case EIcon::Person:
			AddShape(Tree, Canvas, S * 0.34f, S * 0.12f, S * 0.32f, S * 0.32f, C, R);                // hoofd
			AddShape(Tree, Canvas, S * 0.22f, S * 0.50f, S * 0.56f, S * 0.40f, C, S * 0.22f);         // lichaam
			break;
		case EIcon::Message:
			AddShape(Tree, Canvas, S * 0.12f, S * 0.18f, S * 0.76f, S * 0.52f, C, S * 0.16f);         // bubbel
			AddShape(Tree, Canvas, S * 0.22f, S * 0.62f, S * 0.18f, S * 0.18f, C, S * 0.04f, 45.f);    // staartje
			break;
		case EIcon::Gear:
			AddShape(Tree, Canvas, S * 0.10f, S * 0.10f, S * 0.80f, S * 0.80f, C, R);                 // buiten
			AddShape(Tree, Canvas, S * 0.30f, S * 0.30f, S * 0.40f, S * 0.40f, Dim, R);               // gat
			break;
		case EIcon::Map:
			AddShape(Tree, Canvas, S * 0.30f, S * 0.12f, S * 0.40f, S * 0.40f, C, R);                 // pin-kop
			AddShape(Tree, Canvas, S * 0.40f, S * 0.42f, S * 0.20f, S * 0.20f, C, S * 0.04f, 45.f);    // punt
			AddShape(Tree, Canvas, S * 0.43f, S * 0.24f, S * 0.14f, S * 0.14f, Dim, R);               // gaatje
			break;
		case EIcon::Home:
			AddShape(Tree, Canvas, S * 0.50f, S * 0.10f, S * 0.40f, S * 0.40f, C, S * 0.05f, 45.f);    // dak (ruit-helft)
			AddShape(Tree, Canvas, S * 0.24f, S * 0.40f, S * 0.52f, S * 0.46f, C, S * 0.05f);          // huis
			AddShape(Tree, Canvas, S * 0.42f, S * 0.58f, S * 0.16f, S * 0.28f, Dim, S * 0.02f);        // deur
			break;
		case EIcon::Box:
			AddShape(Tree, Canvas, S * 0.16f, S * 0.26f, S * 0.68f, S * 0.58f, C, S * 0.06f);          // doos-body
			AddShape(Tree, Canvas, S * 0.16f, S * 0.26f, S * 0.68f, S * 0.15f, Dim, S * 0.04f);        // bovenflap (schaduw)
			AddShape(Tree, Canvas, S * 0.44f, S * 0.26f, S * 0.12f, S * 0.58f, Dim, S * 0.02f);        // verticale tape
			break;
		}
		return Canvas;
	}

	// --- Item-iconen ---------------------------------------------------------

	// Interne categorie-indeling: bepaalt sleutel (PNG-bestandsnaam), accentkleur en de
	// fallback-glyph als er nog geen PNG is.
	namespace
	{
		struct FItemCat { const TCHAR* Key; FLinearColor Accent; EIcon Glyph; };

		FItemCat CatFor(FName ItemId)
		{
			const FString S = ItemId.ToString();
			auto Has = [&S](const TCHAR* P) { return S.StartsWith(P); };
			auto IsTier = [&S]() { return S.EndsWith(TEXT("_Std")) || S.EndsWith(TEXT("_Pro")) || S.EndsWith(TEXT("_Cheap")); };

			if (ItemId == TEXT("Cash"))                                  return { TEXT("cash"),      FLinearColor(0.35f, 0.85f, 0.45f), EIcon::Coin };
			if (ItemId == TEXT("Atm") || Has(TEXT("Bank")))              return { TEXT("bank"),      FLinearColor(0.45f, 0.8f, 0.55f),  EIcon::Coin };

			// Nat en droog = HETZELFDE hemp-icoon, alleen de kleur verschilt (nat = blauw, droog = groen).
			if (Has(TEXT("WetBud_")))                                    return { TEXT("weed"),      FLinearColor(0.40f, 0.70f, 1.0f),  EIcon::Leaf };
			if (Has(TEXT("Crystal_")))                                   return { TEXT("crystals"),  FLinearColor(0.7f, 0.92f, 1.0f),   EIcon::Leaf };
			if (Has(TEXT("Hash_")))                                      return { TEXT("hash"),      FLinearColor(0.75f, 0.55f, 0.3f),  EIcon::Leaf };
			if (Has(TEXT("Bud_")))                                       return { TEXT("weed"),      FLinearColor(0.45f, 0.95f, 0.55f), EIcon::Leaf };
			if (Has(TEXT("Bag_")))                                       return { TEXT("baggie"),    FLinearColor(0.55f, 0.9f, 0.6f),   EIcon::Leaf };
			if (Has(TEXT("Joint_")))                                     return { TEXT("joint"),     FLinearColor(0.7f, 0.85f, 0.5f),   EIcon::Leaf };
			if (Has(TEXT("Seed_")))                                      return { TEXT("seed"),      FLinearColor(0.6f, 0.8f, 0.45f),   EIcon::Leaf };

			// Edibles-keten.
			if (Has(TEXT("Baked_")))                                     return { TEXT("baked"),     FLinearColor(0.55f, 0.42f, 0.20f), EIcon::Leaf };
			if (Has(TEXT("ButterMix_")))                                 return { TEXT("mix"),       FLinearColor(0.85f, 0.72f, 0.30f), EIcon::Leaf };
			if (Has(TEXT("Edible_")))                                    return { TEXT("edible"),    FLinearColor(0.80f, 0.62f, 0.25f), EIcon::Leaf };
			if (ItemId == TEXT("Butter"))                                return { TEXT("butter"),    FLinearColor(0.95f, 0.85f, 0.35f), EIcon::Shop };
			if (Has(TEXT("Cookie_")))                                    return { TEXT("cookie"),    FLinearColor(0.80f, 0.55f, 0.30f), EIcon::Leaf };
			if (Has(TEXT("Gummy_")))                                     return { TEXT("gummy"),     FLinearColor(0.95f, 0.45f, 0.55f), EIcon::Leaf };

			// Concentraat-PRODUCTEN (de machine met dezelfde naam eindigt op _Std/_Pro/_Cheap; deze niet).
			if (Has(TEXT("Bubble_")))                                    return { TEXT("bubble"),    FLinearColor(0.55f, 0.80f, 0.95f), EIcon::Leaf }; // ice/bubble hash
			if (Has(TEXT("Moonrock_")))                                  return { TEXT("moonrock"),  FLinearColor(0.62f, 0.55f, 0.80f), EIcon::Leaf };
			if (Has(TEXT("Rosin_")) && !IsTier())                        return { TEXT("rosin"),     FLinearColor(0.95f, 0.80f, 0.40f), EIcon::Leaf };
			if (Has(TEXT("Oil_"))   && !IsTier())                        return { TEXT("oil"),       FLinearColor(0.90f, 0.75f, 0.35f), EIcon::Coin };

			if (Has(TEXT("Cont_")))                                      return { TEXT("packaging"), FLinearColor(0.45f, 0.6f, 0.95f),  EIcon::Shop };
			if (Has(TEXT("Papers_")))                                    return { TEXT("papers"),    FLinearColor(0.7f, 0.7f, 0.85f),   EIcon::Message };
			if (Has(TEXT("Bench_")))                                     return { TEXT("bench"),     FLinearColor(0.62f, 0.5f, 0.38f),  EIcon::Home };

			if (Has(TEXT("Soil_")))                                      return { TEXT("soil"),      FLinearColor(0.65f, 0.5f, 0.35f),  EIcon::Leaf };
			if (Has(TEXT("WaterBottle_")) || Has(TEXT("Water")))         return { TEXT("water"),     FLinearColor(0.4f, 0.7f, 0.95f),   EIcon::Coin };
			if (Has(TEXT("Pot_")))                                       return { TEXT("pot"),       FLinearColor(0.6f, 0.55f, 0.45f),  EIcon::Leaf };
			if (Has(TEXT("DryRack_")))                                   return { TEXT("rack"),      FLinearColor(0.55f, 0.6f, 0.7f),   EIcon::Gear };
			if (Has(TEXT("Lamp")) || Has(TEXT("Light")))                 return { TEXT("lamp"),      FLinearColor(0.95f, 0.85f, 0.45f), EIcon::Flame };
			if (Has(TEXT("Tent")))                                       return { TEXT("tent"),      FLinearColor(0.55f, 0.6f, 0.7f),   EIcon::Home };
			if (Has(TEXT("Fert")))                                       return { TEXT("fertilizer"),FLinearColor(0.55f, 0.85f, 0.55f), EIcon::Leaf };
			if (Has(TEXT("Spray")) || Has(TEXT("Pest")))                 return { TEXT("spray"),     FLinearColor(0.6f, 0.9f, 0.7f),   EIcon::Gear };

			// Gootsteen -> water-icoon (i.p.v. het generieke meubel).
			if (ItemId == TEXT("Sink"))                                  return { TEXT("faucet"),    FLinearColor(0.4f, 0.7f, 0.95f),   EIcon::Coin };
			// Kluis/vault -> kist-icoon (opslag/slot).
			if (Has(TEXT("Safe")) || Has(TEXT("Vault")))                 return { TEXT("safe"),      FLinearColor(0.6f, 0.62f, 0.68f),  EIcon::Home };
			// Bouwstukken (muren/vloeren/plafonds) -> blok-icoon.
			if (Has(TEXT("Struct_")))                                    return { TEXT("block"),     FLinearColor(0.6f, 0.62f, 0.66f),  EIcon::Gear };
			// Gear-upgrades & machine-upgrades -> upgrade-icoon (chevron/pijl).
			// Upgrades kregen allemaal hetzelfde chevron; route ze nu naar een passend (vaak bestaand) icoon.
			if (Has(TEXT("Gear_Lamp")))                                  return { TEXT("growlamp"),  FLinearColor(0.95f, 0.85f, 0.45f), EIcon::Flame };
			if (Has(TEXT("Gear_Tent")))                                  return { TEXT("growtent"),  FLinearColor(0.55f, 0.6f, 0.7f),   EIcon::Home };
			if (Has(TEXT("Gear_Water")))                                 return { TEXT("drop"),      FLinearColor(0.4f, 0.7f, 0.95f),   EIcon::Coin };
			if (Has(TEXT("Gear_Bloom")))                                 return { TEXT("bloom"),     FLinearColor(0.95f, 0.55f, 0.7f),  EIcon::Leaf };
			if (Has(TEXT("Gear_Drainage")))                              return { TEXT("drainage"),  FLinearColor(0.65f, 0.5f, 0.35f),  EIcon::Leaf };
			if (Has(TEXT("Gear_Insulation")))                            return { TEXT("insulation"),FLinearColor(0.7f, 0.75f, 0.85f),  EIcon::Upgrade };
			if (Has(TEXT("DryUp_FanSmall")))                             return { TEXT("fan"),       FLinearColor(0.6f, 0.8f, 0.9f),    EIcon::Gear };
			if (Has(TEXT("DryUp_Fan")))                                  return { TEXT("fan"),       FLinearColor(0.6f, 0.8f, 0.9f),    EIcon::Gear };
			if (Has(TEXT("DryUp_Seal")))                                 return { TEXT("seal"),      FLinearColor(0.7f, 0.75f, 0.85f),  EIcon::Upgrade };
			if (Has(TEXT("ProcUp_Motor")))                               return { TEXT("motor"),     FLinearColor(0.7f, 0.74f, 0.8f),   EIcon::Gear };
			if (Has(TEXT("ProcUp_Yield")))                               return { TEXT("filter"),    FLinearColor(0.7f, 0.74f, 0.8f),   EIcon::Gear };
			if (Has(TEXT("Gear_")) || Has(TEXT("DryUp_")) || Has(TEXT("ProcUp_")))
			                                                             return { TEXT("upgrade"),   FLinearColor(0.6f, 0.85f, 0.95f),  EIcon::Upgrade };
			// Verwerkings-machines: elk een eigen icoon (deze items zijn altijd tiers, _Std/_Pro/_Cheap).
			const FLinearColor MachineCol(0.70f, 0.74f, 0.80f);
			if (Has(TEXT("Mesh_")))                                      return { TEXT("mesh"),        MachineCol, EIcon::Gear };
			if (Has(TEXT("Press")) || Has(TEXT("Heatpress")))           return { TEXT("press"),       MachineCol, EIcon::Gear };
			if (Has(TEXT("Oven")))                                       return { TEXT("oven"),        MachineCol, EIcon::Gear };
			if (Has(TEXT("Pan_")))                                       return { TEXT("pan"),         MachineCol, EIcon::Gear };
			if (Has(TEXT("Rosin")))                                      return { TEXT("rosinpress"),  MachineCol, EIcon::Gear };
			if (Has(TEXT("Iso_")))                                       return { TEXT("icehash"),     MachineCol, EIcon::Gear };
			if (Has(TEXT("Oil_")))                                       return { TEXT("oilpress"),    MachineCol, EIcon::Gear };
			if (Has(TEXT("Moon")))                                       return { TEXT("mixstation"),  MachineCol, EIcon::Gear };
			if (Has(TEXT("Fridge_")))                                    return { TEXT("fridge"),      FLinearColor(0.6f, 0.7f, 0.8f), EIcon::Home };
			// Kledingkast -> meubel-icoon (eigen kleur).
			if (Has(TEXT("Wardrobe")))                                   return { TEXT("wardrobe"),  FLinearColor(0.62f, 0.55f, 0.7f),  EIcon::Home };

			// Specifieke meubels (eigen icoon); anders de generieke catch-all.
			if (ItemId == TEXT("Fridge"))                                return { TEXT("fridge"),    FLinearColor(0.6f, 0.7f, 0.8f),    EIcon::Home };
			if (ItemId == TEXT("Mattress") || Has(TEXT("Bed")))          return { TEXT("bed"),       FLinearColor(0.7f, 0.6f, 0.75f),   EIcon::Home };
			if (ItemId == TEXT("Table"))                                 return { TEXT("table"),     FLinearColor(0.6f, 0.5f, 0.4f),    EIcon::Home };
			if (Has(TEXT("Shelf")))                                      return { TEXT("shelf"),     FLinearColor(0.6f, 0.55f, 0.45f),  EIcon::Home };
			if (Has(TEXT("Chest")))                                      return { TEXT("chest"),     FLinearColor(0.6f, 0.5f, 0.35f),   EIcon::Home };

			// NIEUWE HUISKAMER-MEUBELS: elk een eigen icoon (i.p.v. allemaal hetzelfde placeholder-meubel).
			const FLinearColor Wood(0.62f, 0.5f, 0.38f), Grey(0.6f, 0.58f, 0.66f), Green(0.55f, 0.9f, 0.55f);
			if (ItemId == TEXT("Furn_ChairPlastic"))                     return { TEXT("office-chair"),      Grey,  EIcon::Home };
			if (ItemId == TEXT("Furn_ChairGarden"))                      return { TEXT("director-chair"),    Grey,  EIcon::Home };
			if (ItemId == TEXT("Furn_ChairWood"))                        return { TEXT("wooden-chair"),      Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_TableSmall"))                       return { TEXT("table"),             Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_TableRound"))                       return { TEXT("round-table"),       Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_CoffeeTable"))                      return { TEXT("table"),             Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_Desk"))                             return { TEXT("desk"),              Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_Bench"))                            return { TEXT("park-bench"),        Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_Sofa"))                             return { TEXT("sofa"),              FLinearColor(0.7f, 0.6f, 0.55f), EIcon::Home };
			if (ItemId == TEXT("Furn_TV"))                               return { TEXT("tv"),                FLinearColor(0.35f, 0.35f, 0.4f), EIcon::Home };
			if (ItemId == TEXT("Furn_TVStand"))                          return { TEXT("desk"),              Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_Bookshelf"))                        return { TEXT("bookshelf"),         Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_Dresser"))                          return { TEXT("wardrobe"),          FLinearColor(0.62f, 0.55f, 0.7f), EIcon::Home };
			if (ItemId == TEXT("Furn_Nightstand"))                       return { TEXT("chest"),             Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_FloorLamp"))                        return { TEXT("flexible-lamp"),     FLinearColor(0.95f, 0.85f, 0.45f), EIcon::Flame };
			if (ItemId == TEXT("Furn_Plant"))                            return { TEXT("carnivorous-plant"), Green, EIcon::Home };
			if (ItemId == TEXT("Furn_Planter"))                          return { TEXT("plant-roots"),       Green, EIcon::Home };
			if (ItemId == TEXT("Furn_DecoPot"))                          return { TEXT("pot"),               Wood,  EIcon::Home };
			if (ItemId == TEXT("Furn_Crate"))                            return { TEXT("cargo-crate"),       FLinearColor(0.7f, 0.65f, 0.5f), EIcon::Home };

			// Meubels en de rest.
			return { TEXT("furniture"), FLinearColor(0.55f, 0.58f, 0.66f), EIcon::Home };
		}
	}

	FString IconKeyFor(FName ItemId) { return FString(CatFor(ItemId).Key); }

	FLinearColor ItemAccent(FName ItemId) { return CatFor(ItemId).Accent; }

	FLinearColor TagColor(const FString& Tag, float Value, float Sat)
	{
		// Stabiele, GOED GESPREIDE hue uit de tag-string. De extra avalanche-mixing (lowbias32) voorkomt dat korte
		// codes als STR/SH/SM/SD toevallig vlak bij elkaar in de hue-cirkel uitkomen -> elke strain duidelijk eigen kleur.
		uint32 Hh = GetTypeHash(Tag);
		Hh ^= Hh >> 16; Hh *= 0x7feb352du; Hh ^= Hh >> 15; Hh *= 0x846ca68bu; Hh ^= Hh >> 16;
		const float Hue = float(Hh % 360u);
		return FLinearColor(Hue, FMath::Clamp(Sat, 0.f, 1.f), FMath::Clamp(Value, 0.f, 1.f), 1.f).HSVToLinearRGB();
	}

	FLinearColor TagColorForItem(FName ItemId, float Value, float Sat)
	{
		// Tag-pill kleur op ITEM-niveau: alleen echte wiet-/strain-producten krijgen de levendige per-strain hue.
		// Standaard-spul (potten, flessen, grond, mest, sprays, machine-tiers, gereedschap) krijgt een NEUTRALE
		// grijze pill - anders krijgen die betekenisloze hash-kleuren die toevallig op elkaar lijken (PLA/STR/SM-klacht).
		const FString S = ItemId.ToString();
		auto IsTierWord = [](const FString& V) { return V==TEXT("Cheap")||V==TEXT("Std")||V==TEXT("Pro")||V==TEXT("Basic")||V==TEXT("Rich")||V==TEXT("Premium"); };
		static const TCHAR* StrainPrefix[] = {
			TEXT("Seed_"), TEXT("WetBud_"), TEXT("Bud_"), TEXT("Bag_"), TEXT("Joint_"), TEXT("Crystal_"),
			TEXT("Hash_"), TEXT("Baked_"), TEXT("ButterMix_"), TEXT("Edible_"), TEXT("Cookie_"),
			TEXT("Gummy_"), TEXT("Moonrock_"), TEXT("Bubble_"),
		};
		bool bStrain = false;
		for (const TCHAR* P : StrainPrefix) { if (S.StartsWith(P)) { bStrain = true; break; } }
		if (!bStrain && S.StartsWith(TEXT("Rosin_"))) { bStrain = !IsTierWord(S.RightChop(6)); } // Rosin_/Oil_ = product (strain) OF machine (tier)
		if (!bStrain && S.StartsWith(TEXT("Oil_")))   { bStrain = !IsTierWord(S.RightChop(4)); }
		if (bStrain) { return TagColor(ItemTagShort(ItemId), Value, Sat); }
		return FLinearColor(0.33f, 0.35f, 0.41f, 1.f); // neutrale koel-grijze pill voor standaard-items
	}

	FSlateBrush KitBrush(const FString& TexturePath, const FMargin& NineSlice, const FLinearColor& Tint)
	{
		FSlateBrush B;
		if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *TexturePath))
		{
			B.SetResourceObject(Tex);
			B.DrawAs = ESlateBrushDrawType::Box; // 9-slice -> hoeken/rand+schaduw blijven scherp, midden rekt
			B.Margin = NineSlice;
			B.TintColor = FSlateColor(Tint);
			B.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
		}
		else { B = Rounded(Tint, 12.f); }
		return B;
	}

	FLinearColor Hex(uint32 RGB, float Alpha)
	{
		return FLinearColor(FColor((RGB >> 16) & 0xFF, (RGB >> 8) & 0xFF, RGB & 0xFF, 255)).CopyWithNewOpacity(Alpha);
	}

	static const TCHAR* SoundCatKey(int32 Category)
	{
		// 3 = VolWeather (weer-ambience/donder-volume; DayNightController leest deze categorie).
		switch (Category) { case 1: return TEXT("VolGame"); case 2: return TEXT("VolMusic"); case 3: return TEXT("VolWeather"); default: return TEXT("VolUI"); }
	}

	float SoundCategoryVolume(int32 Category)
	{
		float V = 1.f; // alle sliders standaard op 100%
		if (GConfig) { GConfig->GetFloat(TEXT("ThePlugSIM.Audio"), SoundCatKey(Category), V, GGameUserSettingsIni); }
		return FMath::Clamp(V, 0.f, 1.f);
	}

	void SetSoundCategoryVolume(int32 Category, float Volume)
	{
		if (GConfig)
		{
			GConfig->SetFloat(TEXT("ThePlugSIM.Audio"), SoundCatKey(Category), FMath::Clamp(Volume, 0.f, 1.f), GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}

	void PlayUiSound(const UObject* WorldContext, const FString& Key, float Volume, int32 Category)
	{
		if (!WorldContext) { return; }
		Volume *= SoundCategoryVolume(Category); // categorie-volume uit de instellingen
		if (Volume <= 0.001f) { return; }

		// Logische naam -> SoundCue in de Game UI Sound Pack.
		static const TMap<FString, FString> Paths = {
			{ TEXT("click"),   TEXT("/Game/Game_UI_Sound_Pack/Cues/Menu_UI/Reel_Stick-1_Cue.Reel_Stick-1_Cue") },
			{ TEXT("levelup"), TEXT("/Game/Game_UI_Sound_Pack/Cues/Game_Level/SpaceLevelUp-1_Cue.SpaceLevelUp-1_Cue") },
			{ TEXT("cash"),    TEXT("/Game/Game_UI_Sound_Pack/Cues/Highlight/Cash_Out-1_Cue.Cash_Out-1_Cue") },
			{ TEXT("coin"),    TEXT("/Game/Game_UI_Sound_Pack/Cues/Menu_UI/Classic_coin-1_Cue.Classic_coin-1_Cue") },
			{ TEXT("error"),   TEXT("/Game/Game_UI_Sound_Pack/Cues/Highlight/Attempts_failed-1_Cue.Attempts_failed-1_Cue") },
			{ TEXT("open"),    TEXT("/Game/Game_UI_Sound_Pack/Cues/Menu_UI/Dimensional_door-1_Cue.Dimensional_door-1_Cue") },
		};

		// Geladen cues cachen (incl. negatieve treffer) + ge-root zodat de GC ze niet opruimt.
		static TMap<FString, USoundBase*> Cache;
		USoundBase* Sound = nullptr;
		if (USoundBase** Found = Cache.Find(Key))
		{
			Sound = *Found;
		}
		else
		{
			if (const FString* P = Paths.Find(Key))
			{
				Sound = LoadObject<USoundBase>(nullptr, **P);
				if (Sound) { Sound->AddToRoot(); }
			}
			Cache.Add(Key, Sound);
		}

		// Echt 2D UI-geluid: niet-ruimtelijk (geen 3D/galm-feel), als UI-sound gemarkeerd.
		if (Sound)
		{
			if (UAudioComponent* AC = UGameplayStatics::SpawnSound2D(WorldContext, Sound, Volume, 1.f, 0.f, nullptr, false, true))
			{
				AC->bIsUISound = true;
				AC->bAllowSpatialization = false;
			}
		}
	}

	namespace
	{
		// Mogelijke bestandsnamen (zonder pad/extensie, kleine letters) voor dit item, primair
		// eerst. Zo werkt zowel "weed.png" als de pack-eigen naam (bv. "documents" voor vloei).
		TArray<FString> IconCandidatesFor(FName ItemId)
		{
			const FString K = FString(CatFor(ItemId).Key);
			TArray<FString> C; C.AddUnique(K);
			auto Add = [&C](std::initializer_list<const TCHAR*> Xs) { for (const TCHAR* X : Xs) { C.AddUnique(FString(X)); } };
			if      (K == TEXT("cash"))      Add({ TEXT("money"), TEXT("wallet") });
			else if (K == TEXT("bank"))      Add({ TEXT("atm") });
			else if (K == TEXT("weed"))      Add({ TEXT("bud"), TEXT("drugs"), TEXT("cannabis") });
			else if (K == TEXT("weed_wet"))  Add({ TEXT("wetweed"), TEXT("weed"), TEXT("drugs") });
			else if (K == TEXT("baggie"))    Add({ TEXT("bag"), TEXT("packaging"), TEXT("weed") });
			else if (K == TEXT("joint"))     Add({ TEXT("blunt"), TEXT("drugs"), TEXT("weed") });
			else if (K == TEXT("seed"))      Add({ TEXT("seeds"), TEXT("weed") });
			else if (K == TEXT("packaging")) Add({ TEXT("package"), TEXT("box"), TEXT("supply") });
			else if (K == TEXT("papers"))    Add({ TEXT("paper"), TEXT("documents"), TEXT("document") });
			else if (K == TEXT("soil"))      Add({ TEXT("dirt"), TEXT("supply") });
			else if (K == TEXT("water"))     Add({ TEXT("bottle") });
			else if (K == TEXT("pot"))       Add({ TEXT("plantpot"), TEXT("supply"), TEXT("garden") });
			else if (K == TEXT("rack"))      Add({ TEXT("dryrack"), TEXT("repairs"), TEXT("drying") });
			else if (K == TEXT("lamp"))      Add({ TEXT("light"), TEXT("boost"), TEXT("energy") });
			else if (K == TEXT("tent"))      Add({ TEXT("garage"), TEXT("stash") });
			else if (K == TEXT("spray"))     Add({ TEXT("pesticide"), TEXT("tuning"), TEXT("repairs") });
			else if (K == TEXT("fertilizer"))Add({ TEXT("fertilizer-bag"), TEXT("spray"), TEXT("nutrients") });
			else if (K == TEXT("fridge"))    Add({ TEXT("furniture") });
			else if (K == TEXT("bed"))       Add({ TEXT("furniture") });
			else if (K == TEXT("table"))     Add({ TEXT("furniture") });
			else if (K == TEXT("shelf"))     Add({ TEXT("bookshelf"), TEXT("furniture") });
			else if (K == TEXT("chest"))     Add({ TEXT("furniture") });
			else if (K == TEXT("safe"))      Add({ TEXT("chest"), TEXT("bank"), TEXT("furniture") });
			else if (K == TEXT("block"))     Add({ TEXT("furniture") });
			else if (K == TEXT("upgrade"))   Add({ TEXT("ui_upgrade"), TEXT("gear"), TEXT("ui_gear") });
			else if (K == TEXT("machine"))   Add({ TEXT("press"), TEXT("oven"), TEXT("ui_gear") });
			else if (K == TEXT("mesh"))      Add({ TEXT("ui_gear") });
			else if (K == TEXT("oven"))      Add({ TEXT("ui_gear") });
			else if (K == TEXT("pan"))       Add({ TEXT("ui_gear") });
			else if (K == TEXT("rosinpress")) Add({ TEXT("press"), TEXT("ui_gear") });
			else if (K == TEXT("icehash"))   Add({ TEXT("ui_gear") });
			else if (K == TEXT("oilpress"))  Add({ TEXT("press"), TEXT("ui_gear") });
			else if (K == TEXT("mixstation")) Add({ TEXT("ui_gear") });
			else if (K == TEXT("bubble"))    Add({ TEXT("hash"), TEXT("crystals"), TEXT("weed") });
			else if (K == TEXT("rosin"))     Add({ TEXT("hash"), TEXT("crystals"), TEXT("weed") });
			else if (K == TEXT("moonrock"))  Add({ TEXT("crystals"), TEXT("weed") });
			else if (K == TEXT("oil"))       Add({ TEXT("crystals"), TEXT("weed") });
			else if (K == TEXT("drop"))      Add({ TEXT("water") });
			else if (K == TEXT("faucet"))    Add({ TEXT("water") });
			else if (K == TEXT("growlamp"))  Add({ TEXT("lamp"), TEXT("ui_upgrade") });
			// Huiskamer-meubels: terugvallen op een generiek meubel-icoon als de eigen PNG ontbreekt.
			else if (K == TEXT("office-chair") || K == TEXT("director-chair") || K == TEXT("wooden-chair")) Add({ TEXT("furniture") });
			else if (K == TEXT("round-table") || K == TEXT("desk"))       Add({ TEXT("table"), TEXT("furniture") });
			else if (K == TEXT("park-bench"))                             Add({ TEXT("bench"), TEXT("furniture") });
			else if (K == TEXT("sofa"))                                  Add({ TEXT("furniture") });
			else if (K == TEXT("tv"))                                    Add({ TEXT("furniture") });
			else if (K == TEXT("bookshelf"))                             Add({ TEXT("shelf"), TEXT("furniture") });
			else if (K == TEXT("flexible-lamp"))                         Add({ TEXT("lamp"), TEXT("furniture") });
			else if (K == TEXT("carnivorous-plant") || K == TEXT("plant-roots")) Add({ TEXT("furniture") });
			else if (K == TEXT("cargo-crate"))                           Add({ TEXT("furniture") });
			else if (K == TEXT("growtent"))  Add({ TEXT("tent"), TEXT("ui_upgrade") });
			else if (K == TEXT("drainage"))  Add({ TEXT("soil"), TEXT("ui_upgrade") });
			else if (K == TEXT("bloom"))     Add({ TEXT("fertilizer"), TEXT("weed") });
			else if (K == TEXT("insulation")) Add({ TEXT("ui_upgrade"), TEXT("upgrade") });
			else if (K == TEXT("fan"))       Add({ TEXT("ui_gear") });
			else if (K == TEXT("seal"))      Add({ TEXT("ui_upgrade"), TEXT("upgrade") });
			else if (K == TEXT("motor"))     Add({ TEXT("ui_gear") });
			else if (K == TEXT("filter"))    Add({ TEXT("mesh"), TEXT("ui_gear") });
			else if (K == TEXT("hash"))      Add({ TEXT("ui_hash"), TEXT("weed") });
			else if (K == TEXT("crystals"))  Add({ TEXT("weed") });
			else if (K == TEXT("butter"))    Add({ TEXT("edible"), TEXT("furniture") });
			else if (K == TEXT("mix"))       Add({ TEXT("butter"), TEXT("edible") });
			else if (K == TEXT("baked"))     Add({ TEXT("edible"), TEXT("weed") });
			else if (K == TEXT("edible"))    Add({ TEXT("baked"), TEXT("weed") });
			else if (K == TEXT("cookie"))    Add({ TEXT("baked"), TEXT("edible"), TEXT("weed") });
			else if (K == TEXT("gummy"))     Add({ TEXT("edible"), TEXT("baked"), TEXT("weed") });
			else if (K == TEXT("wardrobe"))  Add({ TEXT("furniture") });
			else if (K == TEXT("furniture")) Add({ TEXT("stash"), TEXT("inventory"), TEXT("box") });
			return C;
		}

		// Index van losgelaten PNG's: kleine-letter-stam -> volledig pad. Inclusief stammen zonder
		// veelvoorkomende suffixen (_default, _256, ...), zodat hoofdletters/suffixen niet uitmaken.
		const TMap<FString, FString>& IconFileIndex()
		{
			static TMap<FString, FString> Index;
			static bool bBuilt = false;
			if (bBuilt) { return Index; }
			bBuilt = true;

			const FString IconsDir = FPaths::ProjectContentDir() / TEXT("_Project/UI/Icons/");
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(IconsDir + TEXT("*.png")), true, false);
			static const TCHAR* Suffixes[] = { TEXT("_default"), TEXT("-default"), TEXT("_white"), TEXT("_256"), TEXT("_128"), TEXT("_icon"), TEXT("_normal") };
			for (const FString& F : Files)
			{
				const FString Full = IconsDir + F;
				FString Stem = FPaths::GetBaseFilename(F).ToLower();
				Index.Add(Stem, Full);
				for (const TCHAR* Suf : Suffixes)
				{
					if (Stem.EndsWith(Suf)) { Index.Add(Stem.LeftChop(FCString::Strlen(Suf)), Full); }
				}
			}
			return Index;
		}

		// Laad (en cache) een icoon-texture op kleine-letter-stam; nullptr als die er niet is.
		// Geladen textures worden ge-root zodat de GC ze niet opruimt.
		UTexture2D* LoadByStem(const FString& Stem)
		{
			static TMap<FString, UTexture2D*> Cache;
			const FString Key = Stem.ToLower();
			if (UTexture2D** Found = Cache.Find(Key)) { return *Found; }
			UTexture2D* Tex = nullptr;
			if (const FString* Path = IconFileIndex().Find(Key))
			{
				Tex = FImageUtils::ImportFileAsTexture2D(*Path);
				if (Tex) { Tex->AddToRoot(); }
			}
			Cache.Add(Key, Tex);
			return Tex;
		}
	}

	// Per-ITEM icoon (heeft voorrang op de categorie) zodat tiers niet hetzelfde icoon delen:
	// elke container apart, en de packing-bench een eigen icoon i.p.v. het packaging-icoon.
	static FString ExactIconStem(FName ItemId)
	{
		const FString S = ItemId.ToString();
		if (S.StartsWith(TEXT("WetBud_")))  { return TEXT("weed_wet"); } // natte wiet: bud + druppel
		// Afgewerkte joint: eigen plaatje (joint_rolled.png). joint.png is "handen die rollen" en hoort bij
		// de GELADEN paper (zie ItemIconTexture), niet bij de klaar-joint.
		if (S.StartsWith(TEXT("Joint_")))   { return TEXT("joint_rolled"); }
		// Verpakte wiet (Bag_<strain>_<gram>): GEVULDE container op gram-formaat (zakje / pot / sack).
		if (S.StartsWith(TEXT("Bag_")))
		{
			const int32 G = UInventoryComponent::BagGrams(ItemId);
			if (G > 0)
			{
				if (G <= 5)   { return TEXT("weed_bag"); }   // gevuld zakje (Bag2/Bag5)
				if (G <= 50)  { return TEXT("weed_jar"); }   // gevulde pot/jar (Jar10/Jar15)
				if (G <= 100) { return TEXT("block"); }      // geperste block 100g (wiet-brick)
				return TEXT("weed_sack");                    // bulk vuilniszak 500g (Garbage500)
			}
		}
		if (S.StartsWith(TEXT("Bench_")))  { return TEXT("bench"); }
		// Lege EN gevulde containers gebruiken dezelfde speler-bag/jar-iconen; leeg vs vol is het
		// kleurverschil (Cont_ = packaging-blauwe tint, Bag_ = wiet-groene tint).
		if (S == TEXT("Cont_Bag2"))        { return TEXT("weed_bag"); }
		if (S == TEXT("Cont_Bag5"))        { return TEXT("weed_bag"); }
		if (S == TEXT("Cont_Jar10"))       { return TEXT("weed_jar"); }
		if (S == TEXT("Cont_Jar15"))       { return TEXT("weed_jar"); }
		if (S == TEXT("Cont_Block100"))    { return TEXT("block"); }     // pers-blok 100g
		if (S == TEXT("Cont_Garbage500"))  { return TEXT("weed_sack"); } // bulk vuilniszak = het sack-icoon
		return FString();
	}

	UTexture2D* ItemIconTexture(FName ItemId, int32 WaterChargesOverride)
	{
		if (IconKeyFor(ItemId).IsEmpty()) { return nullptr; }
		// Per-item override eerst (tiers met een eigen icoon).
		{
			const FString Exact = ExactIconStem(ItemId);
			if (!Exact.IsEmpty()) { if (UTexture2D* T = LoadByStem(Exact)) { return T; } }
		}
		// Waterfles: toon VOL of LEEG. Override (>=0) = het water van DEZE specifieke fles (per slot);
		// anders val terug op de actieve fles van de speler.
		if (FString(CatFor(ItemId).Key) == TEXT("water"))
		{
			bool bEmpty = true;
			if (WaterChargesOverride >= 0)
			{
				bEmpty = (WaterChargesOverride <= 0);
			}
			else if (UWorld* W = GWorld)
			{
				if (APawn* P = UGameplayStatics::GetPlayerPawn(W, 0))
				{
					if (const UWaterCanComponent* Can = P->FindComponentByClass<UWaterCanComponent>())
					{
						bEmpty = (Can->GetCharges() <= 0);
					}
				}
			}
			if (UTexture2D* T = LoadByStem(bEmpty ? TEXT("bottle_empty") : TEXT("bottle"))) { return T; }
		}
		// Geladen vloei: heeft de speler weed in de paper geladen (klaar om te rollen), toon dan het
		// "handen die rollen"-icoon (joint.png) i.p.v. het losse boekje (papers.png) - zo zie je dat 'ie
		// nog gerold moet worden. Terug naar het boekje zodra 'ie gerold of leeg is.
		if (FString(CatFor(ItemId).Key) == TEXT("papers"))
		{
			if (UWorld* W = GWorld)
			{
				if (APawn* P = UGameplayStatics::GetPlayerPawn(W, 0))
				{
					if (UPhoneClientComponent* Ph = P->FindComponentByClass<UPhoneClientComponent>())
					{
						if (Ph->IsRollLoadedUI()) { if (UTexture2D* T2 = LoadByStem(TEXT("joint"))) { return T2; } }
					}
				}
			}
		}
		for (const FString& Cand : IconCandidatesFor(ItemId))
		{
			if (UTexture2D* Tex = LoadByStem(Cand)) { return Tex; }
		}
		return nullptr;
	}

	UWidget* ItemIcon(UWidgetTree* Tree, FName ItemId, float Size, int32 WaterChargesOverride)
	{
		// 1) Echt PNG-icoon als het in Content/_Project/UI/Icons/ staat. We tinten het (witte) icoon
		//    met de categoriekleur zodat alles dezelfde kleurtaal als de rest van de game aanhoudt
		//    (bv. nat = blauw, droog = groen op exact hetzelfde hemp-icoon).
		if (UTexture2D* Tex = ItemIconTexture(ItemId, WaterChargesOverride))
		{
			const FLinearColor Accent = ItemAccent(ItemId);
			FSlateBrush B;
			B.SetResourceObject(Tex);
			// Aspect-ratio + wat marge -> consistente, professionele schaal (nooit gestretcht, nooit tot de rand).
			const float TW = FMath::Max(1.f, (float)Tex->GetSizeX());
			const float TH = FMath::Max(1.f, (float)Tex->GetSizeY());
			const float Sc = (Size * 0.94f) / FMath::Max(TW, TH);
			B.ImageSize = FVector2D(TW * Sc, TH * Sc);
			B.DrawAs = ESlateBrushDrawType::Image;

			UImage* Img = Tree->ConstructWidget<UImage>();
			Img->SetBrush(B);
			// Heldere, BIJPASSENDE categoriekleur (geen wit) -> duidelijk en herkenbaar op de donkere slot.
			Img->SetColorAndOpacity(FMath::Lerp(Accent, FLinearColor::White, 0.22f));
			Img->SetVisibility(ESlateVisibility::HitTestInvisible);
			// ScaleToFit: behoudt ALTIJD de aspect-ratio, ook als de container (een SizeBox met vaste
			// vierkante maat) anders de brush zou uitrekken. Zo nooit meer gestretcht in hoogte/breedte.
			UScaleBox* Fit = Tree->ConstructWidget<UScaleBox>();
			Fit->SetStretch(EStretch::ScaleToFit);
			Fit->AddChild(Img);
			Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
			return Fit;
		}

		// 2) Nette procedurele tegel: gekleurde afgeronde achtergrond + flat glyph in dezelfde tint.
		const FItemCat Cat = CatFor(ItemId);
		const FLinearColor BgCol(Cat.Accent.R * 0.22f, Cat.Accent.G * 0.22f, Cat.Accent.B * 0.22f, 1.f);
		UBorder* Tile = Tree->ConstructWidget<UBorder>();
		Tile->SetBrush(Rounded(BgCol, Size * 0.24f));
		Tile->SetHorizontalAlignment(HAlign_Center);
		Tile->SetVerticalAlignment(VAlign_Center);
		Tile->SetVisibility(ESlateVisibility::HitTestInvisible);
		Tile->SetContent(Icon(Tree, Cat.Glyph, Size * 0.6f, Cat.Accent));
		return Tile;
	}

	// EIcon -> PNG-bestandsnaam (ui_<naam>.png). Drop zo'n PNG in Icons/ en alle plekken die dit
	// EIcon gebruiken (telefoon-apps, HUD, kompas, save-indicator) pakken 'm automatisch op.
	static const TCHAR* UiKeyFor(EIcon T)
	{
		switch (T)
		{
		case EIcon::Coin:    return TEXT("ui_coin");
		case EIcon::Clock:   return TEXT("ui_clock");
		case EIcon::Flame:   return TEXT("ui_flame");
		case EIcon::Level:   return TEXT("ui_level");
		case EIcon::Leaf:    return TEXT("ui_leaf");
		case EIcon::Upgrade: return TEXT("ui_upgrade");
		case EIcon::Shop:    return TEXT("ui_shop");
		case EIcon::Person:  return TEXT("ui_person");
		case EIcon::Message: return TEXT("ui_message");
		case EIcon::Gear:    return TEXT("ui_gear");
		case EIcon::Map:     return TEXT("ui_map");
		case EIcon::Home:    return TEXT("ui_home");
		case EIcon::Box:     return TEXT("ui_box");
		}
		return TEXT("");
	}

	UWidget* UiGlyph(UWidgetTree* Tree, const FString& Key, float Size, const FLinearColor& Tint, EIcon Fallback)
	{
		UTexture2D* Tex = Key.IsEmpty() ? nullptr : LoadByStem(Key);
		if (!Tex && Key.StartsWith(TEXT("ui_"))) { Tex = LoadByStem(Key.RightChop(3)); } // ook zonder ui_-prefix
		if (Tex)
		{
			UImage* Img = Tree->ConstructWidget<UImage>();
			FSlateBrush B;
			B.SetResourceObject(Tex);
			// Aspect-ratio behouden (langste zijde = Size) -> portret-glyphs zoals de telefoon worden NIET
			// tot een vierkant uitgerekt.
			const float TW = FMath::Max(1.f, (float)Tex->GetSizeX());
			const float TH = FMath::Max(1.f, (float)Tex->GetSizeY());
			const float Sc = Size / FMath::Max(TW, TH);
			B.ImageSize = FVector2D(TW * Sc, TH * Sc);
			B.DrawAs = ESlateBrushDrawType::Image;
			Img->SetBrush(B);
			Img->SetColorAndOpacity(Tint); // wit PNG -> krijgt de gevraagde kleur
			Img->SetVisibility(ESlateVisibility::HitTestInvisible);
			// ScaleToFit: behoudt ALTIJD de aspect-ratio, ook als de plek (een SizeBox of canvas-slot met
			// vaste vierkante maat) de brush anders zou uitrekken. Eén bron-fix voor alle gestretchte iconen.
			UScaleBox* Fit = Tree->ConstructWidget<UScaleBox>();
			Fit->SetStretch(EStretch::ScaleToFit);
			Fit->AddChild(Img);
			Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
			return Fit;
		}
		return IconShape(Tree, Fallback, Size, Tint);
	}

	UWidget* Icon(UWidgetTree* Tree, EIcon Type, float Size, const FLinearColor& Tint)
	{
		return UiGlyph(Tree, FString(UiKeyFor(Type)), Size, Tint, Type);
	}

	UWidget* KitIcon(UWidgetTree* Tree, const FString& Name, float Size, const FLinearColor& Tint)
	{
		static const FString Base = TEXT("/Game/dark-gui-main-menu-pro-kit---complete-solution--honeti/dark-gui-main-menu-pro-kit---complete-solution--honeti/ultimate_dark_gui/textures/ThemeCommon/Textures/Icons/128/");
		UImage* Img = Tree->ConstructWidget<UImage>();
		Img->SetVisibility(ESlateVisibility::HitTestInvisible);
		const FString Path = Name.StartsWith(TEXT("/")) ? Name : (Base + Name + TEXT(".") + Name);
		if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path))
		{
			Img->SetBrushFromTexture(Tex, false);
			Img->SetBrushSize(FVector2D(Size, Size));
			Img->SetColorAndOpacity(Tint);
		}
		return Img;
	}
}
