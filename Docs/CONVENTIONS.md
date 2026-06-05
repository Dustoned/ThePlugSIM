# Conventies — ThePlugSIM

Korte, levende referentie. Codex/Claude houdt zich hieraan; afwijkingen leg je vast in `DECISIONS.md`.

## Modules
- `ThePlugSIM` — primaire module (First-Person template-boilerplate). Niet uitbreiden tenzij nodig.
- `WeedShopCore` — alle eigen gameplay (C++). Hier komt alles bij.

## Mapstructuur (Source)
```
Source/WeedShopCore/
  Public/  Private/
    Interaction/    IInteractable, UInteractionComponent
    Economy/        UEconomyComponent
    Inventory/      UInventoryComponent
    Cultivation/    AGrowPlant
    Data/           DataTable row-structs (FWeedShopProductRow, FWeedStrainRow, ...)
    Game/           AWeedShopGameMode, AWeedShopGameState
    Test/           wegwerp test-actors (MoneyTestPickup)
```
Per submap een Public/ (headers) en Private/ (cpp).

## Content (editor, jij beheert)
```
Content/_Project/   eigen werk: Maps, Data (DataTables), Blueprints, UI
```
Vendor/template-content blijft buiten `_Project/`.

## Naamprefixes (Unreal-standaard)
`A` Actor · `U` UObject/Component · `F` struct · `E` enum · `I` interface ·
`BP_` Blueprint · `WBP_` Widget · `DT_` DataTable · CSV-bronnen in `Data/`.

## Data-driven
Balans/content uit CSV → DataTable. Per CSV een `USTRUCT : public FTableRowBase` in `Data/`.
CSV-bronbestanden staan in de repo-map `Data/`; importeren naar `Content/_Project/Data`.

## Geld
Altijd in **eurocents** (`int32`/`int64`), nooit float. UI deelt door 100 voor weergave.

## Co-op / replicatie (geldt voor ELK systeem) — zie ook DECISIONS.md
- Server is authority over gedeelde state; clients vragen aan.
- Gedeelde data: `UPROPERTY(Replicated)` of `ReplicatedUsing=OnRep_X`; UI luistert op delegates.
- State-mutaties via Server-RPC of server-side functies met validatie — nooit de client vertrouwen.
- Lokaal (trace, prompt, input-feedback) met `IsLocallyControlled`; gevolgen server-authoritative.
- Referentie-sjabloon: `UInteractionComponent` (lokale focus → `TryInteract` → `ServerInteract`).

## Blueprint-exposure
Wat de editor moet kunnen instellen/aanroepen:
- Tweakbare data: `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WeedShop|...")`
- Aanroepbare functies: `UFUNCTION(BlueprintCallable, ...)`
- UI-events: `DECLARE_DYNAMIC_MULTICAST_DELEGATE...` + `UPROPERTY(BlueprintAssignable)`

## Werkwijze
Eén systeem per commit. Elke beslissing in `DECISIONS.md`. Code wordt lokaal gecompileerd
(`E:\UE\UE_5.7` Build.bat) vóór commit, zodat alles groen is.
 
### Unreal test-loop
- Laat Unreal Editor open terwijl Codex code maakt, zodat jij de huidige live-versie kunt blijven testen.
- Als de code klaar is: sluit Unreal Editor eerst, draai daarna de Unreal build.
- Commit pas nadat de build groen is.
- Start Unreal daarna opnieuw met `Start-Process -FilePath "E:\UE\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"<absolute project>.uproject" -Game'`: het gequote `.uproject`-pad moet de eerste parameter zijn, zodat jij de nieuwe update direct in game-mode kunt testen.
