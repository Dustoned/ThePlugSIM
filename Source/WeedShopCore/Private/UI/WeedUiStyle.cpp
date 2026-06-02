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

	UWidget* Icon(UWidgetTree* Tree, EIcon Type, float Size, const FLinearColor& C)
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

			if (Has(TEXT("WetBud_")))                                    return { TEXT("weed_wet"),  FLinearColor(0.40f, 0.70f, 1.0f),  EIcon::Leaf };
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
			if (Has(TEXT("Spray")) || Has(TEXT("Fert")) || Has(TEXT("Pest"))) return { TEXT("spray"), FLinearColor(0.6f, 0.9f, 0.7f),   EIcon::Gear };

			// Meubels en de rest.
			return { TEXT("furniture"), FLinearColor(0.55f, 0.58f, 0.66f), EIcon::Home };
		}
	}

	FString IconKeyFor(FName ItemId) { return FString(CatFor(ItemId).Key); }

	FLinearColor ItemAccent(FName ItemId) { return CatFor(ItemId).Accent; }

	UTexture2D* ItemIconTexture(FName ItemId)
	{
		const FString Key = IconKeyFor(ItemId);
		if (Key.IsEmpty()) { return nullptr; }

		// Cache (incl. negatieve treffers) zodat we niet elke rebuild de schijf raken. Geladen
		// textures worden ge-root zodat ze niet door de GC worden opgeruimd.
		static TMap<FString, UTexture2D*> Cache;
		if (UTexture2D** Found = Cache.Find(Key)) { return *Found; }

		const FString IconsDir = FPaths::ProjectContentDir() / TEXT("_Project/UI/Icons/");
		const FString Path = IconsDir + Key + TEXT(".png");
		UTexture2D* Tex = nullptr;
		if (IFileManager::Get().FileExists(*Path))
		{
			Tex = FImageUtils::ImportFileAsTexture2D(Path);
			if (Tex) { Tex->AddToRoot(); }
		}
		Cache.Add(Key, Tex);
		return Tex;
	}

	UWidget* ItemIcon(UWidgetTree* Tree, FName ItemId, float Size)
	{
		// 1) Echt PNG-icoon als het in Content/_Project/UI/Icons/ staat.
		if (UTexture2D* Tex = ItemIconTexture(ItemId))
		{
			UImage* Img = Tree->ConstructWidget<UImage>();
			FSlateBrush B;
			B.SetResourceObject(Tex);
			B.ImageSize = FVector2D(Size, Size);
			B.DrawAs = ESlateBrushDrawType::Image;
			Img->SetBrush(B);
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
}
