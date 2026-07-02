#include "UI/WeedUiAuthoring.h"

#if WITH_EDITOR

#include "UI/WeedUiStyle.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"

#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Texture2D.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	// "r,g,b,a" -> FLinearColor (a optioneel, default 1).
	FLinearColor ParseColor(const FString& S, const FLinearColor& Def)
	{
		TArray<FString> P; S.ParseIntoArray(P, TEXT(","));
		if (P.Num() < 3) { return Def; }
		return FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]),
			P.Num() >= 4 ? FCString::Atof(*P[3]) : 1.f);
	}

	// "x,y" -> FVector2D.
	FVector2D ParseVec2(const FString& S, const FVector2D& Def)
	{
		TArray<FString> P; S.ParseIntoArray(P, TEXT(","));
		if (P.Num() < 2) { return Def; }
		return FVector2D(FCString::Atof(*P[0]), FCString::Atof(*P[1]));
	}

	// "l,t,r,b" -> FMargin (1 waarde = uniform).
	FMargin ParseMargin(const FString& S)
	{
		TArray<FString> P; S.ParseIntoArray(P, TEXT(","));
		if (P.Num() >= 4) { return FMargin(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3])); }
		if (P.Num() == 1) { const float V = FCString::Atof(*P[0]); return FMargin(V); }
		return FMargin(0.f);
	}

	UClass* ResolveWidgetClass(const FString& Type)
	{
		if (Type == TEXT("CanvasPanel"))   { return UCanvasPanel::StaticClass(); }
		if (Type == TEXT("VerticalBox"))   { return UVerticalBox::StaticClass(); }
		if (Type == TEXT("HorizontalBox")) { return UHorizontalBox::StaticClass(); }
		if (Type == TEXT("Overlay"))       { return UOverlay::StaticClass(); }
		if (Type == TEXT("ScrollBox"))     { return UScrollBox::StaticClass(); }
		if (Type == TEXT("SizeBox"))       { return USizeBox::StaticClass(); }
		if (Type == TEXT("Border"))        { return UBorder::StaticClass(); }
		if (Type == TEXT("TextBlock"))     { return UTextBlock::StaticClass(); }
		if (Type == TEXT("Image"))         { return UImage::StaticClass(); }
		if (Type == TEXT("Button"))        { return UButton::StaticClass(); }
		if (Type == TEXT("ProgressBar"))   { return UProgressBar::StaticClass(); }
		return nullptr;
	}

	void ApplyProps(UWidget* W, const TSharedPtr<FJsonObject>& Props)
	{
		if (!W || !Props.IsValid()) { return; }
		FString Str; double Num = 0.0; bool bB = false;

		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (Props->TryGetStringField(TEXT("text"), Str)) { T->SetText(FText::FromString(Str)); }
			const int32 FontSize = Props->HasField(TEXT("fontSize")) ? (int32)Props->GetNumberField(TEXT("fontSize")) : 14;
			const bool bBold = Props->TryGetBoolField(TEXT("fontBold"), bB) ? bB : false;
			T->SetFont(WeedUI::Font(FontSize, bBold));
			if (Props->TryGetStringField(TEXT("textColor"), Str)) { T->SetColorAndOpacity(FSlateColor(ParseColor(Str, FLinearColor::White))); }
			if (Props->TryGetNumberField(TEXT("justify"), Num))
			{
				const ETextJustify::Type J = Num >= 2 ? ETextJustify::Right : (Num >= 1 ? ETextJustify::Center : ETextJustify::Left);
				T->SetJustification(J);
			}
		}
		else if (UBorder* B = Cast<UBorder>(W))
		{
			if (Props->TryGetStringField(TEXT("brushColor"), Str))
			{
				const float Radius = Props->HasField(TEXT("radius")) ? (float)Props->GetNumberField(TEXT("radius")) : 0.f;
				B->SetBrush(WeedUI::Rounded(ParseColor(Str, FLinearColor::Black), Radius));
			}
			if (Props->TryGetStringField(TEXT("padding"), Str)) { B->SetPadding(ParseMargin(Str)); }
			if (Props->TryGetNumberField(TEXT("hAlign"), Num)) { B->SetHorizontalAlignment((EHorizontalAlignment)(int32)Num); }
			if (Props->TryGetNumberField(TEXT("vAlign"), Num)) { B->SetVerticalAlignment((EVerticalAlignment)(int32)Num); }
		}
		else if (UImage* Img = Cast<UImage>(W))
		{
			if (Props->TryGetStringField(TEXT("texture"), Str) && !Str.IsEmpty())
			{
				if (UTexture2D* Tex = Cast<UTexture2D>(FSoftObjectPath(Str).TryLoad())) { Img->SetBrushFromTexture(Tex); }
			}
			if (Props->TryGetStringField(TEXT("tint"), Str)) { Img->SetColorAndOpacity(ParseColor(Str, FLinearColor::White)); }
			const bool bW = Props->HasField(TEXT("width")); const bool bH = Props->HasField(TEXT("height"));
			if (bW || bH)
			{
				Img->SetDesiredSizeOverride(FVector2D(bW ? Props->GetNumberField(TEXT("width")) : 0.0,
					bH ? Props->GetNumberField(TEXT("height")) : 0.0));
			}
		}
		else if (UButton* Btn = Cast<UButton>(W))
		{
			if (Props->TryGetStringField(TEXT("brushColor"), Str))
			{
				const FLinearColor C = ParseColor(Str, FLinearColor(0.2f, 0.2f, 0.2f, 1.f));
				const float Radius = Props->HasField(TEXT("radius")) ? (float)Props->GetNumberField(TEXT("radius")) : 8.f;
				FButtonStyle Style;
				Style.Normal = WeedUI::Rounded(C, Radius);
				Style.Hovered = WeedUI::Rounded(C * 1.3f, Radius);
				Style.Pressed = WeedUI::Rounded(C * 0.8f, Radius);
				const FMargin BtnPad = Props->HasField(TEXT("padding")) ? ParseMargin(Props->GetStringField(TEXT("padding"))) : FMargin(14.f, 10.f);
				Style.NormalPadding = BtnPad; Style.PressedPadding = BtnPad;
				Btn->SetStyle(Style);
			}
		}
		else if (UProgressBar* Pb = Cast<UProgressBar>(W))
		{
			if (Props->TryGetNumberField(TEXT("percent"), Num)) { Pb->SetPercent((float)Num); }
			if (Props->TryGetStringField(TEXT("fillColor"), Str)) { Pb->SetFillColorAndOpacity(ParseColor(Str, FLinearColor::White)); }
		}
		else if (USizeBox* Sb = Cast<USizeBox>(W))
		{
			if (Props->TryGetNumberField(TEXT("widthOverride"), Num)) { Sb->SetWidthOverride((float)Num); }
			if (Props->TryGetNumberField(TEXT("heightOverride"), Num)) { Sb->SetHeightOverride((float)Num); }
		}
	}

	void ApplySlot(UPanelSlot* Slot, const TSharedPtr<FJsonObject>& SlotObj)
	{
		if (!Slot || !SlotObj.IsValid()) { return; }
		FString Str; double Num = 0.0; bool bB = false;

		if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Slot))
		{
			FAnchors A(0.f, 0.f, 0.f, 0.f);
			if (SlotObj->TryGetStringField(TEXT("anchorMin"), Str)) { const FVector2D V = ParseVec2(Str, FVector2D::ZeroVector); A.Minimum = V; A.Maximum = V; }
			if (SlotObj->TryGetStringField(TEXT("anchorMax"), Str)) { A.Maximum = ParseVec2(Str, A.Maximum); }
			CS->SetAnchors(A);
			if (SlotObj->TryGetStringField(TEXT("align"), Str)) { CS->SetAlignment(ParseVec2(Str, FVector2D(0.f, 0.f))); }
			if (SlotObj->TryGetBoolField(TEXT("autoSize"), bB)) { CS->SetAutoSize(bB); }
			if (SlotObj->TryGetStringField(TEXT("offsets"), Str)) { CS->SetOffsets(ParseMargin(Str)); }
			else
			{
				if (SlotObj->TryGetStringField(TEXT("pos"), Str)) { CS->SetPosition(ParseVec2(Str, FVector2D::ZeroVector)); }
				if (SlotObj->TryGetStringField(TEXT("size"), Str)) { CS->SetSize(ParseVec2(Str, FVector2D(100.f, 100.f))); }
			}
		}
		else if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Slot))
		{
			if (SlotObj->TryGetStringField(TEXT("padding"), Str)) { VS->SetPadding(ParseMargin(Str)); }
			if (SlotObj->TryGetNumberField(TEXT("hAlign"), Num)) { VS->SetHorizontalAlignment((EHorizontalAlignment)(int32)Num); }
			if (SlotObj->TryGetNumberField(TEXT("vAlign"), Num)) { VS->SetVerticalAlignment((EVerticalAlignment)(int32)Num); }
			if (SlotObj->TryGetNumberField(TEXT("fill"), Num) && Num > 0) { VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
		}
		else if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Slot))
		{
			if (SlotObj->TryGetStringField(TEXT("padding"), Str)) { HS->SetPadding(ParseMargin(Str)); }
			if (SlotObj->TryGetNumberField(TEXT("hAlign"), Num)) { HS->SetHorizontalAlignment((EHorizontalAlignment)(int32)Num); }
			if (SlotObj->TryGetNumberField(TEXT("vAlign"), Num)) { HS->SetVerticalAlignment((EVerticalAlignment)(int32)Num); }
		}
	}

	// Voeg Child toe aan Parent (panel of content-widget) en geef de slot terug.
	UPanelSlot* AttachChild(UWidget* Parent, UWidget* Child)
	{
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Parent)) { return Panel->AddChild(Child); }
		if (UContentWidget* Content = Cast<UContentWidget>(Parent)) { Content->SetContent(Child); return Child->Slot; }
		return nullptr;
	}

	UWidget* BuildNode(UWidgetTree* Tree, const TSharedPtr<FJsonObject>& Node)
	{
		if (!Tree || !Node.IsValid()) { return nullptr; }
		const FString Type = Node->GetStringField(TEXT("type"));
		UClass* Cls = ResolveWidgetClass(Type);
		if (!Cls) { UE_LOG(LogTemp, Warning, TEXT("WeedUiAuthoring: onbekend type '%s'"), *Type); return nullptr; }

		FString Name;
		const bool bHasName = Node->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty();
		UWidget* W = bHasName ? Tree->ConstructWidget<UWidget>(Cls, FName(*Name)) : Tree->ConstructWidget<UWidget>(Cls);
		if (!W) { return nullptr; }

		const TSharedPtr<FJsonObject>* PropsObj = nullptr;
		if (Node->TryGetObjectField(TEXT("props"), PropsObj)) { ApplyProps(W, *PropsObj); }

		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Node->TryGetArrayField(TEXT("children"), Children))
		{
			for (const TSharedPtr<FJsonValue>& CV : *Children)
			{
				const TSharedPtr<FJsonObject> CO = CV->AsObject();
				if (!CO.IsValid()) { continue; }
				UWidget* Child = BuildNode(Tree, CO);
				if (!Child) { continue; }
				UPanelSlot* Slot = AttachChild(W, Child);
				const TSharedPtr<FJsonObject>* SlotObj = nullptr;
				if (Slot && CO->TryGetObjectField(TEXT("slot"), SlotObj)) { ApplySlot(Slot, *SlotObj); }
			}
		}
		return W;
	}
}

bool UWeedUiAuthoring::BuildTree(UObject* InWBP, const FString& JsonSpec)
{
	// UObject*-param (UHT/Shipping-fix, zie header) -> hier pas naar het echte editor-type casten.
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(InWBP);
	if (!WBP) { UE_LOG(LogTemp, Warning, TEXT("WeedUiAuthoring::BuildTree: WBP is null of geen WidgetBlueprint")); return false; }
	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree) { UE_LOG(LogTemp, Warning, TEXT("WeedUiAuthoring::BuildTree: geen WidgetTree")); return false; }

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonSpec);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("WeedUiAuthoring::BuildTree: JSON parse-fout"));
		return false;
	}

	// Bestaande tree wissen + nieuwe root bouwen.
	Tree->RootWidget = nullptr;
	UWidget* NewRoot = BuildNode(Tree, Root);
	if (!NewRoot) { UE_LOG(LogTemp, Warning, TEXT("WeedUiAuthoring::BuildTree: root-node faalde")); return false; }
	Tree->RootWidget = NewRoot;

	WBP->Modify();
	Tree->Modify();
	UE_LOG(LogTemp, Display, TEXT("WeedUiAuthoring::BuildTree: tree gebouwd voor %s (root=%s)"), *WBP->GetName(), *NewRoot->GetName());
	return true;
}

#endif // WITH_EDITOR
