# Plateforme : fenêtre, entrées, son

Le moteur ne connaît ni clavier, ni fenêtre, ni carte son. Il parle à une **interface** (`ge::platform::Backend`) que plusieurs choses peuvent remplir : une fenêtre SDL3, un script de test, plus tard une manette ou un écran tactile. Le jeu, lui, ne voit que des **actions** (`move_x`, `fire`) : il se comporte de la même façon quel que soit le backend.

```
touche / bouton ──► Backend ──► RawEvent ──► InputMap (input.json) ──► LiveInput ──► Actions ──► le jeu
                                  "key:right"      action = Σ valeur × échelle
```

## Le fichier d'entrées

Un jeu déclare dans `game.json` : `"input": "input.json"`. Le fichier associe des **sources** à des **actions** :

```jsonc
{
  "schema_version": 1,
  "bindings": [
    { "action": "move_x", "sources": { "key:right": 1, "key:d": 1, "key:left": -1, "key:a": -1 } },
    { "action": "fire",   "sources": { "key:space": 1, "mouse:left": 1 } },
  ],
}
```

- Une source s'écrit `genre:nom` en minuscules. Genres : `key`, `mouse` ; `pad` et `touch` sont déjà acceptés et réservés aux futurs backends.
- Les noms de touches sont ceux de SDL en minuscules, espaces remplacés par `_` : `key:left`, `key:space`, `key:a`, `key:left_shift`, `key:return`. Souris : `mouse:left`, `mouse:middle`, `mouse:right`, et la position du pointeur en pixels de la fenêtre, `mouse:x` et `mouse:y` (liées à une action, par exemple `pointer_x`, elles ne sont pas limitées à [-1, 1]). La taille de la fenêtre est la clé `window` de `game.json` (`{"width": 800, "height": 600}` par défaut) ; `--size` la remplace.
- Une action vaut la somme `valeur × échelle` de ses sources, bornée à [-1, 1] : deux touches de même sens ne dépassent pas 1, deux touches opposées se compensent.
- Une action ne figure qu'une fois dans le fichier ; toutes ses sources vont dans la même entrée.
- Un jeu sans `input` n'a pas d'entrées de joueur : il tourne, mais seul un script peut le piloter.

`gectl validate` contrôle le fichier (source inconnue, échelle non numérique, action en double…) avec le fichier et l'endroit de l'erreur. `gectl schema` écrit `schemas/game/input.schema.json` pour l'autocomplétion.

## Ce que fait la boucle

`ge::platform::run_loop(app, backend, map)` répète, tant que le backend ne demande pas l'arrêt :

1. `backend.poll()` : les changements bruts depuis la dernière image ;
2. `InputMap::apply` : les sources deviennent des valeurs d'actions, déposées dans la ressource `LiveInput` ;
3. `app.advance(temps réellement écoulé)` : autant de ticks fixes que nécessaire (voir `App::advance`).

Les valeurs de `LiveInput` sont posées sur `Actions` **au début de chaque tick**, après la mémorisation des valeurs précédentes : `pressed` et `released` voient donc une touche au tick où elle change, exactement comme avec une entrée scriptée. Le rendu et le son du backend remplacent ceux de l'`App` (`RenderBackend`, `AudioBackend`).

La simulation ne change pas : pas fixe, déterministe, et la logique de jeu ne lit que des actions.

## Les backends

| Backend | Rôle |
| --- | --- |
| `ScriptedBackend` | Le « joueur » des tests : appuie et relâche des touches à des images données, horloge qui avance d'un tick exact par image, enregistre ce qui est dessiné et joué. |
| `SdlBackend` | Une fenêtre SDL3 : clavier, souris, rendu 2D, son `.wav`. Optionnel (`-DGE_BUILD_PLATFORM=ON`). |

Les deux alimentent le même jeu : un test (`tests/platform/test_backend.cpp`) vérifie qu'une touche pressée via un backend donne le même mouvement qu'une action scriptée.

### Le rendu et le son

Le jeu ne dessine pas : un système (ici `debug_draw.draw`, qui dessine un carré coloré pour chaque `debug_draw.Square` ayant une position) demande au `Renderer` de dessiner des `DrawCommand` (nom, position, taille, couleur), puis termine l'image. Les unités du monde sont des pixels. Le vrai rendu 2D (sprites, atlas, caméra) arrive en M7 derrière la même interface.

Pour le son, une interface `AudioDevice` (`play("nom")`), un périphérique nul par défaut et un enregistreur pour les tests. Avec SDL, les sons sont les `.wav` du dossier `sounds/` du jeu, nommés par leur nom de fichier sans extension. Le même son joué deux fois de suite se met en file ; des sons différents se mélangent.

## Jouer : `geplay`

```
geplay jeux/move_sprite/game.json
geplay games/move_sprite/game.json --size 1024x768
geplay games/move_sprite/game.json --headless --frames 120 --screenshot image.bmp
```

`geplay` est `gerun` avec une fenêtre : mêmes fichiers, tous les modules du dépôt liés. `--headless` n'ouvre ni fenêtre ni son (les pilotes « dummy » de SDL) mais dessine quand même en mémoire, d'où `--screenshot`. C'est ce que la CI utilise pour vérifier le backend sans écran.

## Compiler le backend

```
cmake --preset platform
cmake --build --preset platform
ctest --preset platform
```

SDL3 (3.2) est pris sur le système s'il est installé, et compilé depuis les sources sinon (`FetchContent`), donc un poste neuf n'a besoin que d'un compilateur et d'un accès à GitHub. Sur Linux, SDL3 demande les en-têtes de développement d'un système de fenêtres (X11 ou Wayland) ; la liste des paquets Debian/Ubuntu est dans le job `Platform` de `.github/workflows/ci.yml`. Sans l'option, ni SDL ni `geplay` ne sont compilés et tout le reste est identique.

Une seule règle de dépendance, vérifiée en CI par `scripts/check_modules.py` : **aucun fichier hors de `platform/sdl/` n'inclut un en-tête SDL**. Le moteur, les modules et les jeux ne savent pas que SDL existe, et les en-têtes publics du backend n'en exposent aucun type.

## Ajouter un backend (manette, tactile, autre)

Implémenter `ge::platform::Backend` : `poll` produit des `RawEvent` (`pad:south`, `touch:tap`…), `now` donne l'heure, `renderer` et `audio` fournissent les périphériques. Aucun changement dans le moteur, les modules ni les jeux : il suffit que `input.json` cite les nouvelles sources.
