#include "UI/WeedUiStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

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
}
