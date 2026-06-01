#include "UI/MainMenuWidget.h"

#include "UI/WeedUiStyle.h"
#include "Phone/PhoneClientComponent.h"
#include "Save/SaveGameSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "TextureResource.h"

void UMainMenuWidget::SetPhone(UPhoneClientComponent* InPhone) { PhoneComp = InPhone; }

namespace
{
	UWeedActionButton* MenuBtn(UWidgetTree* Tree, const FString& Label, const FLinearColor& Col, TFunction<void()> OnClick)
	{
		UWeedActionButton* B = Tree->ConstructWidget<UWeedActionButton>();
		B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
		B->OnAction.BindLambda([OnClick](int32, int32) { if (OnClick) { OnClick(); } });
		FButtonStyle S;
		S.Normal = WeedUI::Rounded(Col, 6.f);
		S.Hovered = WeedUI::Rounded(Col * 1.4f, 6.f);
		S.Pressed = WeedUI::Rounded(Col * 0.8f, 6.f);
		S.NormalPadding = FMargin(20.f, 12.f); S.PressedPadding = FMargin(20.f, 12.f);
		B->SetStyle(S);
		// Links uitgelijnde tekst (zoals de mockup-knoppen): brede tekst-box + linkse justering.
		UTextBlock* T = WeedUI::Text(Tree, Label.ToUpper(), 17, FLinearColor(0.96f, 0.97f, 1.f), false, true);
		T->SetJustification(ETextJustify::Left);
		T->SetMinDesiredWidth(286.f);
		B->SetContent(T);
		return B;
	}

	USaveGameSubsystem* GetSave(const UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<USaveGameSubsystem>() : nullptr;
	}

	// Laadt een PNG-verf-streep en maakt er een WIT masker van (RGB->wit, alpha = vorm), zodat je
	// 'm met een tint elke kleur kunt geven (donker = normaal, paars = hover/geselecteerd).
	UTexture2D* LoadWhiteMask(const FString& Path)
	{
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *Path)) { UE_LOG(LogTemp, Warning, TEXT("Swatch: file niet gevonden: %s"), *Path); return nullptr; }
		IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num())) { UE_LOG(LogTemp, Warning, TEXT("Swatch: PNG decode mislukt")); return nullptr; }
		TArray64<uint8> Raw;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)) { UE_LOG(LogTemp, Warning, TEXT("Swatch: GetRaw mislukt")); return nullptr; }
		const int32 W = Wrapper->GetWidth();
		const int32 H = Wrapper->GetHeight();

		// Echte transparantie aanwezig? (24-bit PNG met checkerboard heeft die NIET.)
		bool bHasAlpha = false;
		for (int64 i = 0; i + 3 < Raw.Num(); i += 4) { if (Raw[i + 3] < 250) { bHasAlpha = true; break; } }

		// Per pixel een masker-alpha bepalen: penseel (donker) = vol, checkerboard/licht = weg.
		TArray<uint8> Alpha; Alpha.SetNumUninitialized(W * H);
		for (int64 p = 0; p < (int64)W * H; ++p)
		{
			const uint8 B = Raw[p * 4], G = Raw[p * 4 + 1], R = Raw[p * 4 + 2], SrcA = Raw[p * 4 + 3];
			uint8 A;
			if (bHasAlpha) { A = SrcA; }
			else
			{
				const uint8 MinC = FMath::Min3(R, G, B);
				A = (uint8)(FMath::Clamp((140.f - MinC) / 80.f, 0.f, 1.f) * 255.f); // zwart->vol, grijs/wit->0
			}
			Alpha[p] = A;
		}

		// Bounding box van de penseelstreek (alpha>30), zodat we de lege marge eraf knippen.
		int32 MinX = W, MinY = H, MaxX = 0, MaxY = 0;
		for (int32 y = 0; y < H; ++y) for (int32 x = 0; x < W; ++x)
		{
			if (Alpha[y * W + x] > 30) { MinX = FMath::Min(MinX, x); MaxX = FMath::Max(MaxX, x); MinY = FMath::Min(MinY, y); MaxY = FMath::Max(MaxY, y); }
		}
		if (MaxX < MinX) { MinX = 0; MinY = 0; MaxX = W - 1; MaxY = H - 1; }
		const int32 CW = MaxX - MinX + 1, CH = MaxY - MinY + 1;

		// Bijgesneden BGRA: wit + masker-alpha (vult straks de hele knop).
		TArray<uint8> Out; Out.SetNumUninitialized((int64)CW * CH * 4);
		for (int32 y = 0; y < CH; ++y) for (int32 x = 0; x < CW; ++x)
		{
			const int64 o = ((int64)y * CW + x) * 4;
			Out[o] = 255; Out[o + 1] = 255; Out[o + 2] = 255;
			Out[o + 3] = Alpha[(MinY + y) * W + (MinX + x)];
		}

		UTexture2D* Tex = UTexture2D::CreateTransient(CW, CH, PF_B8G8R8A8);
		if (!Tex) { return nullptr; }
		Tex->SRGB = true;
		void* Dest = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dest, Out.GetData(), Out.Num());
		Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
		Tex->UpdateResource();
		UE_LOG(LogTemp, Log, TEXT("Swatch geladen+bijgesneden: %dx%d -> %dx%d (echte alpha: %d)"), W, H, CW, CH, bHasAlpha ? 1 : 0);
		return Tex;
	}

	// Radiale soft-glow textuur (wit, alpha valt zacht af vanuit het midden). Tintbaar -> neon-halo.
	UTexture2D* MakeRadialGlow(int32 Size)
	{
		UTexture2D* T = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
		if (!T) { return nullptr; }
		T->SRGB = true;
		const float Cc = (Size - 1) * 0.5f;
		void* Data = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		uint8* P = static_cast<uint8*>(Data);
		for (int32 y = 0; y < Size; ++y) for (int32 x = 0; x < Size; ++x)
		{
			const float dx = (x - Cc) / Cc, dy = (y - Cc) / Cc;
			float a = 1.f - FMath::Sqrt(dx * dx + dy * dy);
			a = FMath::Clamp(a, 0.f, 1.f);
			a = a * a * (3.f - 2.f * a); // smoothstep -> zachte halo
			const int64 i = ((int64)y * Size + x) * 4;
			P[i] = 255; P[i + 1] = 255; P[i + 2] = 255; P[i + 3] = (uint8)(a * 255.f);
		}
		T->GetPlatformData()->Mips[0].BulkData.Unlock();
		T->UpdateResource();
		return T;
	}

	// Maakt een Slate-brush van een texture met een tint (voor de knop-swatch).
	FSlateBrush SwatchBrush(UTexture2D* Tex, const FLinearColor& Tint)
	{
		FSlateBrush B;
		B.SetResourceObject(Tex);
		B.ImageSize = FVector2D(Tex ? Tex->GetSizeX() : 256, Tex ? Tex->GetSizeY() : 64);
		B.DrawAs = ESlateBrushDrawType::Image;
		B.TintColor = FSlateColor(Tint);
		return B;
	}
}

TSharedRef<SWidget> UMainMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Canvas;
		BuildShell(Canvas);
	}
	return Super::RebuildWidget();
}

UBorder* UMainMenuWidget::AddGlow(UOverlay* Layers, const FLinearColor& Color, float W, float H,
	EHorizontalAlignment HA, EVerticalAlignment VA, const FMargin& Pad, float Freq)
{
	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
	Box->SetWidthOverride(W); Box->SetHeightOverride(H);
	UBorder* Glow = WidgetTree->ConstructWidget<UBorder>();
	Glow->SetBrush(WeedUI::Rounded(Color, FMath::Min(W, H) * 0.5f)); // sterk afgerond = zachte "blob"
	Box->SetContent(Glow);
	UOverlaySlot* S = Layers->AddChildToOverlay(Box);
	S->SetHorizontalAlignment(HA); S->SetVerticalAlignment(VA); S->SetPadding(Pad);

	Glows.Add(Glow);
	GlowBase.Add(Color);
	GlowPhase.Add(Glows.Num() * 1.7f);   // uiteenlopende fasen zodat lampen niet synchroon flikkeren
	GlowFreq.Add(Freq);
	return Glow;
}

UBorder* UMainMenuWidget::AddGlowAt(UCanvasPanel* C, float Fx, float Fy, float W, float H, const FLinearColor& Color, float Freq, float FlickAmount)
{
	UBorder* Glow = WidgetTree->ConstructWidget<UBorder>();
	if (GlowTex)
	{
		FSlateBrush B;
		B.SetResourceObject(GlowTex);
		B.ImageSize = FVector2D(GlowTex->GetSizeX(), GlowTex->GetSizeY());
		B.DrawAs = ESlateBrushDrawType::Image;
		B.TintColor = FSlateColor(Color);
		Glow->SetBrush(B);
	}
	else
	{
		Glow->SetBrush(WeedUI::Rounded(Color, FMath::Min(W, H) * 0.5f));
	}
	Glow->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* S = C->AddChildToCanvas(Glow);
	S->SetAnchors(FAnchors(Fx, Fy, Fx, Fy));
	S->SetAlignment(FVector2D(0.5f, 0.5f));
	S->SetSize(FVector2D(W, H));

	Glows.Add(Glow);
	GlowBase.Add(Color);
	GlowPhase.Add(Glows.Num() * 1.7f);
	GlowFreq.Add(Freq);
	GlowFlick.Add(FlickAmount);
	return Glow;
}

void UMainMenuWidget::BuildShell(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Volledig dekkende basis-achtergrond (donkere kelder). De wereld erachter is verborgen.
	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuBg"));
	Bg->SetBrush(WeedUI::Rounded(FLinearColor(0.035f, 0.03f, 0.05f, 1.f), 0.f));
	Backdrop = Bg;
	UCanvasPanelSlot* BS = Root->AddChildToCanvas(Bg);
	BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BS->SetOffsets(FMargin(0.f));

	// Overlay om foto/lampen + menu op elkaar te stapelen.
	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
	Bg->SetContent(Layers);

	// Achtergrond-foto + logo vanaf schijf laden (losse PNG's in Content/_Project/UI).
	const FString UIDir = FPaths::ProjectContentDir() / TEXT("_Project/UI/");
	if (!BgTex)     { BgTex     = FImageUtils::ImportFileAsTexture2D(UIDir + TEXT("T_MainMenuBG.png")); }
	if (!LogoTex)   { LogoTex   = FImageUtils::ImportFileAsTexture2D(UIDir + TEXT("T_MainMenuLogo.png")); }
	if (!SwatchTex) { SwatchTex = LoadWhiteMask(UIDir + TEXT("T_BtnSwatch.png")); }

	if (BgTex)
	{
		// === Composite-modus: de hele mockup (achtergrond + logo + geschilderde knoppen) IS de
		// achtergrond. We leggen er alleen ONZICHTBARE klik-knoppen overheen. ===
		UImage* BgImg = WidgetTree->ConstructWidget<UImage>();
		BgImg->SetBrushFromTexture(BgTex, false);
		UOverlaySlot* IS = Layers->AddChildToOverlay(BgImg);
		IS->SetHorizontalAlignment(HAlign_Fill); IS->SetVerticalAlignment(VAlign_Fill);

		// Echte radiale neon-gloeden bovenop de lampen (proportioneel, levend flikkerend).
		GlowTex = MakeRadialGlow(192);
		UCanvasPanel* GlowCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		GlowCanvas->SetVisibility(ESlateVisibility::HitTestInvisible); // vangt geen klikken
		UOverlaySlot* GC = Layers->AddChildToOverlay(GlowCanvas);
		GC->SetHorizontalAlignment(HAlign_Fill); GC->SetVerticalAlignment(VAlign_Fill);

		AddGlowAt(GlowCanvas, 0.46f, 0.17f, 560.f, 230.f, FLinearColor(1.00f, 0.20f, 0.85f, 0.55f), 6.5f); // magenta plafond-buis
		AddGlowAt(GlowCanvas, 0.575f, 0.36f, 360.f, 300.f, FLinearColor(1.00f, 0.26f, 0.78f, 0.50f), 11.0f, 3.0f); // "Good Vibes"-bordje (flikkert meer)
		AddGlowAt(GlowCanvas, 0.875f, 0.30f, 410.f, 580.f, FLinearColor(0.28f, 0.56f, 1.00f, 0.64f), 4.0f, 1.25f); // blauw schap / prices (meer glow, lichte flikker)
		AddGlowAt(GlowCanvas, 0.33f, 0.52f, 540.f, 580.f, FLinearColor(0.64f, 0.26f, 1.00f, 0.60f), 2.8f, 1.3f); // paarse grow-tent (links, meer glow, lichte flikker)
		AddGlowAt(GlowCanvas, 0.50f, 0.78f, 1000.f, 520.f, FLinearColor(0.58f, 0.22f, 0.98f, 0.40f), 1.8f); // paarse vloer-pool
		AddGlowAt(GlowCanvas, 0.80f, 0.62f, 460.f, 300.f, FLinearColor(0.24f, 0.46f, 1.00f, 0.40f), 4.2f); // blauw onder de toonbank

		// Onzichtbare klik-knoppen, proportioneel over de geschilderde knoppen (paarse hover-hint).
		UCanvasPanel* Hit = WidgetTree->ConstructWidget<UCanvasPanel>();
		MenuCanvas = Hit;
		UOverlaySlot* HS = Layers->AddChildToOverlay(Hit);
		HS->SetHorizontalAlignment(HAlign_Fill); HS->SetVerticalAlignment(VAlign_Fill);

		// Exact opgemeten uit T_MainMenuBG.png (1672x941): balk x=145..370, 6 knoppen,
		// gelijke tussenruimte, centers y=440/498/556/613/673/735, hoogte ~47px.
		const float X0 = 0.083f, X1 = 0.285f, HalfH = 0.029f; // langere balken
		const float Centers[6] = { 0.4676f, 0.5292f, 0.5908f, 0.6514f, 0.7152f, 0.7811f };
		TFunction<void()> Acts[6] = {
			[this]() { OnContinue(); },  // CONTINUE -> laatst gebruikte slot
			[this]() { OpenPicker(1); }, // NEW GAME -> kies een slot
			[this]() { OpenPicker(2); }, // LOAD GAME -> kies een slot
			[this]() { OnSettings(); },  // SETTINGS
			[this]() { OnCredits(); },   // CREDITS
			[this]() { OnQuit(); },      // EXIT GAME
		};
		const TCHAR* Labels[6] = { TEXT("CONTINUE"), TEXT("NEW GAME"), TEXT("LOAD GAME"), TEXT("SETTINGS"), TEXT("CREDITS"), TEXT("EXIT GAME") };
		MenuButtons.Reset(); MenuLabels.Reset();
		for (int32 i = 0; i < 6; ++i)
		{
			UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
			B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
			TFunction<void()> Fn = Acts[i];
			B->OnAction.BindLambda([Fn](int32, int32) { if (Fn) { Fn(); } });
			// Zelf-getekende knop: de verf-streep-swatch is de balk. Donker normaal, paars bij hover.
			FButtonStyle St;
			if (SwatchTex)
			{
				St.Normal  = SwatchBrush(SwatchTex, FLinearColor(0.02f, 0.02f, 0.035f, 0.55f)); // donker + doorzichtig
				St.Hovered = SwatchBrush(SwatchTex, FLinearColor(0.62f, 0.26f, 0.95f, 0.97f));
				St.Pressed = SwatchBrush(SwatchTex, FLinearColor(0.74f, 0.36f, 1.00f, 1.00f));
			}
			else
			{
				St.Normal  = WeedUI::Rounded(FLinearColor(0.02f, 0.02f, 0.035f, 0.55f), 6.f);
				St.Hovered = WeedUI::Rounded(FLinearColor(0.42f, 0.18f, 0.72f, 0.96f), 6.f);
				St.Pressed = WeedUI::Rounded(FLinearColor(0.52f, 0.24f, 0.85f, 1.00f), 6.f);
			}
			B->SetStyle(St);
			UCanvasPanelSlot* CSl = Hit->AddChildToCanvas(B);
			CSl->SetAnchors(FAnchors(X0, Centers[i] - HalfH, X1, Centers[i] + HalfH));
			CSl->SetOffsets(FMargin(0.f)); CSl->SetAlignment(FVector2D(0.f, 0.f));

			// Scherpe witte titel als LOSSE tekst, links-uitgelijnd + verticaal gecentreerd op de balk
			// (los van de knop-centrering, zodat 'ie netjes links staat). Vangt geen klikken.
			UTextBlock* Lbl = WeedUI::Text(WidgetTree, Labels[i], 18, FLinearColor(0.97f, 0.98f, 1.f), false, true);
			Lbl->SetVisibility(ESlateVisibility::HitTestInvisible);
			UCanvasPanelSlot* LSl = Hit->AddChildToCanvas(Lbl);
			LSl->SetAnchors(FAnchors(X0 + 0.018f, Centers[i], X0 + 0.018f, Centers[i])); // links in de balk
			LSl->SetAlignment(FVector2D(0.f, 0.5f)); // links-midden
			LSl->SetAutoSize(true);
			LSl->SetPosition(FVector2D(0.f, 0.f));

			MenuButtons.Add(B);
			MenuLabels.Add(Lbl);
		}

		// Kleine status-tekst (laad-feedback), onderaan-midden.
		StatusText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.85f, 0.8f, 1.f), true);
		UOverlaySlot* StS = Layers->AddChildToOverlay(StatusText);
		StS->SetHorizontalAlignment(HAlign_Center); StS->SetVerticalAlignment(VAlign_Bottom); StS->SetPadding(FMargin(0.f, 0.f, 0.f, 40.f));

		// Eigen versie-nummer linksonder.
		UTextBlock* Ver = WeedUI::Text(WidgetTree, TEXT("v0.1.0  -  pre-alpha"), 12, FLinearColor(0.70f, 0.66f, 0.80f), false);
		UOverlaySlot* VS = Layers->AddChildToOverlay(Ver);
		VS->SetHorizontalAlignment(HAlign_Left); VS->SetVerticalAlignment(VAlign_Bottom); VS->SetPadding(FMargin(24.f, 0.f, 0.f, 16.f));

		// --- Slot-picker (3 saves) — gecentreerde kaart, verborgen tot New Game/Load ---
		USizeBox* PickSize = WidgetTree->ConstructWidget<USizeBox>();
		PickSize->SetWidthOverride(540.f);
		UBorder* PickCard = WidgetTree->ConstructWidget<UBorder>();
		PickCard->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.05f, 0.08f, 0.98f), 18.f));
		PickCard->SetPadding(FMargin(26.f, 22.f, 26.f, 22.f));
		PickSize->SetContent(PickCard);
		SlotPanel = PickSize;
		UOverlaySlot* PkS = Layers->AddChildToOverlay(PickSize);
		PkS->SetHorizontalAlignment(HAlign_Center); PkS->SetVerticalAlignment(VAlign_Center);

		UVerticalBox* PickVB = WidgetTree->ConstructWidget<UVerticalBox>();
		PickCard->SetContent(PickVB);
		PickerTitle = WeedUI::Text(WidgetTree, TEXT("CHOOSE A SLOT"), 20, FLinearColor(0.6f, 1.f, 0.6f), true, true);
		PickVB->AddChildToVerticalBox(PickerTitle)->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

		SlotButtons.Reset(); SlotLabels.Reset();
		for (int32 s = 0; s < USaveGameSubsystem::NumSlots; ++s)
		{
			const int32 SlotIdx = s;
			UWeedActionButton* SB = WidgetTree->ConstructWidget<UWeedActionButton>();
			SB->OnClicked.AddDynamic(SB, &UWeedActionButton::Handle);
			SB->OnAction.BindLambda([this, SlotIdx](int32, int32) { OnSlotChosen(SlotIdx); });
			FButtonStyle SS;
			SS.Normal  = WeedUI::Rounded(FLinearColor(0.12f, 0.13f, 0.17f, 0.96f), 8.f);
			SS.Hovered = WeedUI::Rounded(FLinearColor(0.42f, 0.18f, 0.72f, 0.96f), 8.f);
			SS.Pressed = WeedUI::Rounded(FLinearColor(0.52f, 0.24f, 0.85f, 1.f), 8.f);
			SS.NormalPadding = FMargin(16.f, 12.f); SS.PressedPadding = FMargin(16.f, 12.f);
			SB->SetStyle(SS);
			UTextBlock* SL = WeedUI::Text(WidgetTree, TEXT(""), 14, FLinearColor::White, true);
			SB->SetContent(SL);
			PickVB->AddChildToVerticalBox(SB)->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
			SlotButtons.Add(SB);
			SlotLabels.Add(SL);
		}

		UWeedActionButton* BackBtn = WidgetTree->ConstructWidget<UWeedActionButton>();
		BackBtn->OnClicked.AddDynamic(BackBtn, &UWeedActionButton::Handle);
		BackBtn->OnAction.BindLambda([this](int32, int32) { ClosePicker(); });
		FButtonStyle BS2;
		BS2.Normal = WeedUI::Rounded(FLinearColor(0.30f, 0.13f, 0.14f, 0.96f), 8.f);
		BS2.Hovered = WeedUI::Rounded(FLinearColor(0.42f, 0.18f, 0.2f, 0.96f), 8.f);
		BS2.Pressed = WeedUI::Rounded(FLinearColor(0.5f, 0.22f, 0.24f, 1.f), 8.f);
		BS2.NormalPadding = FMargin(16.f, 10.f); BS2.PressedPadding = FMargin(16.f, 10.f);
		BackBtn->SetStyle(BS2);
		BackBtn->SetContent(WeedUI::Text(WidgetTree, TEXT("Back"), 13, FLinearColor::White, true));
		PickVB->AddChildToVerticalBox(BackBtn)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));

		SlotPanel->SetVisibility(ESlateVisibility::Collapsed); // start verborgen
		return;
	}

	// === Fallback (geen foto gevonden): nette vector-versie met tekst-logo + gestylede knoppen. ===
	AddGlow(Layers, FLinearColor(0.95f, 0.20f, 0.85f, 0.42f), 520.f, 40.f, HAlign_Center, VAlign_Top, FMargin(120.f, 70.f, 0.f, 0.f), 6.5f);
	AddGlow(Layers, FLinearColor(0.20f, 0.45f, 1.0f, 0.38f), 180.f, 460.f, HAlign_Right, VAlign_Center, FMargin(0.f, 0.f, 90.f, 0.f), 3.5f);
	AddGlow(Layers, FLinearColor(0.55f, 0.18f, 0.95f, 0.30f), 820.f, 360.f, HAlign_Center, VAlign_Bottom, FMargin(0.f, 0.f, 0.f, 0.f), 1.8f);

	UVerticalBox* Left = WidgetTree->ConstructWidget<UVerticalBox>();
	{
		UOverlaySlot* LS = Layers->AddChildToOverlay(Left);
		LS->SetHorizontalAlignment(HAlign_Left); LS->SetVerticalAlignment(VAlign_Top); LS->SetPadding(FMargin(60.f, 55.f, 0.f, 0.f));
	}
	Left->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("PLUG"), 72, FLinearColor(0.97f, 0.98f, 1.f), false, true));
	Left->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("SIMULATOR"), 40, FLinearColor(0.72f, 0.35f, 1.f), false, true))
		->SetPadding(FMargin(2.f, 0.f, 0.f, 26.f));

	auto AddBtn = [this, Left](const FString& Label, const FLinearColor& Col, TFunction<void()> Fn) -> UWeedActionButton*
	{
		USizeBox* Sz = WidgetTree->ConstructWidget<USizeBox>();
		Sz->SetWidthOverride(330.f); Sz->SetHeightOverride(46.f);
		UWeedActionButton* B = MenuBtn(WidgetTree, Label, Col, Fn);
		Sz->SetContent(B);
		Left->AddChildToVerticalBox(Sz)->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f));
		return B;
	};
	const FLinearColor Hi(0.42f, 0.16f, 0.72f, 0.96f);
	const FLinearColor Dark(0.06f, 0.07f, 0.09f, 0.78f);
	AddBtn(TEXT("Continue"),   Hi,   [this]() { OnStart(); });
	AddBtn(TEXT("New game"),   Dark, [this]() { OnStart(); });
	AddBtn(TEXT("Load game"),  Dark, [this]() { OnContinue(); });
	AddBtn(TEXT("Settings"),   Dark, [this]() { OnSettings(); });
	AddBtn(TEXT("Credits"),    Dark, [this]() { OnCredits(); });
	AddBtn(TEXT("Exit game"),  Dark, [this]() { OnQuit(); });

	StatusText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.8f, 0.7f, 1.f), false);
	Left->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(2.f, 14.f, 0.f, 0.f));

	// Eigen versie-nummer linksonder (ook in de fallback-versie).
	UTextBlock* Ver = WeedUI::Text(WidgetTree, TEXT("v0.1.0  -  pre-alpha"), 12, FLinearColor(0.70f, 0.66f, 0.80f), false);
	UOverlaySlot* VS = Layers->AddChildToOverlay(Ver);
	VS->SetHorizontalAlignment(HAlign_Left); VS->SetVerticalAlignment(VAlign_Bottom); VS->SetPadding(FMargin(24.f, 0.f, 0.f, 16.f));
}

void UMainMenuWidget::OnStart()
{
	if (PhoneComp.IsValid()) { PhoneComp->HideMainMenu(); }
}

void UMainMenuWidget::OnContinue()
{
	if (USaveGameSubsystem* Save = GetSave(GetWorld()))
	{
		if (Save->QuickContinue())
		{
			if (PhoneComp.IsValid()) { PhoneComp->HideMainMenu(); }
			return;
		}
	}
	if (StatusText) { StatusText->SetText(FText::FromString(TEXT("No save found - start a New game."))); }
}

void UMainMenuWidget::OpenPicker(int32 Mode)
{
	MenuMode = Mode;
	if (MenuCanvas) { MenuCanvas->SetVisibility(ESlateVisibility::Collapsed); }
	if (SlotPanel) { SlotPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	RefreshSlots();
}

void UMainMenuWidget::ClosePicker()
{
	MenuMode = 0;
	if (SlotPanel) { SlotPanel->SetVisibility(ESlateVisibility::Collapsed); }
	if (MenuCanvas) { MenuCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
}

void UMainMenuWidget::RefreshSlots()
{
	if (PickerTitle) { PickerTitle->SetText(FText::FromString(MenuMode == 1 ? TEXT("NEW GAME  -  choose a slot") : TEXT("LOAD GAME  -  choose a slot"))); }
	USaveGameSubsystem* Save = GetSave(GetWorld());
	for (int32 s = 0; s < SlotButtons.Num(); ++s)
	{
		FString Info;
		const bool bHas = Save && Save->GetSlotInfo(s, Info);
		const FString Line = bHas
			? FString::Printf(TEXT("Slot %d\n%s"), s + 1, *Info)
			: FString::Printf(TEXT("Slot %d\n(empty)"), s + 1);
		if (SlotLabels.IsValidIndex(s) && SlotLabels[s]) { SlotLabels[s]->SetText(FText::FromString(Line)); }
		// In Load-modus zijn lege slots niet klikbaar; in New-modus alles.
		if (SlotButtons[s]) { SlotButtons[s]->SetIsEnabled(MenuMode == 1 || bHas); }
	}
}

void UMainMenuWidget::OnSlotChosen(int32 SlotIdx)
{
	USaveGameSubsystem* Save = GetSave(GetWorld());
	if (!Save) { return; }
	if (MenuMode == 1) // New Game
	{
		Save->NewGameInSlot(SlotIdx);
		ClosePicker();
		if (PhoneComp.IsValid()) { PhoneComp->HideMainMenu(); }
	}
	else // Load
	{
		if (Save->HasSaveInSlot(SlotIdx) && Save->LoadSlot(SlotIdx))
		{
			ClosePicker();
			if (PhoneComp.IsValid()) { PhoneComp->HideMainMenu(); }
		}
	}
}

void UMainMenuWidget::OnSettings()
{
	// Verberg het titelscherm en open de telefoon-Settings-app.
	if (PhoneComp.IsValid()) { PhoneComp->HideMainMenu(); PhoneComp->OpenToApp(4); }
}

void UMainMenuWidget::OnCredits()
{
	if (StatusText) { StatusText->SetText(FText::FromString(TEXT("THE PLUG SIMULATOR  -  a co-op grow & deal sim.  Built in C++/UE5."))); }
}

void UMainMenuWidget::OnQuit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UMainMenuWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const bool bOpen = PhoneComp.IsValid() && PhoneComp->IsMainMenuOpen();
	if (Backdrop) { Backdrop->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (bOpen != bLastOpen)
	{
		bLastOpen = bOpen;
		if (bOpen)
		{
			ClosePicker(); // altijd op het hoofdmenu beginnen
			if (StatusText) { StatusText->SetText(FText::GetEmpty()); }
			// "Load game" dimmen als er geen save is.
			if (ContinueBtn)
			{
				const USaveGameSubsystem* Save = GetSave(GetWorld());
				ContinueBtn->SetIsEnabled(Save && Save->HasSave());
			}
		}
	}
	if (!bOpen) { return; }

	// Neon-lampen zacht laten flikkeren (alsof ze echt aan staan).
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (int32 i = 0; i < Glows.Num(); ++i)
	{
		if (!Glows[i]) { continue; }
		const float Ph = GlowPhase.IsValidIndex(i) ? GlowPhase[i] : 0.f;
		const float Fr = GlowFreq.IsValidIndex(i) ? GlowFreq[i] : 4.f;
		// Rustige, levende neon-puls (per lamp schaalbaar): trage golf + rimpel + zeldzame stotter.
		const float Fl = GlowFlick.IsValidIndex(i) ? GlowFlick[i] : 1.f;
		float Osc = 0.92f
			+ 0.07f * Fl * FMath::Sin(T * Fr + Ph)
			+ 0.035f * Fl * FMath::Sin(T * Fr * 2.4f + Ph * 1.7f)
			+ FMath::FRandRange(-0.015f * Fl, 0.015f * Fl);
		if (FMath::FRand() < 0.005f * Fl) { Osc *= FMath::FRandRange(0.55f, 0.88f); } // korte neon-flikker (vaker bij hoge Fl)
		Osc = FMath::Clamp(Osc, 0.45f, 1.2f);

		const FLinearColor Base = GlowBase.IsValidIndex(i) ? GlowBase[i] : FLinearColor::White;
		const float K = 0.8f + 0.2f * Osc;
		FLinearColor C(Base.R * K, Base.G * K, Base.B * K, FMath::Clamp(Base.A * Osc, 0.f, 1.f));
		Glows[i]->SetBrushColor(C);
	}
}
