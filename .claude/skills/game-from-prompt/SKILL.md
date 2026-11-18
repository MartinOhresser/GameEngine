---
name: game-from-prompt
description: Transforme une idée de jeu en jeu JSON validé et testé sans fenêtre, en créant un module seulement si le catalogue ne suffit pas. À utiliser quand l'utilisateur décrit un jeu à créer ou à modifier.
---

# Du prompt au jeu

Un jeu de ce dépôt est **uniquement des fichiers JSON** dans `games/<nom>/` : tout y est entité (joueur, décors, interface, plus tard cinématiques et histoire). Tu n'écris jamais de C++ pour un jeu. Si le catalogue de modules ne permet pas ce qu'il faut, tu écris un **module** (skill `add-module`), puis le jeu l'utilise.

## Déroulé

1. **Cadrer** (3 questions au plus, seulement si la réponse change le jeu). Défauts : 2D, solo, bureau, clavier et souris. Réseau, 3D, manette, tactile, fenêtre et rendu ne sont **pas encore disponibles** : dis-le, et propose la version jouable aujourd'hui (le jeu tourne sans fenêtre, ses règles se vérifient par scénarios). N'attends pas la réponse pour avancer si une valeur par défaut convient.
2. **Construire les outils** si `build/dev` n'existe pas : `cmake --preset dev && cmake --build --preset dev`.
3. **Lire le catalogue** : `./build/dev/tools/gectl catalog --compact` (skill `engine-catalog`). Tu ne cites que des modules, composants, champs et réglages qui y figurent.
4. **Découper l'idée en comportements** (« le joueur se déplace », « une pièce disparaît au contact », « un score ») et associe chacun à des composants du catalogue. Liste ce qui manque.
5. **Combler les manques** : un module par besoin cohérent (skill `add-module`), sur sa propre branche et PR. Préfère étendre un module existant d'un petit ajout plutôt que d'en créer un presque identique.
6. **Écrire le jeu** dans `games/<nom>/` : `game.json`, `prefabs/`, `scenes/` (skill `add-level`), `scenarios/` (skill `playtest-headless`). Prends `games/pilot` (entrées) et `games/sparks` (données seules) comme modèles.
7. **Valider** : `./build/dev/tools/gectl validate games/<nom>/game.json`. Corrige jusqu'à zéro erreur ; les messages disent le fichier, l'endroit et souvent la correction.
8. **Jouer sans fenêtre** : `./build/dev/runner/gerun games/<nom>/game.json --test`. Un scénario par règle annoncée. Pour regarder l'état final : `gerun games/<nom>/game.json --ticks 300`.
9. **Contrôler** : `./scripts/check.sh` doit passer (il valide aussi tous les jeux et leurs scénarios), puis branche, commit (titre « Conventional Commits »), PR.
10. **Rendre compte** en peu de mots : ce que le jeu fait, ce qui est vérifié par quel scénario, ce qui n'existe pas encore (fenêtre, rendu), et ce que l'utilisateur peut changer lui-même dans les JSON.

## Règles

- Données seulement : nombres, booléens, noms. Pas de logique dans le JSON. Une règle de jeu est un composant et un système d'un module.
- Noms en `lower_snake_case` ; composants écrits `module.Composant` ; chaque fichier commence par `"schema_version": 1` ; commentaires `//` et virgules finales permis, et bienvenus pour expliquer un choix.
- Plusieurs petits fichiers plutôt qu'un gros : un préfab par type d'entité, une scène par niveau. Les variantes passent par `extends` et ne redisent que ce qui change.
- Valeurs d'équilibrage (vitesses, délais) : en paramètres de module (`params` du manifeste) ou en champs de préfab, jamais répétées.
- Tout ce que tu annonces comme règle du jeu est couvert par une vérification de scénario. Pas de scénario, pas de règle livrée.
- Ne contourne pas une erreur de validation en retirant ce qui la cause sans comprendre : elle indique souvent un module oublié dans `modules` ou une dépendance manquante.

## Limites actuelles (à dire à l'utilisateur quand elles comptent)

- Pas de fenêtre ni de rendu (jalons M6, M7) ni d'interface (M8) : on valide le comportement, pas l'apparence.
- Une entité ne peut pas en désigner une autre dans les fichiers, et on ne change pas de scène en cours de partie : ces besoins se règlent par un module.
- Un composant ne porte que des nombres et des booléens (pas de texte).
- Les actions d'entrée sont des noms convenus (`move_x`, `move_y` pour `action_control`) ; les liaisons touches/boutons viendront avec la plateforme.
