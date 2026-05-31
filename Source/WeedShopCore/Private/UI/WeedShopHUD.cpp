#include "UI/WeedShopHUD.h"

#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"
#include "World/HeatComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Progression/StoreComponent.h"
#include "Phone/ContactsComponent.h"
#include "Phone/PhoneClientComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Interaction/Interactable.h"
#include "Cultivation/GrowPlant.h"
#include "Cultivation/SoilTypes.h"
#include "Cultivation/PotTypes.h"
#include "Cultivation/WaterCanComponent.h"
#include "Placement/PlaceableTypes.h"
#include "Customer/CustomerBase.h"
#include "Placement/BuildComponent.h"
#include "EngineUtils.h"

namespace
{
	constexpr float PhoneW = 380.f;
	constexpr float PhoneH = 660.f;
	constexpr float RowH = 26.f;

	// Leesbare naam voor een item-id, zodat de speler "Wiet" vs "Zaadje" duidelijk ziet
	// (zodat de speler wiet en zaadjes duidelijk uit elkaar houdt).
	FString PrettyItemName(const FName ItemId)
	{
		const FString S = ItemId.ToString();
		if (S.StartsWith(TEXT("Bud_")))    { return FString::Printf(TEXT("Weed: %s"), *S.RightChop(4)); }
		if (S.StartsWith(TEXT("Seed_")))   { return FString::Printf(TEXT("Seed: %s"), *S.RightChop(5)); }
		if (S.StartsWith(TEXT("Soil_")))   { return FString::Printf(TEXT("Soil: %s"), *S.RightChop(5)); }
		if (S.StartsWith(TEXT("WaterBottle_"))) { return FString::Printf(TEXT("Bottle: %s"), *S.RightChop(12)); }
		if (S.StartsWith(TEXT("Joint_")))  { return FString::Printf(TEXT("Joint %s"), *S.RightChop(6)); }
		if (S.StartsWith(TEXT("Papers_")))  { return FString::Printf(TEXT("Papers: %s"), *S.RightChop(7)); }
		if (S.StartsWith(TEXT("Pot_")))     { return FString::Printf(TEXT("Pot: %s"), *S.RightChop(4)); }
		return S;
	}
}

UPhoneClientComponent* AWeedShopHUD::GetPhone() const
{
	APawn* P = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	return P ? P->FindComponentByClass<UPhoneClientComponent>() : nullptr;
}

void AWeedShopHUD::DrawHUD()
{
	Super::DrawHUD();

	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	APawn* P = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	float Y = 40.f;
	const float X = 40.f;

	if (GS && GS->GetEconomy())
	{
		DrawText(FString::Printf(TEXT("Cash: EUR %.2f"), GS->GetEconomy()->GetBalanceEuros()),
			FLinearColor(0.4f, 1.f, 0.4f), X, Y, Font);
		Y += 26.f;
	}

	if (GS && GS->GetMilestones())
	{
		const UMilestoneComponent* M = GS->GetMilestones();
		const TCHAR* PhaseName = TEXT("Street dealer");
		switch (M->GetCurrentPhase())
		{
		case EShopPhase::Shop:      PhaseName = TEXT("Shop"); break;
		case EShopPhase::Franchise: PhaseName = TEXT("Franchise"); break;
		default: break;
		}
		DrawText(FString::Printf(TEXT("Phase: %s   (total earned EUR %.2f)"),
			PhaseName, M->GetTotalEarnedCents() / 100.f), FLinearColor(1.f, 0.85f, 0.3f), X, Y, Font);
		Y += 26.f;
	}

	if (GS && GS->GetDayCycle())
	{
		const UDayCycleComponent* Day = GS->GetDayCycle();
		const int32 TotalMin = FMath::RoundToInt(Day->GetCycleFraction() * 24.f * 60.f);
		const int32 HH = (TotalMin / 60) % 24;
		const int32 MM = TotalMin % 60;
		DrawText(FString::Printf(TEXT("Time: %02d:%02d   (%s)"), HH, MM,
			Day->IsNight() ? TEXT("Night") : TEXT("Day")), FLinearColor::White, X, Y, Font);
		Y += 26.f;
	}

	// Heat / politierisico.
	if (GS && GS->GetHeat())
	{
		const float H = GS->GetHeat()->GetHeat();
		const FLinearColor HeatCol = H >= 75.f ? FLinearColor::Red
			: (H >= 40.f ? FLinearColor(1.f, 0.6f, 0.2f) : FLinearColor(0.6f, 0.8f, 0.6f));
		DrawText(FString::Printf(TEXT("Heat: %.0f%%"), H), HeatCol, X, Y, Font);
		Y += 26.f;
	}

	// Waterfles-stand.
	if (P)
	{
		if (const UWaterCanComponent* Can = P->FindComponentByClass<UWaterCanComponent>())
		{
			if (Can->HasBottle())
			{
				const int32 Cur = Can->GetCharges();
				const int32 Max = Can->GetMaxCharges();
				DrawText(FString::Printf(TEXT("Water: %d/%d"), Cur, Max),
					Cur > 0 ? FLinearColor(0.5f, 0.8f, 1.f) : FLinearColor(1.f, 0.6f, 0.3f), X, Y, Font);
				Y += 26.f;
			}
		}
	}

	// (Voorraad-overzicht staat nu in het inventory-scherm (I), niet meer als tekst linksboven.)

	// Klikbare telefoon.
	if (UPhoneClientComponent* Phone = GetPhone())
	{
		if (Phone->IsOpen())
		{
			HoverTooltip.Empty();
			DrawPhone(Phone);
		}
		else if (Phone->IsRollOpen())
		{
			DrawRollUI(Phone);
		}
		else if (Phone->IsDealOpen())
		{
			DrawDealUI(Phone);
		}
		else if (Phone->IsInventoryOpen())
		{
			if (UInventoryComponent* Inv = P ? P->FindComponentByClass<UInventoryComponent>() : nullptr)
			{
				DrawInventoryUI(Inv);
				// Merge-bevestiging als popup bovenop de inventory.
				if (Phone->IsMergeOpen())
				{
					DrawMergeUI(Phone, Inv);
				}
			}
		}
		else if (Phone->IsPotUpgradeOpen())
		{
			DrawPotUpgradeUI(Phone);
		}
		else if (PlayerOwner)
		{
			// Geen UI open: cursor terug naar standaard (anders blijft het handje hangen).
			PlayerOwner->CurrentMouseCursor = EMouseCursor::Default;
		}
	}

	// Plaats-modus hint (midden-onder).
	if (P)
	{
		if (const UBuildComponent* BC = P->FindComponentByClass<UBuildComponent>())
		{
			if (BC->IsPlacing())
			{
				const float CX = Canvas ? Canvas->ClipX * 0.5f : 640.f;
				const float CY = Canvas ? Canvas->ClipY : 720.f;
				const bool bValid = BC->IsPlacementValid();
				DrawText(bValid ? TEXT("Left-click to place   |   R = rotate   |   Shift = snap to grid   |   switch slot to put away")
								: TEXT("Aim at the floor...   |   R = rotate   |   Shift = snap to grid"),
					bValid ? FLinearColor(0.6f, 1.f, 0.6f) : FLinearColor(1.f, 0.8f, 0.4f),
					CX - 220.f, CY - 140.f, Font);
			}
		}
	}

	// Interactie-prompt.
	if (P)
	{
		if (const UInteractionComponent* IC = P->FindComponentByClass<UInteractionComponent>())
		{
			if (AActor* Focus = IC->GetFocusedActor())
			{
				const float CX = Canvas ? Canvas->ClipX * 0.5f : 640.f;
				const float CY = Canvas ? Canvas->ClipY * 0.5f : 360.f;

				// Mooie info-kaart als je een plant aankijkt.
				if (AGrowPlant* Plant = Cast<AGrowPlant>(Focus))
				{
					const float PW = 300.f;
					const float PXp = CX - PW * 0.5f;
					const float PanelH = Plant->IsPlanted() ? (150.f + Plant->GetNumSlots() * 11.f) : 96.f;
						float PYp = CY - PanelH * 0.5f;
					DrawRect(FLinearColor(0.04f, 0.06f, 0.04f, 0.9f), PXp, PYp, PW, PanelH);
					float ly = PYp + 8.f;
					const float lx = PXp + 12.f;

					if (!Plant->IsPlanted())
					{
						DrawText(TEXT("Empty pot"), FLinearColor(0.7f, 1.f, 0.7f), lx, ly, Font); ly += 24.f;
						if (Plant->HasSoil())
							{
								FSoilDef _sd; const FString _sn = GetSoilDef(Plant->GetSoilId(), _sd) ? _sd.DisplayName : Plant->GetSoilId().ToString();
								DrawText(FString::Printf(TEXT("Soil: %s  (%d harvests left)"), *_sn, Plant->GetSoilUsesLeft()), FLinearColor(0.6f, 0.9f, 0.6f), lx, ly, Font); ly += 18.f;
								DrawText(TEXT("Plant a seed (E)   |   hold G to pick up"), FLinearColor::White, lx, ly, Font); ly += 18.f;
							}
							else
							{
								DrawText(TEXT("No soil! Add soil (E) - buy it from the supplier"), FLinearColor(1.f, 0.7f, 0.3f), lx, ly, Font); ly += 18.f;
								DrawText(TEXT("(soil is required before planting)   |   hold G to pick up"), FLinearColor(0.7f, 0.7f, 0.7f), lx, ly, Font); ly += 18.f;
							}
						if (const UBuildComponent* BC = P->FindComponentByClass<UBuildComponent>())
						{
							const float A = BC->GetPickupAlpha();
							if (A > 0.f)
							{
								DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.9f), lx, ly, PW - 24.f, 8.f);
								DrawRect(FLinearColor(1.f, 0.8f, 0.2f, 0.95f), lx, ly, (PW - 24.f) * A, 8.f);
							}
						}
					}
					else
					{
						const int32 Planted = Plant->GetPlantedCount();
						const int32 NumSlots = Plant->GetNumSlots();
						const int32 Ready = Plant->GetReadyCount();
						DrawText(FString::Printf(TEXT("Plants: %d / %d"), Planted, NumSlots), FLinearColor(0.7f, 1.f, 0.7f), lx, ly, Font); ly += 22.f;

						// Groei-progressiebalk per plant (groen = oogstklaar).
						DrawText(TEXT("Growth:"), FLinearColor(0.8f, 0.85f, 1.f), lx, ly, Font); ly += 16.f;
						const float BarW = PW - 24.f;
						for (int32 s = 0; s < NumSlots; ++s)
						{
							DrawRect(FLinearColor(0.18f, 0.18f, 0.20f, 0.9f), lx, ly, BarW, 8.f);
							if (Plant->IsSlotPlanted(s))
							{
								const float Fr = Plant->GetSlotFraction(s);
								const FLinearColor Col = Plant->IsSlotReady(s) ? FLinearColor(0.45f, 0.95f, 0.35f) : FLinearColor(0.4f, 0.8f, 1.f);
								DrawRect(Col, lx, ly, BarW * Fr, 8.f);
							}
							ly += 11.f;
						}
						ly += 4.f;

						// Water-balk (vocht nu; loopt leeg, vul bij met de fles).
						const float Wtr = Plant->GetWaterLevel();
						DrawText(FString::Printf(TEXT("Water: %.0f%%"), Wtr * 100.f),
							Wtr >= 0.3f ? FLinearColor(0.4f, 0.8f, 1.f) : FLinearColor(1.f, 0.55f, 0.3f), lx, ly, Font); ly += 16.f;
						DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.9f), lx, ly, PW - 24.f, 8.f);
						DrawRect(FLinearColor(0.3f, 0.7f, 1.f, 0.95f), lx, ly, (PW - 24.f) * Wtr, 8.f); ly += 16.f;

						// Gezondheid/care-balk (volgt het water; bepaalt de kwaliteit).
						const float Care = Plant->GetCareMultiplier();
						DrawText(FString::Printf(TEXT("Health: %.0f%%"), Care * 100.f),
							Care >= 0.8f ? FLinearColor::Green : (Care >= 0.5f ? FLinearColor(1.f, 0.7f, 0.2f) : FLinearColor(1.f, 0.4f, 0.4f)),
							lx, ly, Font); ly += 16.f;
						DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.9f), lx, ly, PW - 24.f, 8.f);
						DrawRect(FLinearColor(0.4f, 0.9f, 0.4f, 0.95f), lx, ly, (PW - 24.f) * Care, 8.f); ly += 16.f;

						DrawText(FString::Printf(TEXT("Expected total: %.0fg @ %.0f%% THC"),
							Plant->GetEstimatedTotalYield(), Plant->GetEstimatedThcPercent()),
							FLinearColor(0.9f, 0.9f, 0.7f), lx, ly, Font); ly += 18.f;
						if (Plant->HasSoil())
						{
							FSoilDef _sd; const FString _sn = GetSoilDef(Plant->GetSoilId(), _sd) ? _sd.DisplayName : Plant->GetSoilId().ToString();
							DrawText(FString::Printf(TEXT("Soil: %s  (%d harvests left)"), *_sn, Plant->GetSoilUsesLeft()),
								FLinearColor(0.6f, 0.9f, 0.6f), lx, ly, Font);
						}
					}
				}

				const FText Prompt = IInteractable::Execute_GetInteractionPrompt(Focus);
				if (!Prompt.IsEmpty())
				{
					DrawText(FString::Printf(TEXT("[E / left-click] %s"), *Prompt.ToString()),
						FLinearColor::Yellow, CX - 60.f, CY + 60.f, Font);
				}
				// Oppak-voortgang (hold G) — voor elk oppakbaar object.
				if (const UBuildComponent* BC = P->FindComponentByClass<UBuildComponent>())
				{
					const float A = BC->GetPickupAlpha();
					if (A > 0.f)
					{
						DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.9f), CX - 60.f, CY + 82.f, 160.f, 8.f);
						DrawRect(FLinearColor(1.f, 0.8f, 0.2f, 0.95f), CX - 60.f, CY + 82.f, 160.f * A, 8.f);
					}
				}
			}
		}
	}

	// Hotbar/inventory onderaan het scherm.
	if (P)
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			const int32 SlotCount = UInventoryComponent::HotbarSize; // vaste hotbar (8 slots)
			const int32 Active = Inv->GetActiveSlot();
			const float SlotW = 84.f;
			const float SlotH = 46.f;
			const float ScreenW = Canvas ? Canvas->ClipX : 1280.f;
			const float ScreenH = Canvas ? Canvas->ClipY : 720.f;
			const float TotalW = SlotCount * (SlotW + 6.f);
			float SX = (ScreenW - TotalW) * 0.5f;
			const float SY = ScreenH - SlotH - 24.f;

			for (int32 i = 0; i < SlotCount; ++i)
			{
				const bool bActive = (i == Active);
				// Achtergrond + gele rand voor het geselecteerde ("in de hand") slot.
				if (bActive)
				{
					DrawRect(FLinearColor(1.f, 0.85f, 0.1f, 0.9f), SX - 3.f, SY - 3.f, SlotW + 6.f, SlotH + 6.f);
				}
				DrawRect(bActive ? FLinearColor(0.16f, 0.16f, 0.10f, 0.95f) : FLinearColor(0.08f, 0.08f, 0.10f, 0.85f),
					SX, SY, SlotW, SlotH);
				// Slotnummer (1-8).
				DrawText(FString::Printf(TEXT("%d"), i + 1), FLinearColor(0.5f, 0.5f, 0.6f), SX + SlotW - 14.f, SY + 2.f, Font);

				const int32 HStackIdx = Inv->FindStackById(Inv->GetHotbarStackId(i));
				if (Inv->GetStacks().IsValidIndex(HStackIdx))
				{
					const FInventoryStack& HS = Inv->GetStacks()[HStackIdx];
					const FName HItem = HS.ItemId;
					const bool bIsSeed = HItem.ToString().StartsWith(TEXT("Seed_"));
					DrawText(PrettyItemName(HItem),
						bIsSeed ? FLinearColor(0.7f, 0.85f, 0.6f) : FLinearColor(0.85f, 0.9f, 1.f),
						SX + 5.f, SY + 4.f, Font);
					const bool bHThc = (HItem.ToString().StartsWith(TEXT("Bud_")) || HItem.ToString().StartsWith(TEXT("Joint_")));
					const FString HExtra = bHThc ? FString::Printf(TEXT("  THC%.0f%% Q%.0f%%"), HS.Quality, HS.QualityPct) : TEXT("");
					DrawText(FString::Printf(TEXT("x%d%s"), HS.Quantity, *HExtra),
						FLinearColor::White, SX + 5.f, SY + 24.f, Font);
				}
				SX += SlotW + 6.f;
			}

			// "In de hand" + bruikbaarheid.
			const FName Held = Inv->GetActiveItemId();
			FString HandLine;
			if (Held.IsNone())
			{
				HandLine = TEXT("In hand: (empty)   -   1-8 / scroll to select");
			}
			else
			{
				FPlaceableDef _pd;
				const bool bPlaceable = GetPlaceableDef(Held, _pd);
				HandLine = FString::Printf(TEXT("In hand: %s%s"), *PrettyItemName(Held),
					bPlaceable ? TEXT("   -   left-click to place") : TEXT(""));
			}
			DrawText(HandLine, FLinearColor(1.f, 0.95f, 0.6f), (ScreenW - TotalW) * 0.5f, SY - 24.f, Font);
			DrawText(TEXT("I = inventory   |   right-click papers = roll joint   |   F = give sample"),
				FLinearColor(0.7f, 0.7f, 0.7f), (ScreenW - TotalW) * 0.5f, SY - 44.f, Font);
		}
	}
}

bool AWeedShopHUD::DrawButton(FName BoxName, const FString& Label, float X, float Y, float W, const FLinearColor& BaseColor)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	const bool bHover = (HoveredBox == BoxName);

	DrawRect(bHover ? FLinearColor(0.25f, 0.35f, 0.5f, 0.95f) : FLinearColor(0.12f, 0.12f, 0.16f, 0.9f),
		X, Y, W, RowH - 2.f);
	DrawText(Label, bHover ? FLinearColor::White : BaseColor, X + 6.f, Y + 3.f, Font);
	AddHitBox(FVector2D(X, Y), FVector2D(W, RowH - 2.f), BoxName, /*bConsumesInput*/ true, /*Priority*/ 1);
	return bHover;
}

namespace
{
	// App-namen, korte tags voor het icoon en een kleur per app.
	static const TCHAR* GAppNames[UPhoneClientComponent::AppCount] =
		{ TEXT("Upgrades"), TEXT("Suppliers"), TEXT("Contacts"), TEXT("Messages"), TEXT("Settings"), TEXT("Map") };
	static const TCHAR* GAppTags[UPhoneClientComponent::AppCount] =
		{ TEXT("UPG"), TEXT("SHOP"), TEXT("CONT"), TEXT("MSG"), TEXT("SET"), TEXT("MAP") };
	static const FLinearColor GAppCols[UPhoneClientComponent::AppCount] = {
		FLinearColor(0.45f, 0.35f, 0.85f, 0.98f),  // Upgrades - paars
		FLinearColor(0.20f, 0.55f, 0.30f, 0.98f),  // Suppliers - groen
		FLinearColor(0.20f, 0.50f, 0.80f, 0.98f),  // Contacts - blauw
		FLinearColor(0.85f, 0.55f, 0.20f, 0.98f),  // Messages - oranje
		FLinearColor(0.45f, 0.45f, 0.50f, 0.98f),  // Settings - grijs
		FLinearColor(0.20f, 0.65f, 0.60f, 0.98f),  // Map - teal
	};
}

void AWeedShopHUD::DrawPhone(UPhoneClientComponent* Phone)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	const float PX = (Canvas ? Canvas->ClipX : 1280.f) - PhoneW - 30.f;
	const float PY = 50.f;
	const float SX = PX + 14.f;   // scherm-binnenkant X
	const float SW = PhoneW - 28.f;

	// Telefoon-body (bezel) + scherm.
	DrawRect(FLinearColor(0.02f, 0.02f, 0.03f, 0.99f), PX - 6.f, PY - 8.f, PhoneW + 12.f, PhoneH + 18.f);
	DrawRect(FLinearColor(0.07f, 0.08f, 0.11f, 0.99f), PX, PY, PhoneW, PhoneH);

	// Statusbalk: dag/tijd links, cash rechts.
	float y = PY + 8.f;
	{
		FString Left = TEXT("ThePlug");
		if (GS && GS->GetDayCycle())
		{
			const int32 Mins = ((int32)(GS->GetDayCycle()->GetCycleFraction() * 24.f * 60.f)) % (24 * 60);
			Left = FString::Printf(TEXT("%s  %02d:%02d"), GS->GetDayCycle()->IsNight() ? TEXT("Night") : TEXT("Day"),
				Mins / 60, Mins % 60);
		}
		DrawText(Left, FLinearColor(0.7f, 0.8f, 0.95f), SX, y, Font);
		if (GS && GS->GetEconomy())
		{
			DrawText(FString::Printf(TEXT("EUR %.2f"), GS->GetEconomy()->GetBalanceEuros()),
				FLinearColor(0.7f, 1.f, 0.7f), PX + PhoneW - 150.f, y, Font);
		}
	}
	y += 24.f;
	DrawRect(FLinearColor(0.2f, 0.2f, 0.25f, 0.9f), SX, y, SW, 1.f);
	y += 10.f;

	if (Phone->IsHomeScreen())
	{
		// --- Home-scherm: app-rooster (3 kolommen) ---
		DrawText(TEXT("Apps"), FLinearColor(0.6f, 1.f, 0.6f), SX, y, Font);
		y += 26.f;
		const int32 Cols = 3;
		const float Gap = 14.f;
		const float IconW = (SW - Gap * (Cols - 1)) / Cols;
		const float IconH = IconW;
		for (int32 i = 0; i < UPhoneClientComponent::AppCount; ++i)
		{
			const int32 col = i % Cols;
			const int32 row = i / Cols;
			const float ix = SX + col * (IconW + Gap);
			const float iy = y + row * (IconH + 30.f);
			const FName Box(*FString::Printf(TEXT("app_%d"), i));
			const bool bHover = (HoveredBox == Box);
			// Icoon (afgerond-ish via gevulde rect) + highlight bij hover.
			if (bHover) { DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.18f), ix - 3.f, iy - 3.f, IconW + 6.f, IconH + 6.f); }
			DrawRect(GAppCols[i], ix, iy, IconW, IconH);
			DrawText(GAppTags[i], FLinearColor::White, ix + 8.f, iy + IconH * 0.5f - 8.f, Font);
			DrawText(GAppNames[i], FLinearColor(0.85f, 0.88f, 0.95f), ix, iy + IconH + 3.f, Font);
			AddHitBox(FVector2D(ix, iy), FVector2D(IconW, IconH + 24.f), Box, true, 3);
			if (bHover) { HoverTooltip = FString::Printf(TEXT("Open %s"), GAppNames[i]); }
		}
	}
	else
	{
		// --- Geopende app: titelbalk + Home-knop, dan de inhoud ---
		const int32 App = Phone->GetTab();
		if (DrawButton(FName(TEXT("phonehome")), TEXT("< Home"), SX, y, 90.f, FLinearColor(0.7f, 0.85f, 1.f)))
		{
			HoverTooltip = TEXT("Back to the home screen");
		}
		DrawText(GAppNames[FMath::Clamp(App, 0, UPhoneClientComponent::AppCount - 1)],
			FLinearColor(0.9f, 0.95f, 1.f), SX + 100.f, y + 3.f, Font);
		y += RowH + 10.f;

		const float InnerX = SX;
		const float InnerW = SW;

		if (App == 0) // Upgrades
		{
			if (UUpgradeComponent* Upg = GS ? GS->GetUpgrades() : nullptr)
			{
				int32 idx = 0;
				for (const FName& Id : Upg->GetAllUpgradeIds())
				{
					FText Name; int32 Cost = 0; bool bPurchased = false; bool bAvailable = false;
					if (Upg->GetUpgradeDisplay(Id, Name, Cost, bPurchased, bAvailable))
					{
						const TCHAR* Suffix = bPurchased ? TEXT("  [owned]") : (bAvailable ? TEXT("") : TEXT("  [locked]"));
						const FLinearColor Col = bPurchased ? FLinearColor::Gray
							: (bAvailable ? FLinearColor::White : FLinearColor(0.8f, 0.55f, 0.55f));
						if (DrawButton(FName(*FString::Printf(TEXT("buy_%d"), idx)),
							FString::Printf(TEXT("%s  -  EUR %.2f%s"), *Name.ToString(), Cost / 100.f, Suffix),
							InnerX, y, InnerW, Col))
						{
							HoverTooltip = bPurchased ? TEXT("Already purchased")
								: (bAvailable ? FString::Printf(TEXT("Buy for EUR %.2f"), Cost / 100.f)
									: TEXT("Locked - reach the required phase first"));
						}
						y += RowH;
					}
					if (++idx >= 10) break;
				}
			}
		}
		else if (App == 1) // Suppliers -> grote winkel naast de telefoon
		{
			DrawText(TEXT("Store open ->"), FLinearColor(0.7f, 0.85f, 1.f), InnerX, y, Font); y += 22.f;
			DrawText(TEXT("Browse on the left:"), FLinearColor(0.6f, 0.6f, 0.7f), InnerX, y, Font); y += 18.f;
			DrawText(TEXT("set amount, add to cart,"), FLinearColor(0.6f, 0.6f, 0.7f), InnerX, y, Font); y += 18.f;
			DrawText(TEXT("then checkout."), FLinearColor(0.6f, 0.6f, 0.7f), InnerX, y, Font); y += 18.f;
			if (GS && GS->GetStore()) { DrawStoreUI(Phone); }
		}
		else if (App == 2) // Contacts
		{
			if (UContactsComponent* Con = GS ? GS->GetContacts() : nullptr)
			{
				if (Con->GetContacts().Num() == 0)
				{
					DrawText(TEXT("(no contacts yet - deal with"), FLinearColor::Gray, InnerX, y, Font); y += 18.f;
					DrawText(TEXT(" customers to get numbers)"), FLinearColor::Gray, InnerX, y, Font); y += 18.f;
				}
				for (const FPhoneContact& C : Con->GetContacts())
				{
					DrawText(FString::Printf(TEXT("%s  (%.0f%%)"), *C.DisplayName.ToString(), C.Relationship),
						FLinearColor::White, InnerX, y, Font);
					y += 22.f;
					if (y > PY + PhoneH - 60.f) break;
				}
			}
		}
		else if (App == 3) // Messages
		{
			if (UContactsComponent* Con = GS ? GS->GetContacts() : nullptr)
			{
				if (DrawButton(FName(TEXT("buy_0")), TEXT("Accept (first message)"), InnerX, y, InnerW, FLinearColor(0.7f, 1.f, 0.7f)))
				{
					HoverTooltip = TEXT("Make appointment -> loyalty up");
				}
				y += RowH;
				if (DrawButton(FName(TEXT("buy_1")), TEXT("Decline (first message)"), InnerX, y, InnerW, FLinearColor(1.f, 0.75f, 0.6f)))
				{
					HoverTooltip = TEXT("Cancel -> loyalty down");
				}
				y += RowH + 6.f;
				if (Con->GetMessages().Num() == 0)
				{
					DrawText(TEXT("(no messages)"), FLinearColor::Gray, InnerX, y, Font); y += 22.f;
				}
				for (const FPhoneMessage& M : Con->GetMessages())
				{
					const TCHAR* Tag = (M.Status == 1) ? TEXT("[yes]") : (M.Status == 2 ? TEXT("[no]") : TEXT("[open]"));
					const FLinearColor Col = (M.Status == 1) ? FLinearColor::Green
						: (M.Status == 2 ? FLinearColor(0.7f, 0.5f, 0.5f) : FLinearColor(0.9f, 0.95f, 1.f));
					DrawText(FString::Printf(TEXT("%s %s: %s"), Tag, *M.SenderName.ToString(), *M.Body.ToString()),
						Col, InnerX, y, Font);
					y += 22.f;
					if (y > PY + PhoneH - 60.f) break;
				}
			}
		}
		else if (App == 4) // Settings
		{
			DrawText(TEXT("Game"), FLinearColor(0.7f, 0.85f, 1.f), InnerX, y, Font); y += 24.f;
			if (GS)
			{
				if (GS->GetDayCycle())
				{
					const int32 Mins = ((int32)(GS->GetDayCycle()->GetCycleFraction() * 24.f * 60.f)) % (24 * 60);
					DrawText(FString::Printf(TEXT("Time: %s %02d:%02d"), GS->GetDayCycle()->IsNight() ? TEXT("Night") : TEXT("Day"),
						Mins / 60, Mins % 60),
						FLinearColor::White, InnerX, y, Font); y += 22.f;
				}
				if (GS->GetEconomy())
				{
					DrawText(FString::Printf(TEXT("Cash: EUR %.2f"), GS->GetEconomy()->GetBalanceEuros()),
						FLinearColor::White, InnerX, y, Font); y += 22.f;
				}
				if (GS->GetHeat())
				{
					DrawText(FString::Printf(TEXT("Heat: %.0f%%"), GS->GetHeat()->GetHeat()),
						FLinearColor(1.f, 0.7f, 0.6f), InnerX, y, Font); y += 22.f;
				}
				if (GS->GetMilestones())
				{
					DrawText(FString::Printf(TEXT("Phase: %d"), (int32)GS->GetMilestones()->GetCurrentPhase()),
						FLinearColor::White, InnerX, y, Font); y += 22.f;
				}
			}
			y += 8.f;
			DrawText(TEXT("Controls"), FLinearColor(0.7f, 0.85f, 1.f), InnerX, y, Font); y += 22.f;
			DrawText(TEXT("Tab phone   I inventory"), FLinearColor(0.8f, 0.8f, 0.85f), InnerX, y, Font); y += 18.f;
			DrawText(TEXT("Q home   1-8 hotbar"), FLinearColor(0.8f, 0.8f, 0.85f), InnerX, y, Font); y += 18.f;
			DrawText(TEXT("LMB use   RMB roll papers"), FLinearColor(0.8f, 0.8f, 0.85f), InnerX, y, Font); y += 18.f;
			DrawText(TEXT("R rotate   G pick up   F sample"), FLinearColor(0.8f, 0.8f, 0.85f), InnerX, y, Font); y += 22.f;
		}
		else if (App == 5) // Map
		{
			const float MapX = InnerX, MapY = y;
			const float MapW = InnerW, MapH = FMath::Min(InnerW, PY + PhoneH - 44.f - y);
			DrawRect(FLinearColor(0.10f, 0.14f, 0.12f, 0.98f), MapX, MapY, MapW, MapH);
			const float cx = MapX + MapW * 0.5f, cyc = MapY + MapH * 0.5f;
			const float Range = 4000.f;
			const float Scale = (MapW * 0.5f) / Range;
			const APawn* P = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
			const FVector PL = P ? P->GetActorLocation() : FVector::ZeroVector;

			// Klanten (oranje) en potten (groen) als stipjes rond de speler.
			for (TActorIterator<ACustomerBase> It(GetWorld()); It; ++It)
			{
				const FVector d = It->GetActorLocation() - PL;
				const float px = cx + d.Y * Scale, py = cyc - d.X * Scale;
				if (px > MapX && px < MapX + MapW && py > MapY && py < MapY + MapH)
				{
					DrawRect(FLinearColor(1.f, 0.6f, 0.2f), px - 3.f, py - 3.f, 6.f, 6.f);
				}
			}
			for (TActorIterator<AGrowPlant> It(GetWorld()); It; ++It)
			{
				const FVector d = It->GetActorLocation() - PL;
				const float px = cx + d.Y * Scale, py = cyc - d.X * Scale;
				if (px > MapX && px < MapX + MapW && py > MapY && py < MapY + MapH)
				{
					DrawRect(FLinearColor(0.4f, 0.9f, 0.4f), px - 3.f, py - 3.f, 6.f, 6.f);
				}
			}
			// Speler in het midden (wit).
			DrawRect(FLinearColor::White, cx - 4.f, cyc - 4.f, 8.f, 8.f);
			DrawText(TEXT("you"), FLinearColor::White, cx + 6.f, cyc - 6.f, Font);
			DrawText(TEXT("orange=customer  green=plant"), FLinearColor(0.7f, 0.7f, 0.75f), MapX + 4.f, MapY + MapH - 18.f, Font);
		}
	}

	// Tooltip (hover-info).
	if (!HoverTooltip.IsEmpty())
	{
		const float TipY = PY + PhoneH - 54.f;
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), PX + 6.f, TipY - 2.f, PhoneW - 12.f, 20.f);
		DrawText(HoverTooltip, FLinearColor(1.f, 1.f, 0.6f), SX, TipY, Font);
	}

	// Home-indicator-balk onderaan + sluit-knop.
	DrawRect(FLinearColor(0.5f, 0.5f, 0.55f, 0.9f), PX + PhoneW * 0.5f - 50.f, PY + PhoneH - 12.f, 100.f, 4.f);
	if (DrawButton(FName(TEXT("close")), TEXT("Close (Tab)"), SX, PY + PhoneH - 32.f, 120.f, FLinearColor::Yellow))
	{
		HoverTooltip = TEXT("Close the phone");
	}
}

void AWeedShopHUD::DrawStoreUI(UPhoneClientComponent* Phone)
{
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UStoreComponent* Store = GS ? GS->GetStore() : nullptr;
	if (!Store) { return; }
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;

	const float ClipX = Canvas ? Canvas->ClipX : 1280.f;
	const float StoreW = 780.f;
	const float StoreH = 640.f;
	const float PhoneLeft = ClipX - PhoneW - 30.f;
	const float SX = FMath::Max(12.f, PhoneLeft - StoreW - 16.f);
	const float SY = 56.f;
	const float Pad = 16.f;

	// Knop-helper met gecentreerd label (rect + label + hit-box), geeft hover terug.
	auto Btn = [this, Font](const FName& Box, const FString& Label, float bx, float by, float bw, float bh,
		const FLinearColor& Base, const FLinearColor& TextCol) -> bool
	{
		const bool bH = (HoveredBox == Box);
		DrawRect(bH ? FLinearColor(0.30f, 0.46f, 0.64f, 0.98f) : Base, bx, by, bw, bh);
		float tw = 0.f, th = 0.f; GetTextSize(Label, tw, th, Font);
		DrawText(Label, TextCol, bx + FMath::Max(3.f, (bw - tw) * 0.5f), by + FMath::Max(1.f, (bh - th) * 0.5f), Font);
		AddHitBox(FVector2D(bx, by), FVector2D(bw, bh), Box, true, 5);
		return bH;
	};

	// Achtergrond + bovenbalk.
	DrawRect(FLinearColor(0.07f, 0.08f, 0.11f, 0.99f), SX, SY, StoreW, StoreH);
	DrawRect(FLinearColor(0.11f, 0.13f, 0.18f, 1.f), SX, SY, StoreW, 38.f);
	DrawText(TEXT("SUPPLIER STORE"), FLinearColor(0.6f, 1.f, 0.6f), SX + Pad, SY + 10.f, Font);
	if (GS->GetEconomy())
	{
		const FString Cash = FString::Printf(TEXT("Cash: EUR %.2f"), GS->GetEconomy()->GetBalanceEuros());
		float tw = 0.f, th = 0.f; GetTextSize(Cash, tw, th, Font);
		DrawText(Cash, FLinearColor(0.85f, 0.95f, 1.f), SX + StoreW - Pad - tw, SY + 10.f, Font);
	}

	float y = SY + 48.f;

	// Zones.
	const float CartW = 252.f;
	const float CatX = SX + Pad;
	const float CartX = SX + StoreW - Pad - CartW;
	const float CatRight = CartX - 14.f;
	const float CatZoneW = CatRight - CatX;

	// Categorie-tabs (pillen) over de catalogus-breedte.
	static const TCHAR* CatNames[UStoreComponent::SupplierCatCount] = { TEXT("Seeds"), TEXT("Papers"), TEXT("Pots"), TEXT("Soil"), TEXT("Water"), TEXT("Sell") };
	const int32 Cat = Phone->GetSupplierCat();
	const float CatTabW = CatZoneW / UStoreComponent::SupplierCatCount;
	for (int32 i = 0; i < UStoreComponent::SupplierCatCount; ++i)
	{
		const FLinearColor Col = (i == Cat) ? FLinearColor(0.22f, 0.52f, 0.32f, 0.98f) : FLinearColor(0.15f, 0.16f, 0.21f, 0.95f);
		Btn(FName(*FString::Printf(TEXT("scat_%d"), i)), CatNames[i], CatX + i * CatTabW + 2.f, y, CatTabW - 4.f, 26.f, Col,
			(i == Cat) ? FLinearColor::White : FLinearColor(0.75f, 0.78f, 0.85f));
	}
	y += 38.f;

	const float ListTop = y;
	const float RowHt = 40.f;
	const float ListBottom = SY + StoreH - Pad;

	// --- Winkelwagen-paneel (rechts) tekenen we eerst als achtergrond-kolom ---
	DrawRect(FLinearColor(0.05f, 0.06f, 0.09f, 0.99f), CartX, ListTop, CartW, ListBottom - ListTop);
	DrawRect(FLinearColor(0.12f, 0.14f, 0.19f, 1.f), CartX, ListTop, CartW, 28.f);
	DrawText(FString::Printf(TEXT("CART  (%d)"), Phone->GetCartNumLines()), FLinearColor(0.7f, 1.f, 0.7f), CartX + 10.f, ListTop + 6.f, Font);

	if (Cat == UStoreComponent::SupplierCatSell)
	{
		// Verkoop-lijst in de catalogus-zone.
		DrawText(TEXT("Sell items back (70% of value):"), FLinearColor(0.8f, 0.8f, 0.9f), CatX, y, Font);
		y += 26.f;
		APawn* PP = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
		const UInventoryComponent* PInv = PP ? PP->FindComponentByClass<UInventoryComponent>() : nullptr;
		bool bAny = false;
		if (PInv)
		{
			const TArray<FInventoryStack>& SellStacks = PInv->GetStacks();
			for (int32 si = 0; si < SellStacks.Num(); ++si)
			{
				const int32 Val = Store->GetSellValueCents(SellStacks[si].ItemId);
				if (Val <= 0) { continue; }
				if (y > ListBottom - RowHt) { break; }
				bAny = true;
				DrawRect(FLinearColor(0.10f, 0.11f, 0.14f, 0.95f), CatX, y, CatZoneW, RowHt - 4.f);
				const int32 SQty = SellStacks[si].Quantity;
				DrawText(FString::Printf(TEXT("%s   x%d"), *PrettyItemName(SellStacks[si].ItemId), SQty),
					FLinearColor(0.9f, 0.92f, 1.f), CatX + 10.f, y + 3.f, Font);
				DrawText(FString::Printf(TEXT("EUR %.2f each  (all: EUR %.2f)"), Val / 100.f, (Val * SQty) / 100.f),
					FLinearColor(1.f, 0.85f, 0.5f), CatX + 10.f, y + 19.f, Font);
				Btn(FName(*FString::Printf(TEXT("sella_%d"), si)), TEXT("Sell all"), CatX + CatZoneW - 190.f, y + 5.f, 90.f, RowHt - 12.f,
					FLinearColor(0.45f, 0.30f, 0.12f, 0.98f), FLinearColor::White);
				Btn(FName(*FString::Printf(TEXT("sell_%d"), si)), TEXT("Sell 1"), CatX + CatZoneW - 92.f, y + 5.f, 86.f, RowHt - 12.f,
					FLinearColor(0.40f, 0.34f, 0.18f, 0.98f), FLinearColor::White);
				y += RowHt;
			}
		}
		if (!bAny) { DrawText(TEXT("(nothing sellable in your inventory)"), FLinearColor::Gray, CatX, y, Font); }
	}
	else
	{
		// --- Catalogus (kopen) ---
		const TArray<FName> Items = Store->GetSupplierCategory(Cat);
		int32 idx = 0;
		for (const FName& Id : Items)
		{
			if (y > ListBottom - RowHt) { break; }
			const int32 Price = Store->GetCatalogPriceCents(Id);
			const int32 Pend = Phone->GetPendingQty(Id);
			FString Name = Store->GetCatalogName(Id).ToString();

			// Rij met lichte zebra-arcering.
			DrawRect((idx % 2 == 0) ? FLinearColor(0.11f, 0.12f, 0.15f, 0.95f) : FLinearColor(0.09f, 0.10f, 0.13f, 0.95f),
				CatX, y, CatZoneW, RowHt - 4.f);

			// Control-cluster rechts: prijskaartje | [-] qty [+] | ADD.
			const float AddW = 54.f, IncW = 22.f, QtyW = 26.f, DecW = 22.f, TagW = 78.f, G = 6.f;
			const float ClusterW = TagW + G + DecW + QtyW + IncW + G + AddW;
			const float cxs = CatX + CatZoneW - ClusterW - 6.f;
			const float bh = RowHt - 12.f, bty = y + 5.f;

			// Naam (afgekapt tot aan het cluster).
			FString NameTrim = Name;
			if (NameTrim.Len() > 26) { NameTrim = NameTrim.Left(25) + TEXT("."); }
			DrawText(NameTrim, FLinearColor(0.92f, 0.95f, 1.f), CatX + 10.f, y + 9.f, Font);

			float bx = cxs;
			DrawRect(FLinearColor(0.16f, 0.42f, 0.22f, 0.98f), bx, bty, TagW, bh);
			{
				const FString PriceStr = FString::Printf(TEXT("EUR %.2f"), Price / 100.f);
				float tw = 0.f, th = 0.f; GetTextSize(PriceStr, tw, th, Font);
				DrawText(PriceStr, FLinearColor(0.88f, 1.f, 0.88f), bx + FMath::Max(3.f, (TagW - tw) * 0.5f), bty + 4.f, Font);
			}
			bx += TagW + G;
			Btn(FName(*FString::Printf(TEXT("sdec_%d"), idx)), TEXT("-"), bx, bty, DecW, bh, FLinearColor(0.18f, 0.19f, 0.24f, 0.98f), FLinearColor::White);
			bx += DecW;
			{
				const FString QtyStr = FString::Printf(TEXT("%d"), Pend);
				float tw = 0.f, th = 0.f; GetTextSize(QtyStr, tw, th, Font);
				DrawRect(FLinearColor(0.06f, 0.07f, 0.10f, 0.98f), bx, bty, QtyW, bh);
				DrawText(QtyStr, FLinearColor::White, bx + (QtyW - tw) * 0.5f, bty + 4.f, Font);
			}
			bx += QtyW;
			Btn(FName(*FString::Printf(TEXT("sinc_%d"), idx)), TEXT("+"), bx, bty, IncW, bh, FLinearColor(0.18f, 0.19f, 0.24f, 0.98f), FLinearColor::White);
			bx += IncW + G;
			Btn(FName(*FString::Printf(TEXT("sadd_%d"), idx)), TEXT("ADD"), bx, bty, AddW, bh, FLinearColor(0.20f, 0.38f, 0.55f, 0.98f), FLinearColor::White);

			y += RowHt;
			++idx;
		}
		if (Items.Num() == 0) { DrawText(TEXT("(nothing here yet)"), FLinearColor::Gray, CatX, ListTop, Font); }
	}

	// --- Winkelwagen-inhoud ---
	float cy = ListTop + 34.f;
	const float CartInnerX = CartX + 10.f;
	const float CartFooterY = ListBottom - 92.f;
	const int32 Lines = Phone->GetCartNumLines();
	if (Lines == 0)
	{
		DrawText(TEXT("Cart is empty."), FLinearColor(0.6f, 0.6f, 0.68f), CartInnerX, cy, Font);
	}
	for (int32 li = 0; li < Lines; ++li)
	{
		FName LId; int32 LQty = 0;
		if (!Phone->GetCartLine(li, LId, LQty)) { continue; }
		if (cy > CartFooterY - 36.f) { DrawText(TEXT("..."), FLinearColor::Gray, CartInnerX, cy, Font); break; }
		const int32 LinePrice = Store->GetCatalogPriceCents(LId) * LQty;
		FString LName = Store->GetCatalogName(LId).ToString();
		if (LName.Len() > 18) { LName = LName.Left(17) + TEXT("."); }
		DrawRect(FLinearColor(0.09f, 0.10f, 0.13f, 0.95f), CartX + 4.f, cy - 2.f, CartW - 8.f, 38.f);
		DrawText(FString::Printf(TEXT("%s  x%d"), *LName, LQty), FLinearColor(0.9f, 0.92f, 1.f), CartInnerX, cy, Font);
		DrawText(FString::Printf(TEXT("EUR %.2f"), LinePrice / 100.f), FLinearColor(1.f, 0.95f, 0.7f), CartInnerX, cy + 16.f, Font);
		Btn(FName(*FString::Printf(TEXT("cdec_%d"), li)), TEXT("-"), CartX + CartW - 84.f, cy + 7.f, 22.f, 22.f, FLinearColor(0.18f, 0.19f, 0.24f, 0.98f), FLinearColor::White);
		Btn(FName(*FString::Printf(TEXT("cinc_%d"), li)), TEXT("+"), CartX + CartW - 58.f, cy + 7.f, 22.f, 22.f, FLinearColor(0.18f, 0.19f, 0.24f, 0.98f), FLinearColor::White);
		Btn(FName(*FString::Printf(TEXT("cdel_%d"), li)), TEXT("x"), CartX + CartW - 32.f, cy + 7.f, 22.f, 22.f, FLinearColor(0.42f, 0.18f, 0.18f, 0.98f), FLinearColor::White);
		cy += 42.f;
	}

	// Cart-footer: totaal + CHECKOUT + Clear.
	DrawRect(FLinearColor(0.2f, 0.2f, 0.25f, 0.9f), CartX + 8.f, CartFooterY - 6.f, CartW - 16.f, 1.f);
	DrawText(FString::Printf(TEXT("Total: EUR %.2f"), Phone->GetCartTotalCents() / 100.f),
		FLinearColor(1.f, 0.95f, 0.55f), CartInnerX, CartFooterY, Font);
	Btn(FName(TEXT("checkout")), TEXT("CHECKOUT"), CartInnerX, CartFooterY + 24.f, CartW - 20.f, 30.f,
		Phone->GetCartNumLines() > 0 ? FLinearColor(0.2f, 0.55f, 0.27f, 0.98f) : FLinearColor(0.18f, 0.2f, 0.22f, 0.95f), FLinearColor::White);
	Btn(FName(TEXT("cartclear")), TEXT("Clear"), CartInnerX, CartFooterY + 58.f, CartW - 20.f, 24.f,
		FLinearColor(0.3f, 0.2f, 0.2f, 0.95f), FLinearColor(0.95f, 0.85f, 0.85f));
}

void AWeedShopHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);

	UPhoneClientComponent* Phone = GetPhone();
	if (!Phone)
	{
		return;
	}

	// Deze muisklik is door een UI-knop verwerkt: blokkeer dat dezelfde klik ook nog een
	// wereld-interactie (bv. opnieuw de deal openen) triggert.
	Phone->MarkUiClickConsumed();

	const FString S = BoxName.ToString();
	if (S == TEXT("close"))
	{
		Phone->Toggle();
	}
	else if (S.StartsWith(TEXT("tab_")))
	{
		Phone->SetTab(FCString::Atoi(*S.RightChop(4)));
	}
	else if (S.StartsWith(TEXT("app_")))
	{
		Phone->OpenApp(FCString::Atoi(*S.RightChop(4)));
	}
	else if (S == TEXT("phonehome"))
	{
		Phone->GoHome();
	}
	else if (S.StartsWith(TEXT("buy_")))
	{
		Phone->DoAction(FCString::Atoi(*S.RightChop(4)));
	}
	else if (S.StartsWith(TEXT("scat_")))
	{
		Phone->SetSupplierCat(FCString::Atoi(*S.RightChop(5)));
	}
	else if (S.StartsWith(TEXT("sella_")))
	{
		Phone->SellInventoryIndexAll(FCString::Atoi(*S.RightChop(6)));
	}
	else if (S.StartsWith(TEXT("sell_")))
	{
		Phone->SellInventoryIndex(FCString::Atoi(*S.RightChop(5)));
	}
	else if (S.StartsWith(TEXT("sdec_")) || S.StartsWith(TEXT("sinc_")) || S.StartsWith(TEXT("sadd_")))
	{
		// Catalogus-stepper / add-to-cart: idx -> item-id via de actieve categorie-lijst.
		const int32 Idx = FCString::Atoi(*S.RightChop(5));
		AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
		if (UStoreComponent* Store = GS ? GS->GetStore() : nullptr)
		{
			const TArray<FName> Items = Store->GetSupplierCategory(Phone->GetSupplierCat());
			if (Items.IsValidIndex(Idx))
			{
				const FName Id = Items[Idx];
				if (S.StartsWith(TEXT("sdec_")))      { Phone->AdjustPendingQty(Id, -1); }
				else if (S.StartsWith(TEXT("sinc_"))) { Phone->AdjustPendingQty(Id, +1); }
				else                                  { Phone->AddToCart(Id); }
			}
		}
	}
	else if (S.StartsWith(TEXT("cdec_"))) { Phone->AdjustCartLine(FCString::Atoi(*S.RightChop(5)), -1); }
	else if (S.StartsWith(TEXT("cinc_"))) { Phone->AdjustCartLine(FCString::Atoi(*S.RightChop(5)), +1); }
	else if (S.StartsWith(TEXT("cdel_"))) { Phone->AdjustCartLine(FCString::Atoi(*S.RightChop(5)), -100000); }
	else if (S == TEXT("checkout"))       { Phone->Checkout(); }
	else if (S == TEXT("cartclear"))      { Phone->ClearCart(); }
	else if (S.StartsWith(TEXT("rollg_")))
	{
		Phone->SetRollGrams(FCString::Atoi(*S.RightChop(6)));
	}
	else if (S == TEXT("rollconfirm"))
	{
		Phone->ConfirmRoll();
	}
	else if (S == TEXT("rollclose"))
	{
		Phone->ToggleRollUI();
	}
	else if (S.StartsWith(TEXT("dealp_")))
	{
		// Prijs-stop: index 0..N-1 -> percentage 40%..200% van de markt.
		if (const ACustomerBase* C = Phone->GetDealCustomer())
		{
			const int32 Idx = FCString::Atoi(*S.RightChop(6));
			const float Pct = 0.40f + 0.10f * Idx;
			Phone->SetDealAskCents(FMath::RoundToInt(C->GetMarketPriceCents() * Pct));
		}
	}
	else if (S == TEXT("dealconfirm"))
	{
		Phone->ConfirmDeal();
	}
	else if (S == TEXT("dealclose"))
	{
		Phone->CloseDeal();
	}
	else if (S == TEXT("invclose"))
	{
		Phone->ToggleInventory();
	}
	else if (S.StartsWith(TEXT("merge_")))
	{
		// Merge-knop op een wiet-cel: open de bevestig-popup voor dat product.
		const int32 Idx = FCString::Atoi(*S.RightChop(6));
		if (const APawn* P = PlayerOwner ? PlayerOwner->GetPawn() : nullptr)
		{
			if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
			{
				// Idx is de n-de vrije (niet-hotbar) stapel; vind dezelfde volgorde terug.
				int32 n = 0; FName Target = NAME_None;
				for (const FInventoryStack& St : Inv->GetStacks())
				{
					if (Inv->IsStackOnHotbar(St.StackId)) { continue; }
					if (n == Idx) { Target = St.ItemId; break; }
					++n;
				}
				if (!Target.IsNone()) { Phone->OpenMerge(Target); }
			}
		}
	}
	else if (S == TEXT("mergeconfirm"))
	{
		Phone->ConfirmMerge();
	}
	else if (S == TEXT("mergeclose"))
	{
		Phone->CloseMerge();
	}
	else if (S.StartsWith(TEXT("potupg_")))
	{
		Phone->BuyPotUpgrade(FCString::Atoi(*S.RightChop(7)));
	}
	else if (S == TEXT("potupgclose"))
	{
		Phone->ClosePotUpgrade();
	}
}

void AWeedShopHUD::DrawRollUI(UPhoneClientComponent* Phone)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	const float W = 460.f;
	const float H = 240.f;
	const float PX = (Canvas ? Canvas->ClipX : 1280.f) * 0.5f - W * 0.5f;
	const float PY = (Canvas ? Canvas->ClipY : 720.f) * 0.5f - H * 0.5f;
	const float InnerX = PX + 16.f;

	DrawRect(FLinearColor(0.05f, 0.05f, 0.08f, 0.95f), PX, PY, W, H);
	float y = PY + 14.f;
	DrawText(TEXT("ROLL JOINT"), FLinearColor(0.6f, 1.f, 0.6f), InnerX, y, Font); y += 28.f;

	const int32 G = Phone->GetRollGrams();
	const int32 MaxG = Phone->GetMaxJointGrams();
	if (MaxG <= 0)
	{
		DrawText(TEXT("No papers! Buy a pack from Suppliers (phone, Tab)."),
			FLinearColor(1.f, 0.5f, 0.5f), InnerX, y, Font);
		y += 30.f;
		DrawButton(FName(TEXT("rollclose")), TEXT("Close (right-click)"), InnerX, y, 150.f, FLinearColor::Yellow);
		return;
	}
	DrawText(FString::Printf(TEXT("Grams per joint: %d   (your papers allow up to %dg)"), G, MaxG),
		FLinearColor::White, InnerX, y, Font);
	y += 28.f;

	// Slider met klikbare/sleepbare stops per gram (1..MaxG).
	const float TrackX = InnerX;
	const float TrackW = W - 32.f;
	const float TrackY = y + 6.f;
	const float TrackH = 16.f;
	const float SegW = TrackW / float(MaxG);

	// Welk gram-vak hangt de cursor boven? (betrouwbaar via hit-box hover, geen DPI-gedoe).
	int32 HoverG = 0;
	if (HoveredBox.IsNone() == false)
	{
		const FString HS = HoveredBox.ToString();
		if (HS.StartsWith(TEXT("rollg_")))
		{
			HoverG = FCString::Atoi(*HS.RightChop(6));
		}
	}
	const bool bOverTrack = (HoverG >= 1 && HoverG <= MaxG);

	// Slepen: muisknop ingedrukt + cursor boven de balk -> volg het gram-vak onder de cursor.
	// Werkt puur op de hover-staat (zelfde systeem als klikken), dus DPI-onafhankelijk.
	const bool bMouseDown = PlayerOwner && PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
	const bool bDragging = bMouseDown && bOverTrack;
	if (bDragging && HoverG != G)
	{
		Phone->SetRollGrams(HoverG);
	}

	// Cursor: handje boven de balk, dichte greep terwijl je sleept.
	if (PlayerOwner)
	{
		PlayerOwner->CurrentMouseCursor = bDragging ? EMouseCursor::GrabHandClosed
			: (bOverTrack ? EMouseCursor::GrabHand : EMouseCursor::Default);
	}

	// Achtergrond-track + gevulde balk tot het gekozen gram.
	DrawRect(FLinearColor(0.18f, 0.18f, 0.20f, 0.95f), TrackX, TrackY, TrackW, TrackH);
	DrawRect(FLinearColor(0.35f, 0.8f, 0.35f, 0.95f), TrackX, TrackY, SegW * G, TrackH);

	for (int32 g = 1; g <= MaxG; ++g)
	{
		const float cx = TrackX + (g - 1) * SegW;
		const FName Box(*FString::Printf(TEXT("rollg_%d"), g));
		const bool bHover = (HoverG == g);
		const bool bSel = (g == G);

		// Hover = duidelijk geel oplichten zodat je ziet dat dit vak klikbaar/sleepbaar is.
		if (bHover)
		{
			DrawRect(FLinearColor(1.f, 0.85f, 0.1f, 0.55f), cx, TrackY - 5.f, SegW, TrackH + 10.f);
		}
		else if (bSel)
		{
			DrawRect(FLinearColor(0.6f, 1.f, 0.6f, 0.30f), cx, TrackY - 4.f, SegW, TrackH + 8.f);
		}
		DrawText(FString::Printf(TEXT("%dg"), g),
			bHover ? FLinearColor(1.f, 0.95f, 0.4f) : (bSel ? FLinearColor::White : FLinearColor(0.7f, 0.7f, 0.8f)),
			cx + SegW * 0.5f - 8.f, TrackY + TrackH + 3.f, Font);

		// Klik-/sleep-vak per gram (betrouwbaar, naam -> gram; geen muis-coördinaten nodig).
		AddHitBox(FVector2D(cx, TrackY - 6.f), FVector2D(SegW, TrackH + 28.f), Box, true, 3);
	}

	// Handle op de gekozen positie — geel + groter zodra je boven de balk hovert/sleept.
	const float HandleX = TrackX + (G - 0.5f) * SegW;
	const float HalfW = bDragging ? 9.f : (bOverTrack ? 7.f : 5.f);
	DrawRect((bDragging || bOverTrack) ? FLinearColor(1.f, 0.85f, 0.1f) : FLinearColor::White,
		HandleX - HalfW, TrackY - 8.f, HalfW * 2.f, TrackH + 16.f);

	y += 46.f;

	// THC% van de wiet die gebruikt wordt (eerste passende Bud_-stapel) -> potentie van de joint.
	float WeedThc = 0.f;
	if (APawn* RP = PlayerOwner ? PlayerOwner->GetPawn() : nullptr)
	{
		if (const UInventoryComponent* RInv = RP->FindComponentByClass<UInventoryComponent>())
		{
			for (const FInventoryStack& St : RInv->GetStacks())
			{
				if (St.ItemId.ToString().StartsWith(TEXT("Bud_")) && St.Quantity >= G)
				{
					WeedThc = St.Quality;
					break;
				}
			}
		}
	}
	const float ThcNorm = FMath::Clamp(WeedThc / 40.f, 0.f, 1.f);
	DrawText(FString::Printf(TEXT("Joint potency: %.0f%% THC   (size: %dg)"), WeedThc, G),
		ThcNorm >= 0.6f ? FLinearColor::Green : (ThcNorm >= 0.3f ? FLinearColor(1.f, 0.7f, 0.2f) : FLinearColor(1.f, 0.4f, 0.4f)),
		InnerX, y, Font);
	y += 24.f;
	DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.9f), InnerX, y, W - 32.f, 14.f);
	DrawRect(FLinearColor(0.4f, 0.9f, 0.4f, 0.95f), InnerX, y, (W - 32.f) * ThcNorm, 14.f);
	y += 28.f;

	DrawButton(FName(TEXT("rollconfirm")), FString::Printf(TEXT("Roll joint (costs %dg weed)"), G),
		InnerX, y, 250.f, FLinearColor::White);
	DrawButton(FName(TEXT("rollclose")), TEXT("Close (right-click)"), InnerX + 260.f, y, 150.f, FLinearColor::Yellow);
}

void AWeedShopHUD::DrawDealUI(UPhoneClientComponent* Phone)
{
	ACustomerBase* C = Phone->GetDealCustomer();
	if (!C)
	{
		Phone->CloseDeal();
		return;
	}

	// Loop je weg? Sluit de deal-UI automatisch (alleen onderhandelen als je dichtbij staat).
	if (const APawn* P = PlayerOwner ? PlayerOwner->GetPawn() : nullptr)
	{
		if (FVector::DistSquared(P->GetActorLocation(), C->GetActorLocation()) > FMath::Square(450.f))
		{
			Phone->CloseDeal();
			return;
		}
	}

	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	const float W = 480.f;
	const float H = 330.f;
	const float PX = (Canvas ? Canvas->ClipX : 1280.f) * 0.5f - W * 0.5f;
	const float PY = (Canvas ? Canvas->ClipY : 720.f) * 0.5f - H * 0.5f;
	const float InnerX = PX + 16.f;

	DrawRect(FLinearColor(0.05f, 0.06f, 0.09f, 0.96f), PX, PY, W, H);
	float y = PY + 14.f;
	DrawText(TEXT("DEAL"), FLinearColor(0.6f, 1.f, 0.6f), InnerX, y, Font); y += 28.f;

	const int32 Market = C->GetMarketPriceCents();
	const int32 Qty = C->DesiredQuantity;
	const FString Product = PrettyItemName(C->DesiredProductId);

	if (Market <= 0)
	{
		DrawText(TEXT("This customer has no clear order right now."), FLinearColor(1.f, 0.6f, 0.6f), InnerX, y, Font);
		y += 30.f;
		DrawButton(FName(TEXT("dealclose")), TEXT("Close"), InnerX, y, 150.f, FLinearColor::Yellow);
		return;
	}

	DrawText(FString::Printf(TEXT("Wants: %dx %s   (market EUR %.2f / unit)"), Qty, *Product, Market / 100.f),
		FLinearColor::White, InnerX, y, Font);
	y += 26.f;

	// Hover op een prijs-stop -> prijs/total/% tonen meteen díe waarde (preview), ook vóór je klikt.
	const int32 N = UPhoneClientComponent::DealStepCount; // 17 stops (40..200%)
	int32 HoverIdx = -1;
	if (!HoveredBox.IsNone())
	{
		const FString HS = HoveredBox.ToString();
		if (HS.StartsWith(TEXT("dealp_"))) { HoverIdx = FCString::Atoi(*HS.RightChop(6)); }
	}
	const bool bOverTrack = (HoverIdx >= 0 && HoverIdx < N);

	// Slepen: knop ingedrukt + boven de balk -> zet het bod op de stop onder de cursor.
	const bool bMouseDown = PlayerOwner && PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
	const bool bDragging = bMouseDown && bOverTrack;
	if (bDragging)
	{
		Phone->SetDealAskCents(FMath::RoundToInt(Market * (0.40f + 0.10f * HoverIdx)));
	}
	if (PlayerOwner)
	{
		PlayerOwner->CurrentMouseCursor = bDragging ? EMouseCursor::GrabHandClosed
			: (bOverTrack ? EMouseCursor::GrabHand : EMouseCursor::Default);
	}

	// Vastgezet bod + effectief (preview) bod dat de hover volgt.
	const int32 CommittedAsk = Phone->GetDealAskCents();
	const int32 CommittedIdx = FMath::Clamp(FMath::RoundToInt((float(CommittedAsk) / Market - 0.40f) / 0.10f), 0, N - 1);
	const int32 EffIdx = bOverTrack ? HoverIdx : CommittedIdx;
	const int32 EffAsk = FMath::RoundToInt(Market * (0.40f + 0.10f * EffIdx));
	const float EffPct = float(EffAsk) / Market;

	// Prijs-regel volgt de hover (met "preview"-hint zolang je nog niet geklikt hebt).
	DrawText(FString::Printf(TEXT("Your price: EUR %.2f / unit  (%.0f%% of market)   Total: EUR %.2f%s"),
		EffAsk / 100.f, EffPct * 100.f, (EffAsk * Qty) / 100.f, bOverTrack ? TEXT("   <- preview") : TEXT("")),
		FLinearColor(1.f, 0.95f, 0.6f), InnerX, y, Font);
	y += 26.f;

	// --- Prijs-slider: stops van 40% tot 200% van de markt ---
	const float TrackX = InnerX;
	const float TrackW = W - 32.f;
	const float TrackY = y + 6.f;
	const float TrackH = 16.f;
	const float SegW = TrackW / float(N);

	DrawRect(FLinearColor(0.18f, 0.18f, 0.20f, 0.95f), TrackX, TrackY, TrackW, TrackH);
	// Gevulde balk + handle staan op het VASTGEZETTE bod (verschuift alleen bij klik/sleep).
	const float CommittedPct = float(CommittedAsk) / Market;
	const FLinearColor Fill = (CommittedPct <= 1.f) ? FLinearColor(0.4f, 0.85f, 0.4f) : FLinearColor(0.9f, 0.55f, 0.3f);
	DrawRect(Fill, TrackX, TrackY, SegW * (CommittedIdx + 1), TrackH);

	for (int32 i = 0; i < N; ++i)
	{
		const float cx = TrackX + i * SegW;
		const FName Box(*FString::Printf(TEXT("dealp_%d"), i));
		// Hover = alleen een gele preview-markering (verschuift de balk NIET).
		if (HoverIdx == i && HoverIdx != CommittedIdx)
		{
			DrawRect(FLinearColor(1.f, 0.85f, 0.1f, 0.45f), cx, TrackY - 5.f, SegW, TrackH + 10.f);
		}
		AddHitBox(FVector2D(cx, TrackY - 6.f), FVector2D(SegW, TrackH + 26.f), Box, true, 3);
	}
	// Handle op het vastgezette bod.
	const float HandleX = TrackX + (CommittedIdx + 0.5f) * SegW;
	const float HalfW = bDragging ? 8.f : 6.f;
	DrawRect(bDragging ? FLinearColor(1.f, 0.85f, 0.1f) : FLinearColor::White,
		HandleX - HalfW, TrackY - 8.f, HalfW * 2.f, TrackH + 16.f);
	// Eindlabels.
	DrawText(TEXT("cheap"), FLinearColor(0.6f, 0.9f, 0.6f), TrackX, TrackY + TrackH + 4.f, Font);
	DrawText(TEXT("greedy"), FLinearColor(0.95f, 0.6f, 0.4f), TrackX + TrackW - 50.f, TrackY + TrackH + 4.f, Font);
	y += 48.f;

	// Voorraad van de speler voor dit product: hoeveel + THC%/Kwaliteit%. Weegt mee in de acceptatie.
	float Quality01 = -1.f; float ThcShow = 0.f; float QShow = 0.f; int32 StockQty = 0;
	if (const APawn* P = PlayerOwner ? PlayerOwner->GetPawn() : nullptr)
	{
		if (const UInventoryComponent* PInv = P->FindComponentByClass<UInventoryComponent>())
		{
			StockQty = PInv->GetQuantity(C->DesiredProductId);
			if (StockQty > 0)
			{
				QShow = PInv->GetItemQualityPct(C->DesiredProductId);
				ThcShow = PInv->GetItemQuality(C->DesiredProductId);
				Quality01 = FMath::Clamp(QShow / 100.f, 0.f, 1.f);
			}
		}
	}

	// Stock-regel: duidelijk of je 't überhaupt hebt; zo niet, geen valse kwaliteit-straf op de kans.
	if (StockQty >= C->DesiredQuantity)
	{
		DrawText(FString::Printf(TEXT("Your stock: %dg %s  -  THC %.0f%%  Quality %.0f%%"),
			StockQty, *PrettyItemName(C->DesiredProductId), ThcShow, QShow),
			FLinearColor(0.8f, 0.85f, 1.f), InnerX, y, Font);
	}
	else
	{
		DrawText(FString::Printf(TEXT("Your stock: %dg of %d needed - not enough %s!"),
			StockQty, C->DesiredQuantity, *PrettyItemName(C->DesiredProductId)),
			FLinearColor(1.f, 0.5f, 0.4f), InnerX, y, Font);
	}
	y += 20.f;

	// --- Live acceptatie-% (volgt ook de hover; client berekent dit lokaal want stats repliceren) ---
	const float Chance = C->GetAcceptanceChance(EffAsk, Quality01);
	const FLinearColor ChanceCol = Chance >= 66.f ? FLinearColor::Green
		: (Chance >= 33.f ? FLinearColor(1.f, 0.8f, 0.2f) : FLinearColor(1.f, 0.4f, 0.4f));
	DrawText(FString::Printf(TEXT("Chance they accept: %.0f%%"), Chance), ChanceCol, InnerX, y, Font);
	y += 22.f;
	DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.9f), InnerX, y, W - 32.f, 12.f);
	DrawRect(ChanceCol, InnerX, y, (W - 32.f) * FMath::Clamp(Chance / 100.f, 0.f, 1.f), 12.f);
	y += 16.f;

	// Klant-binding nu + voorspelling NA een geslaagde deal (zelfde formule als de server).
	DrawText(FString::Printf(TEXT("Respect %.0f   Loyalty %.0f   Addiction %.0f"), C->Respect, C->Loyalty, C->Addiction),
		FLinearColor(0.7f, 0.7f, 0.8f), InnerX, y, Font);
	y += 20.f;
	float pR = 0.f, pL = 0.f, pA = 0.f;
	C->PreviewDealOutcome(EffAsk, Quality01, (StockQty > 0 ? ThcShow : -1.f), pR, pL, pA);
	DrawText(FString::Printf(TEXT("If accepted:  R %.0f->%.0f   L %.0f->%.0f   A %.0f->%.0f"),
		C->Respect, pR, C->Loyalty, pL, C->Addiction, pA),
		FLinearColor(0.55f, 0.95f, 0.6f), InnerX, y, Font);
	y += 24.f;

	DrawButton(FName(TEXT("dealconfirm")), TEXT("Offer deal"), InnerX, y, 200.f, FLinearColor::White);
	DrawButton(FName(TEXT("dealclose")), TEXT("Cancel"), InnerX + 210.f, y, 160.f, FLinearColor::Yellow);
}

void AWeedShopHUD::DrawInventoryUI(UInventoryComponent* Inv)
{
	if (!Inv)
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	const float W = 560.f;
	const float H = 440.f;
	const float PX = (Canvas ? Canvas->ClipX : 1280.f) * 0.5f - W * 0.5f;
	const float PY = (Canvas ? Canvas->ClipY : 720.f) * 0.5f - H * 0.5f;
	const float InnerX = PX + 16.f;

	DrawRect(FLinearColor(0.05f, 0.06f, 0.09f, 0.97f), PX, PY, W, H);
	float y = PY + 14.f;
	DrawText(TEXT("INVENTORY"), FLinearColor(0.6f, 1.f, 0.6f), InnerX, y, Font);
	DrawButton(FName(TEXT("invclose")), TEXT("Close (I)"), PX + W - 130.f, y - 2.f, 114.f, FLinearColor::Yellow);
	y += 28.f;

	// Slots + gewicht.
	const int32 UsedSlots = Inv->GetUsedSlots();
	const float Weight = Inv->GetTotalWeight();
	const bool bHeavy = (Inv->MaxWeight > 0.f && Weight > Inv->MaxWeight - 0.001f);
	DrawText(FString::Printf(TEXT("Slots %d/%d        Weight %.1f / %.0f kg"),
		UsedSlots, Inv->MaxStacks, Weight, Inv->MaxWeight),
		bHeavy ? FLinearColor(1.f, 0.5f, 0.4f) : FLinearColor(0.8f, 0.85f, 1.f), InnerX, y, Font);
	y += 24.f;

	const TArray<FInventoryStack>& Stacks = Inv->GetStacks();
	const FString HS = HoveredBox.ToString();
	const bool bMouseDown = PlayerOwner && PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);

	// Hotbar-rij onderaan (drop-doelen).
	const float HbY = PY + H - 92.f;

	// Achtergrond-dropzone voor het grid (om iets van de hotbar te halen door het hierheen te slepen).
	const float GridTop = y;
	const float GridBot = HbY - 28.f;
	AddHitBox(FVector2D(InnerX, GridTop), FVector2D(W - 32.f, GridBot - GridTop), FName(TEXT("invgrid")), true, 1);

	// Grid met alleen de items die NIET op de hotbar staan (geen dubbele weergave).
	TArray<int32> FreeStacks;
	for (int32 i = 0; i < Stacks.Num(); ++i)
	{
		if (!Inv->IsStackOnHotbar(Stacks[i].StackId)) { FreeStacks.Add(i); }
	}

	const int32 Cols = 5;
	const float CellW = (W - 32.f) / Cols;
	const float CellH = 46.f;
	// Gevulde cellen (vrije items) + ghost-cellen voor de resterende vrije slots, zodat je ziet
	// hoeveel ruimte je nog hebt. Vrije slots = totale slots - alle stapels (incl. hotbar).
	const int32 FreeSlots = (Inv->MaxStacks > 0) ? FMath::Max(0, Inv->MaxStacks - Inv->GetUsedSlots()) : 10;
	const int32 TotalCells = FreeStacks.Num() + FreeSlots;
	for (int32 n = 0; n < TotalCells; ++n)
	{
		const int32 col = n % Cols;
		const int32 row = n / Cols;
		const float cx = InnerX + col * CellW;
		const float cy = GridTop + row * CellH;
		if (cy + CellH > GridBot) { break; }

		if (n >= FreeStacks.Num())
		{
			// Lege ghost-slot.
			DrawRect(FLinearColor(0.10f, 0.10f, 0.13f, 0.35f), cx + 2.f, cy + 2.f, CellW - 4.f, CellH - 4.f);
			continue;
		}

		const FInventoryStack& S = Stacks[FreeStacks[n]];
		const FName Box(*FString::Printf(TEXT("inv_%d"), n));
		const bool bHover = (HoveredBox == Box);
		const bool bIsDragged = bDraggingItem && DraggedStackId == S.StackId;
		DrawRect(bIsDragged ? FLinearColor(0.35f, 0.30f, 0.10f, 0.85f)
			: (bHover ? FLinearColor(0.20f, 0.26f, 0.38f, 0.95f) : FLinearColor(0.10f, 0.10f, 0.13f, 0.92f)),
			cx + 2.f, cy + 2.f, CellW - 4.f, CellH - 4.f);
		FString Lbl = PrettyItemName(S.ItemId);
		if (Lbl.Len() > 13) { Lbl = Lbl.Left(13); }
		DrawText(Lbl, FLinearColor(0.85f, 0.9f, 1.f), cx + 6.f, cy + 5.f, Font);
		const bool bThc = (S.ItemId.ToString().StartsWith(TEXT("Bud_")) || S.ItemId.ToString().StartsWith(TEXT("Joint_")));
		const FString GExtra = bThc ? FString::Printf(TEXT("  THC%.0f%% Q%.0f%%"), S.Quality, S.QualityPct) : TEXT("");
		DrawText(FString::Printf(TEXT("x%d%s"), S.Quantity, *GExtra),
			FLinearColor(0.75f, 0.75f, 0.8f), cx + 6.f, cy + 26.f, Font);
		AddHitBox(FVector2D(cx + 2.f, cy + 2.f), FVector2D(CellW - 4.f, CellH - 4.f), Box, true, 3);

		// Meerdere batches van dit wiet-product? Toon een 'MERGE'-knop (hogere prioriteit dan de cel).
		if (bThc && Inv->CountStacksOf(S.ItemId) > 1)
		{
			const FName MBox(*FString::Printf(TEXT("merge_%d"), n));
			const float mw = 52.f, mh = 16.f;
			const float mxp = cx + CellW - mw - 4.f, myp = cy + CellH - mh - 4.f;
			const bool bMHover = (HoveredBox == MBox);
			DrawRect(bMHover ? FLinearColor(0.95f, 0.7f, 0.15f, 0.95f) : FLinearColor(0.5f, 0.4f, 0.1f, 0.95f), mxp, myp, mw, mh);
			DrawText(TEXT("MERGE"), FLinearColor::White, mxp + 4.f, myp + 1.f, Font);
			AddHitBox(FVector2D(mxp, myp), FVector2D(mw, mh), MBox, true, 5);
		}
	}

	// Hotbar-rij (hbar_<i>).
	DrawText(TEXT("Hotbar  (drag items here for quick-select 1-8)"), FLinearColor(0.7f, 0.7f, 0.8f), InnerX, HbY - 22.f, Font);
	const float SlotW = 60.f;
	const float SlotH = 48.f;
	const float Gap = 4.f;
	const int32 N = UInventoryComponent::HotbarSize;
	const float RowW = N * (SlotW + Gap);
	float hx = PX + (W - RowW) * 0.5f;
	int32 DropTarget = -1;
	for (int32 i = 0; i < N; ++i)
	{
		const FName Box(*FString::Printf(TEXT("hbar_%d"), i));
		const bool bHover = (HoveredBox == Box);
		if (bHover) { DropTarget = i; }
		const bool bDropHere = bDraggingItem && bHover;
		const bool bActive = (i == Inv->GetActiveSlot());
		DrawRect(bDropHere ? FLinearColor(1.f, 0.85f, 0.1f, 0.7f)
			: (bActive ? FLinearColor(0.16f, 0.16f, 0.10f, 0.95f) : FLinearColor(0.10f, 0.10f, 0.13f, 0.9f)),
			hx, HbY, SlotW, SlotH);
		DrawText(FString::Printf(TEXT("%d"), i + 1), FLinearColor(0.5f, 0.5f, 0.6f), hx + SlotW - 12.f, HbY + 1.f, Font);
		const int32 HbStackIdx = Inv->FindStackById(Inv->GetHotbarStackId(i));
		if (Stacks.IsValidIndex(HbStackIdx))
		{
			const FInventoryStack& HbS = Stacks[HbStackIdx];
			FString Lbl = PrettyItemName(HbS.ItemId);
			if (Lbl.Len() > 9) { Lbl = Lbl.Left(9); }
			DrawText(Lbl, FLinearColor(0.85f, 0.9f, 1.f), hx + 4.f, HbY + 16.f, Font);
			DrawText(FString::Printf(TEXT("x%d"), HbS.Quantity), FLinearColor(0.7f, 0.7f, 0.7f), hx + 4.f, HbY + 32.f, Font);
		}
		AddHitBox(FVector2D(hx, HbY), FVector2D(SlotW, SlotH), Box, true, 3);
		hx += SlotW + Gap;
	}

	// Merge-popup open? Geen drag/hotbar-interactie in de inventory (de popup vangt de klikken).
	const bool bMergeBlocking = GetPhone() && GetPhone()->IsMergeOpen();

	// Drag-state (gepolled met muisknop + hover, DPI-onafhankelijk). Start vanuit grid (inv_) of hotbar (hbar_).
	if (bMergeBlocking)
	{
		// niets
	}
	else if (!bDraggingItem)
	{
		if (bMouseDown && HS.StartsWith(TEXT("inv_")))
		{
			const int32 Idx = FCString::Atoi(*HS.RightChop(4));
			if (FreeStacks.IsValidIndex(Idx))
			{
				bDraggingItem = true;
				DraggedStackId = Stacks[FreeStacks[Idx]].StackId;
			}
		}
		else if (bMouseDown && HS.StartsWith(TEXT("hbar_")))
		{
			const int32 Slot = FCString::Atoi(*HS.RightChop(5));
			const int32 SlotStackId = Inv->GetHotbarStackId(Slot);
			if (SlotStackId != 0)
			{
				bDraggingItem = true;
				DraggedStackId = SlotStackId;
			}
		}
	}
	else
	{
		const int32 DragIdx = Inv->FindStackById(DraggedStackId);
		const FName DragItem = Stacks.IsValidIndex(DragIdx) ? Stacks[DragIdx].ItemId : NAME_None;
		DrawText(FString::Printf(TEXT("Dragging: %s   -   drop on a hotbar slot, or back in the grid to remove"), *PrettyItemName(DragItem)),
			FLinearColor(1.f, 0.95f, 0.4f), InnerX, PY + H - 20.f, Font);
		if (!bMouseDown)
		{
			if (DropTarget >= 0)
			{
				Inv->AssignHotbarStack(DropTarget, DraggedStackId);    // op hotbar zetten / wisselen
			}
			else if (HS.StartsWith(TEXT("inv_")) || HS == TEXT("invgrid"))
			{
				Inv->UnassignHotbarStack(DraggedStackId);              // terug naar inventory (van hotbar af)
			}
			bDraggingItem = false;
			DraggedStackId = 0;
		}
	}

	if (PlayerOwner)
	{
		PlayerOwner->CurrentMouseCursor = bDraggingItem ? EMouseCursor::GrabHandClosed : EMouseCursor::Default;
	}
}

void AWeedShopHUD::DrawMergeUI(UPhoneClientComponent* Phone, UInventoryComponent* Inv)
{
	const FName ItemId = Phone->GetMergeItemId();
	if (ItemId.IsNone() || !Inv || Inv->CountStacksOf(ItemId) < 2)
	{
		Phone->CloseMerge();
		return;
	}
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;

	const float W = 460.f;
	const float H = 250.f;
	const float PX = (Canvas ? Canvas->ClipX : 1280.f) * 0.5f - W * 0.5f;
	const float PY = (Canvas ? Canvas->ClipY : 720.f) * 0.5f - H * 0.5f;
	const float InnerX = PX + 16.f;

	// Donkere dim achter de popup + paneel.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas ? Canvas->ClipX : 1280.f, Canvas ? Canvas->ClipY : 720.f);
	DrawRect(FLinearColor(0.06f, 0.07f, 0.10f, 0.99f), PX, PY, W, H);

	float y = PY + 14.f;
	DrawText(TEXT("MERGE WEED"), FLinearColor(0.6f, 1.f, 0.6f), InnerX, y, Font); y += 28.f;

	int32 Qty = 0, Batches = 0; float AvgThc = 0.f, AvgQ = 0.f;
	Inv->GetMergePreview(ItemId, Qty, AvgThc, AvgQ, Batches);

	DrawText(FString::Printf(TEXT("Combine %d separate batches of %s into one stack."), Batches, *PrettyItemName(ItemId)),
		FLinearColor(0.9f, 0.9f, 1.f), InnerX, y, Font); y += 24.f;

	// Lijst van de batches (max 4 tonen).
	int32 shown = 0;
	for (const FInventoryStack& S : Inv->GetStacks())
	{
		if (S.ItemId != ItemId) { continue; }
		if (shown < 4)
		{
			DrawText(FString::Printf(TEXT("  - %dg  @  THC %.0f%%  /  Quality %.0f%%"), S.Quantity, S.Quality, S.QualityPct),
				FLinearColor(0.78f, 0.8f, 0.85f), InnerX, y, Font);
			y += 19.f;
		}
		++shown;
	}
	if (shown > 4) { DrawText(FString::Printf(TEXT("  ...and %d more"), shown - 4), FLinearColor(0.6f, 0.6f, 0.65f), InnerX, y, Font); y += 19.f; }
	y += 6.f;

	DrawText(TEXT("The THC% and Quality% become the weighted average:"),
		FLinearColor(0.7f, 0.7f, 0.8f), InnerX, y, Font); y += 22.f;
	DrawText(FString::Printf(TEXT("Result: %dg  @  THC %.0f%%  /  Quality %.0f%%"), Qty, AvgThc, AvgQ),
		FLinearColor(1.f, 0.95f, 0.55f), InnerX, y, Font); y += 30.f;

	DrawButton(FName(TEXT("mergeconfirm")), TEXT("Merge (average)"), InnerX, y, 200.f, FLinearColor::White);
	DrawButton(FName(TEXT("mergeclose")), TEXT("Cancel"), InnerX + 210.f, y, 150.f, FLinearColor::Yellow);

	if (PlayerOwner) { PlayerOwner->CurrentMouseCursor = EMouseCursor::Default; }
}

void AWeedShopHUD::DrawPotUpgradeUI(UPhoneClientComponent* Phone)
{
	AGrowPlant* Pot = Phone->GetUpgradePot();
	if (!Pot)
	{
		Phone->ClosePotUpgrade();
		return;
	}
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	const float W = 480.f;
	const float H = 250.f;
	const float PX = (Canvas ? Canvas->ClipX : 1280.f) * 0.5f - W * 0.5f;
	const float PY = (Canvas ? Canvas->ClipY : 720.f) * 0.5f - H * 0.5f;
	const float InnerX = PX + 16.f;

	DrawRect(FLinearColor(0.05f, 0.06f, 0.09f, 0.96f), PX, PY, W, H);
	float y = PY + 14.f;

	FPotDef Pd;
	const FString PotName = GetPotDef(Pot->GetPotTier(), Pd) ? Pd.DisplayName : TEXT("Pot");
	DrawText(FString::Printf(TEXT("POT UPGRADES  —  %s"), *PotName), FLinearColor(0.6f, 1.f, 0.6f), InnerX, y, Font);
	DrawButton(FName(TEXT("potupgclose")), TEXT("Close (U)"), PX + W - 130.f, y - 2.f, 114.f, FLinearColor::Yellow);
	y += 28.f;
	DrawText(TEXT("Upgrades horen bij deze pot; verkoop je 'm, dan zijn ze weg."),
		FLinearColor(0.7f, 0.7f, 0.8f), InnerX, y, Font);
	y += 26.f;

	const TArray<FPotUpgradeDef>& Ups = GetPotUpgrades();
	for (int32 i = 0; i < Ups.Num(); ++i)
	{
		const bool bOwned = Pot->HasPotUpgrade(i);
		const int32 Cost = GetPotUpgradeCost(i, Pot->GetPotTier());
		DrawText(FString::Printf(TEXT("%s  —  %s"), *Ups[i].DisplayName, *Ups[i].Desc),
			FLinearColor(0.9f, 0.9f, 0.75f), InnerX, y, Font);
		y += 20.f;
		if (bOwned)
		{
			DrawText(TEXT("   Installed"), FLinearColor(0.5f, 1.f, 0.5f), InnerX, y, Font);
		}
		else
		{
			DrawButton(FName(*FString::Printf(TEXT("potupg_%d"), i)),
				FString::Printf(TEXT("Buy  -  EUR %.2f"), Cost / 100.f), InnerX, y, 220.f, FLinearColor::White);
		}
		y += RowH + 4.f;
	}
}

void AWeedShopHUD::NotifyHitBoxBeginCursorOver(FName BoxName)
{
	Super::NotifyHitBoxBeginCursorOver(BoxName);
	HoveredBox = BoxName;
}

void AWeedShopHUD::NotifyHitBoxEndCursorOver(FName BoxName)
{
	Super::NotifyHitBoxEndCursorOver(BoxName);
	if (HoveredBox == BoxName)
	{
		HoveredBox = NAME_None;
	}
}
