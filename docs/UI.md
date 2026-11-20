# Interface, menus et inventaire

Rien n'est câblé en dur : le moteur ne contient ni « le » menu ni « l'» inventaire. Un jeu assemble des **entités** avec les composants des modules `ui` et `inventory`, et décide de tout dans ses fichiers JSON : la taille, les cases, l'affichage, les règles de chaque case. Le jeu de référence est `games/treasure_run` (menu, HUD, barre d'objets) ; les scénarios de `games/treasure_run/scenarios/` montrent comment les tester sans fenêtre.

## Les noms propres au jeu

Les champs qui désignent « un élément d'une liste » s'écrivent avec des noms dans les fichiers de jeu (voir [MANIFEST.md](MANIFEST.md)). Le jeu déclare ses listes dans `lists` de `game.json` :

```jsonc
"lists": {
  "menu": ["main_menu"],                    // les menus (ui.Menu / ui.Button)
  "group": ["menu_buttons", "hotbar"],      // les groupes de cases d'une grille (ui.Grid / ui.Cell)
  "tag": ["player"],                        // les étiquettes (tag.Tag)
  "container": ["backpack", "chest"],       // les conteneurs (inventory.Slot / Item)
  "item_kind": ["key", "sword", "potion"],  // les genres d'objets
  "category": ["tool", "weapon", "consumable"], // les catégories d'objets (32 au plus)
  "action": ["start_game"],                 // actions propres au jeu, en plus de input.json
  "runtime_list": ["games"],                // les listes de textes remplies pendant la partie
  "host_command": ["play"]                  // les commandes que le jeu peut envoyer à son hôte
}
```

La liste `field` est faite par le moteur : tous les champs des modules du jeu, sous la forme `module.Composant.champ` (`pickup.Collector.score`).

## La fenêtre

`window` dans `game.json` donne la taille en pixels (800 par 600 par défaut). Elle est lue par la simulation (ressource `Viewport`), donc une interface ancrée donne les mêmes positions partout, dans un scénario comme dans une fenêtre. `geplay --size` la remplace pour une fenêtre. 
**S'adapter à l'écran.** Les téléphones n'ont pas tous la même forme (16:9, 19,5:9, 20:9…) : avec une zone fixe, un jeu garde des bandes sur le côté. `"window": {"width": 480, "height": 720, "adapt": true}` fait suivre la forme de l'écran : le petit côté garde sa taille (ici la largeur, 480) et le grand s'étire pour remplir l'écran (480 par 853 en 9:16, par 1067 en 9:20). `adapt` vaut `true` (de 0,4 à 0,8 pour un jeu plus haut que large, de 1,25 à 2,4 pour un jeu plus large que haut) ou `{"min_aspect": 0.5, "max_aspect": 0.7}` : la largeur divisée par la hauteur, la forme de la fenêtre y comprise. Au-delà, l'écran garde des bandes. La ressource `Viewport` prend la taille de la zone à chaque image : une interface ancrée (`ui.Anchor`) suit les bords, et `spawner2d` et `bounds2d` s'en servent pour construire le monde devant la caméra et retirer ce qui est sorti. Un jeu qui s'adapte ne doit donc rien placer à une position fixe qui dépende de la hauteur : ancrez-le, ou partez du centre. Ni les scénarios ni `--size` ne s'adaptent (ils jouent la taille de `window`), ce qui garde les tests identiques partout ; `geplay --window 360x800` essaie une forme de téléphone sur un ordinateur.

La position du pointeur arrive comme des actions : liez `mouse:x` et `mouse:y` à des actions de `input.json` (`pointer_x`, `pointer_y`), en pixels de la fenêtre et sans limite à 1.

## Le module `ui`

| Composant | Rôle |
| --- | --- |
| `Anchor` | place la `Position` relativement à la fenêtre : `anchor_x`/`anchor_y` (0 bord haut-gauche, 0,5 milieu, 1 bord bas-droit) et `offset_x`/`offset_y` en pixels |
| `Grid` + `Cell` | une grille place les entités qui ont un `Cell` du même `group` : la cellule `index` va en colonne `index % columns`, ligne `index / columns`. 1 colonne : une liste ; beaucoup : une rangée |
| `Tag` | un nom que porte une entité (« player »), pour la retrouver sans référence |
| `Bind` | recopie chaque tick un champ de l'entité qui porte le `Tag` `source` dans le `Text.value` et le `Bar.value` de sa propre entité (score, vie…) |
| `Bar` | une barre : un `Sprite` de longueur `length * value / max` ; avec un `pivot_x` de 0 elle grandit depuis son bord gauche |
| `Menu` + `Button` | le contrôleur d'un menu et ses boutons, lus comme un tableau d'après leur place à l'écran (les boutons côte à côte, à peu près à la même hauteur, forment une ligne) : `left`/`right` déplacent le focus le long de la ligne, `up`/`down` vont à la ligne du dessus ou du dessous, sur le bouton le plus proche ; des boutons au même endroit se suivent dans l'ordre de leurs numéros ; `accept` appuie ; avec `use_pointer`, la souris focalise le bouton survolé et `click` appuie |

| `Pager` + `Row` | une liste de textes remplie pendant la partie (voir plus bas), montrée une page à la fois : `page_size` rangées (`Row`), des actions `previous` / `next` pour tourner les pages |

Un bouton **lève une action** nommée (`start_game`) pendant un tick entier. Rien d'autre n'est imposé : un `scene_switch.GoToScene` qui écoute cette action change de scène, mais n'importe quel système peut la lire. Un menu est donc une scène ordinaire.

Un champ d'action laissé de côté vaut « aucune » : il ne fait rien.

```jsonc
// Un bouton : un sprite, un texte et un Button, assemblés dans un préfab.
{ "name": "button_play", "prefab": "button", "components": {
    "ui.Cell":   { "group": "menu_buttons", "index": 0 },
    "ui.Button": { "order": 0, "action": "start_game" },
    "text2d.Text": { "string": "menu.play" } } }
```

## Du texte qui n'est pas dans les fichiers du jeu

Les textes d'un jeu viennent de ses fichiers de langue (clés), et les composants ne portent que des nombres. Pour afficher ce qu'on ne connaît qu'à l'exécution (les jeux qu'un lanceur a trouvés, les joueurs d'un salon, les noms d'objets d'un sac), le moteur a des **listes d'exécution** (`RuntimeLists`) : des listes de textes remplies par l'hôte, par un système ou, dans un scénario, par la clé `lists`. Un texte les montre avec `text2d.Dynamic` : une liste (nom de la liste `runtime_list` du manifeste) et l'indice d'un élément ; l'élément remplace le texte de `Text.string`, mais `Text` garde son apparence et son `{value}`. Un élément absent n'affiche rien.

Pour une liste longue, `ui.Pager` et `ui.Row` font la pagination avec les boutons ordinaires :

```jsonc
{ "name": "pages", "components": {
    "ui.Pager": { "list": "games", "menu": "main_menu", "page_size": 8,
                  "previous": "page_prev", "next": "page_next" } } },
// une rangée : un bouton dont le texte est l'élément de la page
{ "name": "row0", "prefab": "button", "components": {
    "ui.Row": { "list": "games", "row": 0 },
    "text2d.Dynamic": {},
    "ui.Button": { "menu": "main_menu", "order": 0, "action": "pick" } } }
```

- Le `Pager` donne à chaque `Row` son élément (`page * page_size + row`) et **éteint** le bouton d'une rangée sans élément : il n'est plus focalisable ni cliquable et montre `disabled_frame`.
- La rangée qui a le focus devient l'**élément sélectionné** de la liste, et le reste quand le focus passe à « Précédent » ou « Jouer ».
- `ui.RowButton { list, row }` est un **second bouton** pour une rangée (une icône d'export à côté de chaque jeu, un bouton « supprimer » à côté de chaque sauvegarde) : il représente le même élément que la rangée, s'éteint avec elle, et quand il a le focus ou qu'on le presse, l'élément de sa rangée est l'élément sélectionné. Placé à droite de la rangée, il se rejoint avec la flèche droite.
- `host.Call` avec `list` envoie cet élément à l'hôte (`play` sait quel jeu).
- Les rangées se choisissent au clavier et à la souris, comme tout bouton. Le défilement fluide viendra si la pagination ne suffit pas.

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

## Contrôles à l'écran (tactile)

Le module `touch_controls` dessine des contrôles pour un écran tactile et les transforme en actions : le jeu ne sait pas qu'un doigt les a produites.

| Composant | Rôle |
| --- | --- |
| `TouchButton` | un rectangle (`width` × `height`, centré sur la `Position`) ; tant qu'un doigt le tient, l'action `action` vaut 1 ; son `Sprite` montre `pressed_frame` au lieu de `normal_frame` |
| `TouchStick` | un joystick : un doigt posé à moins de `zone` pixels de la `Position` le prend ; `action_x` et `action_y` valent le décalage divisé par `radius`, entre -1 et 1, à 0 dans la `dead_zone` (le bas est positif en y) |
| `TouchKnob` | le « pouce » du joystick du même `group` : il suit le doigt |

Mise en place : (1) ajoutez le module et déclarez dans `lists` les actions des contrôles (`action`) et le `group` du joystick ; (2) liez les doigts dans `input.json` : `touch_down`, `touch_x`, `touch_y` aux sources `touch:down`, `touch:x`, `touch:y`, et de même `…2`, `…3`, `…4` pour les doigts suivants (voir `docs/PLATFORM.md`) ; (3) placez les contrôles dans une scène, ancrés aux coins avec `ui.Anchor`. Ils sont invisibles sur un ordinateur jusqu'au premier doigt, tout de suite visibles sur un téléphone ; `opacity` règle leur transparence. Une touche liée à la même action n'est pas annulée tant qu'aucun doigt ne tient le contrôle. Dans un scénario, les doigts s'écrivent comme des actions (`touch_down`, `touch_x`, `touch_y`) : voir `games/doodle_jump/scenarios/touch_stick_steers.json`. Modèles : le joystick et le bouton de tir de `games/doodle_jump`, le joystick de `games/treasure_run`.
