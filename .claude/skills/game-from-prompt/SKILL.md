---
name: game-from-prompt
description: Transforme une idée de jeu en jeu JSON validé et testé sans fenêtre, en créant un module seulement si le catalogue ne suffit pas. À utiliser quand l'utilisateur décrit un jeu à créer ou à modifier.
---

# Du prompt au jeu

Un jeu de ce dépôt est **uniquement des fichiers JSON** dans `games/<nom>/` : tout y est entité (joueur, décors, interface, cinématiques). Tu n'écris jamais de C++ pour un jeu. Si le catalogue de modules ne permet pas ce qu'il faut, tu écris un **module** générique (skill `add-module`), puis le jeu l'utilise.

## Déroulé

1. **Cadrer** (3 questions au plus, seulement si la réponse change le jeu). Défauts : 2D, solo, clavier et souris, une fenêtre ; le tactile vient en plus si le jeu doit tourner sur téléphone. Réseau, 3D et manette n'existent **pas encore** : dis-le, et propose la version jouable aujourd'hui. N'attends pas la réponse pour avancer si une valeur par défaut convient.
2. **Construire les outils** si `build/dev` n'existe pas : `cmake --preset dev && cmake --build --preset dev`.
3. **Lire le catalogue** : `./build/dev/tools/gectl catalog --compact` (skill `engine-catalog`). Tu ne cites que des modules, composants, champs et réglages qui y figurent.
4. **Trouver le jeu de référence** dans `docs/GAMES.md` (besoin → jeu du dépôt → doc), et lis-le : il montre comment les modules se combinent pour ce genre de jeu.
5. **Découper l'idée en comportements** (« le joueur se déplace », « une pièce disparaît au contact », « un score ») et associe chacun à des composants du catalogue. Liste ce qui manque.
6. **Combler les manques** : un module par besoin cohérent (skill `add-module`), sur sa propre branche et dans sa PR vers `dev`. Préfère étendre un module existant d'un petit ajout plutôt que d'en créer un presque identique.
7. **Écrire le jeu** dans `games/<nom>/` : `game.json`, `prefabs/`, `scenes/` (skill `add-level`), `scenarios/` (skill `playtest-headless`), `input.json` (touches, souris, doigts → actions, voir `docs/PLATFORM.md`). Les images se dessinent par un script Python (comme `games/lueur/tools/make_assets.py`) : tu ne peux pas peindre une image à la main. Quand un jeu a beaucoup d'éléments (niveaux, décors), le script écrit aussi les scènes à partir d'une courte description.
8. **Valider** : `./build/dev/tools/gectl validate games/<nom>/game.json`. Corrige jusqu'à zéro erreur ; les messages disent le fichier, l'endroit et souvent la correction.
9. **Jouer sans fenêtre** : `./build/dev/runner/gerun games/<nom>/game.json --test`. Un scénario par règle annoncée. Pour regarder l'état final : `gerun games/<nom>/game.json --ticks 300`. Pour voir le jeu (préréglage `platform`, avec SDL) : `geplay games/<nom>/game.json`, ou `geplay games/<nom>/game.json --headless --frames 60 --screenshot image.bmp` pour une capture sans écran. Regarde la capture : un jeu beau se juge à l'œil.
10. **Contrôler** : `TIDY=changed ./scripts/check.sh` doit passer (il valide aussi tous les jeux et leurs scénarios), puis branche, commit (titre « Conventional Commits »), PR vers `dev`.
11. **Exporter (seulement si l'utilisateur veut envoyer le jeu à quelqu'un)** : `gectl export games/<nom>/game.json` fabrique un zip autonome pour Windows (`--target windows`, `--out DIR`), ou un APK (`--target android`), après avoir rejoué les scénarios. Sous Windows, `scripts/export.cmd games/<nom>/game.json` compile `gectl` si besoin. Le jeu a un `title`, une `version` et, si possible, une `icon` (PNG d'au plus 256 × 256) dans `game.json`. Le programme exporté se lance seul et accepte `--headless --frames N --screenshot` : vérifie qu'il joue. Détails : `docs/EXPORT.md`, `docs/EXPORT_ANDROID.md`.
12. **Rendre compte** en peu de mots : ce que le jeu fait, ce qui est vérifié par quel scénario, ce que tu n'as pas pu vérifier, et ce que l'utilisateur peut changer lui-même dans les JSON.

## Faire un jeu beau

`games/lueur` (`docs/GAME_LUEUR.md`) est le modèle ; `docs/RENDERING.md` explique chaque outil. Dans l'ordre où ils comptent :

1. **Des plans en profondeur** : plusieurs bandes de décor (`sprite2d.Parallax`, `repeat_x`), plus claires et plus floues à mesure qu'elles s'éloignent ; du brouillard entre elles ; un premier plan sombre devant l'action.
2. **Une palette** : peu de couleurs, un contraste fort entre ce qui se joue (silhouettes) et le fond (lumière).
3. **La lumière** : `light2d.Ambient` assombrit la scène, `light2d.Light` éclaire autour d'un personnage ou d'un objet ; un sprite `unlit` garde ses couleurs (ciel, plans lointains), un sprite `light` éclaire sans être dessiné (rai de soleil). Sur un fond clair, une lumière ajoutée (`additive`) se voit mal : donne-lui plutôt ses propres couleurs.
4. **Le mouvement** : particules (`particles2d.Emitter` : lucioles, feuilles, étincelles, poussière), caméra qui suit en douceur (`camera2d.CameraFollow` : zone morte, anticipation, limites du niveau), tremblement (`camera2d.CameraShake`).
5. **Les effets d'écran** : `postfx.Effects` (halo, vignettage, grain, teinte et ombres relevées), et les bandes de cinéma `letterbox` pour une cinématique.
6. **Des personnages vivants** : l'animation selon ce qu'ils font (`sprite2d.Animator` et des `sprite2d.AnimationClip` : une condition sur un nombre, une priorité), l'image tournée vers où ils vont (`sprite2d.Facing`), un compagnon qui suit (`follow2d.Follow`).
7. **Une histoire** : une scène d'ouverture et une de fin, un personnage figé pendant une cinématique (`phase.ActiveIn` : un personnage `Disabled` n'écoute plus ses commandes), un `tween2d.Tween` qui fait entrer ou sortir les bandes, un fondu au noir avant de recommencer.

## Règles

- Données seulement : nombres, booléens, noms. Pas de logique dans le JSON. Une règle de jeu est un composant et un système d'un module.
- Noms en `lower_snake_case` ; composants écrits `module.Composant` ; chaque fichier commence par `"schema_version": 1` ; commentaires `//` et virgules finales permis, et bienvenus pour expliquer un choix.
- Plusieurs petits fichiers plutôt qu'un gros : un préfab par type d'entité, une scène par niveau. Les variantes passent par `extends` et ne redisent que ce qui change.
- Valeurs d'équilibrage (vitesses, délais) : en paramètres de module (`params` du manifeste) ou en champs de préfab, jamais répétées.
- Tout ce que tu annonces comme règle du jeu est couvert par une vérification de scénario. Pas de scénario, pas de règle livrée.
- Ne contourne pas une erreur de validation en retirant ce qui la cause sans comprendre : elle indique souvent un module oublié dans `modules` ou une dépendance manquante.
- **L'axe y descend** : la gravité est positive, monter, c'est diminuer y.

## Ce qui existe, et ce qui n'existe pas encore

- Existe : dessin 2D (sprites animés, texte par langue, carrés de mise au point), caméra, collisions, sauts, caisses à pousser, pièges, monde généré, interface (HUD, menus, inventaire), jeux de plateau et de stratégie, sauvegarde d'un nombre d'une partie à l'autre, clavier, souris et tactile (cinq doigts, boutons et joystick à l'écran, gestes), export Windows et Android. Le détail est dans le catalogue ; les exemples sont dans `docs/GAMES.md`.
- N'existe pas encore : 3D, réseau, manette, son autre que des `.wav` joués tels quels, shaders. Dis-le quand ça compte.
- Un composant ne porte que des nombres et des booléens (pas de texte). Une entité en désigne une autre par une étiquette (`tag.Tag`) ou par son `Owner` ; créer une entité en cours de partie se fait par un préfab (liste `prefab`, `spawner2d`), changer de scène par `scene_switch`.
- Les actions d'entrée sont des noms (`move_x`, `jump`…) ; `input.json` les lie aux touches (`key:`), à la souris (`mouse:`) et aux doigts (`touch:`). Les noms propres au jeu (couches, phases, ensembles d'animations) se déclarent dans `lists` de `game.json` (voir `docs/MANIFEST.md`).
