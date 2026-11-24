# Les jeux du dépôt, et ce qu'ils montrent

Avant d'écrire un jeu, cherche ici celui qui fait déjà ce dont tu as besoin : il montre comment les modules se combinent, et sa doc dit ce qui réalise chaque chose. Ce qu'offrent les modules eux-mêmes se lit dans le catalogue (`gectl catalog`), jamais dans une liste figée.

## Par besoin

| Besoin | Jeu de référence | À lire |
| --- | --- | --- |
| Un jeu minimal fait de données seules | `games/sparks` | [MANIFEST.md](MANIFEST.md) |
| Bouger au clavier, voir des carrés | `games/move_sprite`, `games/pilot` | [PLATFORM.md](PLATFORM.md) |
| Ramasser des objets, un score | `games/coin_dash` | [AUTHORING.md](AUTHORING.md) |
| Sprites, caméra qui suit, murs, HUD, menus, inventaire, texte par langue | `games/treasure_run` | [RENDERING.md](RENDERING.md), [UI.md](UI.md) |
| Gravité, rebonds, monde généré sans fin, mort, meilleur score sauvegardé | `games/doodle_jump` | [DOODLE_JUMP.md](DOODLE_JUMP.md) |
| Le même, de côté (défilement horizontal) | `games/sideways_jump` | [SIDEWAYS_JUMP.md](SIDEWAYS_JUMP.md) |
| Une grille, des tuiles qui glissent, des gestes au doigt | `games/game_2048` | [GAME_2048.md](GAME_2048.md) |
| Un jeu de plateau à tours : pièces, coups légaux, échec, promotion | `games/chess` | [GAME_CHESS.md](GAME_CHESS.md) |
| Stratégie : chemins, tourelles, vagues, économie | `games/tower_defense` | [GAME_TOWER_DEFENSE.md](GAME_TOWER_DEFENSE.md) |
| **Un jeu beau** : plans en parallaxe, lumière, particules, effets d'écran, animation selon l'état | `games/lueur` | [GAME_LUEUR.md](GAME_LUEUR.md), [RENDERING.md](RENDERING.md) |
| Un plateformer : saut réglable, caisse à pousser, pièges, niveaux qui recommencent | `games/lueur` | [GAME_LUEUR.md](GAME_LUEUR.md) |
| Une cinématique : bandes noires, personnage figé, phases, enchaînement de scènes | `games/lueur` (scènes `prologue`, `ending`) | [GAME_LUEUR.md](GAME_LUEUR.md) |
| Jouer au doigt sur un téléphone : boutons et joystick à l'écran, ou un geste de glisser | `games/doodle_jump`, `games/treasure_run` (boutons, joystick), `games/game_2048` (glisser) | [UI.md](UI.md), [TESTING.md](TESTING.md) (`"screen"`) |
| Exporter un jeu (Windows, Android) | n'importe lequel | [EXPORT.md](EXPORT.md), [EXPORT_ANDROID.md](EXPORT_ANDROID.md) |

## Les images

Un jeu n'embarque pas d'image qu'on ne puisse refaire : ceux qui en ont les dessinent par un script (`games/<jeu>/tools/make_assets.py`, en Python). `games/lueur/tools/make_assets.py` écrit aussi les scènes à partir d'une courte description des niveaux. C'est le modèle à suivre pour un jeu qui a beaucoup d'éléments.

## Le lanceur

`tools/launcher` est lui-même un jeu JSON : il liste les jeux du dépôt et les lance ([LAUNCHER.md](LAUNCHER.md)).
