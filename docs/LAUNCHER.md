# Le lanceur

Le lanceur parcourt les jeux du dépôt, montre ce que chacun contient, et joue celui qu'on choisit dans la même fenêtre. C'est **un jeu comme les autres** (des fichiers JSON dans `tools/launcher/`, avec les modules `ui`, `text2d` et `host`) accompagné d'un petit programme, `gelauncher`, qui fait ce qu'un jeu ne peut pas faire : lire le disque et lancer un autre jeu. Voir l'[ADR 0011](adr/0011-lanceur-et-export.md).

Le plus simple : `./gameEngine.sh` (à la racine ; sous Windows depuis Git Bash, ou `scripts\launcher.cmd`) configure, compile et ouvre le lanceur ; ses arguments vont au lanceur (`./gameEngine.sh --headless --frames 5`), et `--build-only` compile sans ouvrir. La compilation va dans `build/launcher`, avec les outils : le bouton Export fonctionne.

```
gelauncher                           # depuis la racine du dépôt
gelauncher --games games --launcher tools/launcher/game.json
gelauncher --headless --frames 20 --screenshot launcher.bmp
```

| Touche | Effet |
| --- | --- |
| Haut / Bas (ou la souris) | choisir un jeu |
| Entrée, Espace ou un clic | jouer le jeu choisi |
| Gauche / Droite, Page haut / bas | tourner les pages de la liste |
| Bouton « Export » (Haut deux fois depuis le premier jeu, puis Entrée) | ouvrir l'écran d'export du jeu choisi |
| Échap, ou le bouton « Quitter » | quitter le lanceur |

Dans un jeu, **Échap** revient au lanceur (le jeu ne le voit pas : la touche appartient à l'hôte), et le bouton « Quitter » du menu d'un jeu fait de même. La fenêtre prend la taille que le jeu demande (`window` de son manifeste), puis retrouve celle du lanceur, avec la sélection là où on l'avait laissée.

## Exporter un jeu depuis le lanceur

Le bouton **Export** ouvre un second écran du lanceur (la scène `export`, elle aussi en JSON) pour le jeu sélectionné : la liste des plateformes (aujourd'hui Windows), un bouton « Back » (ou Retour arrière) et, dessous, le titre du jeu, l'état (« Working… », « Done. The zip is in the dist folder. », « The export failed. ») et les 8 dernières lignes de ce que fait `gectl export`. Choisir une plateforme (Entrée ou un clic) lance l'export **en arrière-plan** : le lanceur continue de répondre et d'afficher l'avancement. Voir [EXPORT.md](EXPORT.md) pour ce que fait l'export et ce qu'il demande (Visual Studio ou ses Build Tools, CMake).

```
gelauncher --source .            # la racine du moteur, où le programme du jeu est compilé
gelauncher --dist dist           # où les zips arrivent
```

- Échap sur l'écran d'export revient aux jeux. Pendant un export, Échap ne quitte pas le lanceur ; fermer la fenêtre le quitte et laisse l'export finir tout seul en arrière-plan.
- Un seul export à la fois. Un jeu invalide ne s'exporte pas.
- Sans le service d'export (un `gelauncher` construit sans les outils), la liste des plateformes est vide et l'écran le dit.

## Comment c'est fait

1. **Le catalogue.** L'hôte appelle `game::discover_games` (la même fonction que `gectl list`) : chaque sous-dossier de `games/` qui a un `game.json`, avec son titre, sa description, sa taille de fenêtre, ses modules et sa validité. Un jeu invalide est listé (« (invalid) ») mais pas lancé.
2. **Les listes.** L'hôte remplit deux listes d'exécution (`RuntimeLists`) : `games` (les titres) et `details` (les lignes sur le jeu sélectionné : titre, version, description coupée à la largeur du panneau, fenêtre, modules). Il met `details` à jour dès que la sélection change.
3. **L'écran.** `tools/launcher/scenes/main.json` (généré par `tools/launcher/tools/make_assets.py`) place huit rangées (`ui.Row`, des boutons dont le texte est un `text2d.Dynamic`), un `ui.Pager`, les boutons `<`, `>`, « Export » et « Quitter », et dix textes dynamiques pour le panneau de détail.
4. **Jouer.** Une rangée pressée lève l'action `play_game` ; un `host.Call` envoie à l'hôte la commande `play` avec l'élément sélectionné comme argument. L'hôte lit la file `HostRequests` entre deux images, arrête la boucle du lanceur (qui reste en pause, intacte), et joue le jeu avec `play_loaded` : nouveaux atlas et sons (`Backend::reset_assets`), fenêtre à la taille du jeu (`Backend::resize`), sauvegardes dans le dossier de données de l'utilisateur. Au retour, il refait la même chose pour le lanceur et relance sa boucle.
5. **Exporter.** Le bouton « Export » lève `open_export` ; un `scene_switch.GoToScene` passe à la scène `export`. Une plateforme pressée lève `start_export` et un `host.Call` envoie la commande `export` (l'argument est l'indice de la plateforme dans la liste `targets`). L'hôte demande à un `ExportService` (interface de `launcher.hpp`) de fabriquer le jeu sélectionné ; celui de `gelauncher` (`tool_export.cpp`) lance `export_game` de `gectl` dans un fil, ses commandes CMake lues ligne par ligne. À chaque image, l'hôte lit ce que le service a dit et écrit la liste `export` (titre, état, 8 dernières lignes) que les textes dynamiques de la scène affichent.

## Tester

Le lanceur se teste comme un jeu : `tools/launcher/scenarios/` préremplit `games` avec la clé `lists` et vérifie la sélection, les pages, la commande `play` et le bouton « Quitter » (`gerun tools/launcher/game.json --test`, et `./build/dev/tests/ge_tests`). L'hôte se teste avec un `ScriptedBackend` (`tests/platform/sdl/test_launcher.cpp`) : choisir un jeu, voir la fenêtre prendre sa taille, revenir avec Échap, et, avec un faux `ExportService`, ouvrir l'écran d'export, lancer un export, voir son avancement et Échap qui ne quitte pas pendant qu'il tourne. `tests/platform/sdl/test_tool_export.cpp` essaie le vrai service (fil, lignes lues, un seul export à la fois) avec un faux CMake.

## Pas encore

- Les miniatures des jeux (il faut charger des images à l'exécution).
- Un défilement fluide de la liste : la pagination suffit tant qu'il y a peu de jeux.
