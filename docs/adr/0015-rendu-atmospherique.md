# ADR 0015 : un rendu 2D atmosphérique, et un plateformer pour le prouver

Statut : accepté (cadrage validé par Martin le 23 novembre 2026). B1 fait (SDL 3.4.20, mesure : voir « Mesure »), B2 fait (rotation, mélanges, lissage, parallaxe, mouvement fluide), B3 fait (la forêt de `games/lueur`), B4 fait (module `jump`, `collision2d.Blocked`, accélération, caméra à zone morte, anticipation et limites ; le tremblement de caméra est reporté à B5, avec les particules) , B5 en cours (la lumière : module `light2d`, `sprite2d.Sprite.unlit` et `light`) ; B6 à B9 à réaliser.

## Contexte

Les jeux, les exports et les compilations marchent. Martin fixe trois objectifs à très long terme :

1. une grande bibliothèque de modules, pour faire facilement des jeux nouveaux et **beaux, dignes des jeux récents** ;
2. la 3D (ou la 2D isométrique) ;
3. le multijoueur (entièrement à définir).

Ce sprint ne porte que sur le premier, volet « beau » : **un plateformer 2D dont l'esthétique vise Ori, Limbo, Little Nightmares, Hollow Knight ou Inside**, de préférence Ori ou Limbo. La bibliothèque se remplira au fil des jeux.

Le rendu actuel (ADR 0008) a été pensé pour du pixel art. Le backend SDL dessine avec `SDL_Renderer` (SDL 3.2.30) :

| Il sait | Il ne sait pas |
| --- | --- |
| des cellules d'atlas teintées et transparentes, avec un miroir horizontal | tourner une image (l'appel SDL le permet, `DrawCommand` ne le transmet pas) |
| une caméra (centre et zoom) | un défilement par plan (parallaxe) |
| des formes pleines (rectangles, cercles, lignes) | un filtrage lissé : tout est agrandi au plus proche |
| l'interface en pixels de la fenêtre | un autre mélange que la transparence (additif, multiplicatif) |
| | dessiner dans une texture intermédiaire (lumière, halo, flou) |
| | des shaders |
| | interpoler entre deux ticks : l'écran montre le dernier tick, ce qui saccade sur un écran à 144 Hz |

Aucun module ne fait de lumière, de particules ni d'effet d'écran, et aucun ne fait de **saut commandé**. Dans `doodle_jump`, `bounce2d` relance le joueur de lui-même quand il retombe sur une surface ; le joueur ne fait que se diriger. `collision2d.Contacts` compte ce qui touche une entité, mais pas de quel côté : un jeu ne sait pas si son héros est **au sol**.

Enfin, les images des jeux sont produites par des scripts Python sans bibliothèque (`make_png.py`), sans anticrénelage, flou ni dégradé de qualité.

## Décision

### 1. Le style : les silhouettes de Limbo, la lumière d'Ori

Ori tient d'abord à **ses images peintes à la main** par des artistes : on ne les produit pas par un script. Limbo tient surtout à **son atmosphère** (silhouettes, brouillard en profondeur, lumière, grain, flou des plans lointains, animation fluide) : c'est du moteur et des images qu'un script sait générer.

Le jeu d'essai prend donc un style **« silhouettes à la Limbo, lumière colorée à la Ori »** :
- une forêt au crépuscule ;
- un premier plan noir et 5 à 7 plans de parallaxe qui se perdent dans la brume ;
- des lucioles et un esprit lumineux qui brillent, des rayons de lumière ;
- du grain et un vignettage ;
- **une petite cinématique d'ouverture** : un lent travelling dans la forêt, le titre, le héros qui se réveille.

Le moteur affichera aussi bien des **décors peints** : un jeu qui fournit des images faites par un artiste ou une IA bascule vers Ori sans changer le code.

**Pas d'isométrique** : Ori et Limbo sont en vue de côté. L'isométrique ira avec l'objectif 3D.

Nom provisoire du jeu : `games/lueur`.

### 2. Un seul renderer : l'existant, amélioré

Il n'y aura **pas de second renderer**. Le renderer SDL actuel est amélioré, et les jeux existants s'affichent comme avant. `NullRenderer` et `RecordingRenderer` (tests, mode sans fenêtre) restent.

- **SDL passe de 3.2.30 à la dernière 3.4.x.** SDL 3.4 permet de donner un shader de fragments personnalisé à `SDL_Renderer` (`SDL_CreateGPURenderState`, `SDL_SetGPURenderState`) quand celui-ci tourne sur son pilote « gpu ». Il lit aussi les PNG lui-même, ce qui pourrait remplacer `stb_image` plus tard.
- **Deux niveaux d'effets** :
  - **sans shader, partout** (WebAssembly et rendu logiciel compris), avec ce que `SDL_Renderer` sait déjà faire : textures cibles, mélanges additif et multiplicatif, filtrage lissé, rotation. Cela couvre la parallaxe, une carte de lumière multipliée sur la scène, un halo approché (scène réduite, lissée, ajoutée par-dessus), le grain, le vignettage et les nappes de brume ;
  - **avec shader**, seulement sous le pilote « gpu » de SDL : étalonnage des couleurs, vrai flou, distorsion. **Sans ce pilote, ces effets sont ignorés**, sans erreur, et le jeu reste jouable et lisible.
- **Ce que `DrawCommand` gagne** (des champs qui valent par défaut le comportement actuel) :
  - un angle de rotation ;
  - un mode de mélange (transparence, additif, multiplicatif) ;
  - un facteur de parallaxe par axe (1 suit le monde, 0 reste fixe à l'écran) ;
  - un dessin répété sur une largeur (plans de fond sans fin) ;
  - une **passe** : monde, lumière, interface.
- **Le renderer compose l'image dans un ordre fixe** : le monde dans une texture, les lumières dans une carte multipliée sur le monde, les effets d'écran, puis l'interface par-dessus, sans lumière ni effet.
- **Filtrage par atlas** : `"filter": "linear"` dans le JSON d'un atlas lisse ses images ; sans ce champ, le pixel art reste net.
- **Interpolation** : les systèmes de dessin placent chaque image entre l'avant-dernier et le dernier tick, selon `Time::alpha`, qui existe déjà. La simulation et son empreinte n'en dépendent pas.

### 3. Des modules génériques

Chaque besoin du plateformer devient un module ou l'extension d'un module, utilisable par n'importe quel jeu (règle du §2 de `CLAUDE.md`). Les noms ci-dessous sont des propositions, fixées à l'étape qui les crée.

| Besoin | Où | Ce que ça fait |
| --- | --- | --- |
| Savoir si on est au sol, contre un mur | `collision2d` (extension) | les côtés où l'entité touche un mur solide pendant ce tick (dessous, dessus, gauche, droite) |
| Sauter | nouveau module de saut | une action déclenche une impulsion contraire à la gravité, seulement au sol ; hauteur selon la durée d'appui ; délai de grâce après le bord ; appui mémorisé juste avant l'atterrissage ; nombre de sauts en l'air |
| Accélérer, freiner | `action_control` (extension) | la vitesse rejoint la commande avec une accélération, au lieu d'y sauter d'un coup (0 garde le comportement actuel) |
| Caméra vivante | `camera2d` (extension) | zone morte, anticipation dans le sens de la marche, limites du niveau, tremblement |
| Parallaxe | `sprite2d` (extension) | facteur de défilement par sprite, répétition horizontale |
| Lumière | nouveau `light2d` | lumière ambiante (la couleur de l'ombre) et lumières ponctuelles colorées : rayon, intensité, scintillement |
| Particules | nouveau `particles2d` | émetteurs (poussière, lucioles, pluie, braises, feuilles). Les particules **ne sont pas des entités** : leur état est de la présentation, hors de l'empreinte, mais avance au rythme des ticks pour que deux captures soient identiques |
| Effets d'écran | nouveau `postfx` | sur l'entité caméra : halo, vignettage, grain, étalonnage, flou |
| Animation par états | `sprite2d` (extension) | choisir une animation (repos, course, saut, chute) à partir de champs du jeu, comme `FrameBind` le fait pour une image |
| Cinématique | `phase` + `tween2d` | des tweens à délais déplacent la caméra, font apparaître le titre et les bandes noires ; une phase bloque les commandes ; une action la passe. Un module de séquence ne sera créé que si cela ne suffit pas |

Le son d'ambiance compte beaucoup chez Limbo, mais il reste hors de ce sprint (G12 `sfx` de l'ADR 0013 est toujours à faire).

### 4. Les images

Les images du jeu sont générées par son `tools/make_assets.py` avec **numpy et Pillow, installés dans WSL**. Windows n'a pas Python ; les scripts tournent déjà dans WSL. Le script produit :
- les silhouettes (arbres, rochers, herbes, ronces) ;
- les dégradés de ciel et les nappes de brume ;
- les textures de lumière (halo, rayon) et de grain ;
- les poses du héros et de l'esprit, dessinées image par image.

Les PNG générés sont versionnés comme ceux des autres jeux. Un atlas ne dépasse pas **4096 pixels de côté**, la limite courante des téléphones. Un générateur décrit en JSON (dans `gectl`) ne viendra que si les scripts se répètent d'un jeu à l'autre.

### 5. Tests

- La simulation ne dépend pas du dessin : les scénarios du jeu tournent sans fenêtre, comme ceux des autres jeux.
- `RecordingRenderer` vérifie ce qui est demandé (angle, mélange, parallaxe, passe, lumières).
- Les effets **sans shader** se vérifient par des captures de pixels sans tête, avec le rendu logiciel de SDL ; cela tourne partout, CI comprise.
- Les effets **à shader** demandent un GPU, que la CI n'a pas : ils se vérifient en local par captures d'écran.
- **La mesure que l'ADR 0008 n'a pas pu faire** (nombre de sprites par image à 60 images/s sur un vrai GPU) est faite à l'étape B1, sur le PC de Martin.

## Étapes

Une PR vers `dev` par étape, chacune vérifiée en local (`check.sh` avant chaque push de C++).

| Étape | Contenu | Sortie vérifiable |
| --- | --- | --- |
| B0 | cet ADR | — |
| B1 | SDL 3.2.30 → 3.4.x ; mesure de performance | tous les jeux et scénarios inchangés ; exports Windows, Linux et Android qui jouent ; chiffres de la mesure dans cet ADR |
| B2 | renderer : rotation, mélanges, filtrage par atlas, répétition, parallaxe, passes, interpolation | captures sans tête ; jeux existants identiques au pixel près |
| B3 | générateur d'images (numpy, Pillow) et premier décor de la forêt en parallaxe | une capture d'écran qui montre déjà le style |
| B4 | déplacement : côtés de contact (`collision2d`), saut, accélération, caméra | scénarios : sauter, délai de grâce, hauteur variable, caméra bornée |
| B5 | `light2d` et `particles2d` | captures ; `RecordingRenderer` |
| B6 | `postfx` sans shader (halo, vignettage, grain), puis avec shader (étalonnage, flou) et leur chaîne de compilation | captures sans tête pour le premier niveau ; captures locales sur GPU pour le second |
| B7 | animation par états ; le héros et l'esprit | scénarios : la bonne animation selon l'état |
| B8 | le jeu `games/lueur` : 2 ou 3 courts niveaux, quelques énigmes (caisse à pousser, pièges), la cinématique | scénarios ; exports Windows et Android qui jouent ; `docs/GAME_LUEUR.md` |
| B9 | documentation (`RENDERING.md`, `MODULES.md`), skills (`engine-catalog`, `game-from-prompt`), `CHANGELOG.md` | `scripts/check_skills.py` vert |

Dès B3, on voit quelque chose de joli.

## Mesure (B1, 23 novembre 2026)

`gebench` (dans `platform/sdl`) dessine à travers le `Renderer` du moteur, comme un jeu, dans une fenêtre de 1280 × 720 sans synchronisation verticale. Il double chaque charge tant qu'une image prend moins de 1/60 s, et mesure deux cas :
- des sprites de 32 × 32 tirés d'un atlas ;
- des plans qui couvrent toute la fenêtre, à demi transparents, comme les plans d'une parallaxe ; c'est le remplissage des pixels qui les limite.

Machine : PC portable de Martin, Intel Core Ultra 7 255U, **carte graphique intégrée** Intel, SDL 3.4.20, compilation Release (MSVC).

| Pilote de `SDL_Renderer` | Sprites 32 × 32 à 60 i/s | Plans plein écran à 60 i/s |
| --- | --- | --- |
| choix de SDL (**direct3d11**) | 64 000 | 128 |
| direct3d11 | 32 000 à 64 000 | 128 |
| direct3d12 | 32 000 | 256 |
| vulkan | 64 000 | 256 |
| opengl | 64 000 | 512 |
| gpu (sur Vulkan) | 64 000 | 128 |
| gpu (sur Direct3D 12, son choix par défaut) | non mesurable | non mesurable |
| software (processeur) | 2 000 | 1 |

Chaque charge double à chaque pas : les chiffres donnent un ordre de grandeur, pas une limite exacte. Deux passages du même pilote peuvent tomber d'un côté ou de l'autre d'un doublement (direct3d11).

Ce qu'on en tire :
- **La marge est très grande.** Une scène du plateformer compte quelques centaines de sprites et une dizaine de plans plein écran. Même une carte intégrée en dessine dix fois plus à 60 i/s. Le dessin ne limitera pas la beauté du jeu sur PC ; sur téléphone, c'est le remplissage des pixels qu'il faudra surveiller (lumière et halo à demi-résolution, §Conséquences).
- **Le pilote par défaut reste celui que SDL choisit** (Direct3D 11 sous Windows).
- **Pilote « gpu »** : sur Direct3D 12, il reste calé sur l'écran (16,7 ms par image) même quand on lui demande de ne pas attendre. On ne peut donc pas mesurer sa marge, mais il tient 60 i/s. Sur Vulkan, il se comporte comme les autres. Le choix de l'API sous le pilote « gpu » pour les effets à shader se fait en B6.
- Le rendu logiciel reste un secours (machines sans carte utilisable, tests sans tête) : 2 000 sprites, un seul plan plein écran.
- Passer à SDL 3.4.20 n'a demandé aucun changement de code, seulement une bibliothèque système de plus sous Linux (XTest, `libxtst-dev`). Tous les tests passent, et les captures sans tête des jeux à l'image fixe sont identiques au pixel près à celles de SDL 3.2.30. Les jeux en mouvement ne se comparent pas ainsi : `geplay --headless` suit l'horloge réelle, et deux lancements donnent des images différentes même sans changer de version.

## Conséquences et risques

- **Passer à SDL 3.4 touche toutes les plateformes**, Android d'abord. C'est la première étape de code, pour savoir tôt ce qu'elle casse. La CI joue l'export Android dans l'émulateur quand le moteur change.
- **WebAssembly** : les effets à shader y sont ignorés (`SDL_Renderer` y tourne sans le pilote « gpu ») ; le reste s'affiche.
- **Téléphones** : la lumière et le halo se calculent dans des textures à demi-résolution, pour ne pas saturer le remplissage des pixels.
- **Taille du dépôt** : des images en haute résolution pèsent plus que du pixel art ; les scripts les génèrent à la taille utile, sans plus.
- Le shader demande un format par API graphique (SPIR-V, DXIL, MSL). La façon de les produire (compilés d'avance et versionnés, ou compilés à la construction) se décide en B6 ; rien avant B6 n'en a besoin.
- Hors de ce sprint : 3D, isométrique, réseau, son.
