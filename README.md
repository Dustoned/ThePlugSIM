# ThePlugSIM

Unreal Engine 5.7 co-op simulatiegame (C++). Kweek, verwerk en verkoop in een Nederlands-getinte coffeeshop/stad-setting.

## Modules
- **ThePlugSIM** — character, player-controller, gameplay-core
- **WeedShopCore** — economie, klanten/NPC's, telefoon, save-systeem, UI, dag/nacht, packing

## Laatste versie: 1.6.0
- Character-keuze man (Tony) / vrouw bij new game + in de telefoon-settings
- Grotere wardrobe met auto-detectie van kleding/haar/accessoires
- Strand-stad (`CityBeachStrip`) als standaard new-game map
- VIP dag-orders, bewoners die hun appartement uit lopen
- Herziene dag/nacht: geen nacht-zonreflecties, zon/maan-boog, getemperde zon
- Binnenverlichting (lampkegel + room-glow), NPC-optimalisatie (anim-budget)
- Nieuwe packing-bench (meerdere bags, iconen)

Volledige patch-historie: zie [Docs/PATCHNOTES.md](Docs/PATCHNOTES.md).

## Belangrijk voor builden / spelen
Een paar grote asset-packs staan **niet** in deze repo (te groot voor GitHub LFS):
de speelmap `CityBeachStrip`, `Casual_Wear_Pack1`, `Citizens_Pack` en enkele andere.
Deze repo bevat de **broncode + project + kleinere content**. Om de game volledig te
kunnen builden/spelen heb je die packs los nodig (vraag de eigenaar), of gebruik een
los gedeelde packaged build.

## Bouwen
Open `ThePlugSIM.uproject` in Unreal Engine 5.7 en laat de modules compileren, of:

```
"<UE>/Engine/Build/BatchFiles/Build.bat" ThePlugSIMEditor Win64 Development -project="<pad>/ThePlugSIM.uproject" -waitmutex
```
