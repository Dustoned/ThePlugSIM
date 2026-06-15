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
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "InputCoreTypes.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "TextureResource.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"

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

	// "€ 12.345" met punt als duizendtal-scheiding.
	FString FmtEuro(int64 Cents)
	{
		const int64 Euro = Cents / 100;
		FString Digits = FString::Printf(TEXT("%lld"), Euro < 0 ? -Euro : Euro);
		FString Grouped;
		int32 Count = 0;
		for (int32 i = Digits.Len() - 1; i >= 0; --i)
		{
			Grouped = Digits.Mid(i, 1) + Grouped;
			if (++Count % 3 == 0 && i > 0) { Grouped = TEXT(".") + Grouped; }
		}
		return FString::Printf(TEXT("%s€ %s"), Euro < 0 ? TEXT("-") : TEXT(""), *Grouped);
	}

	// "2u 13m" / "13m" / "0m".
	FString FmtPlaytime(double Seconds)
	{
		const int64 Total = (int64)FMath::Max(0.0, Seconds);
		const int64 H = Total / 3600;
		const int64 M = (Total % 3600) / 60;
		if (H > 0) { return FString::Printf(TEXT("%lldu %lldm"), H, M); }
		return FString::Printf(TEXT("%lldm"), M);
	}

	// "zojuist" / "5m geleden" / "2u geleden" / "3d geleden".
	FString FmtAgo(const FDateTime& When)
	{
		if (When.GetTicks() <= 0) { return TEXT("-"); }
		const FTimespan D = FDateTime::UtcNow() - When;
		const double S = D.GetTotalSeconds();
		if (S < 45) { return TEXT("zojuist"); }
		if (S < 3600) { return FString::Printf(TEXT("%dm geleden"), FMath::RoundToInt(S / 60.0)); }
		if (S < 86400) { return FString::Printf(TEXT("%du geleden"), FMath::RoundToInt(S / 3600.0)); }
		return FString::Printf(TEXT("%dd geleden"), FMath::RoundToInt(S / 86400.0));
	}

	// Laadt een PNG-verf-streep en maakt er een WIT masker van (RGB->wit, alpha = vorm), zodat je
	// 'm met een tint elke kleur kunt geven (donker = normaal, paars = hover/geselecteerd).
	UTexture2D* LoadWhiteMask(const FString& Path)
	{
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *Path)) { UE_LOG(LogTemp, Warning, TEXT("Swatch: file not found: %s"), *Path); return nullptr; }
		IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num())) { UE_LOG(LogTemp, Warning, TEXT("Swatch: PNG decode failed")); return nullptr; }
		TArray64<uint8> Raw;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)) { UE_LOG(LogTemp, Warning, TEXT("Swatch: GetRaw failed")); return nullptr; }
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
		UE_LOG(LogTemp, Log, TEXT("Swatch loaded+cropped: %dx%d -> %dx%d (real alpha: %d)"), W, H, CW, CH, bHasAlpha ? 1 : 0);
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

UBorder* UMainMenuWidget::AddGlowAt(UCanvasPanel* C, float Fx, float Fy, float W, float H, const FLinearColor& Color, float Freq, float FlickAmount, bool bCandle)
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
	// Opgeslagen positie (in-game lamp-editor) overschrijft de standaard-coördinaat.
	float UseFx = Fx, UseFy = Fy;
	LoadGlowFrac(Glows.Num(), UseFx, UseFy);
	UCanvasPanelSlot* S = C->AddChildToCanvas(Glow);
	S->SetAnchors(FAnchors(UseFx, UseFy, UseFx, UseFy));
	S->SetAlignment(FVector2D(0.5f, 0.5f));
	S->SetSize(FVector2D(W, H));

	Glows.Add(Glow);
	GlowSlots.Add(S);
	GlowFrac.Add(FVector2D(UseFx, UseFy));
	GlowBase.Add(Color);
	GlowPhase.Add(Glows.Num() * 1.7f);
	GlowFreq.Add(Freq);
	GlowFlick.Add(FlickAmount);
	GlowCandle.Add(bCandle ? 1 : 0);
	return Glow;
}

void UMainMenuWidget::LoadGlowFrac(int32 Index, float& Fx, float& Fy) const
{
	FString Val;
	if (GConfig && GConfig->GetString(TEXT("ThePlugSIM.MenuGlows"), *FString::Printf(TEXT("Glow%d"), Index), Val, GGameIni))
	{
		FString L, R;
		if (Val.Split(TEXT(","), &L, &R)) { Fx = FCString::Atof(*L); Fy = FCString::Atof(*R); }
	}
}

void UMainMenuWidget::SaveGlowPositions()
{
	FString Dump;
	if (GConfig)
	{
		for (int32 i = 0; i < GlowFrac.Num(); ++i)
		{
			GConfig->SetString(TEXT("ThePlugSIM.MenuGlows"), *FString::Printf(TEXT("Glow%d"), i),
				*FString::Printf(TEXT("%.4f,%.4f"), GlowFrac[i].X, GlowFrac[i].Y), GGameIni);
		}
		GConfig->Flush(false, GGameIni);
	}
	// Ook naar een leesbaar tekstbestand schrijven (Saved/MenuGlows.txt) zodat de posities makkelijk
	// in de code te bakken zijn, ongeacht waar de game-config landt.
	for (int32 i = 0; i < GlowFrac.Num(); ++i)
	{
		Dump += FString::Printf(TEXT("Glow%d = %.4f , %.4f\n"), i, GlowFrac[i].X, GlowFrac[i].Y);
	}
	FFileHelper::SaveStringToFile(Dump, *(FPaths::ProjectSavedDir() / TEXT("MenuGlows.txt")));
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

		AddGlowAt(GlowCanvas, 0.432f, 0.163f, 560.f, 230.f, FLinearColor(1.00f, 0.20f, 0.85f, 0.55f), 6.5f); // magenta plafond-buis
		AddGlowAt(GlowCanvas, 0.574f, 0.378f, 360.f, 300.f, FLinearColor(1.00f, 0.26f, 0.78f, 0.50f), 11.0f, 3.0f); // "Good Vibes"-bordje (flikkert meer)
		AddGlowAt(GlowCanvas, 0.891f, 0.293f, 410.f, 580.f, FLinearColor(0.28f, 0.56f, 1.00f, 0.64f), 4.0f, 1.25f); // blauw schap / prices (meer glow, lichte flikker)
		AddGlowAt(GlowCanvas, 0.344f, 0.520f, 540.f, 580.f, FLinearColor(0.64f, 0.26f, 1.00f, 0.60f), 2.8f, 1.3f); // paarse grow-tent (links, meer glow, lichte flikker)
		AddGlowAt(GlowCanvas, 0.529f, 0.693f, 1000.f, 520.f, FLinearColor(0.58f, 0.22f, 0.98f, 0.40f), 1.8f); // paarse vloer-pool
		AddGlowAt(GlowCanvas, 0.721f, 0.554f, 460.f, 300.f, FLinearColor(0.24f, 0.46f, 1.00f, 0.40f), 4.2f); // blauw onder de toonbank
		AddGlowAt(GlowCanvas, 0.735f, 0.195f, 300.f, 270.f, FLinearColor(1.00f, 0.52f, 0.16f, 0.66f), 3.0f, 1.0f, /*bCandle*/ true); // warme hanglamp (rustige kaars-gloed)

		// Lamp-editor-laag: markers/labels die je kunt verslepen (alleen zichtbaar in edit-modus).
		EditCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		EditCanvas->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* ECS = Layers->AddChildToOverlay(EditCanvas);
		ECS->SetHorizontalAlignment(HAlign_Fill); ECS->SetVerticalAlignment(VAlign_Fill);

		// Onzichtbare klik-knoppen, proportioneel over de geschilderde knoppen (paarse hover-hint).
		UCanvasPanel* Hit = WidgetTree->ConstructWidget<UCanvasPanel>();
		MenuCanvas = Hit;
		UOverlaySlot* HS = Layers->AddChildToOverlay(Hit);
		HS->SetHorizontalAlignment(HAlign_Fill); HS->SetVerticalAlignment(VAlign_Fill);

		// Exact opgemeten uit T_MainMenuBG.png (1672x941): balk x=145..370, 6 knoppen,
		// gelijke tussenruimte, centers y=440/498/556/613/673/735, hoogte ~47px.
		const float X0 = 0.083f, X1 = 0.285f, HalfH = 0.029f; // langere balken
		// Credits eruit, CO-OP er als gewone menu-knop bij -> 6 knoppen (eigen swatch-balk, geen lege plek).
		const float Centers[6] = { 0.4676f, 0.5292f, 0.5908f, 0.6514f, 0.7152f, 0.7811f };
		TFunction<void()> Acts[6] = {
			[this]() { OnContinue(); },  // CONTINUE -> laatst gebruikte slot
			[this]() { OpenPicker(1); }, // NEW GAME -> kies een slot
			[this]() { OpenPicker(2); }, // LOAD GAME -> kies een slot
			[this]() { OpenCoop(); },    // CO-OP -> host/join via IP
			[this]() { OnSettings(); },  // SETTINGS
			[this]() { OnQuit(); },      // EXIT GAME
		};
		const TCHAR* Labels[6] = { TEXT("CONTINUE"), TEXT("NEW GAME"), TEXT("LOAD GAME"), TEXT("CO-OP"), TEXT("SETTINGS"), TEXT("EXIT GAME") };
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

		// (De "Edit lamps"-editorknop is verwijderd uit het main menu.)

		// Hint tijdens het editen (bovenaan).
		EditHintText = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor(0.9f, 1.f, 0.7f), true, true);
		UOverlaySlot* EHS = Layers->AddChildToOverlay(EditHintText);
		EHS->SetHorizontalAlignment(HAlign_Center); EHS->SetVerticalAlignment(VAlign_Top); EHS->SetPadding(FMargin(0.f, 16.f, 0.f, 0.f));
		EditHintText->SetVisibility(ESlateVisibility::Collapsed);

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
		PickVB->AddChildToVerticalBox(PickerTitle)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

		// --- Vaste balk boven de slots: Autosave aan/uit + wanneer de laatste save was ---
		{
			UBorder* HeadBar = WidgetTree->ConstructWidget<UBorder>();
			HeadBar->SetBrush(WeedUI::Rounded(FLinearColor(0.09f, 0.10f, 0.14f, 0.97f), 8.f));
			HeadBar->SetPadding(FMargin(12.f, 8.f, 12.f, 8.f));
			UHorizontalBox* HeadRow = WidgetTree->ConstructWidget<UHorizontalBox>();
			HeadBar->SetContent(HeadRow);

			AutosaveBtn = WidgetTree->ConstructWidget<UWeedActionButton>();
			AutosaveBtn->OnClicked.AddDynamic(AutosaveBtn, &UWeedActionButton::Handle);
			AutosaveBtn->OnAction.BindLambda([this](int32, int32) { OnToggleAutosave(); });
			FButtonStyle AS;
			AS.Normal  = WeedUI::Rounded(FLinearColor(0.16f, 0.17f, 0.22f, 1.f), 6.f);
			AS.Hovered = WeedUI::Rounded(FLinearColor(0.30f, 0.16f, 0.50f, 1.f), 6.f);
			AS.Pressed = WeedUI::Rounded(FLinearColor(0.40f, 0.20f, 0.62f, 1.f), 6.f);
			AS.NormalPadding = FMargin(12.f, 6.f); AS.PressedPadding = FMargin(12.f, 6.f);
			AutosaveBtn->SetStyle(AS);
			AutosaveLabel = WeedUI::Text(WidgetTree, TEXT("Autosave: aan"), 13, FLinearColor(0.7f, 1.f, 0.7f), true, true);
			AutosaveBtn->SetContent(AutosaveLabel);
			UHorizontalBoxSlot* ABS = HeadRow->AddChildToHorizontalBox(AutosaveBtn);
			ABS->SetVerticalAlignment(VAlign_Center);

			LastSaveText = WeedUI::Text(WidgetTree, TEXT("Laatste save: -"), 12, FLinearColor(0.78f, 0.80f, 0.92f), false);
			LastSaveText->SetJustification(ETextJustify::Right);
			UHorizontalBoxSlot* LSS = HeadRow->AddChildToHorizontalBox(LastSaveText);
			LSS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LSS->SetHorizontalAlignment(HAlign_Right); LSS->SetVerticalAlignment(VAlign_Center);
			LSS->SetPadding(FMargin(12.f, 0.f, 2.f, 0.f));

			PickVB->AddChildToVerticalBox(HeadBar)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		}

		// De slot-rijen worden per refresh dynamisch in deze box gezet (zodat we per slot ook
		// een extra autosave-knop kunnen tonen).
		SlotsBox = WidgetTree->ConstructWidget<UVerticalBox>();
		PickVB->AddChildToVerticalBox(SlotsBox);

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

		// (CO-OP staat nu gewoon als knop in de menu-lijst hierboven, niet meer los rechtsboven.)

		// --- CO-OP-kaart (host / join via IP) — gecentreerd, verborgen tot je op CO-OP klikt ---
		{
			USizeBox* CoopSize = WidgetTree->ConstructWidget<USizeBox>();
			CoopSize->SetWidthOverride(520.f);
			UBorder* CoopCard = WidgetTree->ConstructWidget<UBorder>();
			CoopCard->SetBrush(WeedUI::Rounded(FLinearColor(0.05f, 0.05f, 0.08f, 0.98f), 18.f));
			CoopCard->SetPadding(FMargin(26.f, 22.f));
			CoopSize->SetContent(CoopCard);
			CoopPanel = CoopSize;
			UOverlaySlot* CpS = Layers->AddChildToOverlay(CoopSize);
			CpS->SetHorizontalAlignment(HAlign_Center); CpS->SetVerticalAlignment(VAlign_Center);

			UVerticalBox* CoopVB = WidgetTree->ConstructWidget<UVerticalBox>();
			CoopCard->SetContent(CoopVB);
			CoopVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("CO-OP (LAN)"), 20, FLinearColor(0.6f, 1.f, 0.6f), true, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			CoopVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Host a game or enter the host IP."), 12, FLinearColor(0.78f, 0.8f, 0.92f), true))->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

			auto BigBtn = [&](const FString& Label, const FLinearColor& Col, TFunction<void()> Fn) -> UWeedActionButton*
			{
				UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
				B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
				B->OnAction.BindLambda([Fn](int32, int32) { if (Fn) { Fn(); } });
				FButtonStyle St;
				St.Normal = WeedUI::Rounded(Col, 8.f);
				St.Hovered = WeedUI::Rounded(Col * 1.4f, 8.f);
				St.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
				St.NormalPadding = FMargin(16.f, 12.f); St.PressedPadding = FMargin(16.f, 12.f);
				B->SetStyle(St);
				B->SetContent(WeedUI::Text(WidgetTree, Label, 15, FLinearColor::White, true, true));
				return B;
			};

			// Modus-keuze (host): Co-op (samen) <-> Competitive (versus). Klik om te wisselen.
			bHostCompetitive = false;
			CoopModeBtn = BigBtn(TEXT("Mode: CO-OP  (build together)"), FLinearColor(0.14f, 0.30f, 0.20f, 0.96f), [this]() { OnToggleCoopMode(); });
			CoopVB->AddChildToVerticalBox(CoopModeBtn)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			CoopVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Co-op = everything shared.  Competitive = own money + steal each other's customers."), 10, FLinearColor(0.7f, 0.72f, 0.85f), true))->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

			// Host
			CoopVB->AddChildToVerticalBox(BigBtn(TEXT("Host new co-op game"), FLinearColor(0.16f, 0.34f, 0.22f, 0.96f), [this]() { OnHostCoop(); }))
				->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

			// Join: label + IP-veld + knop.
			CoopVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Connect to host (IP):"), 13, FLinearColor(0.82f, 0.86f, 1.f), false))->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			CoopIpBox = WidgetTree->ConstructWidget<UEditableTextBox>();
			CoopIpBox->SetHintText(FText::FromString(TEXT("192.168.x.x")));
			CoopIpBox->SetText(FText::FromString(TEXT("127.0.0.1")));
			CoopVB->AddChildToVerticalBox(CoopIpBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
			CoopVB->AddChildToVerticalBox(BigBtn(TEXT("Join"), FLinearColor(0.18f, 0.22f, 0.40f, 0.96f), [this]() { OnJoinCoop(); }))
				->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

			// Back
			CoopVB->AddChildToVerticalBox(BigBtn(TEXT("Back"), FLinearColor(0.30f, 0.13f, 0.14f, 0.96f), [this]() { CloseCoop(); }));

			CoopPanel->SetVisibility(ESlateVisibility::Collapsed);
		}
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
	AddBtn(TEXT("Co-op"),      Dark, [this]() { OpenCoop(); });
	AddBtn(TEXT("Settings"),   Dark, [this]() { OnSettings(); });
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
		// Herlaadt het level en laadt de nieuwste save (schone lei).
		if (Save->RequestContinue()) { return; }
	}
	if (StatusText) { StatusText->SetText(FText::FromString(TEXT("No save found - start a New game."))); }
}

void UMainMenuWidget::OpenPicker(int32 Mode)
{
	MenuMode = Mode;
	if (MenuCanvas) { MenuCanvas->SetVisibility(ESlateVisibility::Collapsed); }
	if (CoopPanel) { CoopPanel->SetVisibility(ESlateVisibility::Collapsed); } // niet achter de slot-keuze laten staan
	if (SlotPanel) { SlotPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	RefreshSlots();
}

void UMainMenuWidget::ClosePicker()
{
	MenuMode = 0;
	bHostMode = false; // terug naar het hoofdmenu -> volgende New Game is weer gewoon solo
	if (SlotPanel) { SlotPanel->SetVisibility(ESlateVisibility::Collapsed); }
	if (CoopPanel) { CoopPanel->SetVisibility(ESlateVisibility::Collapsed); }
	if (MenuCanvas) { MenuCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
}

void UMainMenuWidget::OpenCoop()
{
	if (MenuCanvas) { MenuCanvas->SetVisibility(ESlateVisibility::Collapsed); }
	if (SlotPanel) { SlotPanel->SetVisibility(ESlateVisibility::Collapsed); }
	if (CoopPanel) { CoopPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
}

void UMainMenuWidget::CloseCoop()
{
	if (CoopPanel) { CoopPanel->SetVisibility(ESlateVisibility::Collapsed); }
	if (MenuCanvas) { MenuCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
}

void UMainMenuWidget::OnHostCoop()
{
	// Host = vers spel als listen-server. Hergebruik de mode-keuze (Normal/Sandbox/Testing).
	bHostMode = true;
	OpenPicker(1); // toont eerst de slot-keuze, daarna de mode-keuze (-> OnModeChosen)
}

void UMainMenuWidget::OnJoinCoop()
{
	USaveGameSubsystem* Save = GetSave(GetWorld());
	const FString Ip = CoopIpBox ? CoopIpBox->GetText().ToString() : FString();
	if (!Save || Ip.TrimStartAndEnd().IsEmpty())
	{
		if (StatusText) { StatusText->SetText(FText::FromString(TEXT("Enter the host IP (e.g. 192.168.1.50)."))); }
		return;
	}
	if (StatusText) { StatusText->SetText(FText::FromString(FString::Printf(TEXT("Connecting to %s..."), *Ip))); }
	Save->JoinLan(Ip);
}

void UMainMenuWidget::RefreshSlots()
{
	if (PickerTitle)
	{
		const TCHAR* Title = (MenuMode == 3) ? TEXT("NEW GAME  -  pick a mode")
			: (MenuMode == 1) ? TEXT("NEW GAME  -  pick a slot") : TEXT("LOAD  -  pick a slot");
		PickerTitle->SetText(FText::FromString(Title));
	}
	USaveGameSubsystem* Save = GetSave(GetWorld());

	// Vaste balk bijwerken: autosave-status + laatste save-tijdstip.
	if (AutosaveLabel)
	{
		const bool bOn = Save ? Save->IsAutosaveEnabled() : true;
		AutosaveLabel->SetText(FText::FromString(bOn ? TEXT("Autosave: aan") : TEXT("Autosave: uit")));
		AutosaveLabel->SetColorAndOpacity(FSlateColor(bOn ? FLinearColor(0.65f, 1.f, 0.7f) : FLinearColor(1.f, 0.6f, 0.55f)));
	}
	if (LastSaveText)
	{
		FDateTime Last;
		const bool bAny = Save && Save->GetMostRecentSaveTime(Last);
		LastSaveText->SetText(FText::FromString(bAny ? FString::Printf(TEXT("Laatste save: %s"), *FmtAgo(Last)) : TEXT("No save yet")));
	}

	if (!SlotsBox) { return; }
	SlotsBox->ClearChildren();

	// Mode-keuze (na het kiezen van een New Game-slot).
	if (MenuMode == 3)
	{
		// Map-selectie: toggle bovenaan de mode-keuze. De keuze gaat via SetPendingMap mee de start in.
		{
			const bool bBeach = (PendingNewMap == 1);
			const FString MapLbl = bBeach ? TEXT("Map: BEACH CITY  (main)") : TEXT("Map: CITY APARTMENT  (classic)");
			const FLinearColor MapCol = bBeach ? FLinearColor(0.13f, 0.30f, 0.38f, 0.96f) : FLinearColor(0.16f, 0.16f, 0.22f, 0.96f);
			UWeedActionButton* MapB = WidgetTree->ConstructWidget<UWeedActionButton>();
			MapB->OnClicked.AddDynamic(MapB, &UWeedActionButton::Handle);
			MapB->OnAction.BindLambda([this](int32, int32) { PendingNewMap = 1 - PendingNewMap; RefreshSlots(); });
			FButtonStyle MpS;
			MpS.Normal = WeedUI::Rounded(MapCol, 8.f);
			MpS.Hovered = WeedUI::Rounded(MapCol * 1.4f, 8.f);
			MpS.Pressed = WeedUI::Rounded(MapCol * 0.8f, 8.f);
			MpS.NormalPadding = FMargin(16.f, 12.f); MpS.PressedPadding = FMargin(16.f, 12.f);
			MapB->SetStyle(MpS);
			MapB->SetContent(WeedUI::Text(WidgetTree, MapLbl + TEXT("\nClick to switch map"), 14, FLinearColor::White, true));
			SlotsBox->AddChildToVerticalBox(MapB)->SetPadding(FMargin(0.f, 4.f, 0.f, 10.f));
		}

		struct FModeDef { const TCHAR* Name; const TCHAR* Desc; int32 Mode; FLinearColor Col; };
		const FModeDef Modes[3] = {
			{ TEXT("Normal"),  TEXT("Begin from scratch - earn everything"),        0, FLinearColor(0.12f, 0.13f, 0.17f, 0.96f) },
			{ TEXT("Sandbox"), TEXT("Loads of cash + a full kit - free play"),       1, FLinearColor(0.18f, 0.22f, 0.40f, 0.96f) },
			{ TEXT("Testing"), TEXT("Starter budget + starter items (quick test)"),  2, FLinearColor(0.16f, 0.34f, 0.22f, 0.96f) },
		};
		for (const FModeDef& M : Modes)
		{
			const int32 ModeVal = M.Mode;
			UWeedActionButton* MB = WidgetTree->ConstructWidget<UWeedActionButton>();
			MB->OnClicked.AddDynamic(MB, &UWeedActionButton::Handle);
			MB->OnAction.BindLambda([this, ModeVal](int32, int32) { OnModeChosen(ModeVal); });
			FButtonStyle MS;
			MS.Normal = WeedUI::Rounded(M.Col, 8.f);
			MS.Hovered = WeedUI::Rounded(M.Col * 1.4f, 8.f);
			MS.Pressed = WeedUI::Rounded(M.Col * 0.8f, 8.f);
			MS.NormalPadding = FMargin(16.f, 12.f); MS.PressedPadding = FMargin(16.f, 12.f);
			MB->SetStyle(MS);
			MB->SetContent(WeedUI::Text(WidgetTree, FString::Printf(TEXT("%s\n%s"), M.Name, M.Desc), 14, FLinearColor::White, true));
			SlotsBox->AddChildToVerticalBox(MB)->SetPadding(FMargin(0.f, 4.f, 0.f, 4.f));
		}
		return;
	}

	FButtonStyle MainStyle;
	MainStyle.Normal  = WeedUI::Rounded(FLinearColor(0.12f, 0.13f, 0.17f, 0.96f), 8.f);
	MainStyle.Hovered = WeedUI::Rounded(FLinearColor(0.42f, 0.18f, 0.72f, 0.96f), 8.f);
	MainStyle.Pressed = WeedUI::Rounded(FLinearColor(0.52f, 0.24f, 0.85f, 1.f), 8.f);
	MainStyle.NormalPadding = FMargin(16.f, 12.f); MainStyle.PressedPadding = FMargin(16.f, 12.f);

	FButtonStyle AutoStyle;
	AutoStyle.Normal  = WeedUI::Rounded(FLinearColor(0.10f, 0.14f, 0.18f, 0.96f), 7.f);
	AutoStyle.Hovered = WeedUI::Rounded(FLinearColor(0.16f, 0.34f, 0.46f, 0.96f), 7.f);
	AutoStyle.Pressed = WeedUI::Rounded(FLinearColor(0.20f, 0.42f, 0.56f, 1.f), 7.f);
	AutoStyle.NormalPadding = FMargin(12.f, 7.f); AutoStyle.PressedPadding = FMargin(12.f, 7.f);

	for (int32 s = 0; s < USaveGameSubsystem::NumSlots; ++s)
	{
		const int32 SlotIdx = s;
		FSaveSlotInfo Manual; const bool bManual = Save && Save->GetSlotDetailsEx(s, false, Manual);
		FSaveSlotInfo Auto;   const bool bAuto   = Save && Save->GetSlotDetailsEx(s, true, Auto);
		const bool bHasAny = bManual || bAuto;

		// Hoofd-knop: in Load laadt 'ie je handmatige save (of de autosave als er geen handmatige is);
		// in New start 'ie een nieuw spel in dit slot.
		UWeedActionButton* SB = WidgetTree->ConstructWidget<UWeedActionButton>();
		SB->OnClicked.AddDynamic(SB, &UWeedActionButton::Handle);
		SB->OnAction.BindLambda([this, SlotIdx](int32, int32) { OnSlotChosen(SlotIdx); });
		SB->SetStyle(MainStyle);

		const FSaveSlotInfo* Show = bManual ? &Manual : (bAuto ? &Auto : nullptr);
		FString Line;
		if (Show)
		{
			// Geen "SLOT n" meer zodra er een save is. Regel 1: solo/co-op (+autosave-tag als alleen
			// een autosave bestaat). Regel 2: day/saldo/level. Regel 3: speeltijd + tijdstip.
			const FString Who = Show->NumPlayers >= 2 ? FString::Printf(TEXT("Co-op (%d)"), Show->NumPlayers) : TEXT("Solo");
			const TCHAR* Tag = (!bManual && bAuto) ? TEXT("   -   autosave") : TEXT("");
			Line = FString::Printf(TEXT("%s%s\nDay %d      %s      Lvl %d\n%s gespeeld   -   %s"),
				*Who, Tag, Show->DayNumber, *FmtEuro(Show->TotalCents), Show->CrewLevel,
				*FmtPlaytime(Show->PlaytimeSeconds), *FmtAgo(Show->SavedAt));
		}
		else
		{
			Line = FString::Printf(TEXT("SLOT %d\n(empty)"), s + 1);
		}
		SB->SetContent(WeedUI::Text(WidgetTree, Line, 14, FLinearColor::White, true));
		SB->SetIsEnabled(MenuMode == 1 || bHasAny); // lege slots zijn alleen in New klikbaar
		const float BottomPad = (MenuMode == 2 && bManual && bAuto) ? 2.f : 5.f;
		SlotsBox->AddChildToVerticalBox(SB)->SetPadding(FMargin(0.f, 4.f, 0.f, BottomPad));

		// In Load-modus en als er NAAST de handmatige save ook een autosave bestaat: een extra knop
		// om de autosave te laden (vaak nieuwer / verder).
		if (MenuMode == 2 && bManual && bAuto)
		{
			UWeedActionButton* AB = WidgetTree->ConstructWidget<UWeedActionButton>();
			AB->OnClicked.AddDynamic(AB, &UWeedActionButton::Handle);
			AB->OnAction.BindLambda([this, SlotIdx](int32, int32) { OnLoadAutosave(SlotIdx); });
			AB->SetStyle(AutoStyle);
			const FString ALine = FString::Printf(TEXT("autosave  -  Day %d   %s   Lvl %d   -   %s"),
				Auto.DayNumber, *FmtEuro(Auto.TotalCents), Auto.CrewLevel, *FmtAgo(Auto.SavedAt));
			AB->SetContent(WeedUI::Text(WidgetTree, ALine, 11, FLinearColor(0.75f, 0.86f, 1.f), true));
			SlotsBox->AddChildToVerticalBox(AB)->SetPadding(FMargin(16.f, 0.f, 0.f, 8.f));
		}
	}
}

void UMainMenuWidget::OnToggleAutosave()
{
	if (USaveGameSubsystem* Save = GetSave(GetWorld()))
	{
		Save->SetAutosaveEnabled(!Save->IsAutosaveEnabled());
		RefreshSlots();
	}
}

void UMainMenuWidget::OnSlotChosen(int32 SlotIdx)
{
	USaveGameSubsystem* Save = GetSave(GetWorld());
	if (!Save) { return; }
	if (MenuMode == 1) // New Game -> eerst een game-mode kiezen
	{
		PendingNewSlot = SlotIdx;
		MenuMode = 3;     // mode-keuze
		RefreshSlots();
	}
	else // Load handmatige save (level herlaadt, daarna save toegepast)
	{
		Save->RequestLoad(SlotIdx, false);
	}
}

void UMainMenuWidget::OnToggleCoopMode()
{
	bHostCompetitive = !bHostCompetitive;
	if (CoopModeBtn)
	{
		const FString L = bHostCompetitive ? TEXT("Mode: COMPETITIVE  (versus)") : TEXT("Mode: CO-OP  (build together)");
		const FLinearColor Col = bHostCompetitive ? FLinearColor(0.42f, 0.16f, 0.16f, 0.96f) : FLinearColor(0.14f, 0.30f, 0.20f, 0.96f);
		FButtonStyle St;
		St.Normal = WeedUI::Rounded(Col, 8.f);
		St.Hovered = WeedUI::Rounded(Col * 1.4f, 8.f);
		St.Pressed = WeedUI::Rounded(Col * 0.8f, 8.f);
		St.NormalPadding = FMargin(16.f, 12.f); St.PressedPadding = FMargin(16.f, 12.f);
		CoopModeBtn->SetStyle(St);
		CoopModeBtn->SetContent(WeedUI::Text(WidgetTree, L, 15, FLinearColor::White, true, true));
	}
}

void UMainMenuWidget::OnModeChosen(int32 Mode)
{
	if (USaveGameSubsystem* Save = GetSave(GetWorld()))
	{
		// Gekozen map meegeven (leeg = de standaard stad-map). Geldt ook voor de co-op host: de
		// listen-server reist naar deze map en joiners volgen automatisch.
		Save->SetPendingMap(PendingNewMap == 1 ? TEXT("/Game/CityBeachStrip/Maps/CityBeachStrip") : TEXT(""));
		if (bHostMode) { bHostMode = false; Save->SetPendingCoopCompetitive(bHostCompetitive); Save->HostNewGameLan(PendingNewSlot, (EGameStartMode)Mode); } // co-op host (listen)
		else { Save->RequestNewGame(PendingNewSlot, (EGameStartMode)Mode); }
	}
}

void UMainMenuWidget::OnLoadAutosave(int32 SlotIdx)
{
	if (USaveGameSubsystem* Save = GetSave(GetWorld()))
	{
		Save->RequestLoad(SlotIdx, true);
	}
}

void UMainMenuWidget::OnSettings()
{
	// Open het settings-scherm bovenop het titelscherm.
	if (PhoneComp.IsValid()) { PhoneComp->OpenSettings(); }
}

void UMainMenuWidget::OnCredits()
{
	if (StatusText) { StatusText->SetText(FText::FromString(TEXT("THE PLUG SIMULATOR  -  a co-op grow & deal sim.  Built in C++/UE5."))); }
}

void UMainMenuWidget::OnQuit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

// De track is van zichzelf vrij hard: een vaste basis-demping zodat de slider op 100% al rustig
// klinkt en je vanaf daar nog zachter kunt.
static constexpr float GMenuMusicBaseline = 0.5f;

void UMainMenuWidget::StartMenuMusic()
{
	if (!MusicComp)
	{
		USoundBase* S = LoadObject<USoundBase>(nullptr, TEXT("/Game/_Project/Audio/MainMenuMusic.MainMenuMusic"));
		if (!S) { return; }
		if (USoundWave* W = Cast<USoundWave>(S)) { W->bLooping = true; } // op het hoofdmenu blijven loopen
		MusicComp = UGameplayStatics::CreateSound2D(this, S, WeedUI::SoundCategoryVolume(2) * GMenuMusicBaseline, 1.f, 0.f, nullptr, false, false);
		if (MusicComp) { MusicComp->bIsUISound = true; MusicComp->bAllowSpatialization = false; }
	}
	if (MusicComp && !MusicComp->IsPlaying())
	{
		MusicComp->SetVolumeMultiplier(WeedUI::SoundCategoryVolume(2) * GMenuMusicBaseline);
		MusicComp->Play();
	}
}

void UMainMenuWidget::StopMenuMusic()
{
	if (MusicComp) { MusicComp->Stop(); }
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
		if (bOpen) { StartMenuMusic(); } else { StopMenuMusic(); }
		if (bOpen)
		{
			// Standaard op het hoofdmenu; tenzij er een picker is aangevraagd (pauze -> Load).
			if (PendingPickerMode != 0) { OpenPicker(PendingPickerMode); PendingPickerMode = 0; }
			else { ClosePicker(); }
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

	// Music-volume live volgen (slider in Settings -> Audio) x vaste basis-demping.
	if (MusicComp) { MusicComp->SetVolumeMultiplier(WeedUI::SoundCategoryVolume(2) * GMenuMusicBaseline); }

	// Neon-lampen zacht laten flikkeren (alsof ze echt aan staan).
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (int32 i = 0; i < Glows.Num(); ++i)
	{
		if (!Glows[i]) { continue; }
		const float Ph = GlowPhase.IsValidIndex(i) ? GlowPhase[i] : 0.f;
		const float Fr = GlowFreq.IsValidIndex(i) ? GlowFreq[i] : 4.f;
		const float Fl = GlowFlick.IsValidIndex(i) ? GlowFlick[i] : 1.f;
		const bool bCandle = GlowCandle.IsValidIndex(i) && GlowCandle[i] != 0;
		float Osc;
		if (bCandle)
		{
			// Rustige kaars-gloed: trage, zachte golven + minieme ruis + heel af en toe een lichte dip.
			Osc = 0.93f
				+ 0.045f * FMath::Sin(T * 2.3f + Ph)
				+ 0.025f * FMath::Sin(T * 4.7f + Ph * 1.6f)
				+ FMath::FRandRange(-0.012f, 0.012f);
			if (FMath::FRand() < 0.008f) { Osc *= FMath::FRandRange(0.85f, 0.95f); } // zachte vlam-dip
			Osc = FMath::Clamp(Osc, 0.78f, 1.08f);
		}
		else
		{
			// Rustige, levende neon-puls (per lamp schaalbaar): trage golf + rimpel + zeldzame stotter.
			Osc = 0.92f
				+ 0.07f * Fl * FMath::Sin(T * Fr + Ph)
				+ 0.035f * Fl * FMath::Sin(T * Fr * 2.4f + Ph * 1.7f)
				+ FMath::FRandRange(-0.015f * Fl, 0.015f * Fl);
			if (FMath::FRand() < 0.005f * Fl) { Osc *= FMath::FRandRange(0.55f, 0.88f); }
			Osc = FMath::Clamp(Osc, 0.45f, 1.2f);
		}

		const FLinearColor Base = GlowBase.IsValidIndex(i) ? GlowBase[i] : FLinearColor::White;
		const float K = 0.8f + 0.2f * Osc;
		FLinearColor C(Base.R * K, Base.G * K, Base.B * K, FMath::Clamp(Base.A * Osc, 0.f, 1.f));
		Glows[i]->SetBrushColor(C);
	}

	// --- Lamp-editor: sleep een gloed naar z'n lamp ---
	if (bEditGlows)
	{
		APlayerController* PC = GetOwningPlayer();
		FVector2D Vp(1.f, 1.f);
		if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(Vp); }
		float MX = 0.f, MY = 0.f; bool bLmb = false;
		if (PC) { PC->GetMousePosition(MX, MY); bLmb = PC->IsInputKeyDown(EKeys::LeftMouseButton); }
		const FVector2D Frac(Vp.X > 1.f ? MX / Vp.X : 0.f, Vp.Y > 1.f ? MY / Vp.Y : 0.f);

		if (bLmb && !bLmbPrev)
		{
			// Start slepen: pak de dichtstbijzijnde gloed binnen een drempel.
			float Best = 0.07f; int32 Pick = -1;
			for (int32 i = 0; i < GlowFrac.Num(); ++i)
			{
				const float D = (GlowFrac[i] - Frac).Size();
				if (D < Best) { Best = D; Pick = i; }
			}
			DragGlow = Pick;
		}
		if (bLmb && GlowFrac.IsValidIndex(DragGlow))
		{
			GlowFrac[DragGlow] = Frac;
			if (GlowSlots.IsValidIndex(DragGlow) && GlowSlots[DragGlow]) { GlowSlots[DragGlow]->SetAnchors(FAnchors(Frac.X, Frac.Y, Frac.X, Frac.Y)); }
		}
		if (!bLmb && bLmbPrev && DragGlow >= 0)
		{
			SaveGlowPositions();
			DragGlow = -1;
		}
		bLmbPrev = bLmb;

		// Toon altijd ALLE lamp-coördinaten (zodat je ze kunt aflezen/screenshotten).
		if (EditHintText)
		{
			FString List = TEXT("LAMP EDITOR - drag a marker onto its lamp (auto-saved to Saved/MenuGlows.txt)\n");
			for (int32 i = 0; i < GlowFrac.Num(); ++i)
			{
				List += FString::Printf(TEXT("Lamp %d:  %.3f , %.3f%s\n"), i, GlowFrac[i].X, GlowFrac[i].Y, (i == DragGlow) ? TEXT("  <-- dragging") : TEXT(""));
			}
			EditHintText->SetText(FText::FromString(List));
		}

		// Markers bijwerken (volgen de gloeden).
		for (int32 i = 0; i < GlowHandles.Num(); ++i)
		{
			if (!GlowHandles[i] || !GlowFrac.IsValidIndex(i)) { continue; }
			if (UCanvasPanelSlot* HS = Cast<UCanvasPanelSlot>(GlowHandles[i]->Slot))
			{
				HS->SetAnchors(FAnchors(GlowFrac[i].X, GlowFrac[i].Y, GlowFrac[i].X, GlowFrac[i].Y));
			}
		}
	}
}

void UMainMenuWidget::ToggleGlowEdit()
{
	bEditGlows = !bEditGlows;
	DragGlow = -1; bLmbPrev = false;
	if (EditCanvas) { EditCanvas->SetVisibility(bEditGlows ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	if (EditHintText)
	{
		EditHintText->SetVisibility(bEditGlows ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		EditHintText->SetText(FText::FromString(TEXT("LAMP EDITOR  -  drag each numbered marker onto its lamp. Saved automatically. Click 'Edit lamps' again to finish.")));
	}
	if (bEditGlows) { RebuildGlowHandles(); SaveGlowPositions(); /* dump huidige posities meteen */ }
}

void UMainMenuWidget::RebuildGlowHandles()
{
	if (!EditCanvas) { return; }
	EditCanvas->ClearChildren();
	GlowHandles.Reset();
	GlowHandleLabels.Reset();
	for (int32 i = 0; i < GlowFrac.Num(); ++i)
	{
		UBorder* H = WidgetTree->ConstructWidget<UBorder>();
		H->SetBrush(WeedUI::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.22f), 4.f));
		H->SetVisibility(ESlateVisibility::HitTestInvisible);
		UCanvasPanelSlot* S = EditCanvas->AddChildToCanvas(H);
		S->SetAnchors(FAnchors(GlowFrac[i].X, GlowFrac[i].Y, GlowFrac[i].X, GlowFrac[i].Y));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		S->SetSize(FVector2D(28.f, 28.f));
		UTextBlock* L = WeedUI::Text(WidgetTree, FString::Printf(TEXT("%d"), i), 12, FLinearColor::White, true, true);
		H->SetContent(L);
		GlowHandles.Add(H);
		GlowHandleLabels.Add(L);
	}
}
