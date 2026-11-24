# Complete Universe — Roadmap globale

Le jeu complet décrit par le GDD (`complete-universe-gdd.md`) et les fiches (`complete-universe-fiches-fragments.md`) : 5 à 10 heures d'exploration et de plateforme en 2D, 9 pouvoirs, 7 fragments, un monde généré planète après planète.

- Les arbitrages sont dans [DECISIONS.md](DECISIONS.md).
- Le détail du tutoriel est dans [TUTORIEL.md](TUTORIEL.md).

## Façon de travailler sur cette branche

Le jeu se construit sur la branche longue **`complete-universe`**, partie de `dev` le 24 novembre 2026. Le but est de perdre le moins de temps possible en vérifications : on vérifie peu à chaque étape, et tout à des jalons fixés.

**À chaque étape** (une PR vers `complete-universe`, que Martin fusionne) :

- `gectl generate` et `gectl schema` si un `module.json` a changé ;
- la compilation Windows de ce qui sert ;
- les tests du module touché, puis les scénarios du jeu (`gerun games/complete_universe/game.json --test`).

On ne lance ni `check.sh`, ni clang-tidy, ni clang-format, ni WSL, ni export. La CI de GitHub ne tourne pas : les PR vers `complete-universe` sont exclues de `ci.yml`. Seul le contrôle du titre de PR, quelques secondes, reste.

**À chaque jalon** (J1 à J7 ci-dessous), on vérifie tout :

1. `dev` est fusionné dans `complete-universe` s'il a avancé.
2. Dans WSL (`~/ge-verify`) : `TIDY=changed TIDY_BASE=dev CLANG_FORMAT=clang-format-18 ./scripts/check.sh`. C'est clang-tidy sur tout ce que la branche a changé, plus format, ASan, tests et scénarios de tous les jeux. On corrige tout ce qui tombe.
3. Sous Windows : tous les tests, `gectl list` (tous les jeux valides) et l'export Windows de `complete_universe`, lancé pour vérifier.
4. Une PR `complete-universe` → `dev` déclenche la CI normale. Martin la fusionne, puis la branche continue.

Règle qui limite la casse entre deux jalons : un module existant ne s'étend que par des champs dont le défaut ne change rien aux autres jeux.

## Ce qu'il faut au moteur

Tout reste générique : aucun module ne parle de fragment, de couleur du jeu ou de planète. Les noms ci-dessous sont provisoires.

| Module (nouveau ou étendu) | Ce qu'il fait, en général | Ce qu'il sert dans le jeu | Phase |
| --- | --- | --- | --- |
| `terrain2d` (nouveau) | Un terrain en masque de cellules : collision dans toutes les directions, creusement, génération par bruit, dessin par polygones (nouvelle primitive du renderer) | Sol des planètes et des astéroïdes, trous de récolte, destruction, explosion de la planète | 1 |
| `reserve` (nouveau) | Des réserves nommées (une liste du jeu) : quantité, maximum, consommation passive, plancher, coûts à l'usage ou par seconde, seuils qui déclenchent une action | Les 9 ressources, la vie (Noir, Blanc), le seuil de 100 unités, le plancher des structures | 1 |
| `bind` (nouveau) | Un nombre suit un autre nombre (affine, borné) | Bords de l'écran selon le Noir et le Blanc, taille des bulles d'inventaire, couleur des bras | 1 |
| `aim2d` (nouveau) | Le point visé dans le monde (pointeur à travers la caméra), sa direction et sa distance depuis une entité, bornées par une portée | Viser avec chaque main | 1 |
| `loadout` (nouveau) | Des emplacements (les mains) qui tiennent chacun un élément d'une liste : suivant, roue, échange, élément verrouillé qu'on tient sans effet, combinaison quand plusieurs sont activés ensemble | Les deux mains, la roue, les fusions, les couleurs grises (D3) | 1 |
| `attractor2d` (nouveau) | Un puits de gravité : attire les corps, grossit tant qu'on maintient, absorbe ce qui touche son cœur (entités, terrain) et rend ce qu'elles contenaient | Noir, saut, récolte (avec une bulle), trous noirs errants | 1 |
| `barrier2d` (nouveau) | Une bulle ou un segment : solide pour l'extérieur, bloque les effets venus de dehors, fige le temps de ce qu'elle contient, nombre maximum par propriétaire | Blanc, bulle de récolte, pause (D6) | 1 |
| `postfx` (étendu) | Couleur du vignettage, flou par basse résolution | Bords qui blanchissent ou noircissent, flou de la pause | 1 |
| `sound` (nouveau) | Effets sonores sur une action ou un événement, musique en couches dont le volume suit un nombre | Son de chaque pouvoir, musique qui s'enrichit à chaque fragment | 2, 8 |
| `gravity2d` et `camera2d` (étendus) | Gravité vers un point, déplacements et sauts dans le repère local, caméra qui tourne | Astéroïdes ronds (D1) | 3 |
| `color_gate` (nouveau) | Des groupes de couleur et un ensemble débloqué ; ce qui n'est pas débloqué est dessiné avec sa variante grise | Le retour des couleurs | 3 |
| `seek` (nouveau) | La direction de l'entité la plus proche d'une catégorie | Particules du Bleu, lumière du ciel | 4 |
| `area_effect` (nouveau) | Tant qu'il agit, change un nombre des entités d'une zone à une certaine vitesse | Vert (pousser), Orange (vieillir), Jaune (alimenter), Rose (apaiser) | 5 |
| `beam2d` (nouveau) | Un rayon qui rebondit sur certaines surfaces, disparaît dans d'autres, abîme et creuse | Rouge, tourelles | 5 |
| `shadow2d` (nouveau) | Une plateforme placée à la projection d'un objet depuis une lumière | Ombres solides du Jaune | 5 |
| `planes` (nouveau) | Plusieurs plans par scène, passage de l'un à l'autre, portails | Violet | 6 |
| `tether2d` (nouveau) | Deux entités liées partagent mouvement, gravité, dégâts ou temps ; le lien casse s'il est trop étiré | Rose | 7 |

## Phases et jalons

### Phase 0 — Cadrage (fait, 24 novembre 2026)

- La branche `complete-universe` et une CI réduite.
- Le GDD et les fiches dans `docs/complete_universe/`.
- Ce document, [DECISIONS.md](DECISIONS.md) et [TUTORIEL.md](TUTORIEL.md).

### Phases 1 et 2 — Le tutoriel → **J1 « Tutoriel jouable »**

La phase 1 construit les modules de base (le tableau ci-dessus, phase 1), la phase 2 la planète du tutoriel et ses dix étapes. Le détail, en 13 étapes, est dans [TUTORIEL.md](TUTORIEL.md).

**Livrable** : de l'écran titre au départ de la planète. Ouverture, mains, Noir, Blanc, fusion, inventaire, récolte, marche, saut, manque de Blanc, manque de Noir, explosion.

### Phase 3 — L'espace et les planètes → **J2 « Boucle d'exploration »**

1. **Astres ronds** : gravité vers un point, marche et saut dans le repère local, caméra qui tourne. Test sur un astéroïde dont on fait le tour.
2. **Envol et voyage** : pleine charge = envol (D5). Le voyage dure 5 à 10 secondes, sans contrôle : dérive, angle et zoom changent. Le point d'envol fixe le sens du voyage (Q1).
3. **Génération des planètes** : terrain par bruit (grandes planètes plates, astéroïdes ronds), grottes et plafonds, gisements de matière et de carbone, biomes selon la région. Graine tirée à chaque voyage.
4. **Couleurs non débloquées** (`color_gate`) et **lumière du ciel** qui guide gratuitement vers l'Observatoire (P10).
5. **Mort et réapparition** : écran qui blanchit ou noircit, réveil dans l'espace avec un peu de Noir et de Blanc, dérive vers une nouvelle planète. Sauvegarde (fragments, autres ressources) et reprise d'une partie (Q3).
6. **Premiers dangers** : astéroïdes en collision, essaims de débris attirés par le personnage, trous noirs errants à enfermer dans une barrière.
7. **Son**, premier pas : un son par pouvoir et une musique de base.

**Livrable** : après le tutoriel, on dérive d'astre en astre, on récolte et on survit indéfiniment.

### Phase 4 — Le Bleu → **J3 « Premier fragment »** (première démo, environ 1 h)

1. **Perception** (`seek`) : particules de la main bleue vers la ressource de la couleur de l'autre main, grise comprise (D3). Elle coûte de l'Écho ; gisements d'Écho et absorption pendant les voyages (P11).
2. **Fusions du Bleu** : vue panoramique (Noir + Bleu, dézoom) ; zone d'analyse (Blanc + Bleu), qui rend récoltable une ressource grise et la montre en couleur.
3. **Cycle d'un fragment**, commun à tous :
   - seuil de 100 unités ;
   - guidage vers la structure, qui consomme la ressource ;
   - structure qui se brise si la ressource s'épuise, et la mort pendant un guidage fait de même (Q2) ;
   - plancher des structures (P9).
4. **Région** : la ceinture d'astéroïdes (un minerai par astéroïde, gravité faible), avec ses biomes spéciaux et ses sentinelles qui sabotent les gisements.
5. **L'Observatoire**, fait main : entrée aveugle, plateformes fantômes, les trois éclats. À la fin, le fragment entre dans la main, apparaît sur le torse, et le bleu revient dans l'univers.
6. **Civilisations**, la mécanique commune posée sur les Veilleurs :
   - des pièces faites main placées au hasard ;
   - des voix incompréhensibles ;
   - une fresque au cœur, révélée plus tard par le Jaune ;
   - le choix de détruire ou de laisser en paix.

### Phase 5 — Palier 2 : Vert, Rouge, Jaune → **J4**

Chaque fragment demande la même chose :

- son pouvoir et ses fusions ;
- sa région et ses biomes spéciaux ;
- ses ennemis ;
- ses civilisations (sa couleur, avec Noir et avec Blanc) ;
- ses portes de couleur ;
- sa structure ;
- son son et le motif de son bras.

Ce qui est propre à chacun :

1. **Vert** (`area_effect`) : pousser des plantes, des cristaux et des ponts, faire grossir un ennemi ; anti-biomasse ; bulle-source déplaçable. Créatures hostiles, passives et amicales. Structure : la Graine-monde.
2. **Rouge** (`beam2d`) : laser continu qui rebondit sur les barrières, disparaît dans les trous noirs, et déclenche des réactions en chaîne sur la matière instable. Récolte par petites prises (la bulle chauffe), tempêtes. Structure : la Forge.
3. **Jaune** (`light2d`, `area_effect`, `shadow2d`) : halo de la main, mécanismes anciens, ombres solides, miroirs, lumière noire, source dans une barrière ; les fresques deviennent visibles. Structure : le Temple éteint.

Ensuite, des portes qui mélangent les couleurs obtenues, pour garder les anciens pouvoirs utiles.

### Phase 6 — Palier 3 : Orange, Violet → **J5**

1. **Orange** : accélérer le temps d'un objet (un nombre « âge » qu'`area_effect` fait avancer, et que les animations suivent). Remonter le temps avec Noir + Orange. Entité accélératrice avec Blanc + Orange. Rien ne passe une barrière. Ralenti près des trous noirs massifs. Structure : la Cité figée.
2. **Violet** (`planes`) : deux plans par planète, l'autre vu en transparence ; portail (Blanc + Violet) et destruction du portail (Noir + Violet). La matière exotique n'est que dans un plan. Structure : l'Objet impossible.

### Phase 7 — Rose et la fin → **J6 « Jeu complet »**

1. **Rose** (`tether2d`) : un seul lien à la fois, qui casse s'il est trop étiré. Apaiser une créature. Grappin (Noir + Rose), lien protecteur (Blanc + Rose). Le lien marche entre deux plans. Structure : la Toile.
2. **Civilisations à plusieurs pouvoirs**, les adversaires les plus dangereux, et la famille des prospecteurs (bâtiments et comportement communs, ressource qui change).
3. **Structure finale** :
   - sept zones dans l'ordre d'obtention ;
   - la contraction de l'univers ;
   - le centre ;
   - la cinématique de fin : fusion, nébuleuses, dézoom jusqu'au cœur du personnage.

### Phase 8 — Finition → **J7 « Version 1.0 »**

- Menus : écran titre, options (son, affichage, touches modifiables), pause dans la bulle (D6).
- Accessibilité : motif de chaque couleur sur le bras et son propre son, vérifiés sur toutes les couleurs.
- Musique en couches, une par fragment obtenu.
- Équilibrage global, durée vérifiée (5 à 10 heures), performances sur un portable.
- Exports Windows et Linux ; macOS, que le moteur n'exporte pas encore ; contrôles sur téléphone (Q4) et Android.

## Ordre de grandeur

Environ 60 étapes (PR) :

| Phases | Étapes |
| --- | --- |
| 1 et 2 | 13 |
| 3 | 7 |
| 4 | 6 |
| 5 | 12 |
| 6 | 8 |
| 7 | 7 |
| 8 | 7 |

Les plus gros risques techniques sont au début, et c'est voulu :

- le terrain destructible (collision et dessin rapides) ;
- la caméra qui tourne ;
- le flou sans shader ;
- un personnage aux animations fluides façon Ori, fait d'images générées.

Le tutoriel les rencontre tous sauf la caméra qui tourne.
