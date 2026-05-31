#include "Input/ControlSettings.h"

namespace
{
	struct FActionDef { FName Id; const TCHAR* Name; FKey Default; };

	static const TArray<FActionDef>& Defs()
	{
		static const TArray<FActionDef> D = {
			{ FName(TEXT("Phone")),       TEXT("Open / close phone"),     EKeys::Tab },
			{ FName(TEXT("PhoneTab")),    TEXT("Switch app / home"),      EKeys::Q   },
			{ FName(TEXT("Inventory")),   TEXT("Open / close inventory"), EKeys::I   },
			{ FName(TEXT("GiveSample")),  TEXT("Give sample to NPC"),     EKeys::F   },
			{ FName(TEXT("Rotate")),      TEXT("Rotate while placing"),   EKeys::R   },
			{ FName(TEXT("PotUpgrade")),  TEXT("Pot upgrade panel"),      EKeys::U   },
		};
		return D;
	}
}

UControlSettings* UControlSettings::Get()
{
	return GetMutableDefault<UControlSettings>();
}

const TArray<FName>& UControlSettings::AllActions()
{
	static TArray<FName> Ids;
	if (Ids.Num() == 0) { for (const FActionDef& A : Defs()) { Ids.Add(A.Id); } }
	return Ids;
}

FText UControlSettings::DisplayName(FName Action)
{
	for (const FActionDef& A : Defs()) { if (A.Id == Action) { return FText::FromString(A.Name); } }
	return FText::FromName(Action);
}

FKey UControlSettings::DefaultKey(FName Action, bool bAlt)
{
	if (bAlt) { return EKeys::Invalid; } // alternatief is standaard leeg
	for (const FActionDef& A : Defs()) { if (A.Id == Action) { return A.Default; } }
	return EKeys::Invalid;
}

FKey UControlSettings::GetKey(FName Action, bool bAlt) const
{
	const TMap<FName, FString>& Map = bAlt ? KeysAlt : KeysMain;
	if (const FString* S = Map.Find(Action))
	{
		if (S->IsEmpty()) { return EKeys::Invalid; } // bewust leeggemaakt
		const FKey K = FKey(FName(**S));
		return K.IsValid() ? K : EKeys::Invalid;
	}
	return DefaultKey(Action, bAlt);
}

bool UControlSettings::SetKey(FName Action, bool bAlt, FKey NewKey, FName& OutConflict)
{
	OutConflict = NAME_None;
	if (!NewKey.IsValid()) { return false; }

	// Conflict: gebruikt een ander slot (main of alt) van een actie deze toets al?
	for (const FName& Other : AllActions())
	{
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const bool bOtherAlt = (Slot == 1);
			if (Other == Action && bOtherAlt == bAlt) { continue; }
			if (GetKey(Other, bOtherAlt) == NewKey) { OutConflict = Other; return false; }
		}
	}

	(bAlt ? KeysAlt : KeysMain).Add(Action, NewKey.GetFName().ToString());
	SaveConfig();
	OnBindingsChanged.Broadcast();
	return true;
}

void UControlSettings::ClearKey(FName Action, bool bAlt)
{
	// Lege string = expliciet leeg (anders zou de default terugkomen).
	(bAlt ? KeysAlt : KeysMain).Add(Action, FString());
	SaveConfig();
	OnBindingsChanged.Broadcast();
}

void UControlSettings::ResetToDefaults()
{
	KeysMain.Reset();
	KeysAlt.Reset();
	SaveConfig();
	OnBindingsChanged.Broadcast();
}
