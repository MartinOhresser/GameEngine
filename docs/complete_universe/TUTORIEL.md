# Complete Universe — Roadmap du tutoriel

Phases 1 et 2 de la [roadmap globale](ROADMAP.md), jusqu'au jalon **J1 « Tutoriel jouable »**. Source : GDD §12 ; arbitrages : [DECISIONS.md](DECISIONS.md) (D4 immobile jusqu'à l'étape 6, D6 pause dans la bulle, D7 mort = on recommence l'étape).

## 1. Le tutoriel tel qu'on le jouera

### Le lieu

Une seule planète, sans barrière autour (D27) :

- **Vue de l'espace** (cinématiques), elle est ronde.
- **En jouant**, son sol est plat (D1, P6). Il fait environ quatre écrans de large, et on le voit de côté, comme dans Ori : un peu du dessus du sol et des plans de décor en profondeur.
- **En noir, blanc et gris.** La seule lumière est celle des étoiles.

De gauche à droite :

| Zone | Contenu | Étapes |
| --- | --- | --- |
| A — l'atterrissage | Le point d'arrivée, à portée de main : deux rochers (matière), un fossile (carbone) et un petit objet à protéger | 1 à 5 |
| B — le chemin | Un sol à suivre, une marche, puis un fossé et un rebord en hauteur | 6 et 7 |
| C — la cuvette | Gisements de carbone, puis de matière, assez loin les uns des autres pour qu'il faille bouger | 8 et 9 |
| D — le bord | Un espace dégagé au bout de la planète | 10 |

### Les contrôles

Les touches sont modifiables à la phase 8. Par défaut :

| Action | Touche |
| --- | --- |
| Main gauche : le Blanc | Clic gauche |
| Main droite : le Noir | Clic droit |
| Changer de couleur, roue (après le tutoriel) | Maj |
| Récolte | F |
| Inventaire | Tab |
| Se déplacer | Q/D ou flèches |
| Sauter | Espace |
| Se déplacer dans l'espace | Z Q S D ou flèches |
| Pause | Échap |

Les textes affichés à l'écran ne décrivent que les contrôles, comme le demande le GDD, et sont écrits en français.

### Les étapes

Refaites le 27 novembre 2026 sur les textes de Martin (DECISIONS.md, D31). Deux sortes
d'étapes :

1. **Démonstration** : le jeu montre, de près, pendant que le titre et la ligne en bas de l'écran
   expliquent ; puis une pause, le texte toujours là ; puis l'étape suivante.
2. **Test** : le joueur le fait ; l'étape suivante vient dès que c'est fait. Le pouvoir d'une
   étape se débloque à la fin de sa démonstration ; ceux des étapes suivantes ne marchent pas
   encore.

La caméra est proche du personnage du début jusqu'à la fin du saut (D18). Une mort ramène au début
de l'étape en cours (D7).

| # | Étape | Sorte | Ce qui se passe | Durée ou fin |
| --- | --- | --- | --- | --- |
| 0 | Atterrissage | — | il se réveille | 3 s |
| 1 | Une entité, deux parties | démo | le personnage immobile, en gros plan | 5 s + 2 s |
| 2 | Noir | démo | sa moitié noire s'illumine, un petit trou noir apparaît | 4 s + 1,5 s |
| 3 | Blanc | démo | sa moitié blanche s'illumine ; une bulle autour de lui, puis autour d'une pierre | 4 s + 1,5 s |
| 4 | Deux forces complémentaires | démo | les deux réserves (les bulles de l'inventaire), puis elles disparaissent | 4 s + 2 s |
| 5 | Le trou noir | démo | un trou noir avale des débris | 4 s + 1,5 s |
| 6 | Utiliser le Noir | test | avaler deux pierres | — |
| 7 | Un pouvoir qui consomme | démo | un trou noir grandit ; la réserve de matière baisse | 4 s + 1,5 s |
| 8 | La puissance du Noir | test | maintenir le clic droit jusqu'à un grand trou noir | — |
| 9 | La barrière | démo | une bulle, puis un mur | 4 s + 1,5 s |
| 10 | Utiliser le Blanc | test | une bulle puis un mur | — |
| 11 | Le coût du Blanc | démo | une petite et une grande barrière ; le carbone baisse plus pour la grande | 4 s + 1,5 s |
| 12 | À l'abri | démo | une bulle autour de lui ; des pierres tombent dessus sans l'atteindre | 4 s + 2 s |
| 13 | La protection | test | une bulle autour de soi, y rester 3 s | — |
| 14 | La récolte | démo | les deux mains, puis une bulle de récolte avale un fossile | 5 s + 2 s |
| 15 | Récolter plus | test | récolter un fossile entier (sinon un nouveau tombe toutes les 12 s) | — |
| 16 | Les réserves | démo | les deux réserves et les mains qu'elles nourrissent | 3 s + 1,5 s |
| 17 | Marcher | démo | quelques pas, les petits trous noirs sous les pieds | 3 s + 1 s |
| 18 | Le déplacement | test | traverser le chemin | — |
| 19 | Sauter | démo | la charge, le trou noir, la propulsion, au ralenti | 4 s + 1,5 s |
| 20 | Le saut | test | franchir le fossé, atteindre le rebord | — |
| 21 | Les deux mains | démo | la caméra regarde devant : un trou noir creuse le pilier, un mur de Blanc couvre le gouffre | 3 s + 1,5 s |
| 22 | Combiner les pouvoirs | test | détruire le pilier, franchir le gouffre sur un mur | — |
| 23 | Le carbone s'épuise | démo | le carbone baisse, les bords noircissent, le Blanc s'éteint | 4 s + 2 s |
| 24 | Récupérer du carbone | test | le carbone au-dessus de la moitié | — |
| 25 | La matière s'épuise | démo | la matière baisse, les bords blanchissent, le Noir s'éteint | 4 s + 2 s |
| 26 | Récupérer de la matière | test | la matière au-dessus de la moitié | — |
| 27 | Sans matière, le corps se défait | démo | la matière tombe à zéro en 4 s, il ne bouge plus, tout devient blanc | il revient |
| 28 | Sans carbone, ce serait le noir | démo | l'écran noircit un instant ; ses réserves reviennent | 4 s + 2 s |
| 29 | Le départ | test | garder Espace chargé : au bout de 3 s, le trou noir grandit vite ; relâcher : hors de l'atmosphère, il monte en orbite | en orbite |
| 30 | L'espace | démo | il vole tout seul à droite, en haut, à gauche, en bas | 4 s + 1,5 s |
| 31 | Le déplacement orbital | test | rejoindre la plateforme (un rocher qui flotte) ; puis « À suivre… » | — |

Le Noir et le Blanc vides tuent (D7) ; ailleurs dans le jeu, une réserve vide éteindra le pouvoir
qu'elle nourrit (D31).

La pause (Échap) est disponible partout en jeu : le personnage s'enferme dans une bulle et reste net, tout s'arrête (la consommation aussi), tout le reste devient flou, et les deux boutons (Reprendre, Retour au titre) s'affichent en bas (D6, D25). La pause par une barrière posée sur soi reste à faire. Un écran titre (Jouer, Quitter) précède l'ouverture : le ciel du big bang, encore en couleurs, et le personnage endormi dans sa bulle.

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
- Coût en carbone à la pose, une seule barrière à la fois : la nouvelle remplace l'ancienne (D29). Elle apparaît en grandissant, lâche des éclats de verre, puis respire légèrement.
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

- La scène `tutorial`, générée par `make_assets.py` : zones A à D.
- Les dix étapes en phases (`phase`), leurs conditions (`rules.Watch`) et leurs textes (`text2d`, `phase.ShowIn`).
- Le déplacement est verrouillé jusqu'à l'étape 6 (D4), par un verrou d'actions selon la phase. C'est une petite extension générique de `phase` ou d'`action_control`.
- Scénarios :
  - un par étape (`step_02_hands` … `step_10_departure`) ;
  - `cannot_walk_before_step_6` ;
  - `tutorial_through`, qui joue tout le tutoriel. Il est calé avec des sondes, comme dans `playtest-headless`.

**U11 — Les cinématiques.**

- L'ouverture (D23, D24, D30) : noir quelques secondes, un point qui vient lentement, reste immobile, tremble de plus en plus, puis l'explosion (rayons, reflets, nuages de gaz colorés, lueurs) ; couleurs qui s'éteignent, le ciel qui se construit ; la lumière du centre s'évanouit et laisse voir le personnage au loin, qui grandit et erre vers la planète ; elle grossit derrière lui jusqu'à couvrir le fond, puis glisse vers le bas (pivot), le ciel se fige, et il tombe sur le sol du tutoriel, sans coupure (scène « arrival »). Échap pour passer.
- La mini-cinématique de chaque étape : cadrage, geste, texte.
- Le départ (D19, D20) : grand saut vers le ciel ; l'attraction le ralentit et le dévie ; il se recroqueville dans une bulle et dérive, sans coupure, vers la planète suivante, puis « à suivre ». La planète quittée disparaît une fois hors de vue.
- Outils : `tween2d`, `camera2d`, `particles2d`, `postfx` et `terrain2d`.
- Scénarios : `opening`, `opening_skips`, `departure`.

**U12 — Pause dans la bulle et écran titre.**

- La pause de D6 : bulle autour du personnage, tout s'arrête (temps global à 0), y compris la consommation.
- Le flou du monde, par une basse résolution dans `postfx` (T4).
- Le menu dans la bulle, avec `ui` ; l'écran titre.
- Scénarios : `pause_stops_everything`, `pause_resumes`, `title_starts`.

**U13 — Beauté et son.**

- Les images définitives, générées par `make_assets.py` :
  - le personnage d'ovales, de profil, le torse un peu tourné vers nous et coupé en deux (D2). Ses animations sont fluides comme dans Ori. Ses bras portent des motifs : une spirale pour le Noir, des hexagones pour le Blanc ;
  - le sol, dont on voit un peu le dessus, les plans de décor, les étoiles ;
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
- **Animations du personnage** (U13) : si des animations fluides image par image sont trop difficiles à rendre belles avec numpy et Pillow, on décompose le corps en ovales articulés (`transform2d.Attach`), animés par le moteur.
