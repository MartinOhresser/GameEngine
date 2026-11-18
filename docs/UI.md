# Interface, menus et inventaire

Rien n'est câblé en dur : le moteur ne contient ni « le » menu ni « l'» inventaire. Un jeu assemble des **entités** avec les composants des modules `ui` et `inventory`, et décide de tout dans ses fichiers JSON : la taille, les cases, l'affichage, les règles de chaque case. Le jeu de référence est `games/treasure_run` (menu, HUD, barre d'objets) ; les scénarios de `games/treasure_run/scenarios/` montrent comment les tester sans fenêtre.

## Les noms propres au jeu

Les champs qui désignent « un élément d'une liste » s'écrivent avec des noms dans les fichiers de jeu (voir [MANIFEST.md](MANIFEST.md)). Le jeu déclare ses listes dans `lists` de `game.json` :

```jsonc
"lists": {
  "menu": ["main_menu"],                    // les menus (ui.Menu / ui.Button)
  "group": ["menu_buttons", "hotbar"],      // les groupes de cases d'une grille (ui.Grid / ui.Cell)
  "tag": ["player"],                        // les étiquettes (ui.Tag)
  "container": ["backpack", "chest"],       // les conteneurs (inventory.Slot / Item)
  "item_kind": ["key", "sword", "potion"],  // les genres d'objets
  "category": ["tool", "weapon", "consumable"], // les catégories d'objets (32 au plus)
  "action": ["start_game"]                  // actions propres au jeu, en plus de input.json
}
```

La liste `field` est faite par le moteur : tous les champs des modules du jeu, sous la forme `module.Composant.champ` (`pickup.Collector.score`).

## La fenêtre

`window` dans `game.json` donne la taille en pixels (800 par 600 par défaut). Elle est lue par la simulation (ressource `Viewport`), donc une interface ancrée donne les mêmes positions partout, dans un scénario comme dans une fenêtre. `geplay --size` la remplace pour une fenêtre. La position du pointeur arrive comme des actions : liez `mouse:x` et `mouse:y` à des actions de `input.json` (`pointer_x`, `pointer_y`), en pixels de la fenêtre et sans limite à 1.

## Le module `ui`

| Composant | Rôle |
| --- | --- |
| `Anchor` | place la `Position` relativement à la fenêtre : `anchor_x`/`anchor_y` (0 bord haut-gauche, 0,5 milieu, 1 bord bas-droit) et `offset_x`/`offset_y` en pixels |
| `Grid` + `Cell` | une grille place les entités qui ont un `Cell` du même `group` : la cellule `index` va en colonne `index % columns`, ligne `index / columns`. 1 colonne : une liste ; beaucoup : une rangée |
| `Tag` | un nom que porte une entité (« player »), pour la retrouver sans référence |
| `Bind` | recopie chaque tick un champ de l'entité qui porte le `Tag` `source` dans le `Text.value` et le `Bar.value` de sa propre entité (score, vie…) |
| `Bar` | une barre : un `Sprite` de longueur `length * value / max` ; avec un `pivot_x` de 0 elle grandit depuis son bord gauche |
| `Menu` + `Button` | le contrôleur d'un menu et ses boutons : haut/bas déplacent le focus, `accept` appuie ; avec `use_pointer`, la souris focalise le bouton survolé et `click` appuie |

Un bouton **lève une action** nommée (`start_game`) pendant un tick entier. Rien d'autre n'est imposé : un `scene_switch.GoToScene` qui écoute cette action change de scène, mais n'importe quel système peut la lire. Un menu est donc une scène ordinaire.

Un champ d'action laissé de côté vaut « aucune » : il ne fait rien.

```jsonc
// Un bouton : un sprite, un texte et un Button, assemblés dans un préfab.
{ "name": "button_play", "prefab": "button", "components": {
    "ui.Cell":   { "group": "menu_buttons", "index": 0 },
    "ui.Button": { "order": 0, "action": "start_game" },
    "text2d.Text": { "string": "menu.play" } } }
```

## Le module `inventory`

Un inventaire est fait de **cases** (`Slot`) et d'**objets** (`Item`) ; il n'y en a ni un seul ni un nombre fixe.

- Une case a une adresse (`container`, `index`) et ses propres règles : `accepts` (l'ensemble des catégories qu'elle prend ; vide : tout), `max_stack` (taille de pile maximale, 0 : celle de l'objet), `locked` (ne reçoit ni ne donne). Une case « épées seulement » : `"accepts": ["weapon"]`.
- Un objet a un `kind`, des `categories`, une `quantity`, un `max_stack` (1 : il ne s'empile pas) et, une fois rangé, l'adresse de sa case. Les objets sont des entités créées par des préfabs : un objet est donc entièrement décrit en JSON.
- La **taille** d'un inventaire est le nombre de cases que la scène crée. Une grille, une barre rapide ou une silhouette d'équipement ne sont que des `ui.Grid` (ou des positions écrites à la main) sur les mêmes cases. Les indices d'un conteneur vont de 0 sans trou.

Ce qui les fait bouger, toujours dans la simulation (donc testable en scénario) :

| Composant | Effet |
| --- | --- |
| `Gatherer` | ramasse dans son conteneur les objets posés à moins de `radius` : sur une pile du même genre qui a de la place, sinon dans la première case libre qui accepte l'objet ; sinon l'objet reste au sol |
| `Transfer` | une demande de déplacement d'une case à une autre, éventuellement d'un autre conteneur : sur une case vide la pile entière passe ; sur le même genre les piles fusionnent autant qu'il y a de place ; sur un autre genre les deux objets s'échangent si chaque case accepte l'objet de l'autre. `status` devient 1 (fait) ou 2 (refusé) |
| `Hand` | le curseur d'un joueur : gauche/droite/haut/bas (actions du jeu) déplacent le curseur sur les cases (`columns` par rangée), `select` prend l'objet puis le pose sur la case suivante, `cancel` le lâche ; avec `use_pointer` la souris pointe une case. Mêmes règles que `Transfer` |

Un objet rangé est **affiché** sur sa case : sa position suit celle de la case, son `Sprite` et son `Text` passent en coordonnées d'écran ; un `Text` de l'objet affiche la quantité (`"x{value}"`) tant qu'il est rangé. Un objet posé dans le monde reste un sprite du monde.

```jsonc
// Cinq cases en rangée : la 0 n'accepte que des outils, la 1 que des armes, la 4 deux objets de même genre au plus.
{ "name": "slot_0", "prefab": "slot", "components": { "inventory.Slot": { "index": 0, "accepts": ["tool"] }, "ui.Cell": { "index": 0 } } }
```

## Ce qui n'y est pas encore

- La `Hand` déplace dans un seul conteneur ; passer d'un conteneur à un autre (sac vers coffre, boutique) se fait par des `Transfer` créés par une règle : une règle pilotée par données n'existe pas encore.
- Une pile ne se coupe pas : un `Transfer` n'a pas de quantité, la pile passe en entier ou fusionne.
- Pas de défilement, pas de mise en page automatique au-delà des grilles, pas de texte enrichi, pas de glisser-déposer à la souris (clic puis clic).
- Les coordonnées sont en pixels de la fenêtre : pas d'échelle automatique selon la résolution ni de mise en page adaptative.
