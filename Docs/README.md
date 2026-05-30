# Weedshop Game (Unreal Engine)

First-person wiet-sim: begin als straatdealer in je appartement, word een legale winkel, groei naar een franchise.

## Belangrijkste documenten
- **[docs/CODEX_BRIEF.md](docs/CODEX_BRIEF.md)** — de complete visie, architectuur en het A–Z bouwplan. **Lees dit eerst.** Dit is de bron van waarheid voor de game.
- **[docs/DECISIONS.md](docs/DECISIONS.md)** — log van alle beslissingen die we gaandeweg maken (geheugen voor de AI-agent).

## Voor de AI-agent (Codex)
Volg altijd de architectuur, naamconventies en werkafspraken uit `docs/CODEX_BRIEF.md`. Werk in kleine commits (één systeem per commit). Schrijf elke nieuwe beslissing bij in `docs/DECISIONS.md`.

## Tech
- Unreal Engine (nieuwste stabiele 5.x — versie vastgepind, zie DECISIONS.md)
- C++ voor logica, Blueprint voor koppeling en UI
- Git + Git LFS voor binaire assets

## Status
In opbouw. Huidige focus: **MVP — fase 1 (appartement), kern-loop met grijze blokjes.**
