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

FKey UControlSettings::DefaultKey(FName Action)
{
	for (const FActionDef& A : Defs()) { if (A.Id == Action) { return A.Default; } }
	return EKeys::Invalid;
}

FKey UControlSettings::GetKey(FName Action) const
{
	if (const FString* S = Bindings.Find(Action))
	{
		const FKey K = FKey(FName(**S));
		if (K.IsValid()) { return K; }
	}
	return DefaultKey(Action);
}

bool UControlSettings::SetKey(FName Action, FKey NewKey, FName& OutConflict)
{
	OutConflict = NAME_None;
	if (!NewKey.IsValid()) { return false; }

	// Conflict: gebruikt een andere actie deze toets al?
	for (const FName& Other : AllActions())
	{
		if (Other == Action) { continue; }
		if (GetKey(Other) == NewKey) { OutConflict = Other; return false; }
	}

	Bindings.Add(Action, NewKey.GetFName().ToString());
	SaveConfig();
	OnBindingsChanged.Broadcast();
	return true;
}

void UControlSettings::ResetToDefaults()
{
	Bindings.Reset();
	SaveConfig();
	OnBindingsChanged.Broadcast();
}
