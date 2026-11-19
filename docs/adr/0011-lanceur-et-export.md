# ADR 0011 : un lanceur qui est un jeu, et un export autonome par plateforme

Statut : accepté (conception ; les écarts constatés à l'implémentation s'ajoutent à la fin).

## Contexte

Le MVP et un premier vrai jeu (`doodle_jump`) tournent. On veut maintenant que le moteur ait une **interface** qui :

- parcourt tous les jeux du dépôt ;
- en lance un (en redimensionnant la fenêtre si le jeu le demande) ;
- permet d'en sortir depuis le menu du jeu ;
- **exporte** un jeu pour une plateforme choisie, en un zip que n'importe qui lance : l'exécutable sous Windows, l'APK sous Android. Pas de CMake à installer, pas de compilation, pas de terminal.

Aujourd'hui, `gerun` et `geplay` contiennent **tous** les modules du dépôt (`registry.cpp` est généré à partir de `modules/`), il n'existe pas de quoi quitter un jeu depuis ses menus, la fenêtre garde sa taille, et rien ne produit un paquet. Seul Windows sera une cible d'export pour l'instant, mais Android, iOS, macOS et des consoles suivront : le mécanisme doit donc accepter une cible de plus sans toucher au reste.

## Décisions

### 1. Le lanceur est un jeu, avec un hôte mince

Le lanceur est un dossier de JSON (`tools/launcher/`) qui utilise `ui` et `text2d` comme n'importe quel jeu, et qui se teste en scénarios. Un petit programme **hôte**, `gelauncher`, fait ce qu'un jeu ne peut pas faire : lire le disque (le catalogue des jeux), lancer un jeu, lancer une compilation. Ce découpage garde le cœur sans code de plateforme (règle 2 du README) et fait du lanceur la première preuve que l'interface ordinaire suffit à un vrai outil.

Alternative écartée : une interface en Dear ImGui. Elle irait plus vite, mais elle ne servirait que le lanceur, alors que le texte dynamique manque de toute façon à `ui` (noms de joueurs d'un lobby, tableau des scores, noms d'objets).

### 2. Du texte qui n'est pas dans une table de clés : les listes d'exécution

L'ADR 0009 interdit les champs chaîne : le texte vient d'une table de clés. Un lanceur doit afficher les noms des jeux, connus seulement à l'exécution. On garde la règle pour les composants (des nombres seulement) et on ajoute une ressource `RuntimeLists` : des listes de textes remplies par l'**hôte** ou par un système, jamais par un fichier de jeu. Un `Text` désigne **une liste et un indice** (des nombres), comme il désigne aujourd'hui une clé. La ressource n'entre pas dans l'empreinte du monde : ce que la simulation lit, ce sont les indices.

Une liste affichée à l'écran est paginée (lignes visibles, curseur, Précédent et Suivant) : le défilement fluide attend. Les scénarios peuvent préremplir les listes, donc le lanceur se teste sans fenêtre.

### 3. Les commandes vers l'hôte : une file, comme `SceneRequest`

Un bouton qui doit déclencher quelque chose hors de la simulation (« jouer ce jeu », « exporter ») lève une **commande nommée** dans une file `HostRequests` ; la boucle de l'hôte la lit entre deux ticks. Le cœur ne sait pas ce que fait une commande, ce qui évite d'y mettre du code de plateforme. Les commandes sont une liste de noms du jeu (`host_command`), validés au chargement.

### 4. Quitter, redimensionner, revenir

- Une ressource `QuitRequest`, levée par un composant sur une action, termine la boucle (`LoopResult::quit_requested`). Les jeux qui ont un menu (`doodle_jump`, `treasure_run`) reçoivent un bouton « Quitter ».
- Le retour au lanceur **ne dépend pas du jeu** : `Échap` (le bouton retour sous Android) est réservé à l'hôte. Un jeu sans menu (`sparks`, `pilot`) reste quittable.
- La fenêtre prend la taille de `window` dans le manifeste du jeu, puis retrouve celle du lanceur au retour. Pour cela, ce qui appartient à la fenêtre (persistant) est séparé de ce qui appartient à un jeu (atlas, sons).
- Sur mobile, un bouton « Quitter » est inutile (iOS l'interdit) : l'hôte donne au jeu un indicateur de plateforme (bureau ou mobile) que `ui` sait lire pour masquer le bouton. Le détail est réglé à l'étape qui l'implémente.

### 5. Un jeu se lance dans le même processus que le lanceur

Le lanceur est mis en pause (son `App` n'avance plus) pendant que le jeu choisi tourne dans un nouvel `App`, sur la même fenêtre ; au retour, la sélection du lanceur est intacte. Alternative écartée : un sous-processus (`geplay`). Il isole mieux un plantage, mais il complique le redimensionnement et le retour.

### 6. L'export est autonome : une promesse vérifiée

Un export est **un zip qu'on peut envoyer à quelqu'un d'autre**. Pour Windows, on garantit :

- **Un seul `.exe` et un dossier `data/`.** La bibliothèque C++ et SDL sont liées statiquement : ni `SDL3.dll` ni Visual C++ Redistributable à installer. La CI liste les DLL dont dépend l'exécutable et échoue si l'une n'est pas une DLL de Windows.
- **Il tourne depuis n'importe quel dossier**, y compris un chemin avec espaces ou accents, ou en lecture seule : les données sont cherchées à côté de l'exécutable, les sauvegardes vont dans le dossier de données de l'utilisateur.
- **Il tourne sans carte graphique récente** : le rendu logiciel de SDL sert de secours, et la CI le force.
- **Le lancement est un double-clic**, sans console, avec l'icône et le titre du manifeste.
- **Le zip contient un `README.txt`** (contrôles, emplacement des sauvegardes, avertissement SmartScreen) et les licences.
- **Seuls les modules du jeu sont liés** (ceux de son manifeste, dépendances comprises), et seuls les fichiers utilisés sont copiés (pas les scénarios, pas les sources d'art).

### 7. Où et comment on compile

L'export compile **en local**, avec le CMake et le compilateur que le développeur a déjà. Une fonction `ge_add_player(NOM MODULES …)` crée l'exécutable d'un jeu à partir d'un registre généré avec la seule liste de ses modules. Comme les modules sont des bibliothèques statiques, seul le registre et l'édition de liens changent d'un jeu à l'autre : après le premier export (qui compile SDL et le moteur, plusieurs minutes), les suivants sont rapides.

Avant d'exporter, `gectl export` valide le jeu et rejoue ses scénarios : un export qui échoue le dit, et `--skip-tests` le force.

Chaque plateforme est une **cible** (`ExportTarget` : un nom, la possibilité de s'exécuter sur cette machine, la fabrication du paquet). Windows est la seule proposée, une cible `linux` cachée sert aux tests dans l'intégration continue. Alternatives pour les suivantes :

- **Android** : même mécanisme, avec un projet Gradle autour de `SDLActivity`, la lecture des fichiers dans l'APK et l'entrée tactile.
- **macOS et iOS** : nécessitent un Mac et la signature d'Apple, donc une compilation lancée par la CI plutôt qu'en local.
- **Consoles** : les SDK sont sous NDA, donc des cibles privées, hors du dépôt.

### 8. Données en clair, zip sans dépendance

Le dossier `data/` contient les fichiers du jeu tels quels (JSON, PNG, sons). Les compacter dans un seul fichier est possible plus tard sans toucher aux jeux, puisque la lecture passe déjà par un `FileReader` interchangeable (c'est aussi ce qui permettra de lire dans un APK). Le zip est fabriqué par `cmake -E tar --format=zip`.

## Écarts constatés à l'implémentation

- **E2.** Le module `host` porte `Quit` et `Call` ; les ressources `QuitRequest`, `HostRequests` et `HostInfo` sont dans le cœur (`ge/app/host.hpp`). `Backend` gagne `resize` ; l'hôte, pas le jeu, remet la fenêtre à la taille du lanceur au retour. Sur mobile, `Quit` ne fait rien (`HostInfo::mobile`), mais le **bouton** reste visible : le masquer demande à `ui` de lire `HostInfo` et viendra avec le texte dynamique (E3).

- **E3.** `text2d.Dynamic` (liste, indice) est un composant à part plutôt que de nouveaux champs de `Text` : les jeux existants gardent leur empreinte. La liste affichée est paginée par `ui.Pager` + `ui.Row`, faits de boutons ordinaires (clavier et souris gratuits) ; le `Pager` éteint les boutons sans élément (`Button.enabled`). `RuntimeLists` mémorise l'élément sélectionné de chaque liste, que `host.Call` envoie comme argument de la commande.

- **E4.** `gelauncher` joue le jeu choisi avec la même fonction que `geplay` (`play_loaded`). Les atlas et les sons d'un jeu sont séparés de la fenêtre par `Backend::reset_assets` : le lanceur recharge les siens à son retour. Le lanceur est en pause pendant le jeu parce que sa boucle s'arrête (`QuitRequest` posée par l'hôte) puis reprend avec la même `App`. Détails du jeu : l'hôte écrit la liste d'exécution `details` quand la sélection change. Voir `docs/LAUNCHER.md`.

- **E5.** `gectl export` (`tools/gectl/export.cpp`) enchaîne validation, scénarios rejoués dans le processus (`gectl` est lié à `ge_runner`), `.ico` tiré du PNG de l'icône, `cmake` configure et build de `ge_player` dans `build/export/<cible>/<jeu>`, dossier du paquet, README, licences, zip. Pas de Ninja imposé : le générateur par défaut de CMake convient (Visual Studio sous Windows), d'où la recherche de `ge_player` avec ou sans sous-dossier `Release`. Le programme (`ge_player`, `player.cpp`) est séparé de `geplay` : pas de console, seulement `data/` et ses options de test. Un `CommandRunner` injectable remplace CMake dans les tests. Voir `docs/EXPORT.md`.

- **E6.** L'export du lanceur est une seconde scène JSON (`export`) et un `ExportService` abstrait dans `launcher.hpp` : le lanceur ne dépend pas des outils, `gelauncher` lui donne un service qui exécute `export_game` de `gectl` dans un fil détaché (un seul export à la fois, lignes de sortie lues par `run_process_lines`). Échap revient de l'écran d'export aux jeux et ne quitte pas le lanceur pendant un export ; fermer la fenêtre le quitte sans attendre. `LauncherResult::export_lines` expose l'écran pour les tests.

## Limites assumées

- **SmartScreen.** Un `.exe` non signé téléchargé affiche « Windows a protégé votre PC » ; le destinataire clique sur « Informations complémentaires » puis « Exécuter quand même ». Seul un certificat de signature (payant) l'évite ; une option `--sign` viendra plus tard.
- **Le courrier.** Gmail et Outlook refusent les `.exe`, même dans un zip. Un lien de téléchargement convient mieux.
- **Pas d'exécution Windows ici.** L'environnement de développement de Claude est Linux : le test Windows réel se fait sur le job `windows-latest` de la CI, qui publie le zip en téléchargement.
- **Premier export lent**, ensuite incrémental.
- **Pas de miniatures** dans la première version du lanceur : texte, description, taille de fenêtre et modules. Elles demandent de charger des images à l'exécution (un atlas réservé que l'hôte remplace à la sélection).

## Conséquences

- `ui` et `text2d` gagnent le texte dynamique, utile à tous les jeux.
- Le manifeste gagne `title`, `icon`, `version` et `id` (facultatifs), qui servent le catalogue et les paquets.
- Le noyau reste sans code de plateforme : tout ce qui touche au disque, aux processus et aux fenêtres est dans l'hôte.
- Le lanceur est testé par scénarios, comme un jeu.

## Ce qui reste ouvert

- La pagination suffira-t-elle quand il y aura beaucoup de jeux ? Un défilement viendra si besoin.
- Compacter les données (`.gepak`) et réduire la taille des exécutables (liens à la demande).
- La signature des exécutables (Windows) et des applications (Apple).
- La mise à jour d'un jeu déjà exporté.
