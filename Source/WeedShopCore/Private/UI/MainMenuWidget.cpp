#include "UI/MainMenuWidget.h"
#include "WeedShopCore.h"
#include "WeedShopVersion.h" // centrale versie-string (auto-bijgewerkt door upload-build.ps1)

#include "UI/WeedUiStyle.h"
#include "UI/BootCoverWidget.h"
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
		const int64 Euro = WeedRoundEuros(Cents) / 100;
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

	// Standaard swatch-knopstijl (exact zoals de 6 hoofdscherm-knoppen): donker normaal -> paars hover.
	// Zo voelen de New Game/Co-op-sub-knoppen als hetzelfde menu.
	FButtonStyle SwatchStyle(UTexture2D* Tex, const FMargin& Pad)
	{
		FButtonStyle S;
		const FLinearColor Norm(0.02f, 0.02f, 0.04f, 0.80f);
		const FLinearColor Hover(0.62f, 0.26f, 0.95f, 0.97f);
		const FLinearColor Press(0.74f, 0.36f, 1.00f, 1.00f);
		if (Tex) { S.Normal = SwatchBrush(Tex, Norm); S.Hovered = SwatchBrush(Tex, Hover); S.Pressed = SwatchBrush(Tex, Press); }
		else { S.Normal = WeedUI::Rounded(Norm, 6.f); S.Hovered = WeedUI::Rounded(Hover, 6.f); S.Pressed = WeedUI::Rounded(Press, 6.f); }
		S.NormalPadding = Pad; S.PressedPadding = Pad;
		return S;
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

	// --- Boot-canary: string-geladen UI-kit-templates + joint-meshes MOETEN in de packaged build resolven.
	// Deze werden al eerder stil weggecookt (toggles/sliders/pauzemenu/skins). Dit logt bij de eerste boot
	// of ze laden -> een MISSING-regel wijst direct een cook-gat aan (Shipping-verificatie zonder UI-klik). ---
	{
		static bool bCookCheckDone = false;
		if (!bCookCheckDone)
		{
			bCookCheckDone = true;
			auto Cls = [](const TCHAR* P) { return LoadClass<UUserWidget>(nullptr, P) != nullptr; };
			auto Obj = [](const TCHAR* P) { return LoadObject<UObject>(nullptr, P) != nullptr; };
			const bool bT = Cls(TEXT("/Game/minimalist_gui/widgets/templates/toggle/W_Toggle_Template.W_Toggle_Template_C"));
			const bool bS = Cls(TEXT("/Game/minimalist_gui/widgets/templates/slider/W_Slider_Template.W_Slider_Template_C"));
			const bool bP = Cls(TEXT("/Game/UI/Screens/WBP_PauseMenu.WBP_PauseMenu_C"));
			const bool bJS = Obj(TEXT("/Game/_Project/Models/Joints/SM_JointSmall.SM_JointSmall"));
			const bool bJF = Obj(TEXT("/Game/_Project/Models/Joints/SM_JointFat.SM_JointFat"));
			UE_LOG(LogWeedShop, Display, TEXT("[COOKCHECK] Toggle=%d Slider=%d Pause=%d JointSmall=%d JointFat=%d -> %s"),
				bT, bS, bP, bJS, bJF, (bT && bS && bP && bJS && bJF) ? TEXT("ALL OK") : TEXT("*** MISSING IN BUILD ***"));
		}
	}

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
		UTextBlock* Ver = WeedUI::Text(WidgetTree, FString::Printf(TEXT("v%s  -  pre-alpha"), WEEDSHOP_VERSION_STRING), 12, FLinearColor(0.70f, 0.66f, 0.80f), false);
		UOverlaySlot* VS = Layers->AddChildToOverlay(Ver);
		VS->SetHorizontalAlignment(HAlign_Left); VS->SetVerticalAlignment(VAlign_Bottom); VS->SetPadding(FMargin(24.f, 0.f, 0.f, 16.f));

		// (De "Edit lamps"-editorknop is verwijderd uit het main menu.)

		// Hint tijdens het editen (bovenaan).
		EditHintText = WeedUI::Text(WidgetTree, TEXT(""), 13, FLinearColor(0.85f, 0.72f, 1.f), true, true);
		UOverlaySlot* EHS = Layers->AddChildToOverlay(EditHintText);
		EHS->SetHorizontalAlignment(HAlign_Center); EHS->SetVerticalAlignment(VAlign_Top); EHS->SetPadding(FMargin(0.f, 16.f, 0.f, 0.f));
		EditHintText->SetVisibility(ESlateVisibility::Collapsed);

		// --- Slot-picker (3 saves) — LINKS op de achtergrond (zoals de hoofdmenu-knoppen), GEEN kaart ---
		// Eigen canvas voor de keuze-panelen: proportioneel positioneren (DPI-onafhankelijk, zoals de menu-knoppen).
		UCanvasPanel* PanelCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		PanelCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UOverlaySlot* PanC = Layers->AddChildToOverlay(PanelCanvas);
		PanC->SetHorizontalAlignment(HAlign_Fill); PanC->SetVerticalAlignment(VAlign_Fill);

		USizeBox* PickSize = WidgetTree->ConstructWidget<USizeBox>();
		PickSize->SetWidthOverride(470.f);
		SlotPanel = PickSize;
		UCanvasPanelSlot* PkS = PanelCanvas->AddChildToCanvas(PickSize);
		PkS->SetAnchors(FAnchors(0.5f, 0.438f, 0.5f, 0.438f)); // horizontaal gecentreerd, top op CONTINUE-hoogte
		PkS->SetAlignment(FVector2D(0.5f, 0.f));
		PkS->SetAutoSize(true); PkS->SetPosition(FVector2D(0.f, 0.f));

		UVerticalBox* PickVB = WidgetTree->ConstructWidget<UVerticalBox>();
		PickSize->SetContent(PickVB);
		// Slot-rijen (dynamisch per refresh) — EERST in de lijst, precies waar de menu-knoppen staan.
		// Geen titel + geen autosave-koptekst-balk meer: gewoon een schone knoppenlijst, net als het hoofdmenu.
		SlotsBox = WidgetTree->ConstructWidget<UVerticalBox>();
		PickVB->AddChildToVerticalBox(SlotsBox);

		// Autosave-toggle als gewone swatch-knop onderaan (geen losse "laatste save"-tekst).
		AutosaveBtn = WidgetTree->ConstructWidget<UWeedActionButton>();
		AutosaveBtn->OnClicked.AddDynamic(AutosaveBtn, &UWeedActionButton::Handle);
		AutosaveBtn->OnAction.BindLambda([this](int32, int32) { OnToggleAutosave(); });
		AutosaveBtn->SetStyle(SwatchStyle(SwatchTex, FMargin(16.f, 11.f)));
		AutosaveLabel = WeedUI::Text(WidgetTree, TEXT("Autosave: aan"), 15, FLinearColor(0.92f, 0.96f, 1.f), true, true);
		AutosaveBtn->SetContent(AutosaveLabel);
		PickVB->AddChildToVerticalBox(AutosaveBtn)->SetPadding(FMargin(0.f, 6.f, 0.f, 4.f));

		UWeedActionButton* BackBtn = WidgetTree->ConstructWidget<UWeedActionButton>();
		BackBtn->OnClicked.AddDynamic(BackBtn, &UWeedActionButton::Handle);
		BackBtn->OnAction.BindLambda([this](int32, int32) { ClosePicker(); });
		BackBtn->SetStyle(SwatchStyle(SwatchTex, FMargin(16.f, 10.f)));
		BackBtn->SetContent(WeedUI::Text(WidgetTree, TEXT("Back"), 13, FLinearColor::White, true));
		PickVB->AddChildToVerticalBox(BackBtn)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));

		SlotPanel->SetVisibility(ESlateVisibility::Collapsed); // start verborgen

		// (CO-OP staat nu gewoon als knop in de menu-lijst hierboven, niet meer los rechtsboven.)

		// --- CO-OP-kaart (host / join via IP) — gecentreerd, verborgen tot je op CO-OP klikt ---
		{
			USizeBox* CoopSize = WidgetTree->ConstructWidget<USizeBox>();
			CoopSize->SetWidthOverride(470.f);
			CoopPanel = CoopSize;
			UCanvasPanelSlot* CpS = PanelCanvas->AddChildToCanvas(CoopSize);
			CpS->SetAnchors(FAnchors(0.5f, 0.55f, 0.5f, 0.55f)); // co-op-formulier iets onder midden (onder het logo)
			CpS->SetAlignment(FVector2D(0.5f, 0.5f));
			CpS->SetAutoSize(true); CpS->SetPosition(FVector2D(0.f, 0.f));

			UVerticalBox* CoopVB = WidgetTree->ConstructWidget<UVerticalBox>();
			CoopSize->SetContent(CoopVB);
			CoopVB->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("CO-OP  (LAN)"), 24, FLinearColor(0.78f, 0.55f, 1.f), true, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

			auto BigBtn = [&](const FString& Label, const FLinearColor& Col, TFunction<void()> Fn) -> UWeedActionButton*
			{
				UWeedActionButton* B = WidgetTree->ConstructWidget<UWeedActionButton>();
				B->OnClicked.AddDynamic(B, &UWeedActionButton::Handle);
				B->OnAction.BindLambda([Fn](int32, int32) { if (Fn) { Fn(); } });
				(void)Col; // sub-knoppen krijgen de uniforme swatch-stijl (zoals het hoofdscherm)
				B->SetStyle(SwatchStyle(SwatchTex, FMargin(16.f, 12.f)));
				B->SetContent(WeedUI::Text(WidgetTree, Label, 15, FLinearColor::White, true, true));
				return B;
			};

			// === Stap 0: keuze Host / Join ===
			UVerticalBox* ChooseBox = WidgetTree->ConstructWidget<UVerticalBox>();
			CoopChooseBox = ChooseBox;
			ChooseBox->AddChildToVerticalBox(BigBtn(TEXT("Host a game"), FLinearColor::White, [this]() { SetCoopStage(1); }))
				->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
			ChooseBox->AddChildToVerticalBox(BigBtn(TEXT("Join a game"), FLinearColor::White, [this]() { SetCoopStage(2); }))
				->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
			CoopVB->AddChildToVerticalBox(ChooseBox);

			// === Stap 1: host (modus-keuze + host-knop) ===
			UVerticalBox* HostBox = WidgetTree->ConstructWidget<UVerticalBox>();
			CoopHostBox = HostBox;
			bHostCompetitive = false;
			CoopModeBtn = BigBtn(TEXT("Mode: CO-OP  (build together)"), FLinearColor(0.14f, 0.30f, 0.20f, 0.96f), [this]() { OnToggleCoopMode(); });
			CoopModeLabel = Cast<UTextBlock>(CoopModeBtn->GetContent()); // persistent label -> later SetText i.p.v. rebuild
			HostBox->AddChildToVerticalBox(CoopModeBtn)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
			HostBox->AddChildToVerticalBox(BigBtn(TEXT("Host new co-op game"), FLinearColor(0.16f, 0.34f, 0.22f, 0.96f), [this]() { OnHostCoop(); }))
				->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
			CoopVB->AddChildToVerticalBox(HostBox);

			// === Stap 2: join (IP-veld + join-knop) ===
			UVerticalBox* JoinBox = WidgetTree->ConstructWidget<UVerticalBox>();
			CoopJoinBox = JoinBox;
			JoinBox->AddChildToVerticalBox(WeedUI::Text(WidgetTree, TEXT("Connect to host (IP):"), 13, FLinearColor(0.82f, 0.86f, 1.f), false))->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			CoopIpBox = WidgetTree->ConstructWidget<UEditableTextBox>();
			CoopIpBox->SetHintText(FText::FromString(TEXT("192.168.x.x")));
			CoopIpBox->SetText(FText::FromString(TEXT("127.0.0.1")));
			{
				// Donker invoerveld dat bij het thema past (i.p.v. het standaard witte vak).
				FEditableTextBoxStyle TBS = CoopIpBox->WidgetStyle;
				TBS.BackgroundImageNormal   = WeedUI::Rounded(FLinearColor(0.05f, 0.06f, 0.10f, 0.92f), 6.f);
				TBS.BackgroundImageHovered  = WeedUI::Rounded(FLinearColor(0.07f, 0.09f, 0.14f, 0.95f), 6.f);
				TBS.BackgroundImageFocused  = WeedUI::Rounded(FLinearColor(0.10f, 0.12f, 0.19f, 0.98f), 6.f);
				TBS.BackgroundImageReadOnly = WeedUI::Rounded(FLinearColor(0.05f, 0.06f, 0.10f, 0.92f), 6.f);
				TBS.ForegroundColor = FSlateColor(FLinearColor(0.92f, 0.95f, 1.f));
				TBS.Padding = FMargin(12.f, 9.f);
				CoopIpBox->SetWidgetStyle(TBS);
			}
			JoinBox->AddChildToVerticalBox(CoopIpBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
			JoinBox->AddChildToVerticalBox(BigBtn(TEXT("Join"), FLinearColor(0.18f, 0.22f, 0.40f, 0.96f), [this]() { OnJoinCoop(); }))
				->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
			CoopVB->AddChildToVerticalBox(JoinBox);

			// Gedeelde Back: in host/join terug naar de keuze, in de keuze sluit co-op.
			CoopVB->AddChildToVerticalBox(BigBtn(TEXT("Back"), FLinearColor(0.30f, 0.13f, 0.14f, 0.96f), [this]() { if (CoopStage != 0) { SetCoopStage(0); } else { CloseCoop(); } }));

			SetCoopStage(0); // start bij de Host/Join-keuze

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
	AddBtn(TEXT("Continue"),   Hi,   [this]() { OnContinue(); }); // laatst gebruikte slot -> nieuwste autosave/save
	AddBtn(TEXT("New game"),   Dark, [this]() { OnStart(); });
	AddBtn(TEXT("Load game"),  Dark, [this]() { OnContinue(); });
	AddBtn(TEXT("Co-op"),      Dark, [this]() { OpenCoop(); });
	AddBtn(TEXT("Settings"),   Dark, [this]() { OnSettings(); });
	AddBtn(TEXT("Exit game"),  Dark, [this]() { OnQuit(); });

	StatusText = WeedUI::Text(WidgetTree, TEXT(""), 12, FLinearColor(0.8f, 0.7f, 1.f), false);
	Left->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(2.f, 14.f, 0.f, 0.f));

	// Eigen versie-nummer linksonder (ook in de fallback-versie).
	UTextBlock* Ver = WeedUI::Text(WidgetTree, FString::Printf(TEXT("v%s  -  pre-alpha"), WEEDSHOP_VERSION_STRING), 12, FLinearColor(0.70f, 0.66f, 0.80f), false);
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
	SetCoopStage(0); // altijd starten bij de Host/Join-keuze
}

void UMainMenuWidget::CloseCoop()
{
	if (CoopPanel) { CoopPanel->SetVisibility(ESlateVisibility::Collapsed); }
	if (MenuCanvas) { MenuCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
}

void UMainMenuWidget::SetCoopStage(int32 Stage)
{
	CoopStage = Stage;
	if (CoopChooseBox) { CoopChooseBox->SetVisibility(Stage == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (CoopHostBox)   { CoopHostBox->SetVisibility(Stage == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (CoopJoinBox)   { CoopJoinBox->SetVisibility(Stage == 2 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void UMainMenuWidget::OnHostCoop()
{
	// Host = vers spel als listen-server. Hergebruik de New Game-slot-keuze (-> OnSlotChosen host-tak).
	bHostMode = true;
	OpenPicker(1);
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

	// FULLSCREEN-LAADSCHERM meteen tonen (zelfde look als de movie + de in-game cover) zodat de VERBIND-fase
	// niet kaal is met alleen kleine 'connecting'-tekst. De gedeelde laad-timer wordt hier vers gezet zodat de
	// cover-progress klopt; daarna nemen de movie-loadingscreen (bij map-load) en de in-game cover het naadloos
	// over - alle drie zien er identiek uit, dus de overgang is onzichtbaar.
	WeedShop_RequestGameLoadingScreen();
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (UBootCoverWidget* Cover = CreateWidget<UBootCoverWidget>(PC, UBootCoverWidget::StaticClass()))
		{
			Cover->AddToViewport(1000); // bovenop het hele menu
		}
	}

	Save->JoinLan(Ip);
}

// Alleen de autosave-knop-tekst + kleur in-place bijwerken (een toggle wijzigt géén slot-rij).
void UMainMenuWidget::UpdateAutosaveLabel()
{
	if (!AutosaveLabel) { return; }
	const USaveGameSubsystem* Save = GetSave(GetWorld());
	const bool bOn = Save ? Save->IsAutosaveEnabled() : true;
	AutosaveLabel->SetText(FText::FromString(bOn ? TEXT("Autosave: aan") : TEXT("Autosave: uit")));
	AutosaveLabel->SetColorAndOpacity(FSlateColor(bOn ? FLinearColor(0.74f, 0.55f, 1.f) : FLinearColor(1.f, 0.6f, 0.55f)));
}

void UMainMenuWidget::RefreshSlots()
{
	if (PickerTitle)
	{
		const TCHAR* Title = (MenuMode == 1) ? TEXT("NEW GAME  -  pick a slot") : TEXT("LOAD  -  pick a slot");
		PickerTitle->SetText(FText::FromString(Title));
	}
	USaveGameSubsystem* Save = GetSave(GetWorld());

	// Vaste balk bijwerken: autosave-status + laatste save-tijdstip (in-place, geen rebuild).
	UpdateAutosaveLabel();
	if (LastSaveText)
	{
		FDateTime Last;
		const bool bAny = Save && Save->GetMostRecentSaveTime(Last);
		LastSaveText->SetText(FText::FromString(bAny ? FString::Printf(TEXT("Laatste save: %s"), *FmtAgo(Last)) : TEXT("No save yet")));
	}

	if (!SlotsBox) { return; }

	const FButtonStyle MainStyle = SwatchStyle(SwatchTex, FMargin(16.f, 12.f));
	const FButtonStyle AutoStyle = SwatchStyle(SwatchTex, FMargin(12.f, 7.f));

	// Persistente rij-pool op maat brengen (1 rij per save-slot). Aantal slots is vast (NumSlots),
	// dus dit groeit precies één keer en daarna nooit meer -> geen ClearChildren, geen flash.
	while (SlotMainBtns.Num() < USaveGameSubsystem::NumSlots)
	{
		const int32 SlotIdx = SlotMainBtns.Num();

		// Hoofd-knop van deze rij.
		UWeedActionButton* SB = WidgetTree->ConstructWidget<UWeedActionButton>();
		SB->OnClicked.AddDynamic(SB, &UWeedActionButton::Handle);
		SB->OnAction.BindLambda([this, SlotIdx](int32, int32) { OnSlotChosen(SlotIdx); });
		SB->SetStyle(MainStyle);
		UTextBlock* SLbl = WeedUI::Text(WidgetTree, FString(), 14, FLinearColor::White, true);
		SB->SetContent(SLbl);
		SlotsBox->AddChildToVerticalBox(SB);

		// Autosave-sub-knop van deze rij (in Load, alleen als er náást een handmatige save óók een
		// autosave is). Altijd gebouwd, standaard Collapsed -> we tonen/verbergen 'm i.p.v. add/remove.
		UWeedActionButton* AB = WidgetTree->ConstructWidget<UWeedActionButton>();
		AB->OnClicked.AddDynamic(AB, &UWeedActionButton::Handle);
		AB->OnAction.BindLambda([this, SlotIdx](int32, int32) { OnLoadAutosave(SlotIdx); });
		AB->SetStyle(AutoStyle);
		UTextBlock* ALbl = WeedUI::Text(WidgetTree, FString(), 11, FLinearColor(0.75f, 0.86f, 1.f), true);
		AB->SetContent(ALbl);
		AB->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* AS = Cast<UVerticalBoxSlot>(SlotsBox->AddChildToVerticalBox(AB)))
		{
			AS->SetPadding(FMargin(16.f, 0.f, 0.f, 8.f));
		}

		SlotMainBtns.Add(SB); SlotMainLabels.Add(SLbl);
		SlotAutoBtns.Add(AB); SlotAutoLabels.Add(ALbl);
		SlotRowSigs.Add(TEXT("\x01")); // sentinel -> forceer eerste vulling
	}

	// Per-rij diffen: alleen een rij waarvan de handtekening wijzigde krijgt nieuwe tekst/stijl.
	for (int32 s = 0; s < USaveGameSubsystem::NumSlots; ++s)
	{
		FSaveSlotInfo Manual; const bool bManual = Save && Save->GetSlotDetailsEx(s, false, Manual);
		FSaveSlotInfo Auto;   const bool bAuto   = Save && Save->GetSlotDetailsEx(s, true, Auto);
		const bool bHasAny = bManual || bAuto;
		const bool bShowAuto = (MenuMode == 2 && bManual && bAuto);

		// Handtekening: alle zichtbare waarden + MenuMode (bepaalt enable + autosave-sub + padding).
		const FSaveSlotInfo* Show = bManual ? &Manual : (bAuto ? &Auto : nullptr);
		const FString Sig = FString::Printf(TEXT("M%d|%d%d|D%d|C%lld|L%d|T%lld|P%d|A%d%d|aD%d|aC%lld|aL%d|aT%lld"),
			MenuMode,
			bManual ? 1 : 0, bAuto ? 1 : 0,
			Show ? Show->DayNumber : -1, (long long)(Show ? Show->TotalCents : 0), Show ? Show->CrewLevel : -1,
			(long long)(Show ? Show->SavedAt.GetTicks() : 0), Show ? Show->NumPlayers : -1,
			bShowAuto ? 1 : 0, 0,
			bShowAuto ? Auto.DayNumber : -1, (long long)(bShowAuto ? Auto.TotalCents : 0), bShowAuto ? Auto.CrewLevel : -1,
			(long long)(bShowAuto ? Auto.SavedAt.GetTicks() : 0));

		if (!SlotMainBtns.IsValidIndex(s) || SlotRowSigs[s] == Sig) { continue; }
		SlotRowSigs[s] = Sig;

		// Hoofd-knop: in Load laadt 'ie je handmatige save (of de autosave als er geen handmatige is);
		// in New start 'ie een nieuw spel in dit slot.
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
		if (SlotMainLabels.IsValidIndex(s) && SlotMainLabels[s]) { SlotMainLabels[s]->SetText(FText::FromString(Line)); }
		SlotMainBtns[s]->SetIsEnabled(MenuMode == 1 || bHasAny); // lege slots zijn alleen in New klikbaar
		const float BottomPad = bShowAuto ? 2.f : 5.f;
		if (UVerticalBoxSlot* MS = Cast<UVerticalBoxSlot>(SlotMainBtns[s]->Slot)) { MS->SetPadding(FMargin(0.f, 4.f, 0.f, BottomPad)); }

		// Autosave-sub-knop: alleen tonen (met nieuwe tekst) in Load als er ook een autosave is; anders Collapsen.
		if (SlotAutoBtns.IsValidIndex(s) && SlotAutoBtns[s])
		{
			if (bShowAuto)
			{
				const FString ALine = FString::Printf(TEXT("autosave  -  Day %d   %s   Lvl %d   -   %s"),
					Auto.DayNumber, *FmtEuro(Auto.TotalCents), Auto.CrewLevel, *FmtAgo(Auto.SavedAt));
				if (SlotAutoLabels.IsValidIndex(s) && SlotAutoLabels[s]) { SlotAutoLabels[s]->SetText(FText::FromString(ALine)); }
				SlotAutoBtns[s]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
			else
			{
				SlotAutoBtns[s]->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

void UMainMenuWidget::OnToggleAutosave()
{
	if (USaveGameSubsystem* Save = GetSave(GetWorld()))
	{
		Save->SetAutosaveEnabled(!Save->IsAutosaveEnabled());
		UpdateAutosaveLabel(); // enkel de toggle-tekst/kleur; slot-rijen wijzigen niet
	}
}

void UMainMenuWidget::OnSlotChosen(int32 SlotIdx)
{
	USaveGameSubsystem* Save = GetSave(GetWorld());
	if (!Save) { return; }
	if (MenuMode == 1) // New Game: direct starten (geen mode-keuze meer; elke verse game is Normaal)
	{
		// CityBeachStrip is de enige speelmap. Geldt ook voor de co-op host: de listen-server
		// reist hierheen en joiners volgen automatisch.
		Save->SetPendingMap(TEXT("/Game/CityBeachStrip/Maps/CityBeachStrip"));
		if (bHostMode) { bHostMode = false; Save->SetPendingCoopCompetitive(bHostCompetitive); Save->HostNewGameLan(SlotIdx); } // co-op host (listen)
		else { Save->RequestNewGame(SlotIdx); }
	}
	else // Load handmatige save (level herlaadt, daarna save toegepast)
	{
		Save->RequestLoad(SlotIdx, false);
	}
}

void UMainMenuWidget::OnToggleCoopMode()
{
	bHostCompetitive = !bHostCompetitive;
	if (CoopModeLabel)
	{
		// Alleen de tekst wisselen op het persistente label (geen style-rebuild / SetContent).
		const FString L = bHostCompetitive ? TEXT("Mode: COMPETITIVE  (versus)") : TEXT("Mode: CO-OP  (build together)");
		CoopModeLabel->SetText(FText::FromString(L));
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
