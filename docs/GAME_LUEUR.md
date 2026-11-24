# Lueur : un plateformer d'ombres et de lumière, fait de données

`games/lueur` est le jeu de l'[ADR 0015](adr/0015-rendu-atmospherique.md) : un petit plateformer au style « silhouettes de Limbo, lumière d'Ori ». Un enfant traverse une forêt au crépuscule, suivi d'un petit esprit de lumière bleue. Comme les autres jeux du dépôt, il n'a **pas une ligne de C++ qui lui soit propre** : tout est fait de modules génériques et de données.

```
geplay games/lueur/game.json
```

Gauche et droite (ou A et D) : marcher. Espace, Haut ou W : sauter, plus haut si on tient la touche.

## Les niveaux

| Niveau | Ce qu'il faut faire |
| --- | --- |
| **La forêt** (`forest`) | Sauter une fosse d'épines, puis une pierre. Pousser la caisse contre le rocher, trop haut pour un saut ; monter sur la caisse, puis sur le rocher. Sauter une seconde fosse. La lumière au bout mène au sous-bois. |
| **Le sous-bois** (`hollow`), plus sombre | Attendre qu'une cosse épineuse remonte sur son fil pour passer dessous. Pousser la caisse dans une fosse trop large pour un saut : elle la comble à ras du sol, et il reste un saut court. Attendre une autre cosse, sauter une dernière fosse. La sortie ramène à la forêt. |

Une seule touche d'épines ou d'une cosse suffit : le niveau recommence. Un saut monte d'environ 144 unités et porte d'environ 200 unités à pleine vitesse : une fosse plus large ou un rocher plus haut demande la caisse.

## Ce qui le réalise

| Dans le jeu | Dans les fichiers |
| --- | --- |
| Marcher en prenant de l'élan, s'arrêter vite | `action_control.Controlled` (`acceleration`, `braking`) |
| Sauter plus haut en tenant la touche, avec un peu de tolérance au bord | `jump.Jumper` (`cut`, `coyote_time`, `buffer_time`), `gravity2d.Gravity` |
| Le sol, les fosses, les rochers | `collision2d.Collider` `solid`, `Body`, `Blocked` |
| La caisse qu'on pousse, plus lentement qu'on ne court ; elle tombe dans une fosse | `collision2d.Pushable` (`speed`), avec `Body` et `gravity2d.Gravity` |
| Les épines et les cosses tuent | `combat2d.ContactDamage` sur le héros, qui a `health.Health` (1) ; `on_zero` lève `died` |
| Mourir fait recommencer le niveau | `scene_switch.GoToScene` (`use_action` `died`) vers la scène elle-même |
| Les cosses montent et descendent | `patrol2d.Patrol` sur l'axe y |
| La sortie mène au niveau suivant | `scene_switch.GoToScene` (`touch_layers` `exit`) sur le héros, qui a `collision2d.Contacts` |
| Le héros reste debout, court, monte, retombe, se retourne | `sprite2d.Animator`, quatre `AnimationClip`, `Facing` |
| L'esprit le suit en flottant, et éclaire | `follow2d.Follow` (`turn`, `wave_*`), `light2d.Light`, `particles2d.Emitter` |
| La caméra le suit sans à-coups et ne sort pas du niveau | `camera2d.CameraFollow` (zone morte, anticipation, limites) |
| Le crépuscule, la brume, les lucioles, les feuilles | `sprite2d.Parallax`, `light2d.Ambient`, `particles2d`, `postfx.Effects` |

## Les images et les scènes sont générées

```
python3 games/lueur/tools/make_assets.py      (numpy et Pillow)
```

dessine toutes les images (ciel, plans de forêt, brume, sol, rochers, caisse, épines, cosses, héros, esprit, feuilles) et écrit les deux scènes. Les niveaux sont décrits en haut du script (`LEVELS`) : segments de sol, fosses et leur profondeur, rochers, caisses, cosses. Changer un niveau, c'est changer ces lignes et relancer le script.

## Les scénarios

`gerun games/lueur/game.json --test` joue neuf scénarios :
- les mouvements : marcher, sauter, la caméra qui reste dans le niveau ;
- l'animation et l'esprit ;
- les deux niveaux traversés de bout en bout, avec les entrées d'un joueur ;
- une chute dans les épines et une cosse qui tue, suivies du niveau qui recommence.
