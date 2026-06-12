# Weedshop Game (Unreal Engine)

First-person wiet-sim: begin als straatdealer in je appartement, word een legale winkel, groei naar een franchise.

## Belangrijkste documenten
- **[ROADMAP.md](ROADMAP.md)** — de levende roadmap: wat er gebouwd wordt en in welke volgorde. **Plan vanaf hier.**
- **[Claude-brief-weedshop.md](Claude-brief-weedshop.md)** — de complete visie en architectuur. Bron van waarheid voor wát de game is.
- **[DECISIONS.md](DECISIONS.md)** — log van alle beslissingen die we gaandeweg maken (geheugen voor de AI-agent).

## Voor de AI-agent (Codex)
Volg altijd de architectuur, naamconventies en werkafspraken uit `docs/CODEX_BRIEF.md`. Werk in kleine commits (één systeem per commit). Schrijf elke nieuwe beslissing bij in `docs/DECISIONS.md`.

## Tech
- Unreal Engine (nieuwste stabiele 5.x — versie vastgepind, zie DECISIONS.md)
- C++ voor logica, Blueprint voor koppeling en UI
- Git + Git LFS voor binaire assets

## Status
Speelbaar t/m level 50 (eerste helft). Huidige focus: **beach-map (CityBeachStrip) als echte spelwereld + levels 1-50 écht goed** — zie ROADMAP.md.
