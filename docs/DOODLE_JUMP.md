# Doodle Jump : un jeu de saut sans fin, fait de données

`games/doodle_jump` reproduit les mécaniques de Doodle Jump (l'esthétique est sommaire) **sans une ligne de C++ propre au jeu** : tout est dans `game.json`, les préfabs et les scènes. Pour que ce soit possible, le moteur a gagné des modules génériques (`gravity2d`, `bounce2d`, `patrol2d`, `spawner2d`, `combat2d`, `progress`, `records`), réutilisables pour tout jeu de plateformes ou de montée (un jeu de plateformes classique, un jeu de tir vertical, un « endless runner »).

```
geplay games/doodle_jump/game.json
```

Flèches ou A / D : bouger. Espace ou Haut : tirer. Entrée ou clic : choisir dans les menus. R : rejouer tout de suite après une défaite.

## Les mécaniques, et ce qui les réalise

| Dans le jeu | Dans les fichiers |
| --- | --- |
| Le sauteur retombe | `gravity2d.Gravity` du sauteur (1400 unités/s², vitesse de chute plafonnée à 1800) |
| Il rebondit tout seul sur une plateforme où il atterrit, la traverse par en dessous et par le côté | `bounce2d.Bouncer` (sauteur) et `bounce2d.Bouncy` (surface, avec sa force de rebond) |
| Plateforme verte, bleue qui glisse, blanche qui disparaît après un rebond, marron qui cède | préfabs `platform`, `platform_moving` (`patrol2d.Patrol`), `platform_vanishing` (`vanish`), `platform_breakable` (`breaks`) |
| Ressort : un rebond bien plus haut | préfab `spring` (force 1250 contre 760) |
| Monstre : tue au contact, meurt si on lui saute dessus (et on rebondit très haut) ou si on lui tire dessus | préfabs `monster` et `monster_flying` : `Bouncy` avec `vanish`, `health.Health` (une vie), `combat2d.ContactDamage` sur la couche du joueur, couche `monster` |
| Tir vers le haut | `spawner2d.Shooter` du sauteur (action `shoot`), préfab `bullet` (`combat2d.Projectile`) |
| Sortir par un côté ramène de l'autre | `screen_wrap.WrapArea` avec `wrap_y` éteint |
| L'écran ne fait que monter | `camera2d.CameraFollow` avec `only_up`, `follow_x` éteint et `offset_y` |
| Le monde se construit à l'infini, de plus en plus dur | trois `spawner2d.Generator` (plateformes, plateformes cassables, monstres) ; `bounds2d.OutOfView` retire ce qui est tombé sous l'écran |
| Le score est la hauteur | `progress.Progress` (`scale` 0,5) |
| Tomber sous l'écran ou toucher un monstre = perdu | `health.Health` (une vie, action `died`, le joueur reste à l'écran) blessé par le `ContactDamage` des monstres, et `bounds2d.OutOfView` (sortie par le bas, action `died`) puis `scene_switch.GoToScene` vers `gameover` |
| Meilleur score, gardé d'une partie à l'autre | `records.Record` (entité `scoreboard`, persistante) ; sauvegardé sur le disque par `geplay` |
| Menu, partie, fin de partie, « Rejouer » | scènes `menu`, `game`, `gameover` ; `GoToScene` avec une action de bouton |

## L'axe y descend

Comme à l'écran, **y augmente vers le bas** : la gravité est positive, monter c'est diminuer y, et la « hauteur » d'une entité est `départ - y`. Tous les modules ci-dessus suivent cette convention.

## Régler la difficulté (sans toucher au C++)

- La forme du saut : `gravity2d.Gravity.y` (player.json) et `bounce2d.Bouncy.strength` (platform.json). La hauteur d'un saut est `force² / (2 × gravité)` : 760 et 1400 donnent 206 unités.
- **L'écart maximal entre deux plateformes doit rester sous la hauteur du saut**, sinon la partie devient impossible : c'est `gap_max_end` du générateur `platforms` (scenes/game.json, 170 pour un saut de 206).
- La difficulté monte sur `ramp_distance` unités de distance parcourue vers le haut (12000) : chaque réglage `*_end` (écarts, chance des plateformes mobiles et blanches, chance des ressorts, fréquence des monstres) est atteint à cette hauteur. Un `*_end` à -1 veut dire « ne change pas ».
- Les monstres n'apparaissent qu'à partir de `from_distance` (1800).
- Le score : `progress.Progress.scale` (player.json).

## Le banc d'essai

`scenes/test_*.json` sont de toutes petites scènes que seuls les scénarios jouent : un ressort, une plateforme cassable, un monstre… avec les vrais préfabs. Elles permettent de vérifier chaque règle sans attendre que le hasard produise la situation. Elles sont générées avec le reste par `tools/make_assets.py` ; chaque règle annoncée ci-dessus a son scénario dans `scenarios/`.

## La sauvegarde

Le meilleur score est enregistré sous le nom `best_height` (liste `record` du manifeste). `geplay` le lit au démarrage et le réécrit quand il change, dans le dossier de données de l'utilisateur (`~/.local/share/ge/doodle_jump/save.txt` sous Linux), ou dans le fichier donné par `--save`. Avec `--headless`, rien n'est écrit sauf si `--save` est donné. Les scénarios partent d'une sauvegarde vide.

## Ce qui n'existe pas encore

- L'inclinaison du téléphone (le tactile et les capteurs ne sont pas dans le moteur) et la visée à la souris ; le tir part toujours vers le haut.
- Les objets du vrai jeu qui changent le saut en cours de route (hélice, jetpack, trampoline, bouclier) : ils se feraient avec un composant « Boost » dans un module, sans changer ce qui existe.
- Une animation de mort : à la mort, l'écran de fin arrive tout de suite.
- Aucune mesure de performance : il n'y a pas de GPU dans l'environnement où ce jeu a été fabriqué (voir l'ADR 0008).
