#include "UI/WeedUiStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
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

	FString PrettyItemName(FName ItemId)
	{
		FString S = ItemId.ToString();
		if (S.StartsWith(TEXT("WetBud_")))    { return S.RightChop(7) + TEXT(" (wet)"); }
		if (S.StartsWith(TEXT("Bag_")))       { return S.RightChop(4) + TEXT(" bag"); }
		if (S.StartsWith(TEXT("DryRack_")))   { return S.RightChop(8) + TEXT(" rack"); }
		if (S == TEXT("Bench_Pack"))          { return TEXT("Packing bench"); }
		if (S == TEXT("Cont_Bag2"))           { return TEXT("Small baggies"); }
		if (S == TEXT("Cont_Bag5"))           { return TEXT("Big baggies"); }
		if (S == TEXT("Cont_Jar10"))          { return TEXT("Small jars"); }
		if (S == TEXT("Cont_Jar15"))          { return TEXT("Jars"); }
		if (S == TEXT("Cont_Block100"))       { return TEXT("Press blocks"); }
		if (S == TEXT("Cont_Garbage500"))     { return TEXT("Garbage bags"); }
		if (S.StartsWith(TEXT("Bud_")))       { return S.RightChop(4); }
		if (S.StartsWith(TEXT("Seed_")))      { return S.RightChop(5) + TEXT(" seed"); }
		if (S.StartsWith(TEXT("Joint_")))     { return S.RightChop(6) + TEXT(" joint"); }
		if (S.StartsWith(TEXT("Papers_")))    { return S.RightChop(7) + TEXT(" papers"); }
		if (S.StartsWith(TEXT("Soil_")))      { return S.RightChop(5) + TEXT(" soil"); }
		if (S.StartsWith(TEXT("WaterBottle_"))) { return S.RightChop(12) + TEXT(" bottle"); }
		if (S.StartsWith(TEXT("Pot_")))       { return S.RightChop(4) + TEXT(" pot"); }
		return S;
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

			if (ItemId == TEXT("Cash"))                                  return { TEXT("cash"),      FLinearColor(0.35f, 0.85f, 0.45f), EIcon::Coin };
			if (ItemId == TEXT("Atm") || Has(TEXT("Bank")))              return { TEXT("bank"),      FLinearColor(0.45f, 0.8f, 0.55f),  EIcon::Coin };

			// Nat en droog = HETZELFDE hemp-icoon, alleen de kleur verschilt (nat = blauw, droog = groen).
			if (Has(TEXT("WetBud_")))                                    return { TEXT("weed"),      FLinearColor(0.40f, 0.70f, 1.0f),  EIcon::Leaf };
			if (Has(TEXT("Bud_")))                                       return { TEXT("weed"),      FLinearColor(0.45f, 0.95f, 0.55f), EIcon::Leaf };
			if (Has(TEXT("Bag_")))                                       return { TEXT("baggie"),    FLinearColor(0.55f, 0.9f, 0.6f),   EIcon::Leaf };
			if (Has(TEXT("Joint_")))                                     return { TEXT("joint"),     FLinearColor(0.7f, 0.85f, 0.5f),   EIcon::Leaf };
			if (Has(TEXT("Seed_")))                                      return { TEXT("seed"),      FLinearColor(0.6f, 0.8f, 0.45f),   EIcon::Leaf };

			if (Has(TEXT("Cont_")))                                      return { TEXT("packaging"), FLinearColor(0.45f, 0.6f, 0.95f),  EIcon::Shop };
			if (Has(TEXT("Papers_")))                                    return { TEXT("papers"),    FLinearColor(0.7f, 0.7f, 0.85f),   EIcon::Message };
			if (ItemId == TEXT("Bench_Pack"))                            return { TEXT("packaging"), FLinearColor(0.5f, 0.65f, 0.95f),  EIcon::Shop };

			if (Has(TEXT("Soil_")))                                      return { TEXT("soil"),      FLinearColor(0.65f, 0.5f, 0.35f),  EIcon::Leaf };
			if (Has(TEXT("WaterBottle_")) || Has(TEXT("Water")))         return { TEXT("water"),     FLinearColor(0.4f, 0.7f, 0.95f),   EIcon::Coin };
			if (Has(TEXT("Pot_")))                                       return { TEXT("pot"),       FLinearColor(0.6f, 0.55f, 0.45f),  EIcon::Leaf };
			if (Has(TEXT("DryRack_")))                                   return { TEXT("rack"),      FLinearColor(0.55f, 0.6f, 0.7f),   EIcon::Gear };
			if (Has(TEXT("Lamp")) || Has(TEXT("Light")))                 return { TEXT("lamp"),      FLinearColor(0.95f, 0.85f, 0.45f), EIcon::Flame };
			if (Has(TEXT("Tent")))                                       return { TEXT("tent"),      FLinearColor(0.55f, 0.6f, 0.7f),   EIcon::Home };
			if (Has(TEXT("Fert")))                                       return { TEXT("fertilizer"),FLinearColor(0.55f, 0.85f, 0.55f), EIcon::Leaf };
			if (Has(TEXT("Spray")) || Has(TEXT("Pest")))                 return { TEXT("spray"),     FLinearColor(0.6f, 0.9f, 0.7f),   EIcon::Gear };

			// Specifieke meubels (eigen icoon); anders de generieke catch-all.
			if (ItemId == TEXT("Fridge"))                                return { TEXT("fridge"),    FLinearColor(0.6f, 0.7f, 0.8f),    EIcon::Home };
			if (ItemId == TEXT("Mattress") || Has(TEXT("Bed")))          return { TEXT("bed"),       FLinearColor(0.7f, 0.6f, 0.75f),   EIcon::Home };
			if (ItemId == TEXT("Table"))                                 return { TEXT("table"),     FLinearColor(0.6f, 0.5f, 0.4f),    EIcon::Home };
			if (Has(TEXT("Shelf")))                                      return { TEXT("shelf"),     FLinearColor(0.6f, 0.55f, 0.45f),  EIcon::Home };
			if (Has(TEXT("Chest")))                                      return { TEXT("chest"),     FLinearColor(0.6f, 0.5f, 0.35f),   EIcon::Home };

			// Meubels en de rest.
			return { TEXT("furniture"), FLinearColor(0.55f, 0.58f, 0.66f), EIcon::Home };
		}
	}

	FString IconKeyFor(FName ItemId) { return FString(CatFor(ItemId).Key); }

	FLinearColor ItemAccent(FName ItemId) { return CatFor(ItemId).Accent; }

	void PlayUiSound(const UObject* WorldContext, const FString& Key, float Volume)
	{
		if (!WorldContext) { return; }

		// Logische naam -> SoundCue in de Game UI Sound Pack.
		static const TMap<FString, FString> Paths = {
			{ TEXT("click"),   TEXT("/Game/Game_UI_Sound_Pack/Cues/Menu_UI/Brick_button-1_Cue.Brick_button-1_Cue") },
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

		if (Sound) { UGameplayStatics::PlaySound2D(WorldContext, Sound, Volume); }
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

	UTexture2D* ItemIconTexture(FName ItemId)
	{
		if (IconKeyFor(ItemId).IsEmpty()) { return nullptr; }
		for (const FString& Cand : IconCandidatesFor(ItemId))
		{
			if (UTexture2D* Tex = LoadByStem(Cand)) { return Tex; }
		}
		return nullptr;
	}

	UWidget* ItemIcon(UWidgetTree* Tree, FName ItemId, float Size)
	{
		// 1) Echt PNG-icoon als het in Content/_Project/UI/Icons/ staat. We tinten het (witte) icoon
		//    met de categoriekleur zodat alles dezelfde kleurtaal als de rest van de game aanhoudt
		//    (bv. nat = blauw, droog = groen op exact hetzelfde hemp-icoon).
		if (UTexture2D* Tex = ItemIconTexture(ItemId))
		{
			UImage* Img = Tree->ConstructWidget<UImage>();
			FSlateBrush B;
			B.SetResourceObject(Tex);
			B.ImageSize = FVector2D(Size, Size);
			B.DrawAs = ESlateBrushDrawType::Image;
			Img->SetBrush(B);
			Img->SetColorAndOpacity(ItemAccent(ItemId));
			Img->SetVisibility(ESlateVisibility::HitTestInvisible);
			return Img;
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
			B.ImageSize = FVector2D(Size, Size);
			B.DrawAs = ESlateBrushDrawType::Image;
			Img->SetBrush(B);
			Img->SetColorAndOpacity(Tint); // wit PNG -> krijgt de gevraagde kleur
			Img->SetVisibility(ESlateVisibility::HitTestInvisible);
			return Img;
		}
		return IconShape(Tree, Fallback, Size, Tint);
	}

	UWidget* Icon(UWidgetTree* Tree, EIcon Type, float Size, const FLinearColor& Tint)
	{
		return UiGlyph(Tree, FString(UiKeyFor(Type)), Size, Tint, Type);
	}
}
