#include "UI/WeedShopHUD.h"

#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
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

namespace
{
	constexpr float PhoneW = 420.f;
	constexpr float PhoneH = 440.f;
	constexpr float RowH = 26.f;
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
		DrawText(FString::Printf(TEXT("Kas: EUR %.2f"), GS->GetEconomy()->GetBalanceEuros()),
			FLinearColor(0.4f, 1.f, 0.4f), X, Y, Font);
		Y += 26.f;
	}

	if (GS && GS->GetMilestones())
	{
		const UMilestoneComponent* M = GS->GetMilestones();
		const TCHAR* PhaseName = TEXT("Straatdealer");
		switch (M->GetCurrentPhase())
		{
		case EShopPhase::Shop:      PhaseName = TEXT("Winkel"); break;
		case EShopPhase::Franchise: PhaseName = TEXT("Franchise"); break;
		default: break;
		}
		DrawText(FString::Printf(TEXT("Fase: %s   (totaal verdiend EUR %.2f)"),
			PhaseName, M->GetTotalEarnedCents() / 100.f), FLinearColor(1.f, 0.85f, 0.3f), X, Y, Font);
		Y += 26.f;
	}

	if (GS && GS->GetDayCycle())
	{
		const UDayCycleComponent* Day = GS->GetDayCycle();
		const int32 TotalMin = FMath::RoundToInt(Day->GetCycleFraction() * 24.f * 60.f);
		const int32 HH = (TotalMin / 60) % 24;
		const int32 MM = TotalMin % 60;
		DrawText(FString::Printf(TEXT("Tijd: %02d:%02d   (%s)"), HH, MM,
			Day->IsNight() ? TEXT("Nacht") : TEXT("Dag")), FLinearColor::White, X, Y, Font);
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
			DrawText(TEXT("Voorraad:"), FLinearColor(0.8f, 0.8f, 1.f), X, Y, Font);
			Y += 22.f;
			if (Inv->GetStacks().Num() == 0)
			{
				DrawText(TEXT("  (leeg)"), FLinearColor::Gray, X, Y, Font);
				Y += 20.f;
			}
			for (const FInventoryStack& Stack : Inv->GetStacks())
			{
				DrawText(FString::Printf(TEXT("  %s  x%d"), *Stack.ItemId.ToString(), Stack.Quantity),
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
	}

	// Interactie-prompt.
	if (P)
	{
		if (const UInteractionComponent* IC = P->FindComponentByClass<UInteractionComponent>())
		{
			if (AActor* Focus = IC->GetFocusedActor())
			{
				const FText Prompt = IInteractable::Execute_GetInteractionPrompt(Focus);
				if (!Prompt.IsEmpty())
				{
					const float CX = Canvas ? Canvas->ClipX * 0.5f : 640.f;
					const float CY = Canvas ? Canvas->ClipY * 0.5f : 360.f;
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
					DrawText(S.ItemId.ToString(), FLinearColor(0.85f, 0.9f, 1.f), SX + 5.f, SY + 4.f, Font);
					DrawText(FString::Printf(TEXT("x%d"), S.Quantity), FLinearColor::White, SX + 5.f, SY + 24.f, Font);
				}
				SX += SlotW + 6.f;
			}

			DrawText(TEXT("R = joint draaien   |   F = sample geven (kijk NPC aan)"),
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
	DrawText(TEXT("TELEFOON"), FLinearColor(0.5f, 1.f, 0.5f), InnerX, y, Font); y += 26.f;

	if (GS && GS->GetEconomy())
	{
		DrawText(FString::Printf(TEXT("Kas: EUR %.2f"), GS->GetEconomy()->GetBalanceEuros()),
			FLinearColor::White, InnerX, y, Font);
		y += 26.f;
	}

	// Tab-knoppen (klikbaar).
	static const TCHAR* TabNames[4] = { TEXT("Upgrades"), TEXT("Suppliers"), TEXT("Contacten"), TEXT("Berichten") };
	const float TabW = InnerW / 4.f;
	for (int32 i = 0; i < 4; ++i)
	{
		const FName Box(*FString::Printf(TEXT("tab_%d"), i));
		const FLinearColor Col = (i == Tab) ? FLinearColor(0.6f, 1.f, 0.6f) : FLinearColor(0.7f, 0.7f, 0.8f);
		if (DrawButton(Box, TabNames[i], InnerX + i * TabW, y, TabW - 4.f, Col))
		{
			HoverTooltip = FString::Printf(TEXT("Open de %s-tab"), TabNames[i]);
		}
	}
	y += RowH + 6.f;

	// Inhoud per tab.
	if (Tab == 1) // Suppliers
	{
		if (UStoreComponent* Store = GS ? GS->GetStore() : nullptr)
		{
			int32 idx = 0;
			for (const FName& Id : Store->GetSeedCatalog())
			{
				FText Name; int32 Price = 0;
				if (Store->GetSeedDisplay(Id, Name, Price))
				{
					const FName Box(*FString::Printf(TEXT("buy_%d"), idx));
					if (DrawButton(Box, FString::Printf(TEXT("Zaad: %s  -  EUR %.2f"), *Name.ToString(), Price / 100.f),
						InnerX, y, InnerW, FLinearColor::White))
					{
						HoverTooltip = FString::Printf(TEXT("Koop een %s-zaadje voor EUR %.2f"), *Name.ToString(), Price / 100.f);
					}
					y += RowH;
				}
				if (++idx >= 6) break;
			}
		}
	}
	else if (Tab == 2) // Contacten (alleen weergave)
	{
		if (UContactsComponent* Con = GS ? GS->GetContacts() : nullptr)
		{
			if (Con->GetContacts().Num() == 0)
			{
				DrawText(TEXT("(nog geen contacten - deal met klanten)"), FLinearColor::Gray, InnerX, y, Font);
				y += 22.f;
			}
			for (const FPhoneContact& C : Con->GetContacts())
			{
				DrawText(FString::Printf(TEXT("%s   (relatie %.0f%%)"), *C.DisplayName.ToString(), C.Relationship),
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
			if (DrawButton(FName(TEXT("buy_0")), TEXT("Accepteren (eerste open bericht)"), InnerX, y, InnerW, FLinearColor(0.7f, 1.f, 0.7f)))
			{
				HoverTooltip = TEXT("Spreek af -> loyaliteit omhoog; klant komt op tijd langs");
			}
			y += RowH;
			if (DrawButton(FName(TEXT("buy_1")), TEXT("Weigeren (eerste open bericht)"), InnerX, y, InnerW, FLinearColor(1.f, 0.75f, 0.6f)))
			{
				HoverTooltip = TEXT("Zeg af -> loyaliteit omlaag; klant komt niet");
			}
			y += RowH + 6.f;

			if (Con->GetMessages().Num() == 0)
			{
				DrawText(TEXT("(geen berichten)"), FLinearColor::Gray, InnerX, y, Font);
				y += 22.f;
			}
			for (const FPhoneMessage& M : Con->GetMessages())
			{
				const TCHAR* Tag = (M.Status == 1) ? TEXT("[ja]") : (M.Status == 2 ? TEXT("[nee]") : TEXT("[open]"));
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
					const TCHAR* Suffix = bPurchased ? TEXT("  [gekocht]") : (bAvailable ? TEXT("") : TEXT("  [vergrendeld]"));
					const FLinearColor Col = bPurchased ? FLinearColor::Gray
						: (bAvailable ? FLinearColor::White : FLinearColor(0.8f, 0.55f, 0.55f));
					const FName Box(*FString::Printf(TEXT("buy_%d"), idx));
					if (DrawButton(Box, FString::Printf(TEXT("%s  -  EUR %.2f%s"), *Name.ToString(), Cost / 100.f, Suffix),
						InnerX, y, InnerW, Col))
					{
						HoverTooltip = bPurchased ? TEXT("Al gekocht")
							: (bAvailable ? FString::Printf(TEXT("Koop voor EUR %.2f"), Cost / 100.f)
								: TEXT("Vergrendeld - bereik eerst de juiste fase"));
					}
					y += RowH;
				}
				if (++idx >= 6) break;
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
	if (DrawButton(FName(TEXT("close")), TEXT("Sluiten (Tab)"), InnerX, PY + PhoneH - 30.f, 130.f, FLinearColor::Yellow))
	{
		HoverTooltip = TEXT("Sluit de telefoon");
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
