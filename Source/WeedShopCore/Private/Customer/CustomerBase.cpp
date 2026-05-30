#include "Customer/CustomerBase.h"

#include "WeedShopCore.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeedShopProduct.h"
#include "Economy/EconomyComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Game/WeedShopGameState.h"
#include "Phone/ContactsComponent.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// Kleine namenpool zodat elke klant een herkenbare naam krijgt voor de contacten-app.
	FText MakeContactName(uint32 Seed)
	{
		static const TCHAR* Names[] = {
			TEXT("Tom"), TEXT("Kevin"), TEXT("Sjors"), TEXT("Naomi"), TEXT("Driss"),
			TEXT("Bram"), TEXT("Lisa"), TEXT("Youssef"), TEXT("Mees"), TEXT("Fatima")
		};
		const int32 Count = UE_ARRAY_COUNT(Names);
		return FText::FromString(Names[Seed % Count]);
	}
}

ACustomerBase::ACustomerBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// Zorg dat de interactie-trace (ECC_Visibility) de klant raakt.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Zichtbaar placeholder-lichaam (capsule-vormige blob) tot er een echte mesh is.
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
	Body->SetRelativeScale3D(FVector(0.9f, 0.9f, 1.9f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylFinder.Succeeded())
	{
		Body->SetStaticMesh(CylFinder.Object);
	}
}

void ACustomerBase::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		State = ECustomerState::WantsToOrder;
		BasePatienceSeconds = PatienceSeconds;
	}
}

void ACustomerBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACustomerBase, DesiredProductId);
	DOREPLIFETIME(ACustomerBase, DesiredQuantity);
	DOREPLIFETIME(ACustomerBase, Respect);
	DOREPLIFETIME(ACustomerBase, Loyalty);
	DOREPLIFETIME(ACustomerBase, Addiction);
	DOREPLIFETIME(ACustomerBase, State);
}

void ACustomerBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	// Geduld loopt af zolang hij wacht (wil bestellen of onderhandelt).
	if (State == ECustomerState::WantsToOrder || State == ECustomerState::Negotiating)
	{
		PatienceSeconds -= DeltaSeconds;
		if (PatienceSeconds <= 0.f)
		{
			LeaveAngry();
		}
	}
	// Klaar (geholpen of vertrokken).
	else if (State == ECustomerState::Served || State == ECustomerState::Leaving)
	{
		LeaveTimer += DeltaSeconds;

		if (bDespawnAfterServed)
		{
			// Afspraak-klant: vertrekt na een tijdje.
			if (LeaveTimer >= 12.f)
			{
				Destroy();
			}
		}
		else if (LeaveTimer >= OrderCooldownSeconds)
		{
			// Vaste klant heeft z'n spul opgerookt -> wil weer iets.
			State = ECustomerState::WantsToOrder;
			PatienceSeconds = BasePatienceSeconds;
			LeaveTimer = 0.f;
		}
	}
}

int32 ACustomerBase::GetMarketPriceCents() const
{
	if (!ProductTable || DesiredProductId.IsNone())
	{
		return 0;
	}
	const FWeedShopProductRow* Row =
		ProductTable->FindRow<FWeedShopProductRow>(DesiredProductId, TEXT("ACustomerBase::GetMarketPriceCents"), false);
	return Row ? Row->MarketPriceCents : 0;
}

float ACustomerBase::GetAcceptanceChance(int32 AskPriceCentsPerUnit) const
{
	return UWeedDealLibrary::CalculateAcceptanceChance(
		static_cast<float>(GetMarketPriceCents()), static_cast<float>(AskPriceCentsPerUnit),
		Respect, Loyalty, Addiction);
}

EDealResult ACustomerBase::SubmitOffer(int32 AskPriceCentsPerUnit, UEconomyComponent* PayTo, UInventoryComponent* StockFrom)
{
	if (!HasAuthority())
	{
		return EDealResult::Refused;
	}
	if (State != ECustomerState::WantsToOrder && State != ECustomerState::Negotiating)
	{
		return EDealResult::Refused;
	}

	const int32 Market = GetMarketPriceCents();
	if (Market <= 0)
	{
		return EDealResult::Refused;
	}

	// Voorraad-check.
	if (!StockFrom || !StockFrom->HasItem(DesiredProductId, DesiredQuantity))
	{
		UE_LOG(LogWeedShop, Log, TEXT("Klant: geen voorraad van %s (x%d)."), *DesiredProductId.ToString(), DesiredQuantity);
		return EDealResult::NoStock;
	}

	// Boven budget -> dingt af.
	if (AskPriceCentsPerUnit > BudgetCentsPerUnit)
	{
		State = ECustomerState::Negotiating;
		return EDealResult::Haggle;
	}

	const float Chance = GetAcceptanceChance(AskPriceCentsPerUnit);
	const bool bAccepts = FMath::FRandRange(0.f, 100.f) <= Chance;

	if (!bAccepts)
	{
		// Te duur -> onderhandelen; anders simpelweg geweigerd (kleine respect-knauw).
		if (AskPriceCentsPerUnit > Market)
		{
			State = ECustomerState::Negotiating;
			return EDealResult::Haggle;
		}
		Respect = ClampAttr(Respect - 4.f);
		return EDealResult::Refused;
	}

	// Deal rond: betalen, voorraad af, attributen bijwerken.
	const int32 Total = AskPriceCentsPerUnit * DesiredQuantity;
	StockFrom->RemoveItem(DesiredProductId, DesiredQuantity);
	if (PayTo)
	{
		PayTo->AddMoney(Total);
	}

	if (AskPriceCentsPerUnit <= Market)
	{
		Respect = ClampAttr(Respect + 5.f);
		Loyalty = ClampAttr(Loyalty + 8.f);
	}
	else
	{
		// Boven markt maar tóch geaccepteerd: cash nu, maar uitknijpen voelt.
		Respect = ClampAttr(Respect - 3.f);
		Loyalty = ClampAttr(Loyalty - 2.f);
	}
	Addiction = ClampAttr(Addiction + 6.f);

	State = ECustomerState::Served;
	UE_LOG(LogWeedShop, Log, TEXT("Deal: %dx %s voor %d cents (resp %.0f loy %.0f ver %.0f)."),
		DesiredQuantity, *DesiredProductId.ToString(), Total, Respect, Loyalty, Addiction);
	return EDealResult::Accepted;
}

void ACustomerBase::Interact_Implementation(APawn* InstigatorPawn)
{
	// Server-authoritative (via de interactie-component). Snel-test: verkoop tegen marktprijs uit
	// de voorraad van de speler naar de gedeelde kas op de GameState.
	if (!HasAuthority())
	{
		return;
	}

	AWeedShopGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWeedShopGameState>() : nullptr;
	UEconomyComponent* Econ = GS ? GS->GetEconomy() : nullptr;
	UInventoryComponent* Stock = InstigatorPawn ? InstigatorPawn->FindComponentByClass<UInventoryComponent>() : nullptr;

	// Eerste contact -> in de telefoon-contacten.
	if (GS && GS->GetContacts())
	{
		const float Rel = (Respect + Loyalty + Addiction) / 3.f;
		GS->GetContacts()->RegisterContact(GetFName(), MakeContactName(GetUniqueID()), Rel);
	}

	const EDealResult Result = SubmitOffer(GetMarketPriceCents(), Econ, Stock);
	UE_LOG(LogWeedShop, Log, TEXT("Klant-interactie resultaat: %d"), static_cast<int32>(Result));

	if (GEngine)
	{
		FColor C = FColor::White;
		FString Msg;
		switch (Result)
		{
		case EDealResult::Accepted: C = FColor::Green;  Msg = TEXT("Verkocht!"); break;
		case EDealResult::NoStock:  C = FColor::Orange; Msg = FString::Printf(TEXT("Geen voorraad: %s"), *DesiredProductId.ToString()); break;
		case EDealResult::Haggle:   C = FColor::Yellow; Msg = TEXT("Klant vindt het te duur"); break;
		default:                    C = FColor::Red;    Msg = TEXT("Klant weigert"); break;
		}
		GEngine->AddOnScreenDebugMessage(-1, 3.f, C, Msg);
	}
}

FText ACustomerBase::GetInteractionPrompt_Implementation() const
{
	switch (State)
	{
	case ECustomerState::WantsToOrder:
	case ECustomerState::Negotiating:
		return FText::FromString(FString::Printf(TEXT("Verkoop %dx %s  (~EUR %.2f)"),
			DesiredQuantity, *DesiredProductId.ToString(), (GetMarketPriceCents() * DesiredQuantity) / 100.f));
	case ECustomerState::Served:
		return FText::FromString(TEXT("Tevreden klant"));
	default:
		return FText::GetEmpty();
	}
}

void ACustomerBase::OnRep_Order()
{
	// Hook voor UI (bv. order-bubbel boven de klant updaten).
}

void ACustomerBase::LeaveAngry()
{
	Respect = ClampAttr(Respect - 10.f);
	State = ECustomerState::Leaving;
	UE_LOG(LogWeedShop, Log, TEXT("Klant vertrekt boos (geduld op). Respect nu %.0f."), Respect);
}
