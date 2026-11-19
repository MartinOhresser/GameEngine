# ADR 0012 : exporter un APK Android

Statut : proposé. A0 est fait : ses mesures confirment le plan (voir « Écarts constatés »). Les écarts constatés aux jalons suivants s'ajoutent à la fin, comme pour l'ADR 0011.

## Contexte

L'export de l'[ADR 0011](0011-lanceur-et-export.md) fabrique un zip Windows qu'on envoie à quelqu'un. On veut la même promesse sur Android : **un fichier `.apk` qu'on copie sur un téléphone et qu'on installe**, sans rien compiler ni installer d'autre côté destinataire. L'ADR 0011 avait déjà prévu la forme : « même mécanisme, avec un projet Gradle autour de `SDLActivity`, la lecture des fichiers dans l'APK et l'entrée tactile ».

Ce que le code d'aujourd'hui suppose et qu'Android casse :

| Aujourd'hui | Sur Android |
| --- | --- |
| Un exécutable (`ge_player`) lancé par le système | Une bibliothèque partagée `libmain.so` chargée par une activité Java (`SDLActivity`), dans un APK |
| Les fichiers du jeu sont sur le disque, à côté de l'exécutable (`std::ifstream`, `fs::directory_iterator`) | Ils sont dans l'APK (`assets/`) : `std::ifstream` ne les voit pas, et un dossier ne se parcourt pas |
| Clavier et souris ; le tactile est « réservé » dans `input.json` | Le doigt, sans clavier : `move_x` et `jump` n'ont aucune source |
| La fenêtre prend la taille de `window` | L'écran est plein, de taille et de forme variables, en portrait ou en paysage |
| `gectl export` : CMake compile, on copie, on zippe | Il faut en plus Gradle, le SDK Android, le NDK, un JDK et une signature |
| Échap quitte vers le lanceur | Le bouton Retour arrive comme `SDLK_AC_BACK` ; un bouton « Quitter » est inutile (`HostInfo::mobile`) |

Ce qui est déjà prêt : le noyau et les modules de gameplay compilent pour Android à chaque pull request (job `android`, NDK, arm64-v8a) ; les sauvegardes passent par `SDL_GetPrefPath` ; la lecture de fichiers passe déjà par un `FileReader` interchangeable (l'ADR 0011 l'avait voulu pour cela) ; le chemin de sortie d'un export est une `ExportTarget`.

## Décisions

### 1. Une cible de plus, mais un export en étapes

`android` devient une `ExportTarget`. Or l'export actuel suppose le chemin Windows (CMake construit un exécutable, on copie, on zippe) et le code de `export_game` l'écrit en dur. Première étape : le découper en **étapes que la cible fournit** : *construire le programme* et *assembler le paquet*, les étapes communes (validation, scénarios rejoués, icône, copie des données, README, licences) restant partagées. Windows migre sans changer de comportement (ses tests le garantissent) ; Android fournit ses propres étapes. Le panneau d'export du lanceur n'a rien à savoir : il liste les cibles.

### 2. Le programme est une bibliothèque `main` liée statiquement

`ge_add_player` sait déjà ne lier que les modules du jeu. Sous Android, il crée une bibliothèque **partagée** nommée `main` (au lieu d'un exécutable), avec SDL3 et les modules liés statiquement dedans, et le point d'entrée SDL (`SDL_main`). Une sous-classe de `SDLActivity` (dans notre gabarit) indique la seule bibliothèque à charger. Un seul ABI au début, **arm64-v8a** (tous les téléphones récents), plus **x86_64** pour l'émulateur de la CI.

### 3. Un projet Gradle généré à l'export, pas écrit à la main

Le dépôt contient un **gabarit** (`platform/android/template/` : `build.gradle`, `AndroidManifest.xml`, la classe d'activité, le `gradlew` avec son jar) ; les classes Java de SDL viennent de la même source SDL que le C++ (`android-project/` de l'archive téléchargée par CMake), donc elles ne divergent jamais de la bibliothèque. L'export remplit le gabarit à partir du manifeste du jeu : identifiant d'application (`ge.<id>`), nom affiché (`title`), numéro de version (`version`), orientation, icône (le PNG du manifeste, aux tailles Android), et copie `data/` dans `assets/`. Puis `gradlew assembleRelease`.

Alternative écartée : un APK **modèle** unique contenant tous les modules, auquel on ajoute les données du jeu. Il évite SDK et NDK à l'export (il faudrait quand même `aapt2` et un outil de signature pour le nom, l'icône et la signature) et serait rapide, mais l'APK embarquerait tous les modules (contraire à « seuls les modules du jeu sont liés ») et changer le nom ou l'icône d'un APK déjà compilé est fragile. À reconsidérer plus tard comme « export rapide ».

### 4. Les fichiers du jeu sont lus par SDL

Un lecteur de fichiers fondé sur `SDL_IOFromFile` (qui lit aussi les `assets/` d'un APK, et le disque partout ailleurs) remplace `std::ifstream` à tous les endroits où le programme lit le jeu : manifeste et JSON (`FileReader`), atlas (`sdl_backend.cpp`), sons. Un dossier ne se parcourt pas dans un APK : l'export écrit un **index des fichiers** (`data/files.txt`) que la lecture des sons (et tout futur parcours) utilise sur toutes les plateformes, pour ne garder qu'un chemin de code. `gectl` et `gerun` continuent de lire le disque directement.

### 5. Le doigt d'abord, les contrôles à l'écran ensuite

- **Dès le premier APK**, SDL transforme un toucher en clic de souris : tous les menus de `ui` (boutons à focus, souris) marchent au doigt sans autre code.
- Le backend SDL lit en plus les événements de doigts (`SDL_EVENT_FINGER_*`) : les sources `touch:x`, `touch:y` et `touch:down` de `input.json` (« réservées » depuis M6) deviennent réelles, avec plusieurs doigts.
- Un module `touch_controls` dessine des **contrôles à l'écran** déclarés en JSON (boutons ronds, croix, joystick) et les fait produire des actions comme le clavier (`move_x`, `jump`) : les systèmes de gameplay ne changent pas. Les jeux de référence reçoivent un jeu de contrôles tactiles dans leur `input.json`.
- Le bouton Retour devient l'action de retour de l'hôte, comme Échap ; le bouton « Quitter » est masqué quand `HostInfo::mobile` (reporté depuis l'ADR 0011, E2).
- L'accéléromètre (inclinaison) est repoussé : il passerait par `SDL_Sensor`, comme une source de plus.

### 6. Écran plein, orientation, mise à l'échelle

Le manifeste gagne `window.orientation` (`landscape`, `portrait`, `any`) qui devient l'orientation de l'activité. La taille logique du jeu (`window.width/height`) est présentée sur l'écran avec des bandes (`SDL_SetRenderLogicalPresentation`, que le backend SDL utilise déjà pour la fenêtre du bureau), pour que les jeux gardent leurs coordonnées. Les zones sûres (encoche, barre de gestes) viennent plus tard.

### 7. Outils : on dit ce qui manque

L'export Android demande, côté exportateur : JDK 17+, SDK Android (plateforme, build-tools, cmdline-tools) et NDK. `buildable_here()` les cherche (`ANDROID_HOME`, `ANDROID_NDK_HOME`, `JAVA_HOME`, emplacements d'Android Studio) et `needs()` dit **ce qui manque et où le prendre**, en une phrase par outil. Une aide d'installation (téléchargement des outils en ligne de commande, licences à accepter par la personne) viendra dans un jalon à part : le premier objectif est un export qui marche pour quelqu'un qui a déjà Android Studio.

### 8. Signature

Par défaut, l'APK est signé avec la clé de débogage d'Android : il s'installe en copiant le fichier (« sources inconnues ») ou avec `adb install`, mais il **ne peut pas être publié** sur Google Play. `--keystore FICHIER` (avec `--key-alias` et le mot de passe demandé ou lu dans une variable d'environnement) signera avec une clé à soi, plus tard ; le paquet pour Google Play (AAB, `bundletool`) est un jalon à part.

### 9. Vérifier sans téléphone

L'environnement de développement de Claude n'a ni Android ni émulateur ; la CI d'Ubuntu a le SDK, le NDK et un JDK, et l'accélération matérielle pour un émulateur x86_64. Le job « Export Android » exporte `doodle_jump`, vérifie le paquet (`aapt2 dump badging` : identifiant, nom, ABI, aucune permission), l'**installe dans un émulateur, le lance et vérifie par une capture d'écran qu'il dessine**, comme le job Windows. La release publie les APK de référence à côté des zips Windows.

## Jalons

Chaque jalon est une pull request. A0 vient d'abord parce que c'est lui qui peut faire changer le reste (version de SDL, forme du projet Gradle).

| | Jalon | Sortie |
| --- | --- | --- |
| A0 | **Essai à blanc** : un APK minimal (SDL3, une fenêtre, un carré) construit par un script de CI, installé et lancé dans l'émulateur | on sait que la chaîne marche, quelle version de SDL il faut (3.2.30 suffit-elle ? les outils APK de SDL 3.4 aident-ils ?) et combien de temps ça prend ; l'ADR est corrigé en conséquence |
| A1 | **Export en étapes** : `ExportTarget` fournit « construire » et « assembler » ; Windows migre | les tests d'export existent toujours et passent, aucune différence pour Windows |
| A2 | **Lire sans le disque** : lecteur SDL pour jeu, atlas et sons, index `files.txt`, `ge_add_player` crée la bibliothèque `main` sous Android | `ge_player` lit son jeu par le nouveau lecteur sur Linux et Windows (tests), et compile pour Android en CI |
| A3 | **La cible `android`** : gabarit Gradle, manifeste, icône, orientation, `gectl export --target android`, diagnostic des outils manquants | `gectl export games/doodle_jump/game.json --target android` donne un `.apk` qui s'installe et se lance dans l'émulateur (menus au doigt) |
| A4 | **Le tactile** : événements de doigts, sources `touch:`, bouton Retour, bouton « Quitter » masqué | un scénario scripté de doigts, et un jeu qui répond à plusieurs doigts |
| A5 | **Contrôles à l'écran** : module `touch_controls`, orientation, mise à l'échelle, `input.json` tactiles de `doodle_jump`, `treasure_run` et `coin_dash` | les trois jeux de référence se jouent à l'écran tactile dans l'émulateur |
| A6 | **CI et publication** : job « Export Android » avec émulateur, APK de référence dans la release, `docs/EXPORT_ANDROID.md` (pas à pas, USB et `adb`) | la CI échoue si l'APK ne s'installe pas, ne démarre pas ou ne dessine pas |
| A7 | **Confort** : cible visible dans le panneau d'export du lanceur, `--install` (via `adb`), `--keystore`, aide d'installation des outils | quelqu'un sans Android Studio exporte son APK en suivant la doc |

## Écarts constatés à l'implémentation

- **A0** (`platform/android/spike/`, job « Android APK (spike) »). Un programme SDL de 150 lignes, un gabarit Gradle et un script de build, exécutés sur un runner Ubuntu puis dans un émulateur Android 14 (x86_64). Tout a marché du premier coup, et la suite en est simplifiée :
  - **SDL 3.2.30 suffit**, il n'y a pas à changer de version. L'archive de SDL contient `android-project/` : ses classes Java, ses icônes et styles, **et un `gradlew` avec son jar** (Gradle 8.12, plugin Android 8.7.3). L'export les copie depuis là : aucun binaire n'entre dans notre dépôt (la décision 3 y gagne).
  - **SDL lié dans `libmain.so`, ça marche** : avec `--whole-archive` (sinon l'éditeur de liens écarte les points d'entrée JNI) et une activité dont `getLibraries()` ne renvoie que `main`. Taille de l'APK, deux ABI (arm64-v8a et x86_64), bibliothèque sans symboles : **2,4 Mo** (1,1 Mo par ABI compressé). Sans retirer les symboles : 9,5 Mo ; avec SDL en bibliothèque à part : 7,5 Mo, d'où le choix de la décision 2.
  - **Durées** sur le runner : 55 s pour compiler SDL et le programme pour les deux ABI, 30 s pour Gradle, soit environ 1 min 30 au premier export, avant l'émulateur.
  - **Les fichiers de l'APK** : `SDL_LoadFile("data/hello.txt")` lit `assets/data/hello.txt`, `std::ifstream` échoue sur le même chemin. La décision 4 est confirmée. `SDL_GetPrefPath` renvoie `/data/data/<id>/files/`, où l'écriture et la relecture marchent.
  - **Le toucher** arrive en **deux** événements : un clic de souris (identifiant de souris `SDL_TOUCH_MOUSEID`, coordonnées de la fenêtre) et un événement de doigt (coordonnées entre 0 et 1). Les menus de `ui` marchent donc au doigt, comme prévu en décision 5, mais le jeu reçoit aussi `mouse:left` quand un doigt est sur un contrôle à l'écran : A4 devra décider qui compte (ignorer les clics de souris d'origine tactile pour les actions de jeu, ou désactiver la synthèse avec `SDL_HINT_TOUCH_MOUSE_EVENTS` dès que `ui` lit les doigts).
  - **Le bouton Retour** termine l'activité si on ne fait rien : le jeu ne voit rien et le processus reste, en pause. Avec `SDL_HINT_ANDROID_TRAP_BACK_BUTTON=1`, il arrive comme la touche « AC Back » et le jeu continue. `ge_player` doit donc poser ce réglage.
  - **L'écran** : l'émulateur donne 320×616 pixels à la fenêtre pour un écran de 320×640 (les barres du système en prennent une partie). La présentation logique avec bandes (décision 6) en tient compte ; le mode immersif et les zones sûres restent à faire.
  - **Pas vérifié** : l'exécution sur arm64 (l'émulateur de la CI est x86_64), un Android plus ancien que 14, et un vrai téléphone. Le build « SDL en bibliothèque à part » a marché une fois puis a échoué une fois dans l'émulateur sans qu'on sache pourquoi ; il n'est plus dans le job.

- **A1.** `ExportTarget` a deux étapes, `build` et `assemble`, qui reçoivent un `ExportJob` (le jeu vérifié, les modules, les dossiers, l'exécuteur de commandes). `export_game` fait ce qui est commun (cible, chargement, validation, scénarios) puis appelle les deux. Windows et Linux en héritent d'une base `DesktopTarget` ; la copie des fichiers du jeu est devenue `stage_game_files`, réutilisable par Android. Aucun changement de comportement : les tests d'export existants passent tels quels.

- **A2.** `ge/platform/sdl/assets.hpp` : `read_file` (SDL_LoadFile : le disque ou l'APK, y compris avec des accents dans le chemin sous Windows) et `list_game_files` (l'index `files.txt` s'il existe, sinon le dossier parcouru). Le joueur lit son jeu, les atlas et la liste des sons par là ; `std::ifstream` ne sert plus qu'aux sauvegardes (le dossier de données de l'application, qui est un vrai dossier sur Android). L'export écrit `files.txt` dans `data/` pour tous les systèmes, donc le chemin « index » est celui que la CI Windows et Linux exerce déjà. Sous Android, `ge_add_player` crée la bibliothèque `main` (SDL entier lié dedans, symboles retirés) et `player_main.cpp` inclut `SDL_main.h` ; le dossier des données est `data` (dans les `assets/`). Le job `android` de la CI construit ce `libmain.so`.

## Limites assumées

- **Beaucoup plus d'outils qu'en Windows** pour l'exportateur (JDK, SDK, NDK, Gradle) ; seul le destinataire n'a rien à installer.
- **Premier export long** (téléchargement de Gradle et de ses dépendances, compilation de SDL et des modules).
- **Pas de test sur un vrai téléphone ici** : l'émulateur de la CI x86_64 ne prouve ni les performances ni l'arm64 ; la première vérification sur un téléphone se fait par la personne, avec un APK téléchargé de la CI.
- **APK, pas AAB** : pas de publication sur Google Play au début. Android Play Protect peut avertir pour une application inconnue.
- **Taille** : SDL, la bibliothèque C++ et les modules liés statiquement ; l'ordre de grandeur (quelques dizaines de Mo avec les images) se mesure en A0.
- **iOS** reste une autre cible (Mac et signature Apple obligatoires).

## Ce qui reste ouvert

- Le niveau d'API minimal : 21 (celui du gabarit de SDL) a été compilé, mais installé seulement sur Android 14 ; le job CI de compilation du cœur vise 24. À tester sur un émulateur plus ancien avant d'annoncer une version.
- Contrôles tactiles : une seule disposition par jeu, ou plusieurs (gaucher, taille) ?
- Les jeux pensés pour le tactile (pas seulement adaptés) : inclinaison, glisser, pincer.
- Compacter les données (`.gepak`) plutôt que `assets/` en clair (déjà ouvert dans l'ADR 0011).
