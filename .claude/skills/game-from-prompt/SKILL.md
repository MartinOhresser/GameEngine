---
name: game-from-prompt
description: Transforme une idée de jeu en jeu JSON validé et testé sans fenêtre, en créant un module seulement si le catalogue ne suffit pas. À utiliser quand l'utilisateur décrit un jeu à créer ou à modifier.
---

# Du prompt au jeu

Un jeu de ce dépôt est **uniquement des fichiers JSON** dans `games/<nom>/` : tout y est entité (joueur, décors, interface, plus tard cinématiques et histoire). Tu n'écris jamais de C++ pour un jeu. Si le catalogue de modules ne permet pas ce qu'il faut, tu écris un **module** (skill `add-module`), puis le jeu l'utilise.

## Déroulé

1. **Cadrer** (3 questions au plus, seulement si la réponse change le jeu). Défauts : 2D, solo, bureau, clavier et souris. Réseau, 3D, manette, tactile et vrai rendu (sprites, texte) ne sont **pas encore disponibles** : dis-le, et propose la version jouable aujourd'hui (le jeu se joue dans une fenêtre avec des carrés colorés, et ses règles se vérifient par scénarios). N'attends pas la réponse pour avancer si une valeur par défaut convient.
2. **Construire les outils** si `build/dev` n'existe pas : `cmake --preset dev && cmake --build --preset dev`.
3. **Lire le catalogue** : `./build/dev/tools/gectl catalog --compact` (skill `engine-catalog`). Tu ne cites que des modules, composants, champs et réglages qui y figurent.
4. **Découper l'idée en comportements** (« le joueur se déplace », « une pièce disparaît au contact », « un score ») et associe chacun à des composants du catalogue. Liste ce qui manque.
5. **Combler les manques** : un module par besoin cohérent (skill `add-module` : issue « Module gap », puis sa propre branche et sa PR vers `dev`). Préfère étendre un module existant d'un petit ajout plutôt que d'en créer un presque identique.
6. **Écrire le jeu** dans `games/<nom>/` : `game.json`, `prefabs/`, `scenes/` (skill `add-level`), `scenarios/` (skill `playtest-headless`). Prends `games/move_sprite` (entrées clavier, dessin), `games/pilot` (entrées) et `games/sparks` (données seules) comme modèles. Un jeu jouable au clavier déclare `"input": "input.json"` (touches → actions, voir `docs/PLATFORM.md`) et se voit soit par des carrés colorés (`debug_draw`, `Square`), soit par des sprites : `games/treasure_run` (atlas PNG dans `atlases`, `sprite2d.Sprite` avec `"atlas": "nom"`, `camera2d` pour suivre le héros, `collision2d` pour les murs, `text2d` pour le titre en pixels d'écran avec `screen: true`). Voir `docs/RENDERING.md`. Pour un jeu de sauts, de plateformes ou de montée sans fin (gravité, rebonds, monde généré, mort, score, record) : `games/doodle_jump` et `docs/DOODLE_JUMP.md`.
7. **Valider** : `./build/dev/tools/gectl validate games/<nom>/game.json`. Corrige jusqu'à zéro erreur ; les messages disent le fichier, l'endroit et souvent la correction.
8. **Jouer sans fenêtre** : `./build/dev/runner/gerun games/<nom>/game.json --test`. Un scénario par règle annoncée. Pour regarder l'état final : `gerun games/<nom>/game.json --ticks 300`. Pour voir le jeu (si `-DGE_BUILD_PLATFORM=ON`, préréglage `platform`) : `geplay games/<nom>/game.json`, ou `geplay games/<nom>/game.json --headless --frames 60 --screenshot image.bmp` pour une capture sans écran.
9. **Contrôler** : `./scripts/check.sh` doit passer (il valide aussi tous les jeux et leurs scénarios), puis branche, commit (titre « Conventional Commits »), PR.
10. **Exporter (seulement si l'utilisateur veut envoyer le jeu à quelqu'un)** : `gectl export games/<nom>/game.json` fabrique un zip autonome pour Windows (`--target windows`, `--out DIR`), après avoir rejoué les scénarios ; sous Windows, `scripts/export.cmd games/<nom>/game.json` compile `gectl` si besoin. Le jeu a un `title`, une `version` et, si possible, une `icon` (PNG d'au plus 256 × 256) dans `game.json`. L'export se fait aussi depuis le lanceur (`gelauncher`, bouton Export). Tu ne peux pas exécuter Windows : dis que la vérification du zip Windows se fait par la CI (job « Export »). Détails : `docs/EXPORT.md`.
11. **Rendre compte** en peu de mots : ce que le jeu fait, ce qui est vérifié par quel scénario, ce qui n'existe pas encore (fenêtre, rendu), et ce que l'utilisateur peut changer lui-même dans les JSON.

## Règles

- Données seulement : nombres, booléens, noms. Pas de logique dans le JSON. Une règle de jeu est un composant et un système d'un module.
- Noms en `lower_snake_case` ; composants écrits `module.Composant` ; chaque fichier commence par `"schema_version": 1` ; commentaires `//` et virgules finales permis, et bienvenus pour expliquer un choix.
- Plusieurs petits fichiers plutôt qu'un gros : un préfab par type d'entité, une scène par niveau. Les variantes passent par `extends` et ne redisent que ce qui change.
- Valeurs d'équilibrage (vitesses, délais) : en paramètres de module (`params` du manifeste) ou en champs de préfab, jamais répétées.
- Tout ce que tu annonces comme règle du jeu est couvert par une vérification de scénario. Pas de scénario, pas de règle livrée.
- Ne contourne pas une erreur de validation en retirant ce qui la cause sans comprendre : elle indique souvent un module oublié dans `modules` ou une dépendance manquante.

## Limites actuelles (à dire à l'utilisateur quand elles comptent)

- Le dessin offre des carrés (`debug_draw`), des sprites animés (`sprite2d`) et une caméra (`camera2d`) ; du texte par langue (`text2d`, fichiers `strings`, polices en atlas : `scripts/make_font.py`) et des changements de scène (`scene_switch`) ; l'interface avec `ui` (HUD ancré à la fenêtre, barres liées à une valeur du jeu, menus à boutons) et `inventory` (conteneurs, cases avec leurs règles, objets) : lis `docs/UI.md` et prends `games/treasure_run` comme modèle ; chaque menu est une scène, et ce que fait un bouton se règle par `GoToScene`. Les noms propres au jeu (couches, catégories) se déclarent dans `lists` de `game.json` (voir `docs/MANIFEST.md`). Tu ne peux pas peindre une image : utilise des atlas existants ou génère-les avec un script comme `games/treasure_run/tools/make_assets.py`.
- La physique de saut et le monde généré existent : gravité (`gravity2d`), atterrissage à sens unique sur plateformes, ressorts et monstres (`bounce2d`), allers-retours (`patrol2d`), génération de rangées devant la caméra avec difficulté croissante, tir de préfabs (`spawner2d`), sortie de l'écran (`bounds2d`), points de vie (`health`), dégâts de contact et projectiles (`combat2d`), score de hauteur (`progress`), meilleur score sauvegardé (`records`). **L'axe y descend** (gravité positive, monter = diminuer y). Lis `docs/DOODLE_JUMP.md`.
- Le saut commandé d'un plateformer existe : `jump.Jumper` (au sol, délai de grâce après le bord, appui mémorisé, hauteur selon la durée d'appui, sauts en l'air) lit `collision2d.Blocked` (de quel côté un mur arrête le corps : `below` = au sol) ; `action_control.Controlled.acceleration`/`braking` donnent un départ et un arrêt progressifs ; `camera2d.CameraFollow` a une zone morte, une anticipation et des limites de niveau. Exemple : `games/lueur`.
- Les jeux de stratégie existent : chemins et suiveurs (`path2d`), tourelles à visée automatique (`autofire`), vagues d'ennemis (`waves`), or, primes, achat, amélioration et vente sur une grille (`economy`). Lis `docs/GAME_TOWER_DEFENSE.md`.
- Une entité ne peut pas en désigner une autre dans les fichiers ; une règle qui en a besoin se règle par un module. Créer une entité en cours de partie se fait par un préfab (liste `prefab`, `spawner2d`) et changer de scène par `scene_switch`.
- Un nombre garde d'une partie à l'autre (`SaveData`) est un entier ; il s'écrit sur le disque avec `geplay` seulement (voir `docs/PLATFORM.md`).
- Un composant ne porte que des nombres et des booléens (pas de texte).
- Les actions d'entrée sont des noms convenus (`move_x`, `move_y` pour `action_control`) ; `input.json` les lie aux touches (`key:`) et à la souris (`mouse:`) ; manette et tactile viendront plus tard sans changer les jeux.
