# Small Tone — Studio Wiga

Chorus analogique **VST3** (Windows 64 bits + macOS universel Apple Silicon/Intel) au format pédale,
modélisé composant par composant sur l'Electro-Harmonix **Small Clone** (EH-4600) : LFO LM358, horloge CD4047,
ligne à retard BBD MN3207 de 1024 étages. Même taille de pédale et de fenêtre que **Holyverb**.

## Compiler avec GitHub
1. Mets **tous les fichiers à la racine du dépôt** (avec le dossier `.github/workflows`).
2. Un push lance l'onglet **Actions** → workflow *Build Small Tone VST3 (Windows + macOS)*.
3. À la fin, télécharge les artefacts :
   - `SmallTone-VST3-Windows` → copie `Small Tone.vst3` dans `C:\Program Files\Common Files\VST3\`
   - `SmallTone-VST3-macOS` → copie `Small Tone.vst3` dans `~/Library/Audio/Plug-Ins/VST3/` puis lance une fois
     `xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Small Tone.vst3"`
4. Dans Reaper : re-scanne les plug-ins. Le plugin apparaît comme **Small Tone (Studio Wiga)**.

Le job *Test du moteur chorus* compile `dsp_test.cpp` (plage du RATE, balayage du BBD, niveaux, stabilité
de 44,1 à 192 kHz). S'il échoue, le build est signalé en rouge.

## Commandes
| Commande | Rôle |
|---|---|
| **RATE** | vitesse du LFO, ≈ 0,42 Hz → 9,4 Hz, courbe du potard 1M « ccw log » d'origine (beaucoup de résolution dans le lent). Tirer haut/bas ou gauche/droite (course complète ≈ 420 px de souris, quelle que soit la taille), molette, **Maj** = 4× plus fin, double-clic = milieu (≈ 2 Hz) |
| Switch **DIN. / DEPTH** | haut = **DIN.** (léger, doublage), bas = **DEPTH** (profond, le son « liquide »). Clic sur le levier ou sur les inscriptions |
| Footswitch | effet / bypass (anneau, LED STATUS et anneau du RATE s'allument) |
| LED **STATUS** / mot **SYNC** | clic sur la LED ou sur SYNC = synchro du RATE sur le tempo du DAW (le potard choisit alors la valeur de note) ; la LED clignote en mesure et SYNC s'illumine. Nouveau clic = vitesse libre |
| Ligne d'état (à la place de « USA ») | n'apparaît que pendant un réglage (vitesse, mode, preset, MIDI…) puis s'efface en fondu |

Comme sur l'original, en **DEPTH** le condensateur de 10 µF arrondit le balayage : plus le RATE est rapide,
plus la modulation devient douce. Basculer le switch fait glisser le délai pendant ~½ s (charge du 10 µF).

## Presets
Menu des presets de la fenêtre du DAW (programmes VST3) : Classic Deep, Grunge Water, Slow Ocean, Seasick,
Rotary Fast, Subtle Thickener, Shimmer Clean, Warble, Bass Chorus. Clic droit → 9 presets utilisateur
communs à toutes les instances (`%APPDATA%\Studio Wiga\Small Tone\user_presets.xml`).

## Clic droit (n'importe où)
- **Window size** : défaut (493 px de haut = taille minimale, comme Holyverb), 60 %, 75 %, 100 % — ou tirer le coin.
- **Options** : RATE synchronisé au tempo du DAW (2 mesures → 1/16, aussi par clic sur la LED STATUS), stéréo élargie (LFO inversé à droite ;
  l'original est mono), souffle du BBD, anneau du RATE qui pulse avec le LFO.
- **MIDI** : canal, *Learn* — par défaut Footswitch = CC 80, Rate = CC 81, Depth = CC 82 ;
  pédale à verrouillage ou momentanée ; Program Change n = preset n+1.

## Fonctionnement (résumé)
- **LFO** : intégrateur LM358 + 2,2 µF et comparateur à hystérésis 120k/470k, polarisations 180k/68k/82k :
  triangle 0,86 → 2,79 V, résistance de charge = potard 1M log + 47k.
- **DEPTH** : sortie LFO → 4,7k → nœud 10 µF → 39k → horloge. Switch DIN. = 2,7k à la masse (diviseur ×0,37 + filtre rapide).
- **Horloge + BBD** : ≈ 85 → 195 kHz, délai 1024 / (2·f) ≈ 2,6 → 6 ms ; filtres anti-repliement et de
  reconstruction ~6 kHz, saturation douce du MN3207, léger souffle.
- **Mix** : sec + retardé à parts égales (pas de réglage de mix sur la pédale), niveau compensé.
- Le 0,047 µF (repère A) découple la référence de l'intégrateur (pin 3) : il ne change ni la vitesse ni la forme du LFO.

Police Inter (licence SIL OFL, `OFL-Inter.txt`).
