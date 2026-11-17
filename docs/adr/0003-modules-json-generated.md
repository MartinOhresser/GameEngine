# ADR 0003 : modules décrits en JSON, code généré et versionné

Statut : accepté.

## Contexte

Le moteur doit accepter de nouveaux composants et systèmes très facilement, y compris écrits par Claude à partir d'un prompt, et ce sans casser les jeux existants ni les cibles mobiles et web.

## Décision

- Un module est un dossier avec un `module.json` (source de vérité), des systèmes écrits à la main et du code généré.
- Le format est du JSON avec un schéma, relu par `gectl check` avec des erreurs localisées (fichier, chemin, raison).
- Le code généré (structs de composants, déclarations de systèmes, `info()`, `install()`) est versionné. Les cibles croisées n'ont ainsi pas besoin d'exécuter `gectl`, et la CI vérifie la fraîcheur avec `gectl generate --check`.
- Les modules sont liés statiquement. Le catalogue (`gectl catalog`) est lu depuis les `module.json`, sans lier les modules.
- Le parseur JSON est écrit dans le dépôt : pas de dépendance à télécharger, même fonctionnement sur toutes les plateformes, erreurs avec ligne et colonne, clés dupliquées refusées.

## Conséquences

- Ajouter un composant ou un système est une modification de `module.json`, plus la logique du système.
- Un système déclaré mais non écrit échoue à l'édition de liens ; une référence inconnue échoue à `gectl check`.
- `info()` connaît les noms de composants qualifiés par module : c'est la base d'une empreinte du monde qui distingue les types et d'une sérialisation à venir.
- Les paramètres de jeu (valeurs, bornes) et la lecture des niveaux au moment de l'exécution arrivent avec le manifeste de jeu (M3).
