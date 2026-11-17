# Game Engine

[![Licence : MIT](https://img.shields.io/badge/licence-MIT-blue.svg)](LICENSE)

Un moteur de jeu en **C++20**, construit autour d'un **ECS** (Entity-Component-System) volontairement minimal et générique : le cœur ne sait rien d'un jeu en particulier, tout le reste s'ajoute par **composants**, **systèmes** et **plugins**.

> **Statut : très tôt (v0.1).** Seul le cœur ECS et la boucle applicative existent. Il n'y a encore ni rendu, ni entrées, ni réseau. Le dépôt a d'abord été bâti pour que *chaque modification soit contrôlée* ; les fonctionnalités arrivent ensuite, une par une, derrière cette barrière.

## Les enjeux

### L'ambition

Pouvoir faire **n'importe quel type de jeu** avec le même socle, et décrire chaque jeu dans un **fichier de configuration** (modules, affichage, entrées, niveaux) que Claude peut écrire à partir d'un simple prompt. Cible à terme :

| Axe | Cible |
| --- | --- |
| Dimensions | 2D et 3D |
| Entrées | clavier, souris, manette, tactile |
| Mode | solo ou réseau (lobby, matchmaking) |
| Interface | HUD, menus, inventaires |
| Plateformes | desktop (Linux, macOS, Windows), mobile (Android, iOS), web (WebAssembly) |

Le **MVP** est plus modeste : un jeu **2D solo sur desktop**, défini par config. Le réseau, la 3D, la manette, le tactile et le mobile ne sont pas planifiés, mais l'architecture est contrainte dès maintenant pour ne pas les bloquer (voir la [roadmap](docs/ROADMAP.md)).

### Les paris techniques

1. **Générique avant tout.** Un moteur « pour tout faire » devient vite un moteur qui impose sa façon de faire. Ici, le cœur reste petit : ajouter un composant ou un système ne demande de modifier *rien* dans le cœur (voir le test `plugin_flow`).
2. **Le cœur ne contient aucun code de plateforme.** Fenêtre, GPU, audio, réseau vivent derrière des interfaces, dans des modules séparés. C'est ce qui permet de viser le mobile et le web sans réécrire le cœur.
3. **Un état de monde copiable et déterministe.** C'est la condition d'un bon netcode (prédiction, rollback), de la sauvegarde et du rejeu. On évite donc dès maintenant les choix qui l'interdiraient (pointeurs bruts partagés entre entités, état caché dans des globales).
4. **Qualité automatisée dès le premier commit.** Le coût d'une régression augmente avec la taille du moteur ; la CI est donc construite *avant* le code qu'elle protège.

### Ce que le projet n'est pas (pour l'instant)

- Pas un éditeur visuel.
- Pas un concurrent d'Unity, Unreal ou Godot : le but est un socle propre, testé et extensible, pas un produit fini.
- Pas de promesse de performance « haut de gamme » tant que rien n'est mesuré.

## Pour commencer

Prérequis : un compilateur C++20 (GCC 13+, Clang 16+, MSVC 2022), CMake ≥ 3.25, Ninja.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/examples/hello_ecs
```

Avant d'ouvrir une pull request, lancez l'équivalent local de la CI :

```bash
./scripts/check.sh
```

## Un aperçu de l'API

```cpp
#include "ge/app/app.hpp"

struct Position { float x, y; };
struct Velocity { float x, y; };

void apply_velocity(ge::ecs::World& world) {
    world.each<Position, const Velocity>(
        [](ge::ecs::Entity, Position& p, const Velocity& v) {
            p.x += v.x;
            p.y += v.y;
        });
}

// Un plugin regroupe ce qu'une fonctionnalité apporte.
struct MovementPlugin {
    void build(ge::app::App& app) const {
        app.add_system(ge::app::Stage::Update, apply_velocity, "apply_velocity");
    }
};

int main() {
    ge::app::App app;
    app.add_plugin(MovementPlugin{});
    app.world().spawn_with(Position{0, 0}, Velocity{1, 0.5F});
    app.run_frames(60);
}
```

Concepts du cœur :

- **Entity** : un identifiant avec génération. Une référence périmée n'est jamais confondue avec une nouvelle entité qui réutilise la même case.
- **Composant** : n'importe quel type C++ déplaçable, sans enregistrement préalable.
- **World** : entités, composants (stockage *sparse set*), requêtes typées `each<...>` et *ressources* (état global typé).
- **Système** : une fonction `void(World&)` rattachée à une étape (`Startup`, `PreUpdate`, `Update`, `PostUpdate`).
- **Plugin** : un objet avec `build(App&)` ou une fonction `void(App&)` qui enregistre composants, ressources et systèmes.

Modifier la structure du monde (ajouter ou retirer des composants, détruire des entités) pendant une itération `each` est interdit et détecté par une assertion ; créer des entités reste permis.

## Organisation du dépôt

```
include/ge/   API publique (core, ecs, app)
src/          implémentations
tests/        tests unitaires (framework maison sans dépendance)
examples/     programmes d'exemple, exécutés aussi comme tests
docs/adr/     décisions d'architecture
cmake/        options et politique d'avertissements
scripts/      scripts locaux (check.sh)
```

## CI/CD : ce qui contrôle chaque modification

Toute pull request doit passer le job agrégé **`ci-success`** :

| Contrôle | Pourquoi |
| --- | --- |
| Lint des workflows (actionlint) | une CI cassée ne protège rien |
| clang-format 18 | style uniforme, zéro débat en revue |
| Build + tests : GCC 13, Clang 18, AppleClang, MSVC | portabilité réelle, pas supposée |
| ASan + UBSan | détecte les erreurs mémoire et comportements indéfinis |
| clang-tidy 18 (avertissements = erreurs) | bugs probables et règles de conception |
| Couverture ≥ 90 % (gcovr) | un seuil, pour que les tests ne s'érodent pas |
| WebAssembly (Emscripten), Android (NDK), iOS | le cœur reste portable vers le web et le mobile |
| CodeQL | analyse de sécurité |
| Titre de PR en *Conventional Commits* | historique lisible, changelog et releases propres |

Les releases sont publiées automatiquement sur tag `vX.Y.Z` (le tag doit correspondre à la version CMake). Voir [CONTRIBUTING.md](CONTRIBUTING.md) pour le détail et les réglages GitHub recommandés (protection de branche exigeant `ci-success`, merge par squash).


## Modules et `gectl`

Un module ajoute des composants et des systèmes sans toucher au noyau : on décrit le module dans un `module.json`, `gectl generate` produit le code des composants et l'enregistrement des systèmes, il ne reste qu'à écrire la logique. Trois modules d'exemple sont dans `modules/`.

```bash
cmake --preset dev && cmake --build --preset dev
./build/dev/tools/gectl check            # valide les modules
./build/dev/tools/gectl catalog          # ce que les modules offrent, en JSON
./build/dev/tools/gectl generate         # régénère le code des modules
```

Format, règles et marche à suivre : [docs/MODULES.md](docs/MODULES.md).

## Un jeu, c'est du JSON

Un jeu est un dossier de fichiers JSON (manifeste, préfabs, scènes), où tout est entité. Aucun C++ : `games/sparks/` en est un exemple.

```bash
./build/dev/tools/gectl validate games/sparks/game.json      # valide le jeu
./build/dev/runner/gerun games/sparks/game.json --ticks 120  # le lance sans fenêtre
./build/dev/tools/gectl schema                               # schémas pour l'autocomplétion VS Code
```

Format et exemples : [docs/MANIFEST.md](docs/MANIFEST.md).

## Feuille de route

La roadmap détaillée (jalons, critères de sortie, décisions d'architecture, garde-fous) est dans [docs/ROADMAP.md](docs/ROADMAP.md). En résumé :

1. ✅ **M0** Cœur ECS, boucle applicative, plugins, CI.
2. ✅ **M1** Noyau de simulation : pas fixe, commandes différées, événements, hasard à graine, copie du monde.
3. ✅ **M2** Modules et catalogue (`module.json`, `gectl`).
4. ✅ **M3** Jeux décrits en JSON : manifeste, préfabs, scènes, validation, lanceur `gerun`.
5. **M4** Tests sans fenêtre (headless).
6. **M5** Skills : du prompt au jeu.
7. **M6 à M8** Plateforme desktop, rendu 2D, interface (fin du MVP).

Chaque jalon est une ou plusieurs pull requests qui passent la CI ci-dessus.

## Contribuer

Lisez [CONTRIBUTING.md](CONTRIBUTING.md). En bref : une pull request à la fois, des tests avec chaque changement, `./scripts/check.sh` vert avant de pousser.

## Licence

[MIT](LICENSE).
