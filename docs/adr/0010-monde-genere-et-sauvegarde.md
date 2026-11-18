# ADR 0010 : créer des entités en cours de partie, sauvegarder, générer un monde

Statut : accepté (conçu et réalisé avec `games/doodle_jump`).

## Contexte

Le premier jeu demandé « pour de vrai » après le MVP est un Doodle Jump. Il demande trois choses que le moteur n'avait pas : un monde qui se **construit pendant la partie** (des plateformes à l'infini, des balles tirées), un **meilleur score qui survit** à la fermeture du programme, et de la physique de saut (gravité, rebonds à sens unique, caméra qui ne descend pas, mort). La règle du projet reste : un jeu est des fichiers de données, la logique est dans des modules génériques.

## Décisions

### 1. Créer un préfab en cours de partie : une file de demandes, pas un appel direct

Un module ne connaît pas les préfabs du jeu (ils sont dans les fichiers, lus par la couche `game`). Un système écrit donc une `SpawnRequest` (numéro de préfab, position, vitesse facultative) dans la ressource `SpawnRequests` ; l'application la sert **entre deux ticks**, par un « créateur de préfabs » que `setup_game` installe, comme elle sert déjà les changements de scène. Conséquences : rien n'apparaît en plein milieu d'un tick, l'ordre est celui des demandes, et un changement de scène abandonne les demandes en attente.

Le numéro de préfab s'écrit par son nom dans les fichiers grâce à une liste intégrée `prefab` (comme `scene` ou `action`) : une faute de frappe est détectée au chargement.

### 2. Les listes du jeu sont disponibles par nom pendant la partie

Une sauvegarde se range sous un **nom** (« best_height »), pas sous un numéro qui changerait quand on ajoute un élément à une liste. La ressource `ListNames` donne le nom d'un élément de n'importe quelle liste du jeu. Le jeu déclare les noms sauvegardés dans sa liste `record`.

### 3. La sauvegarde est une ressource, hors de l'état du monde

`SaveData` associe des noms à des entiers. Elle n'est pas dans l'empreinte du monde (elle ne décrit pas la partie, mais ce que le joueur garde) et les scénarios partent d'une sauvegarde vide : ils restent déterministes. Ce que fait un lecteur du jeu (`geplay`) : charger un fichier texte avant la première image, le réécrire dès que `dirty` est vrai. Un autre lecteur (navigateur, téléphone) l'écrira autrement, sans toucher aux jeux. Pas de nombres à virgule : un score est entier.

### 4. Des modules petits et génériques, pas un module « Doodle Jump »

`gravity2d` (une gravité), `bounce2d` (des surfaces qui renvoient un corps qui tombe dessus, à sens unique), `patrol2d` (aller-retour), `spawner2d` (générer des rangées devant la caméra, retirer ce qui est derrière, tirer), `combat2d` (mourir, projectiles), `altitude` (la meilleure hauteur) et `records` (meilleur et dernier score). Chacun sert au moins un autre jeu possible. Trois modules existants gagnent des interrupteurs par axe (`action_control`, `screen_wrap`, `camera2d`) avec des valeurs par défaut qui ne changent pas les jeux existants.

Choix de détail :
- **L'axe y descend**, comme à l'écran ; la hauteur est `départ - y`.
- **`bounce2d` compare la position avant et après le déplacement** (le bas du corps un tick plus tôt, déduit de sa vitesse) plutôt que de garder un état : une chute rapide ne traverse pas une plateforme fine, et il n'y a rien à initialiser.
- **Une surface qui cède ne fait jamais perdre le rebond** : un corps qui traverse en un tick une plateforme cassable et une plateforme solide casse la première et rebondit sur la seconde.
- **La difficulté est une rampe sur la hauteur** (`x` au pied, `x_end` en haut) pour chaque réglage du générateur, plutôt qu'une table de niveaux.
- **Pas plus de 64 rangées par tick et par générateur** : une caméra qui saute très loin ne gèle pas le jeu.
- **Le hasard vient de `Random` seulement** : même graine, même monde (testé).

### 5. Des scènes d'essai pour que chaque règle ait son scénario

Un monde aléatoire ne produit pas à la demande « un monstre juste au-dessus du joueur ». Le jeu contient donc de toutes petites scènes `test_*` (générées par le même script que les images) qui ne servent qu'aux scénarios, avec les vrais préfabs. Chaque règle annoncée a son scénario, et on a vérifié, en cassant une règle à la fois, que le scénario correspondant échoue.

## Conséquences

- Un jeu peut maintenant générer son monde, tirer, et garder des records, sans C++.
- Le moteur reste sans code de plateforme : le fichier de sauvegarde est écrit par `geplay`, pas par le noyau.
- Limites connues : une seule sorte d'entité créée par demande (pas de hiérarchie), pas d'entité qui en désigne une autre, `SaveData` ne garde que des entiers, et aucune mesure de performance (pas de GPU).

## Ce qui reste ouvert

- Un composant `Boost` (hélice, jetpack) ; une animation de mort avant l'écran de fin ; la visée ; l'inclinaison du téléphone.
- Sauvegarde sur les autres plateformes (navigateur, mobile) : la même ressource, une autre écriture.
