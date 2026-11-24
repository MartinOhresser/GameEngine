# Complete Universe — Roadmap du tutoriel

Phases 1 et 2 de la [roadmap globale](ROADMAP.md), jusqu'au jalon **J1 « Tutoriel jouable »**. Source : GDD §12 ; arbitrages : [DECISIONS.md](DECISIONS.md) (D4 immobile jusqu'à l'étape 6, D6 pause dans la bulle, D7 mort = on recommence l'étape).

## 1. Le tutoriel tel qu'on le jouera

### Le lieu

Une seule planète, enfermée dans l'immense barrière du début :

- **Vue de l'espace** (cinématiques), elle est ronde, sous un dôme qui brille faiblement.
- **En jouant**, son sol est plat (D1, P6). Il fait environ quatre écrans de large, et on le voit en 3/4 : un bandeau pour la face du dessus du sol, des plans de décor en profondeur, et le dôme en arc dans le ciel.
- **En noir, blanc et gris.** La seule lumière est celle des étoiles, à travers le dôme.

De gauche à droite :

| Zone | Contenu | Étapes |
| --- | --- | --- |
| A — l'atterrissage | Le point d'arrivée, à portée de main : deux rochers (matière), un fossile (carbone) et un petit objet à protéger | 1 à 5 |
| B — le chemin | Un sol à suivre, une marche, puis un fossé et un rebord en hauteur | 6 et 7 |
| C — la cuvette | Gisements de carbone, puis de matière, assez loin les uns des autres pour qu'il faille bouger | 8 et 9 |
| D — le bord | Un espace dégagé sous le sommet du dôme | 10 |

### Les contrôles

Les touches sont modifiables à la phase 8. Par défaut :

| Action | Touche |
| --- | --- |
| Main gauche | Clic gauche |
| Main droite | Clic droit |
| Changer de couleur, roue | Maj |
| Récolte | F |
| Inventaire | Tab |
| Se déplacer | Q/D ou flèches |
| Sauter | Espace |
| Pause | Échap |

Les textes affichés à l'écran ne décrivent que les contrôles, comme le demande le GDD, et sont écrits en français.

### Les dix étapes

Chaque étape suit le même schéma :

1. Une courte cinématique : la caméra cadre la cible, le personnage fait le geste, et le texte du contrôle apparaît.
2. Le joueur essaie. L'étape suivante commence dès que la condition est remplie.
3. Une mort ramène au début de l'étape en cours (D7).

| # | Cinématique | Le joueur… | Suite quand… |
| --- | --- | --- | --- |
| 1 | **Ouverture.** Explosion, l'univers s'étend, plein de couleurs qui s'éteignent une à une jusqu'au gris. Des étoiles apparaissent. Plan large sur une immense barrière : le personnage, recroquevillé, dérive vers la planète. Zoom, fondu, il se pose. | regarde (maintenir Échap pour passer) | fin de la cinématique |
| 2 | **Mains.** Il regarde ses mains vides, aux bras gris (P5). Le Noir et le Blanc flottent près de lui. | met le Noir dans une main et le Blanc dans l'autre (Maj + clic) | chaque main tient une couleur |
| 3a | **Noir.** La caméra montre un rocher, la main noire se tend. | maintient le clic de la main noire sur le rocher : un trou noir l'aspire | le rocher a disparu et la matière a augmenté |
| 3b | **Blanc.** Une pierre se détache du dôme et tombe vers le petit objet. | protège l'objet avec une bulle ou une barrière plane (clic, ou clic glissé) | l'objet est intact quand la pierre a fini de tomber |
| 3c | **Fusion.** Les deux mains se rejoignent. | fait les deux clics ensemble sur le second rocher : bulle de récolte | la récolte est finie |
| 4 | **Inventaire.** Les mains s'ouvrent, des bulles sortent du corps. | ouvre l'inventaire (Tab) | l'inventaire est ouvert |
| 5 | **Récolte.** Les bras prennent d'eux-mêmes le Noir et le Blanc. | récolte le fossile (F, puis viser), puis regarde la bulle de carbone grossir | le carbone a augmenté et l'inventaire a été ouvert après |
| 6 | **Déplacements.** Sous ses pieds, de petites ondes sombres : il attire ses jambes. | marche jusqu'au bout du chemin. La marche coûte un peu de Noir | il atteint la marche |
| 7 | **Sauts.** Un mini trou noir grandit au-dessus de sa tête. | maintient Espace, relâche : saute le fossé, puis monte sur le rebord | il est sur le rebord |
| 8 | **Manque de Blanc.** Le Blanc baisse vite, les bords de l'écran noircissent. | récolte du carbone dans la cuvette avant qu'il ne soit trop tard | le Blanc est au-dessus de la moitié |
| 9 | **Manque de Noir.** Même chose : le Noir baisse, les bords blanchissent. | récolte de la matière | le Noir est au-dessus de la moitié |
| 10 | **Départ.** Le personnage lève les deux mains vers le sommet du dôme. | maintient le clic de la main noire : le trou noir grossit, échappe à tout contrôle (secousses, le sol est aspiré). La planète explose, le dôme se dissout, il dérive dans l'espace | fin : « à suivre » jusqu'à la phase 3, puis le voyage spatial |

La pause (Échap, ou une barrière posée sur soi) est disponible dès l'étape 2 : le personnage s'enferme dans une bulle, tout s'arrête, le monde devient flou, et le menu (reprendre, quitter) s'affiche dans la bulle (D6). Un écran titre (Jouer, Quitter) précède l'ouverture.

## 2. Les étapes de travail

Chaque étape est une PR vers `complete-universe`, vérifiée comme le dit la [roadmap](ROADMAP.md) : compilation, tests du module, scénarios du jeu. Les scénarios cités sont ceux qu'on écrit à cette étape. La phase 1 (modules) se joue dans de petites scènes `test_*`, la phase 2 dans la scène du tutoriel.

### Phase 1 — Les modules de base

**U1 — Squelette du jeu.**

- `games/complete_universe/` avec `game.json`, `input.json` (contrôles ci-dessus) et `tools/make_assets.py` (images provisoires).
- Une scène de sol plat et un personnage provisoire qui marche et saute avec `action_control` et `jump`.
- Scénarios : `walks`, `jumps`.

**U2 — Le terrain (`terrain2d`).** C'est l'étape la plus risquée, coupée en deux PR.

- **U2a** : le masque de cellules, la collision des corps contre le masque dans toutes les directions, le creusement d'un disque, la génération d'un sol à partir d'un profil.
- **U2b** : le dessin par polygones (marching squares), avec une nouvelle primitive du renderer (`SDL_RenderGeometry` derrière l'interface du renderer, plus un équivalent sans fenêtre).
- Scénarios : `stands_on_terrain`, `falls_in_hole`.
- Fini quand on voit un sol creusé dans une capture `geplay --headless`, sans chute de fluidité.

**U3 — Réserves et bords de l'écran (`reserve`, `bind`, `postfx`).**

- Le personnage a ses réserves de Noir et de Blanc, avec une consommation passive (P8).
- `postfx` gagne une couleur de vignettage, que `bind` relie aux réserves.
- Une réserve vide déclenche une action.
- Scénarios : `drains`, `edges_darken`, `empty_white_dies`.

**U4 — Les mains (`aim2d`, `loadout`).**

- La visée par le pointeur, avec une portée.
- Deux emplacements, suivant (Maj + clic), roue (Maj maintenu) avec ralenti (D8, T5), échange (P2), fusion (P3), couleurs grises tenables (D3).
- Les bras prennent la couleur de leur main.
- Scénarios : `assigns_hands`, `swaps`, `wheel_slows_time`, `fusion_needs_both`.

**U5 — Le Noir (`attractor2d`).**

- Un trou noir au point visé, qui grossit tant qu'on maintient et coûte de la matière par seconde.
- Il attire les corps, aspire les entités et creuse le terrain : le Noir seul ne rend que de la matière.
- Scénarios : `black_hole_absorbs_rock`, `black_hole_costs`, `black_hole_digs`.

**U6 — Le Blanc (`barrier2d`).**

- Une bulle qui grossit tant qu'on maintient, ou un segment par clic glissé.
- Coût en carbone à la pose, 3 barrières au plus (la plus ancienne disparaît).
- Solide pour l'extérieur (P4), elle bloque les effets venus de dehors et fige le temps de ce qu'elle contient.
- Scénarios : `barrier_blocks_rock`, `barrier_max_three`, `barrier_freezes_inside`.

**U7 — Récolte et inventaire.**

- La fusion Noir + Blanc donne une bulle de récolte, qui rend ce qu'elle a pris, trié dans les réserves, une fois finie.
- La touche de récolte met le Noir et le Blanc dans les mains, puis rend les couleurs d'avant.
- L'inventaire : des bulles autour du corps, dont la taille suit les réserves (`bind`, P7).
- Scénarios : `harvest_sorts`, `harvest_key_restores_hands`, `inventory_bubbles_grow`.

**U8 — Marche et saut.**

- La marche coûte du Noir selon la vitesse.
- Le saut chargé : la hauteur suit la charge, et la déviation, tirée par `Random`, grandit avec elle. Pleine charge = envol (D5).
- Scénarios : `walking_costs_black`, `charged_jump_higher`, `full_charge_takes_off`.

**U9 — Mort et reprise d'étape.**

- Deux morts :
  - plus de Noir : figé, l'écran blanchit ;
  - plus de Blanc : consumé par son trou noir, l'écran noircit.
- Un module générique de point de reprise (`checkpoint`) garde les nombres choisis (position, réserves) quand une action le demande, et les remet sur une autre.
- Scénario : `dies_and_restarts_step`.

### Phase 2 — Le tutoriel

**U10 — La planète et la séquence.**

- La scène `tutorial`, générée par `make_assets.py` : zones A à D et dôme.
- Les dix étapes en phases (`phase`), leurs conditions (`rules.Watch`) et leurs textes (`text2d`, `phase.ShowIn`).
- Le déplacement est verrouillé jusqu'à l'étape 6 (D4), par un verrou d'actions selon la phase. C'est une petite extension générique de `phase` ou d'`action_control`.
- Scénarios :
  - un par étape (`step_02_hands` … `step_10_departure`) ;
  - `cannot_walk_before_step_6` ;
  - `tutorial_through`, qui joue tout le tutoriel. Il est calé avec des sondes, comme dans `playtest-headless`.

**U11 — Les cinématiques.**

- L'ouverture : explosion, couleurs qui s'éteignent, plan large sur la barrière, dérive, zoom, atterrissage. Maintenir Échap pour passer.
- La mini-cinématique de chaque étape : cadrage, geste, texte.
- Le départ : trou noir qui échappe au contrôle, secousses, sol aspiré, explosion, dôme dissous, dérive, puis « à suivre ».
- Outils : `tween2d`, `camera2d`, `particles2d`, `postfx` et `terrain2d`.
- Scénarios : `opening`, `opening_skips`, `departure`.

**U12 — Pause dans la bulle et écran titre.**

- La pause de D6 : bulle autour du personnage, tout s'arrête (temps global à 0), y compris la consommation.
- Le flou du monde, par une basse résolution dans `postfx` (T4).
- Le menu dans la bulle, avec `ui` ; l'écran titre.
- Scénarios : `pause_stops_everything`, `pause_resumes`, `title_starts`.

**U13 — Beauté et son.**

- Les images définitives, générées par `make_assets.py` :
  - le personnage d'ovales, de 3/4, coupé en deux (D2), avec les motifs de ses bras : spirale pour le Noir, hexagones pour le Blanc ;
  - le sol en bandeau, les plans de décor, le dôme, les étoiles ;
  - les trous noirs et les bulles.
- Lumière et particules dans l'esprit Limbo : gris, contre-jours, poussière.
- Le module `sound` : un son par pouvoir (grave qui aspire, cristallin) et une musique d'ambiance.
- Fini quand des captures de chaque étape sont envoyées à Martin et qu'il les a validées.

### Jalon J1

Le protocole complet de la [roadmap](ROADMAP.md) :

1. `check.sh` dans WSL, puis toutes les corrections ;
2. tous les tests et `gectl list` ;
3. l'export Windows de `complete_universe`, joué de l'écran titre au « à suivre » ;
4. la PR `complete-universe` → `dev`, avec les captures et la doc du jeu (`docs/GAME_COMPLETE_UNIVERSE.md`, et une ligne dans `docs/GAMES.md`).

## 3. Ce qui pourrait changer le plan

- **Terrain** (U2) : si la collision contre le masque est trop lente, on passe à des cellules plus grosses, avec un contour lissé seulement au dessin.
- **Personnage de 3/4** (U13) : s'il est trop difficile à rendre beau avec numpy et Pillow, on le décompose en ovales articulés (`transform2d.Attach`), animés par le moteur plutôt qu'image par image.
