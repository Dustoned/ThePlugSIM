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
#include "Customer/CustomerBase.h"

namespace
{
	constexpr float PhoneW = 420.f;
	constexpr float PhoneH = 440.f;
	constexpr float RowH = 26.f;

	// Leesbare naam voor een item-id, zodat de speler "Wiet" vs "Zaadje" duidelijk ziet
	// (zodat de speler wiet en zaadjes duidelijk uit elkaar houdt).
	FString PrettyItemName(const FName ItemId)
	{
		const FString S = ItemId.ToString();
		if (S.StartsWith(TEXT("Bud_")))    { return FString::Printf(TEXT("Weed: %s"), *S.RightChop(4)); }
		if (S.StartsWith(TEXT("Seed_")))   { return FString::Printf(TEXT("Seed: %s"), *S.RightChop(5)); }
		if (S.StartsWith(TEXT("Joint_")))  { return FString::Printf(TEXT("Joint %s"), *S.RightChop(6)); }
		if (S == TEXT("Papers_Small"))     { return TEXT("Papers (small)"); }
		if (S == TEXT("Papers_Big"))       { return TEXT("Papers (big)"); }
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

	if (P)
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			DrawText(TEXT("Inventory:"), FLinearColor(0.8f, 0.8f, 1.f), X, Y, Font);
			Y += 22.f;
			if (Inv->GetStacks().Num() == 0)
			{
				DrawText(TEXT("  (empty)"), FLinearColor::Gray, X, Y, Font);
				Y += 20.f;
			}
			for (const FInventoryStack& Stack : Inv->GetStacks())
			{
				DrawText(FString::Printf(TEXT("  %s  x%d"), *PrettyItemName(Stack.ItemId), Stack.Quantity),
					FLinearColor::White, X, Y, Font);
				Y += 20.f;
			}
		}
	}

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
		else if (PlayerOwner)
		{
			// Geen UI open: cursor terug naar standaard (anders blijft het handje hangen).
			PlayerOwner->CurrentMouseCursor = EMouseCursor::Default;
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
					float PYp = CY - 150.f;
					DrawRect(FLinearColor(0.04f, 0.06f, 0.04f, 0.9f), PXp, PYp, PW, 132.f);
					float ly = PYp + 8.f;
					const float lx = PXp + 12.f;

					if (!Plant->IsPlanted())
					{
						DrawText(TEXT("Empty pot"), FLinearColor(0.7f, 1.f, 0.7f), lx, ly, Font); ly += 24.f;
						DrawText(TEXT("Plant a seed (E)."), FLinearColor::White, lx, ly, Font);
					}
					else
					{
						static const TCHAR* PhaseNames[] = { TEXT("Seedling"), TEXT("Vegetative"), TEXT("Pre-flower"), TEXT("Flower"), TEXT("Ready to harvest") };
						const int32 Pi = FMath::Clamp((int32)Plant->GetPhase(), 0, 4);
						DrawText(FString::Printf(TEXT("%s"), *Plant->StrainId.ToString()), FLinearColor(0.7f, 1.f, 0.7f), lx, ly, Font); ly += 24.f;
						DrawText(FString::Printf(TEXT("Phase: %s  (%.0f%%)"), PhaseNames[Pi], Plant->GetGrowthFraction() * 100.f),
							FLinearColor::White, lx, ly, Font); ly += 20.f;

						const int32 Rem = FMath::RoundToInt(Plant->GetSecondsRemaining());
						if (Plant->GetPhase() == EGrowthPhase::Harvestable)
						{
							DrawText(TEXT("READY TO HARVEST — press E"), FLinearColor::Green, lx, ly, Font);
						}
						else
						{
							DrawText(FString::Printf(TEXT("Time to harvest: %d:%02d"), Rem / 60, Rem % 60),
								FLinearColor::White, lx, ly, Font);
						}
						ly += 20.f;

						// Verzorging-balk.
						const float Care = Plant->GetCareMultiplier();
						DrawText(FString::Printf(TEXT("Care: %.0f%%"), Care * 100.f),
							Care >= 0.8f ? FLinearColor::Green : (Care >= 0.5f ? FLinearColor(1.f, 0.7f, 0.2f) : FLinearColor(1.f, 0.4f, 0.4f)),
							lx, ly, Font); ly += 18.f;
						DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.9f), lx, ly, PW - 24.f, 8.f);
						DrawRect(FLinearColor(0.3f, 0.6f, 1.f, 0.95f), lx, ly, (PW - 24.f) * Care, 8.f); ly += 16.f;

						DrawText(FString::Printf(TEXT("Expected: %.0fg @ %.0f%% THC"),
							Plant->GetEstimatedYieldGrams(), Plant->GetEstimatedThcPercent()),
							FLinearColor(0.9f, 0.9f, 0.7f), lx, ly, Font);
					}
				}

				const FText Prompt = IInteractable::Execute_GetInteractionPrompt(Focus);
				if (!Prompt.IsEmpty())
				{
					DrawText(FString::Printf(TEXT("[E] %s"), *Prompt.ToString()),
						FLinearColor::Yellow, CX - 60.f, CY + 60.f, Font);
				}
			}
		}
	}

	// Hotbar/inventory onderaan het scherm.
	if (P)
	{
		if (const UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>())
		{
			const TArray<FInventoryStack>& Stacks = Inv->GetStacks();
			const int32 SlotCount = FMath::Max(8, Stacks.Num());
			const float SlotW = 84.f;
			const float SlotH = 46.f;
			const float ScreenW = Canvas ? Canvas->ClipX : 1280.f;
			const float ScreenH = Canvas ? Canvas->ClipY : 720.f;
			const float TotalW = SlotCount * (SlotW + 6.f);
			float SX = (ScreenW - TotalW) * 0.5f;
			const float SY = ScreenH - SlotH - 24.f;

			for (int32 i = 0; i < SlotCount; ++i)
			{
				DrawRect(FLinearColor(0.08f, 0.08f, 0.10f, 0.85f), SX, SY, SlotW, SlotH);
				if (Stacks.IsValidIndex(i))
				{
					const FInventoryStack& S = Stacks[i];
					const bool bIsSeed = S.ItemId.ToString().StartsWith(TEXT("Seed_"));
					DrawText(PrettyItemName(S.ItemId),
						bIsSeed ? FLinearColor(0.7f, 0.85f, 0.6f) : FLinearColor(0.85f, 0.9f, 1.f),
						SX + 5.f, SY + 4.f, Font);
					DrawText(FString::Printf(TEXT("x%d"), S.Quantity), FLinearColor::White, SX + 5.f, SY + 24.f, Font);
				}
				SX += SlotW + 6.f;
			}

			DrawText(TEXT("R = roll joint   |   F = give sample (look at NPC)"),
				FLinearColor(0.7f, 0.7f, 0.7f), (ScreenW - TotalW) * 0.5f, SY - 22.f, Font);
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
	if (Tab == 1) // Suppliers: zaden + supplies (vloei)
	{
		if (UStoreComponent* Store = GS ? GS->GetStore() : nullptr)
		{
			int32 idx = 0;
			const TArray<FName> Seeds = Store->GetSeedCatalog();
			for (const FName& Id : Seeds)
			{
				FText Name; int32 Price = 0;
				if (Store->GetSeedDisplay(Id, Name, Price))
				{
					if (DrawButton(FName(*FString::Printf(TEXT("buy_%d"), idx)),
						FString::Printf(TEXT("Seed: %s  -  EUR %.2f"), *Name.ToString(), Price / 100.f),
						InnerX, y, InnerW, FLinearColor::White))
					{
						HoverTooltip = FString::Printf(TEXT("Buy a %s seed"), *Name.ToString());
					}
					y += RowH;
				}
				++idx;
				if (idx >= 8) break;
			}
			// Supplies (vloei) ná de zaden, doorlopende index.
			int32 si = Seeds.Num();
			for (const FName& Id : Store->GetSupplyCatalog())
			{
				FText Name; int32 Price = 0; int32 Pack = 0;
				if (Store->GetSupplyDisplay(Id, Name, Price, Pack) && y < PY + PhoneH - 60.f)
				{
					if (DrawButton(FName(*FString::Printf(TEXT("buy_%d"), si)),
						FString::Printf(TEXT("%s  -  EUR %.2f"), *Name.ToString(), Price / 100.f),
						InnerX, y, InnerW, FLinearColor(0.85f, 0.95f, 0.8f)))
					{
						HoverTooltip = FString::Printf(TEXT("Buy %d papers"), Pack);
					}
					y += RowH;
				}
				++si;
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
		DrawButton(FName(TEXT("rollclose")), TEXT("Close (R)"), InnerX, y, 150.f, FLinearColor::Yellow);
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

	// Kwaliteit-balk.
	const float Quality = FMath::Clamp(G / 5.f, 0.f, 1.f);
	DrawText(FString::Printf(TEXT("Quality: %.0f%%"), Quality * 100.f),
		Quality >= 0.6f ? FLinearColor::Green : (Quality >= 0.3f ? FLinearColor(1.f, 0.7f, 0.2f) : FLinearColor(1.f, 0.4f, 0.4f)),
		InnerX, y, Font);
	y += 24.f;
	DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.9f), InnerX, y, W - 32.f, 14.f);
	DrawRect(FLinearColor(0.4f, 0.9f, 0.4f, 0.95f), InnerX, y, (W - 32.f) * Quality, 14.f);
	y += 28.f;

	DrawButton(FName(TEXT("rollconfirm")), FString::Printf(TEXT("Roll joint (costs %dg weed)"), G),
		InnerX, y, 250.f, FLinearColor::White);
	DrawButton(FName(TEXT("rollclose")), TEXT("Close (R)"), InnerX + 260.f, y, 150.f, FLinearColor::Yellow);
}

void AWeedShopHUD::DrawDealUI(UPhoneClientComponent* Phone)
{
	ACustomerBase* C = Phone->GetDealCustomer();
	if (!C)
	{
		Phone->CloseDeal();
		return;
	}

	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	const float W = 480.f;
	const float H = 270.f;
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

	const int32 Ask = Phone->GetDealAskCents();
	const float AskPct = (Market > 0) ? float(Ask) / float(Market) : 1.f;
	DrawText(FString::Printf(TEXT("Your price: EUR %.2f / unit  (%.0f%% of market)   Total: EUR %.2f"),
		Ask / 100.f, AskPct * 100.f, (Ask * Qty) / 100.f),
		FLinearColor(1.f, 0.95f, 0.6f), InnerX, y, Font);
	y += 26.f;

	// --- Prijs-slider: stops van 40% tot 200% van de markt ---
	const float TrackX = InnerX;
	const float TrackW = W - 32.f;
	const float TrackY = y + 6.f;
	const float TrackH = 16.f;
	const int32 N = UPhoneClientComponent::DealStepCount; // 17 stops (40..200%)
	const float SegW = TrackW / float(N);

	// Welke stop hangt de cursor boven? (hit-box hover, DPI-onafhankelijk).
	int32 HoverIdx = -1;
	if (!HoveredBox.IsNone())
	{
		const FString HS = HoveredBox.ToString();
		if (HS.StartsWith(TEXT("dealp_"))) { HoverIdx = FCString::Atoi(*HS.RightChop(6)); }
	}
	const bool bOverTrack = (HoverIdx >= 0 && HoverIdx < N);

	// Slepen: knop ingedrukt + boven de balk -> volg de stop onder de cursor.
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

	// Welke stop is nu geselecteerd (dichtstbij het huidige bod)?
	const int32 SelIdx = FMath::Clamp(FMath::RoundToInt((AskPct - 0.40f) / 0.10f), 0, N - 1);

	DrawRect(FLinearColor(0.18f, 0.18f, 0.20f, 0.95f), TrackX, TrackY, TrackW, TrackH);
	// Kleur de gevulde balk naar de prijs: goedkoop=groen, duur=rood.
	const FLinearColor Fill = (AskPct <= 1.f) ? FLinearColor(0.4f, 0.85f, 0.4f) : FLinearColor(0.9f, 0.55f, 0.3f);
	DrawRect(Fill, TrackX, TrackY, SegW * (SelIdx + 1), TrackH);

	for (int32 i = 0; i < N; ++i)
	{
		const float cx = TrackX + i * SegW;
		const FName Box(*FString::Printf(TEXT("dealp_%d"), i));
		if (HoverIdx == i)
		{
			DrawRect(FLinearColor(1.f, 0.85f, 0.1f, 0.55f), cx, TrackY - 5.f, SegW, TrackH + 10.f);
		}
		AddHitBox(FVector2D(cx, TrackY - 6.f), FVector2D(SegW, TrackH + 26.f), Box, true, 3);
	}
	// Handle op de selectie.
	const float HandleX = TrackX + (SelIdx + 0.5f) * SegW;
	const float HalfW = (bDragging || bOverTrack) ? 8.f : 5.f;
	DrawRect((bDragging || bOverTrack) ? FLinearColor(1.f, 0.85f, 0.1f) : FLinearColor::White,
		HandleX - HalfW, TrackY - 8.f, HalfW * 2.f, TrackH + 16.f);
	// Eindlabels.
	DrawText(TEXT("cheap"), FLinearColor(0.6f, 0.9f, 0.6f), TrackX, TrackY + TrackH + 4.f, Font);
	DrawText(TEXT("greedy"), FLinearColor(0.95f, 0.6f, 0.4f), TrackX + TrackW - 50.f, TrackY + TrackH + 4.f, Font);
	y += 48.f;

	// --- Live acceptatie-% (client kan dit lokaal: stats repliceren) ---
	const float Chance = C->GetAcceptanceChance(Ask);
	const FLinearColor ChanceCol = Chance >= 66.f ? FLinearColor::Green
		: (Chance >= 33.f ? FLinearColor(1.f, 0.8f, 0.2f) : FLinearColor(1.f, 0.4f, 0.4f));
	DrawText(FString::Printf(TEXT("Chance they accept: %.0f%%"), Chance), ChanceCol, InnerX, y, Font);
	y += 22.f;
	DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.9f), InnerX, y, W - 32.f, 12.f);
	DrawRect(ChanceCol, InnerX, y, (W - 32.f) * FMath::Clamp(Chance / 100.f, 0.f, 1.f), 12.f);
	y += 16.f;

	// Klant-binding (respect/loyaliteit/verslaving) als context.
	DrawText(FString::Printf(TEXT("Respect %.0f   Loyalty %.0f   Addiction %.0f"), C->Respect, C->Loyalty, C->Addiction),
		FLinearColor(0.7f, 0.7f, 0.8f), InnerX, y, Font);
	y += 26.f;

	DrawButton(FName(TEXT("dealconfirm")), TEXT("Offer deal"), InnerX, y, 200.f, FLinearColor::White);
	DrawButton(FName(TEXT("dealclose")), TEXT("Cancel"), InnerX + 210.f, y, 160.f, FLinearColor::Yellow);
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
