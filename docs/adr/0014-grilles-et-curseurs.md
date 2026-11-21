# ADR 0014 : une seule grille et un seul curseur de cases

Statut : accepté (option A, choisie par Martin le 21 novembre 2026), en cours de réalisation.

## Contexte

L'audit de généricité des modules (novembre 2026) a relevé deux doublons que Martin avait vus de lui-même : « l'inventaire de treasure run et l'échiquier, ou les tuiles en général, semblent faire doublon ».

**Deux grilles qui font le même travail :**

| | `ui.Grid` + `ui.Cell` | `grid2d.Grid` + `grid2d.GridPos` |
| --- | --- | --- |
| Adresse d'une case | un numéro (`index`), colonne `n % columns`, ligne `n / columns` | `col`, `row` |
| Origine | la **première case** est centrée sur la Position de la grille | le **plateau entier** est centré sur la Position |
| Espace | l'écran (pixels, avec `ui.Anchor`) | le monde (unités du monde, à travers la caméra) |
| Espacement | `spacing_x`, `spacing_y` | aucun |
| Nombre de lignes | implicite (le nombre de cases) | `rows`, explicite |
| Ce qui s'appuie dessus | rien (simple placement) | `GridCursor`, `SpawnOnEmpty`, `Zone`, `grid_moves`, `slide_merge`, `economy` |
| Jeux | treasure_run (barre d'objets), doodle_jump (boutons des menus) | chess, game_2048, tower_defense |

**Trois curseurs qui font en partie le même travail :**

| | `ui.Menu` | `inventory.Hand` | `grid2d.GridCursor` |
| --- | --- | --- | --- |
| Sur quoi | des `Button` posés n'importe où, de tailles libres | les `Slot` d'un conteneur, rangés en `columns` | les cases d'un `grid2d.Grid` |
| Clavier / manette | oui (rangées déduites de la position à l'écran) | oui (`columns` cases par rangée) | **non** |
| Pointeur | oui (rectangle de chaque bouton) | oui (`slot_width` × `slot_height` autour de chaque slot) | oui (case sous le pointeur, à travers la caméra) |
| Jeux | les menus de tous les jeux, le lanceur | treasure_run | chess, tower_defense |

`inventory.Hand` refait donc, pour un seul jeu, ce que `GridCursor` fait pour les plateaux (case sous le pointeur) et ce que `Menu` fait au clavier (déplacement d'une case à l'autre). Un jeu de plateau joué à la manette (un échiquier sur console, un démineur) n'a aujourd'hui aucun curseur qui convienne, et un inventaire posé dans le monde (un coffre ouvert sur la carte) non plus.

`ui.Menu` est différent : il parcourt des boutons **posés librement**, de tailles différentes, pas des cases. Il n'est pas un doublon et reste tel quel dans toutes les options.

**L'obstacle** : une grille à l'écran doit être placée **après** `ui.anchor`, qui met la grille à sa place dans la fenêtre. Mais le moteur refuse un ordre « après » qui vise un système d'un module absent (`grid2d.place` après `ui.anchor` obligerait chaque jeu de plateau à charger `ui`), et `ui` ne peut pas non plus dépendre de `grid2d` et de `camera2d` pour se placer avant.

## Options

### A. Unification complète (recommandée)

1. **`ui.Anchor` devient `transform2d.Anchor`** (système `transform2d.anchor`). Placer une entité par rapport aux bords de la fenêtre concerne la position, pas l'interface ; `transform2d` est déjà une dépendance de tout le monde. `grid2d.place` se range alors après `transform2d.anchor` sans dépendre de `ui`. Changement de nom seulement dans les jeux (tous l'utilisent : chess, doodle_jump, game_2048, tower_defense, treasure_run, le lanceur).
2. **`grid2d.Grid` apprend l'écran et l'espacement** : `spacing_x`, `spacing_y` et `screen` (la grille est en pixels de la fenêtre : elle se place avec un `Anchor`, et le pointeur n'y passe pas par la caméra). `ui.Grid` et `ui.Cell` disparaissent : une case de barre d'objets ou un bouton de menu rangé en tableau prend un `grid2d.GridPos`.
3. **`grid2d.GridCursor` apprend le clavier et la manette** : `left`, `right`, `up`, `down` (d'une case à la voisine, avec `wrap`) et `accept` (agit comme un clic sur la case courante). Un échiquier se joue alors à la manette sans rien d'autre.
4. **`inventory.Hand` s'appuie sur le `GridCursor` de sa propre entité** (comme `economy.Placer`) : le slot sous le curseur est celui dont le `GridPos` est sur cette case. Hand garde ce qui est propre à un inventaire (prendre, poser, annuler, ses images) et perd sa navigation, `columns`, `use_pointer`, `slot_width`, `slot_height`. Un inventaire peut ainsi être à l'écran (barre d'objets) ou dans le monde (coffre sur la carte).

Ce qui casse : `ui.Anchor` → `transform2d.Anchor` partout (renommage mécanique) ; treasure_run (barre d'objets et Hand) et doodle_jump (menus) refont leur mise en page (la position d'une grille change d'origine : première case → centre ; leurs `make_assets.py` la recalculent) ; docs `UI.md`, `INVENTORY`, skills.

Résultat : **une grille** (`grid2d`) pour les plateaux, les inventaires, les barres d'objets et les tableaux de boutons ; **un curseur de cases** (`GridCursor`) au pointeur, au clavier et à la manette ; **un curseur de menu** (`ui.Menu`) pour les boutons posés librement.

### B. Consolidation partielle

`GridCursor` reçoit seulement la navigation au clavier et à la manette (point 3 de A). `ui.Grid`/`ui.Cell` et `inventory.Hand` restent ; la logique « déplacer le focus d'une case à la voisine » est mise dans un en-tête commun que `Hand` et `GridCursor` partagent.

Ce qui casse : rien. Ce qui reste : deux grilles, et `Hand` qui ne sait toujours servir qu'un inventaire à l'écran.

### C. Ne rien changer, documenter

`docs/MODULES.md` dit quand prendre quelle grille et quel curseur.

## Décision

À prendre par Martin. Recommandation : **A**, faite en trois PR qui passent chacune la CI :

1. `ui.Anchor` devient `transform2d.Anchor` (renommage seul, tous les jeux) ;
2. `grid2d` : écran, espacement, navigation au clavier et à la manette du `GridCursor` (ajouts seuls, rien ne casse) ;
3. retrait de `ui.Grid`/`ui.Cell`, `inventory.Hand` sur `GridCursor`, migration de treasure_run et doodle_jump, docs et skills.

Les positions des boutons et des cases ne doivent pas bouger à l'écran : les scénarios existants (dont ceux joués sur un écran de téléphone) et les touchers de l'émulateur Android le vérifient.
