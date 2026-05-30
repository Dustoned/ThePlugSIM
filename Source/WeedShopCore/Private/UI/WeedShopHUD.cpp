#include "UI/WeedShopHUD.h"

#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "Game/WeedShopGameState.h"
#include "Economy/EconomyComponent.h"
#include "World/DayCycleComponent.h"
#include "Progression/MilestoneComponent.h"
#include "Progression/UpgradeComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Interaction/InteractionComponent.h"
#include "Interaction/Interactable.h"

void AWeedShopHUD::DrawHUD()
{
	Super::DrawHUD();

	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	APawn* P = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;

	float Y = 40.f;
	const float X = 40.f;

	// Kas.
	if (GS && GS->GetEconomy())
	{
		DrawText(FString::Printf(TEXT("Kas: EUR %.2f"), GS->GetEconomy()->GetBalanceEuros()),
			FLinearColor(0.4f, 1.f, 0.4f), X, Y, Font);
		Y += 26.f;
	}

	// Fase + totaal verdiend.
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

	// Dag/nacht.
	if (GS && GS->GetDayCycle())
	{
		const UDayCycleComponent* Day = GS->GetDayCycle();
		DrawText(FString::Printf(TEXT("%s  (%.0f%%)"), Day->IsNight() ? TEXT("Nacht") : TEXT("Dag"),
			Day->GetCycleFraction() * 100.f), FLinearColor::White, X, Y, Font);
		Y += 26.f;
	}

	// Voorraad.
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

	// Telefoon-overlay (Tab).
	if (bPhoneOpen)
	{
		DrawPhone();
	}

	// Interactie-prompt (gecentreerd, iets onder het midden).
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
}

void AWeedShopHUD::DrawPhone()
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UUpgradeComponent* Upg = GS ? GS->GetUpgrades() : nullptr;

	const float W = 380.f;
	const float H = 320.f;
	const float PX = (Canvas ? Canvas->ClipX : 1280.f) - W - 30.f;
	const float PY = 80.f;

	DrawRect(FLinearColor(0.05f, 0.05f, 0.08f, 0.92f), PX, PY, W, H);

	float y = PY + 12.f;
	DrawText(TEXT("== TELEFOON =="), FLinearColor(0.5f, 1.f, 0.5f), PX + 14.f, y, Font); y += 28.f;

	if (GS && GS->GetEconomy())
	{
		DrawText(FString::Printf(TEXT("Kas: EUR %.2f"), GS->GetEconomy()->GetBalanceEuros()),
			FLinearColor::White, PX + 14.f, y, Font);
		y += 26.f;
	}
	DrawText(TEXT("Kopen (toets 1-6):"), FLinearColor(0.8f, 0.8f, 1.f), PX + 14.f, y, Font); y += 26.f;

	if (Upg)
	{
		int32 n = 1;
		for (const FName& Id : Upg->GetAllUpgradeIds())
		{
			FText Name; int32 Cost = 0; bool bPurchased = false; bool bAvailable = false;
			if (Upg->GetUpgradeDisplay(Id, Name, Cost, bPurchased, bAvailable))
			{
				const TCHAR* Suffix = bPurchased ? TEXT("  [gekocht]") : (bAvailable ? TEXT("") : TEXT("  [vergrendeld]"));
				const FLinearColor Col = bPurchased ? FLinearColor::Gray
					: (bAvailable ? FLinearColor::White : FLinearColor(0.7f, 0.45f, 0.45f));
				DrawText(FString::Printf(TEXT("%d) %s - EUR %.2f%s"), n, *Name.ToString(), Cost / 100.f, Suffix),
					Col, PX + 14.f, y, Font);
				y += 23.f;
			}
			if (++n > 6) break;
		}
	}

	DrawText(TEXT("[Tab] sluiten"), FLinearColor::Yellow, PX + 14.f, PY + H - 28.f, Font);
}
