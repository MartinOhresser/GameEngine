# Échecs : un jeu de plateau à deux, fait de données

`games/chess` est un jeu d'échecs complet pour deux joueurs sur un même écran, **sans une ligne de C++ propre au jeu**. Toutes les règles sont des entités des scènes : la façon de se déplacer de chaque pièce, le roi à protéger, les cases de promotion, les écrans de fin de partie. Il éprouve que `turns`, `grid_moves`, `grid2d`, `phase` et `rules` se composent pour faire un jeu de plateau entier.

```
geplay games/chess/game.json
```

On touche (ou on clique) une pièce du camp qui a la main : les cases où elle peut aller s'allument (un point pour une case libre, un anneau pour une prise). Une seconde touche sur une case allumée joue le coup ; une touche ailleurs relâche la pièce. R ou N, ou le bouton : nouvelle partie. Chaque camp peut abandonner.

## Les règles, et ce qui les réalise

| Dans le jeu | Dans les fichiers |
| --- | --- |
| Un plateau de 8 × 8, les blancs en bas, toujours en entier à l'écran | `grid2d.Grid`, `camera2d.CameraFit` |
| Chaque pièce se déplace à sa façon | une entité `grid_moves.Pattern` par direction (cavalier : huit sauts ; fou, tour, dame : glisseurs ; roi : portée 1) |
| Un pion avance sans prendre, prend en diagonale, fait deux pas la première fois | `Pattern` `mode` (déplacement seul / prise seule), `min_range`, `first_move_only`, `player` (le blanc monte, le noir descend) |
| Prise en passant | `Pattern` avec `victim_kind` pion, `victim_dy` et `victim_min_distance` 2 (le pion pris vient d'avancer de deux cases) |
| Roque des deux côtés, refusé si le roi traverse une case menacée ou si la tour a bougé | `Pattern` du roi avec `companion_*` (la tour saute) et `safe_path` ; `first_move_only` |
| Un coup qui laisse son roi en échec n'est pas offert ; un clouage empêche de bouger | `grid_moves.Guard` sur les rois |
| Échec, mat, pat | `grid_moves.Side.status` (0 libre, 1 échec, 2 pat, 3 mat), lu par des `rules.Watch` |
| « Échec ! » affiché tant qu'il dure | `rules.OnAction` sur l'alpha d'un texte |
| Le tour des blancs, puis des noirs | `turns.Turn`, `Team` ; le coup passe le tour (`MoveRequest.ends_turn`) ; un carré blanc ou noir (`sprite2d.FrameBind`) dit qui joue |
| Un pion qui arrive au bout : choix entre dame, tour, fou et cavalier | `grid2d.Zone` (une par bout) → action → phase `promotion` ; quatre boutons dont chacun fait un `rules.OnAction` avec `link` : il change le genre de la pièce que la zone a vue |
| Mat ou pat : l'écran de fin | `phase.Transition` vers `white_won`, `black_won`, `draw` ; `phase.ShowIn` |
| Le plateau ne répond que pendant la partie | `phase.ActiveIn` |
| Nouvelle partie à tout moment | `scene_switch.GoToScene` sur `new_game` |

## Les images et les scènes sont générées

```
python3 games/chess/tools/make_assets.py
```

écrit les douze pièces (32 pixels, dessinées par formes dans le script : aucun binaire qu'on ne pourrait relire), les images de l'interface, les préfabs, les scènes, les scénarios et `game.json`. La police est celle de `doodle_jump`.

## Les scénarios

`gerun games/chess/game.json --test` joue vingt-trois scénarios, tous par des touches à l'écran (on touche la case de la pièce, puis celle d'arrivée) : la position de départ, le pas du pion (un ou deux), le camp qui a la main, les coups refusés, un clouage, un échec qu'il faut parer, le mat du fou, le mat du berger, le pat, le roque des deux côtés (et refusé : case menacée, tour déjà bougée), la prise en passant (et pas plus tard), la promotion en chacune des quatre pièces (blancs et noirs, et une promotion qui fait mat), l'abandon, et une nouvelle partie (bouton et touche). Les `test_*` sont la vraie scène avec peu de pièces posées à la main.
