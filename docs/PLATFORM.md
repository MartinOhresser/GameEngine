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

- Une source s'écrit `genre:nom` en minuscules. Genres : `key`, `mouse`, `touch` ; `pad` est déjà accepté et réservé à un futur backend.
- Les noms de touches sont ceux de SDL en minuscules, espaces remplacés par `_` : `key:left`, `key:space`, `key:a`, `key:left_shift`, `key:return`. Souris : `mouse:left`, `mouse:middle`, `mouse:right`, et la position du pointeur en pixels de la fenêtre, `mouse:x` et `mouse:y` (liées à une action, par exemple `pointer_x`, elles ne sont pas limitées à [-1, 1]). Les positions de la souris et des doigts sont en pixels **du jeu** (la taille de `window`), même si la fenêtre est redimensionnée ou entourée de bandes. **Tactile** : jusqu'à cinq doigts, dans l'ordre où ils se posent ; le premier donne `touch:down`, `touch:x`, `touch:y`, le deuxième `touch:down2`, `touch:x2`, `touch:y2`, etc. jusqu'à `touch:down5`. Le premier doigt est aussi une souris (`mouse:left`, `mouse:x`, `mouse:y`) : un jeu fait pour la souris se joue au doigt sans rien changer, et le toucher n'est compté qu'une fois (les événements souris de SDL pour le tactile sont coupés). Le bouton Retour d'Android est la touche `key:ac_back` et quitte le jeu comme Échap. Un hôte mobile ne fait rien d'un bouton « Quitter » (module `host`). La taille de la fenêtre est la clé `window` de `game.json` (`{"width": 800, "height": 600}` par défaut) ; `--size` la remplace. Avec `"adapt": true` la zone de dessin suit la forme de l'écran ([UI.md](UI.md#la-fenêtre)) ; `Backend::adapt_view` la demande, `view_size` la donne et la boucle de jeu la recopie dans `Viewport` à chaque image.
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

### Sortir de la boucle

La boucle rend un `LoopResult` qui dit pourquoi elle s'est arrêtée :

- `backend_stopped` : le joueur a fermé la fenêtre ;
- `quit_requested` : le **jeu** a demandé à partir (la ressource `QuitRequest`, levée par un composant `host.Quit` quand son action est pressée, typiquement un bouton « Quitter ») ;
- `back_requested` : le joueur a pressé une source de `LoopOptions::back_sources` (`key:escape` pour `geplay`). Ces sources appartiennent à l'**hôte** : le jeu ne les voit pas, donc on peut toujours revenir au lanceur, même d'un jeu sans menu.

Un jeu reste un fichier de données : le module `host` (composants `Quit` et `Call`) lève des demandes, la boucle ou l'hôte décide quoi en faire. `Call` met une commande nommée (liste `host_command`) dans la file `HostRequests`, que le lanceur lit entre deux ticks. Les scénarios testent ces demandes sans fenêtre : `game quits` et `host calls NOM`.

`Backend::reset_assets()` repart de zéro côté jeu (nouveaux atlas et sons) sur la même fenêtre, et `Backend::load_sound` charge un `.wav` : un lanceur les appelle avant chaque jeu et à son retour (voir [LAUNCHER.md](LAUNCHER.md)).

`Backend::resize(largeur, hauteur)` donne à la fenêtre la taille demandée par le jeu (clé `window` du manifeste) ; `ScriptedBackend` la mémorise pour les tests. `HostInfo::mobile` dit au jeu si l'hôte est un téléphone : `host.Quit` n'y fait rien par défaut (iOS interdit de quitter), sauf avec `on_mobile`.

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
geplay games/doodle_jump/game.json --window 360x800
geplay games/move_sprite/game.json --headless --frames 120 --screenshot image.bmp
geplay games/doodle_jump/game.json --save mes_scores.txt
```
`gelauncher` est le lanceur ([LAUNCHER.md](LAUNCHER.md)). `geplay` est `gerun` avec une fenêtre : mêmes fichiers, tous les modules du dépôt liés. `--window LxH` donne à la fenêtre elle-même cette taille (un jeu qui s'adapte prend sa forme : `--window 360x800` essaie un téléphone). `--headless` n'ouvre ni fenêtre ni son (les pilotes « dummy » de SDL) mais dessine quand même en mémoire, d'où `--screenshot`. C'est ce que la CI utilise pour vérifier le backend sans écran.

Ce que le jeu garde d'une partie à l'autre (un meilleur score : `SaveData`, voir [DOODLE_JUMP.md](DOODLE_JUMP.md)) est lu dans un fichier texte au démarrage et réécrit dès qu'il change. Par défaut ce fichier est `save.txt` dans le dossier de données de l'utilisateur, un sous-dossier par jeu (`~/.local/share/ge/NOM/` sous Linux, `~/Library/Application Support/ge/NOM/` sous macOS, `%APPDATA%\ge\NOM\` sous Windows) ; `--save FICHIER` en choisit un autre. Avec `--headless`, rien n'est lu ni écrit sauf si `--save` est donné.

## Mesurer le dessin : `gebench`

```
gebench                                   tous les pilotes de rendu que SDL connaît
gebench --drivers default,vulkan          SDL choisit (default), puis Vulkan
gebench --atlas art/tiles.png --cell 16 --size 1920x1080
```

`gebench` ouvre une fenêtre par pilote de rendu, sans synchronisation verticale, et double une charge tant qu'une image prend moins de 1/60 s. Il mesure deux charges : des sprites de 32 × 32, et des plans qui couvrent toute la fenêtre (le remplissage des pixels, ce que coûte une parallaxe). Il dessine à travers le `Renderer` du moteur, comme un jeu, et sort un tableau Markdown. Le lancer depuis la racine du dépôt (son atlas par défaut est celui de treasure_run). Mesure de référence : [ADR 0015](adr/0015-rendu-atmospherique.md).

`WindowOptions` permet aussi de demander un pilote (`render_driver` : `direct3d11`, `vulkan`, `gpu`… ; le rendu logiciel reste le secours) et de couper la synchronisation verticale (`vsync`).

## Compiler le backend

```
cmake --preset platform
cmake --build --preset platform
ctest --preset platform
```

SDL3 (3.4) est pris sur le système s'il est installé, et compilé depuis les sources sinon (`FetchContent`), donc un poste neuf n'a besoin que d'un compilateur et d'un accès à GitHub. Sur Linux, SDL3 demande les en-têtes de développement d'un système de fenêtres (X11 ou Wayland) ; la liste des paquets Debian/Ubuntu est dans le job `Platform` de `.github/workflows/ci.yml`. Sans l'option, ni SDL ni `geplay` ne sont compilés et tout le reste est identique.

Une seule règle de dépendance, vérifiée en CI par `scripts/check_modules.py` : **aucun fichier hors de `platform/sdl/` n'inclut un en-tête SDL**. Le moteur, les modules et les jeux ne savent pas que SDL existe, et les en-têtes publics du backend n'en exposent aucun type.

## Ajouter un backend (manette, tactile, autre)

Implémenter `ge::platform::Backend` : `poll` produit des `RawEvent` (`pad:south`, `touch:tap`…), `now` donne l'heure, `renderer` et `audio` fournissent les périphériques. Aucun changement dans le moteur, les modules ni les jeux : il suffit que `input.json` cite les nouvelles sources.
