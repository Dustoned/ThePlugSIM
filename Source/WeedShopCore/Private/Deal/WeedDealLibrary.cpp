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

	// Kwaliteit-term: rond 45% is neutraal (lage-% wiet wordt vroeg niet zwaar gestraft). Boven = bonus,
	// eronder = straf. Het effect is groot bij lage verslaving (kieskeurig) en NUL bij hoge verslaving:
	// stevig verslaafde klanten (>=80) nemen elke kwaliteit, ook lage-% wiet (ze willen gewoon hun fix).
	if (Quality01 >= 0.f)
	{
		const float Q = FMath::Clamp(Quality01, 0.f, 1.f);
		const float Pickiness = FMath::Max(0.f, 1.f - Addiction / 80.f); // 1 = niet verslaafd, 0 vanaf ~80
		End += (Q - 0.45f) * 55.f * Pickiness;
	}

	return FMath::Clamp(End, 0.f, 100.f);
}
