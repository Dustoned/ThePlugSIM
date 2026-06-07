#include "Deal/WeedDealLibrary.h"

float UWeedDealLibrary::CalculateAcceptanceChance(float MarketPriceCents, float AskPriceCents,
	float Respect, float Loyalty, float Addiction, float Quality01)
{
	if (MarketPriceCents <= 0.f)
	{
		return 0.f;
	}

	const float PriceRatio = AskPriceCents / MarketPriceCents;
	const float Base = 70.f - (PriceRatio - 1.f) * 100.f;
	float End = Base + Respect * 0.15f + Loyalty * 0.15f + Addiction * 0.25f;

	// Kwaliteit-term: rond 60% is neutraal. Boven 60% = bonus, eronder = straf. Het effect is groot
	// bij lage verslaving (kieskeurig) en bijna nul bij hoge verslaving (ze willen toch wel). Kwaliteit
	// blijft dus belangrijk; vroeg-spel los je op door dat klanten LAGE-level strains vragen (zie CustomerBase).
	if (Quality01 >= 0.f)
	{
		const float Q = FMath::Clamp(Quality01, 0.f, 1.f);
		const float Pickiness = 1.f - Addiction / 100.f; // 1 = totaal niet verslaafd, 0 = volledig
		End += (Q - 0.60f) * 55.f * Pickiness;
	}

	return FMath::Clamp(End, 0.f, 100.f);
}
