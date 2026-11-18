# Le lanceur

Le lanceur parcourt les jeux du dépôt, montre ce que chacun contient, et joue celui qu'on choisit dans la même fenêtre. C'est **un jeu comme les autres** (des fichiers JSON dans `tools/launcher/`, avec les modules `ui`, `text2d` et `host`) accompagné d'un petit programme, `gelauncher`, qui fait ce qu'un jeu ne peut pas faire : lire le disque et lancer un autre jeu. Voir l'[ADR 0011](adr/0011-lanceur-et-export.md).

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
| Échap, ou le bouton « Quitter » | quitter le lanceur |

Dans un jeu, **Échap** revient au lanceur (le jeu ne le voit pas : la touche appartient à l'hôte), et le bouton « Quitter » du menu d'un jeu fait de même. La fenêtre prend la taille que le jeu demande (`window` de son manifeste), puis retrouve celle du lanceur, avec la sélection là où on l'avait laissée.

## Comment c'est fait

1. **Le catalogue.** L'hôte appelle `game::discover_games` (la même fonction que `gectl list`) : chaque sous-dossier de `games/` qui a un `game.json`, avec son titre, sa description, sa taille de fenêtre, ses modules et sa validité. Un jeu invalide est listé (« (invalid) ») mais pas lancé.
2. **Les listes.** L'hôte remplit deux listes d'exécution (`RuntimeLists`) : `games` (les titres) et `details` (les lignes sur le jeu sélectionné : titre, version, description coupée à la largeur du panneau, fenêtre, modules). Il met `details` à jour dès que la sélection change.
3. **L'écran.** `tools/launcher/scenes/main.json` (généré par `tools/launcher/tools/make_assets.py`) place huit rangées (`ui.Row`, des boutons dont le texte est un `text2d.Dynamic`), un `ui.Pager`, les boutons `<`, `>` et « Quitter », et dix textes dynamiques pour le panneau de détail.
4. **Jouer.** Une rangée pressée lève l'action `play_game` ; un `host.Call` envoie à l'hôte la commande `play` avec l'élément sélectionné comme argument. L'hôte lit la file `HostRequests` entre deux images, arrête la boucle du lanceur (qui reste en pause, intacte), et joue le jeu avec `play_loaded` : nouveaux atlas et sons (`Backend::reset_assets`), fenêtre à la taille du jeu (`Backend::resize`), sauvegardes dans le dossier de données de l'utilisateur. Au retour, il refait la même chose pour le lanceur et relance sa boucle.

## Tester

Le lanceur se teste comme un jeu : `tools/launcher/scenarios/` préremplit `games` avec la clé `lists` et vérifie la sélection, les pages, la commande `play` et le bouton « Quitter » (`gerun tools/launcher/game.json --test`, et `./build/dev/tests/ge_tests`). L'hôte se teste avec un `ScriptedBackend` (`tests/platform/sdl/test_launcher.cpp`) : choisir un jeu, voir la fenêtre prendre sa taille, revenir avec Échap.

## Pas encore

- Le bouton **Exporter** et le choix de la plateforme (étapes E5 et E6).
- Les miniatures des jeux (il faut charger des images à l'exécution).
- Un défilement fluide de la liste : la pagination suffit tant qu'il y a peu de jeux.
