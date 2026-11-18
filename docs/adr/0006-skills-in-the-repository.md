# ADR 0006 : les skills vivent dans le dépôt et sont vérifiés

Statut : accepté.

## Contexte

Claude écrit les jeux et, quand il le faut, les modules. Ce qu'il sait faire dépend des instructions qu'on lui donne et des outils sur lesquels elles s'appuient. Des instructions qui citent une commande disparue ou un fichier déplacé le mènent dans une impasse sans que personne ne le voie.

## Décision

- Les skills sont des fichiers `.claude/skills/<nom>/SKILL.md`, dans le dépôt : relus en pull request, versionnés avec le code qu'ils décrivent.
- `scripts/check_skills.py` les vérifie en CI : front matter, commandes `gectl`, options de `gectl` et `gerun`, chemins du dépôt.
- Un module est un dossier : CMake découvre `modules/*/module.json` et les tests de `modules/<nom>/tests/`, `gerun` lie tous les modules. `gectl new-module` crée un squelette qui compile et passe son test.
- Le pipeline est de bout en bout par la ligne de commande : `catalog`, `new-module`, `generate`, `validate`, `gerun --test`. `gectl build` du plan initial est remplacé par l'export, reporté.
- Chaque règle de jeu annoncée a un scénario ; chaque module a ses tests.

## Conséquences

- Ajouter un module ne touche aucun fichier CMake ni les tests existants.
- Un changement d'outil qui casse un skill casse la CI.
- Les skills ne remplacent pas la relecture : un module écrit par Claude passe par une PR, la CI (sanitizers, clang-tidy, couverture) et l'avis d'une personne.
