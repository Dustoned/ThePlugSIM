#include "Deal/WeedDealLibrary.h"

float UWeedDealLibrary::CalculateAcceptanceChance(float MarketPriceCents, float AskPriceCents,
	float Respect, float Loyalty, float Addiction)
{
	if (MarketPriceCents <= 0.f)
	{
		return 0.f;
	}

	const float PriceRatio = AskPriceCents / MarketPriceCents;
	const float Base = 70.f - (PriceRatio - 1.f) * 100.f;
	const float End = Base + Respect * 0.15f + Loyalty * 0.15f + Addiction * 0.25f;
	return FMath::Clamp(End, 0.f, 100.f);
}
