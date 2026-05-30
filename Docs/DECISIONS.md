# Decisions Log

Elke architectuur-, ontwerp- of balans-beslissing hier in 1-2 regels bijschrijven, met datum. Dit is het geheugen van het project — de AI-agent leest dit om consistent te blijven over sessies heen.

> Formaat: `- [JJJJ-MM-DD] Beslissing — korte reden.`

## Vastgelegd (uit de brief, v2)

- Perspectief: first-person, PC (muis/toetsenbord) — speler staat zelf achter de toonbank/deur.
- Structuur: endless met milestones; milestones unlocken producten/gear en sturen fase-overgangen.
- 3 fases: straatdealer (appartement, = MVP) → legale winkel → franchise.
- Voorraad: zelf kweken (geen inkoop); planten groeien real-time, oogst = voorraad.
- Werving: gratis joint-samples op straat verhogen verslaving/respect/loyaliteit → prospect komt naar appartement.
- Deal-mechaniek: prijs-slider t.o.v. markt; live acceptatie-% uit prijs + respect/loyaliteit/verslaving; afdingen → boos weglopen.
- Klanten: een paar archetypes; wachtrij + geduld-timer.
- Dag/nacht: real-time 20 min licht / 10 min donker, doorlopend (geen "volgende dag"). Nacht = meer/schichtigere klanten, hoger politierisico (heat → lichte bust), overval-risico.
- Upgrades: kweek-gear, opslag/voorraad, beveiliging, pand/personeel.
- Fase 1 → 2: geld-drempel + vergunning kopen.
- Verhaal: licht (intro + milestone-beats). Onboarding: leren door te doen, geen tutorial.
- Art: stylized/cartoon, Amsterdamse coffeeshop-vibe. Audio: chill/lo-fi/reggae.
- Fail-state: busted-risico licht, failliet mogelijk, geen harde game-over.
- Scope: kleinst mogelijke eerste versie (alleen kern-loop), daarna uitbouwen.
- Tech: C++ voor logica + Blueprint voor koppeling; C++ state machine voor klant-AI (geen Behaviour Tree).

## Bouw-log (gaandeweg)

- [2026-05-30] Engine-versie vastgepind op **Unreal Engine 5.7** — project aangemaakt vanuit de **First-Person C++**-template (heet `ThePlugSIM`). Stap B + C uit het A–Z-plan klaar.
- [2026-05-30] **Co-op vanaf dag 1, solo speelbaar.** De game moet in je eentje volledig speelbaar zijn, maar **online co-op (2-3 spelers)** moet erin komen. Daarom is élke feature **replication-aware**: server-authoritative state, mutaties via Server-RPC of op de server, visuals/UI lokaal. Vuistregel per nieuw systeem: bedenk vooraf wie de authority is, wat repliceert (`Replicated`/`RepNotify` + `OnRep_`-delegates), en welke acties een Server-RPC nodig hebben. Subsystems voor gedeelde staat (geld, voorraad) draaien server-authoritative; per-speler-data hangt aan de (replicerende) pawn/PlayerState. (Vervangt de eerdere single-player-only aanname.)
- [2026-05-30] Tweede C++-module **`WeedShopCore`** aangemaakt (Source/WeedShopCore, leeg + compileert). Alle eigen gameplay komt hierin; de template-module `ThePlugSIM` blijft boilerplate. Stap E uit het A–Z-plan. Module-naam volgt de brief (`WeedShopCore`), niet de projectnaam.
- [2026-05-30] **Git + Git LFS** geïnitialiseerd. `.gitignore` (Binaries/Intermediate/Saved/DDC etc.) + `.gitattributes` (LFS voor *.uasset/*.umap/*.fbx/*.png/*.wav etc.). Stap D. Remote + push moet de gebruiker nog doen.
- [2026-05-30] **Interaction-systeem** (stap I): `IInteractable` (interface, methodes `Interact` + `GetInteractionPrompt` als BlueprintNativeEvent) + `UInteractionComponent` (camera line-trace, focus-tracking, `OnFocusedInteractableChanged`-delegate voor prompt-UI, `TryInteract()` voor de input). Let op: in UE 5.7 genereert UHT zelf de default-body van interface-BlueprintNativeEvents — dus géén `_Implementation` in de .cpp zetten. `CanInteract` bewust weggelaten (auto-default zou `false` zijn); gating gebeurt binnen `Interact`.
- [2026-05-30] **Interaction is co-op-correct** (eerste toepassing van de replication-regel): component repliceert; focus/prompt-trace draait alleen lokaal (`IsLocallyControlled`); `TryInteract` voert direct uit bij authority (host/SP) of stuurt anders een **Server-RPC** `ServerInteract(Target)` die op de server valideert (geldig, IInteractable, binnen bereik) en dan `Interact` uitvoert. Dit is het sjabloon voor alle latere interacties.

## Nog te beslissen

- Specifieke klant-archetypes.
- Of edibles van eigen oogst gemaakt worden (crafting) + accessoires als merch.
- Alle balans-getallen (tunen tijdens spelen).
