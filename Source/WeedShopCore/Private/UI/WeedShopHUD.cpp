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

namespace
{
	constexpr float PhoneW = 420.f;
	constexpr float PhoneH = 520.f;
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

void AWeedShopHUD::DrawPhone(UPhoneClientComponent* Phone)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	const int32 Tab = Phone->GetTab();

	const float PX = (Canvas ? Canvas->ClipX : 1280.f) - PhoneW - 30.f;
	const float PY = 80.f;
	const float InnerX = PX + 14.f;
	const float InnerW = PhoneW - 28.f;

	DrawRect(FLinearColor(0.05f, 0.05f, 0.08f, 0.94f), PX, PY, PhoneW, PhoneH);

	float y = PY + 12.f;
	DrawText(TEXT("PHONE"), FLinearColor(0.5f, 1.f, 0.5f), InnerX, y, Font); y += 26.f;

	if (GS && GS->GetEconomy())
	{
		DrawText(FString::Printf(TEXT("Cash: EUR %.2f"), GS->GetEconomy()->GetBalanceEuros()),
			FLinearColor::White, InnerX, y, Font);
		y += 26.f;
	}

	// Tab-knoppen (klikbaar).
	static const TCHAR* TabNames[4] = { TEXT("Upgrades"), TEXT("Suppliers"), TEXT("Contacts"), TEXT("Messages") };
	const float TabW = InnerW / 4.f;
	for (int32 i = 0; i < 4; ++i)
	{
		const FName Box(*FString::Printf(TEXT("tab_%d"), i));
		const FLinearColor Col = (i == Tab) ? FLinearColor(0.6f, 1.f, 0.6f) : FLinearColor(0.7f, 0.7f, 0.8f);
		if (DrawButton(Box, TabNames[i], InnerX + i * TabW, y, TabW - 4.f, Col))
		{
			HoverTooltip = FString::Printf(TEXT("Open the %s tab"), TabNames[i]);
		}
	}
	y += RowH + 6.f;

	// Inhoud per tab.
	if (Tab == 1) // Suppliers: subcategorie-knoppen + items van de gekozen categorie
	{
		if (UStoreComponent* Store = GS ? GS->GetStore() : nullptr)
		{
			// Categorie-knoppen (Seeds / Papers / Pots / Soil / Water).
			static const TCHAR* CatNames[UStoreComponent::SupplierCatCount] = { TEXT("Seeds"), TEXT("Papers"), TEXT("Pots"), TEXT("Soil"), TEXT("Water"), TEXT("Sell") };
			const int32 Cat = Phone->GetSupplierCat();
			const float CatW = InnerW / UStoreComponent::SupplierCatCount;
			for (int32 i = 0; i < UStoreComponent::SupplierCatCount; ++i)
			{
				const FLinearColor Col = (i == Cat) ? FLinearColor(0.6f, 1.f, 0.6f) : FLinearColor(0.7f, 0.7f, 0.8f);
				DrawButton(FName(*FString::Printf(TEXT("scat_%d"), i)), CatNames[i], InnerX + i * CatW, y, CatW - 3.f, Col);
			}
			y += RowH + 6.f;

			// Items van de gekozen categorie (index = positie in de lijst).
			const bool bSeeds = UStoreComponent::IsSeedCategory(Cat);
			const TArray<FName> Items = Store->GetSupplierCategory(Cat);
			int32 idx = 0;
			for (const FName& Id : Items)
			{
				if (y > PY + PhoneH - 50.f) { break; }
				FText Name; int32 Price = 0; int32 Pack = 1;
				const bool bOk = bSeeds ? Store->GetSeedDisplay(Id, Name, Price)
										: Store->GetSupplyDisplay(Id, Name, Price, Pack);
				if (bOk)
				{
					const FString Label = bSeeds
						? FString::Printf(TEXT("%s  -  EUR %.2f"), *Name.ToString(), Price / 100.f)
						: FString::Printf(TEXT("%s  -  EUR %.2f"), *Name.ToString(), Price / 100.f);
					DrawButton(FName(*FString::Printf(TEXT("buy_%d"), idx)), Label, InnerX, y, InnerW,
						bSeeds ? FLinearColor::White : FLinearColor(0.85f, 0.95f, 0.8f));
					y += RowH;
				}
				++idx;
			}
			if (Cat == UStoreComponent::SupplierCatSell)
				{
					APawn* PP = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
					if (const UInventoryComponent* PInv = PP ? PP->FindComponentByClass<UInventoryComponent>() : nullptr)
					{
						const TArray<FInventoryStack>& SellStacks = PInv->GetStacks();
						for (int32 si = 0; si < SellStacks.Num(); ++si)
						{
							const int32 Val = Store->GetSellValueCents(SellStacks[si].ItemId); if (Val > 0 && y < PY + PhoneH - 50.f)
							{
								DrawButton(FName(*FString::Printf(TEXT("sell_%d"), si)),
									FString::Printf(TEXT("Sell %s  x%d   (+EUR %.2f each)"), *PrettyItemName(SellStacks[si].ItemId), SellStacks[si].Quantity, Val / 100.f),
									InnerX, y, InnerW, FLinearColor(1.f, 0.8f, 0.5f));
								y += RowH;
							}
							/* */
						}
					}
				}
				if (Items.Num() == 0 && Cat != UStoreComponent::SupplierCatSell)
			{
				DrawText(TEXT("(nothing here yet)"), FLinearColor::Gray, InnerX, y, Font);
			}
		}
	}
	else if (Tab == 2) // Contacten (alleen weergave)
	{
		if (UContactsComponent* Con = GS ? GS->GetContacts() : nullptr)
		{
			if (Con->GetContacts().Num() == 0)
			{
				DrawText(TEXT("(no contacts yet - deal with customers)"), FLinearColor::Gray, InnerX, y, Font);
				y += 22.f;
			}
			for (const FPhoneContact& C : Con->GetContacts())
			{
				DrawText(FString::Printf(TEXT("%s   (relationship %.0f%%)"), *C.DisplayName.ToString(), C.Relationship),
					FLinearColor::White, InnerX, y, Font);
				y += 22.f;
				if (y > PY + PhoneH - 60.f) break;
			}
		}
	}
	else if (Tab == 3) // Berichten
	{
		if (UContactsComponent* Con = GS ? GS->GetContacts() : nullptr)
		{
			if (DrawButton(FName(TEXT("buy_0")), TEXT("Accept (first open message)"), InnerX, y, InnerW, FLinearColor(0.7f, 1.f, 0.7f)))
			{
				HoverTooltip = TEXT("Make appointment -> loyalty up; customer shows up on time");
			}
			y += RowH;
			if (DrawButton(FName(TEXT("buy_1")), TEXT("Decline (first open message)"), InnerX, y, InnerW, FLinearColor(1.f, 0.75f, 0.6f)))
			{
				HoverTooltip = TEXT("Cancel -> loyalty down; customer won't come");
			}
			y += RowH + 6.f;

			if (Con->GetMessages().Num() == 0)
			{
				DrawText(TEXT("(no messages)"), FLinearColor::Gray, InnerX, y, Font);
				y += 22.f;
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
	else // Upgrades
	{
		if (UUpgradeComponent* Upg = GS ? GS->GetUpgrades() : nullptr)
		{
			int32 idx = 0;
			for (const FName& Id : Upg->GetAllUpgradeIds())
			{
				FText Name; int32 Cost = 0; bool bPurchased = false; bool bAvailable = false;
				if (Upg->GetUpgradeDisplay(Id, Name, Cost, bPurchased, bAvailable))
				{
					const TCHAR* Suffix = bPurchased ? TEXT("  [purchased]") : (bAvailable ? TEXT("") : TEXT("  [locked]"));
					const FLinearColor Col = bPurchased ? FLinearColor::Gray
						: (bAvailable ? FLinearColor::White : FLinearColor(0.8f, 0.55f, 0.55f));
					const FName Box(*FString::Printf(TEXT("buy_%d"), idx));
					if (DrawButton(Box, FString::Printf(TEXT("%s  -  EUR %.2f%s"), *Name.ToString(), Cost / 100.f, Suffix),
						InnerX, y, InnerW, Col))
					{
						HoverTooltip = bPurchased ? TEXT("Already purchased")
							: (bAvailable ? FString::Printf(TEXT("Buy for EUR %.2f"), Cost / 100.f)
								: TEXT("Locked - reach the required phase first"));
					}
					y += RowH;
				}
				if (++idx >= 8) break;
			}
		}
	}

	// Tooltip (hover-info).
	if (!HoverTooltip.IsEmpty())
	{
		const float TipY = PY + PhoneH - 56.f;
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), PX + 6.f, TipY - 2.f, PhoneW - 12.f, 22.f);
		DrawText(HoverTooltip, FLinearColor(1.f, 1.f, 0.6f), InnerX, TipY, Font);
	}

	// Sluit-knop.
	if (DrawButton(FName(TEXT("close")), TEXT("Close (Tab)"), InnerX, PY + PhoneH - 30.f, 130.f, FLinearColor::Yellow))
	{
		HoverTooltip = TEXT("Close the phone");
	}
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
	else if (S.StartsWith(TEXT("buy_")))
	{
		Phone->DoAction(FCString::Atoi(*S.RightChop(4)));
	}
	else if (S.StartsWith(TEXT("scat_")))
	{
		Phone->SetSupplierCat(FCString::Atoi(*S.RightChop(5)));
	}
	else if (S.StartsWith(TEXT("sell_")))
	{
		Phone->SellInventoryIndex(FCString::Atoi(*S.RightChop(5)));
	}
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
