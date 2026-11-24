# Complete Universe — Décisions

Ce que le GDD (`complete-universe-gdd.md`) et les fiches (`complete-universe-fiches-fragments.md`) laissaient ouvert ou imprécis. Trois sections :

- les choix de Martin ;
- les propositions par défaut, que Martin peut contester ;
- les choix techniques, faits sans lui.

Une décision prise plus tard s'ajoute ici avec sa date. Ce fichier prime sur le GDD en cas de conflit.

## 1. Tranché par Martin (24 novembre 2026)

| # | Sujet | Décision |
| --- | --- | --- |
| D1 | Forme des astres | **Les deux.** Les petits astres (astéroïdes, lunes) sont ronds : gravité vers leur centre, on en fait le tour, la caméra tourne avec le personnage. Les grandes planètes ont un sol plat qui défile : on ne voit jamais leur courbure en jouant. Vues de l'espace (cinématiques, voyage), elles sont rondes. |
| D2 | Vue du personnage et du sol | **Vue de côté, comme Ori, pas de 3/4** (précisé le 24 novembre 2026). L'ambiance reste le mélange Limbo et Ori. Le personnage est de profil et se retourne selon sa marche, mais ses animations, fluides comme dans Ori, le tournent un peu vers nous : on voit un peu son torse. On voit aussi un peu le dessus du sol, comme dans Ori (bord, herbe, tranche éclairée). |
| D3 | Guider vers une couleur pas encore obtenue | **Les couleurs grises se tiennent.** La roue montre les couleurs du palier en cours, en gris. On peut en tenir une : elle n'a aucun pouvoir et sert seulement de cible au Bleu. |
| D4 | Tutoriel, étapes 2 à 5 | **Immobile.** Le personnage ne sait pas encore marcher : marcher, c'est déjà utiliser le Noir. Tout ce qu'il faut est à portée du point d'atterrissage. L'ordre du GDD est gardé. |
| D5 | Saut et envol | **Espace, maintenu pour charger.** Le trou noir au-dessus de la tête grossit : plus haut, mais moins précis. Relâché à pleine charge (quelques secondes), le saut fait s'envoler. Les grottes et les plafonds empêchent les envols accidentels. |
| D6 | S'abriter dans sa barrière | **C'est la pause du jeu.** Une barrière autour du personnage arrête tout, l'intérieur comme l'extérieur, y compris la consommation passive. Le monde autour devient flou, pour qu'on ne puisse pas analyser la scène pendant la pause. Ça ne sert jamais à avancer. |
| D7 | Mourir dans le tutoriel | **On recommence l'étape en cours.** Fondu, puis le personnage reprend au début de l'étape avec les réserves qu'il avait à ce moment-là. |
| D8 | Roue des couleurs | **Ralenti fort** (environ 20 %) tant qu'elle est ouverte. |

Conséquences de D2 :

- Le GDD place le noir à droite du corps et le blanc à gauche. En marchant vers la droite, on voit donc surtout sa moitié gauche, la blanche ; vers la gauche, la noire. Le torse, un peu tourné vers nous, laisse voir la ligne de séparation.
- Le clic gauche commande toujours **sa** main gauche, quel que soit le côté où il regarde. Ses animations montrent ses deux bras.

Conséquences de D6 :

- La touche Échap fait le même geste : le personnage s'enferme dans une barrière. Le menu de pause (reprendre, options, quitter) s'affiche dans la bulle.
- Une barrière posée ailleurs que sur le personnage reste un pouvoir normal : elle protège ce qu'elle contient, et le temps y est figé.

## 2. Proposé par défaut (à contester)

Ces propositions s'appliquent tant que Martin ne dit rien.

### Contrôles et pouvoirs

| # | Sujet | Proposition |
| --- | --- | --- |
| P1 | Touche de changement et roue | Touche de changement maintenue + clic d'une main : couleur suivante pour cette main. Touche maintenue plus d'un tiers de seconde sans clic : la roue s'ouvre (avec le ralenti de D8). La direction de la souris choisit une couleur, le clic d'une main la lui donne. |
| P2 | Deux mains, une couleur | Une couleur n'est que dans une main à la fois. Donner à une main la couleur de l'autre échange les deux. |
| P3 | Fusion | Les deux boutons enfoncés ensemble (à 0,15 s près) font la fusion, à l'endroit visé. Sinon chaque main agit seule. |
| P4 | Barrières et corps | Une barrière est un sol et un mur pour tout ce qui vient de l'extérieur : on peut marcher dessus. La bulle de récolte ne compte pas dans les 3 barrières. |
| P5 | Mains au début du tutoriel | Les deux mains sont vides au réveil (bras gris) : l'étape 2 consiste à y mettre le Noir et le Blanc. |

### Monde et progression

| # | Sujet | Proposition |
| --- | --- | --- |
| P6 | Planète du tutoriel | Ronde vue de l'espace, dans les cinématiques ; sol plat en jouant (D1). On passe de l'une à l'autre par un fondu pendant le zoom. |
| P7 | Inventaire | Les bulles apparaissent autour du corps, dans le monde. Le jeu continue pendant ce temps. |
| P8 | Durée de vie | Réserves pleines, sans rien faire, le personnage tient environ 5 minutes. Valeur d'équilibrage. |
| P9 | Structures | « Limiter le Noir et le Blanc » : dans une structure, les réserves ne descendent pas sous 30 %. En dessous, les pouvoirs coûteux ne partent plus, et la consommation passive s'arrête. |
| P10 | Avant le Bleu | Juste après le tutoriel, la lumière du ciel mène à l'Observatoire en 2 ou 3 planètes, sans seuil de 100 unités. |
| P11 | Écho originel | Le personnage en absorbe un peu pendant chaque voyage, dès le début du jeu. Il arrive donc au Bleu avec une petite réserve. |
| P12 | Taille du monde | 3 à 5 planètes par fragment, de 8 à 12 minutes chacune, plus la structure (15 à 20 minutes) : environ 60 minutes par fragment. |

### Questions ouvertes du GDD (§17), avec une recommandation

| # | Question | Recommandation |
| --- | --- | --- |
| Q1 | Guidage et voyage aléatoire | La proposition du GDD : le point d'envol fixe le sens du voyage, seule la planète d'arrivée est aléatoire. |
| Q2 | Mort pendant un guidage | La structure se brise, comme quand on manque de ressource. Ce qui a été dépensé est perdu, le reste est gardé. |
| Q3 | Recharger une partie | Comme après une mort : on repart dans l'espace, avec les fragments et les ressources sauvegardées. Les planètes ne sont jamais sauvegardées. |
| Q4 | Contrôles sur téléphone | Voir à la phase 8 : joystick à gauche ; à droite, deux boutons de main qu'on fait glisser pour viser ; la roue s'ouvre par un appui long. |
| Q5 | Équilibrage | Coûts réglés fragment par fragment, au jalon de chacun ; seuil de guidage à 100 unités. |

## 3. Choix techniques (faits sans Martin)

| # | Sujet | Choix |
| --- | --- | --- |
| T1 | Vue de côté façon Ori | Le jeu est en 2D (x, y), vu de côté, sans profondeur de jeu. Le dessus du sol est dessiné comme une tranche vue légèrement d'en haut (bord, herbe, lumière). Le torse visible vient du dessin et de l'animation : beaucoup d'images par mouvement, générées par `make_assets.py`, ou un corps d'ovales articulés animé par le moteur. Le choix se fait à U13, selon le rendu. |
| T2 | Décor destructible | Un module de terrain : un masque de cellules (quelques pixels chacune) qu'on creuse, avec lequel les corps entrent en collision dans toutes les directions, ce qui marche aussi pour un astéroïde rond. Il est dessiné par des polygones (marching squares), grâce à une nouvelle primitive du renderer. |
| T3 | Couleurs non débloquées | Pas de shader : chaque image colorée a sa variante grise, générée par `make_assets.py`. Un module choisit la variante selon les couleurs débloquées. |
| T4 | Flou de la pause (D6) | Le monde est dessiné en basse résolution puis agrandi avec lissage. Pas de shader. |
| T5 | Ralenti | Les vitesses passent par `transform2d.SpeedScale` et un facteur de temps global. Les scénarios restent déterministes. |
| T6 | Sauvegarde | `SaveData` du moteur, en entiers : fragments obtenus, ressources autres que le Noir et le Blanc. |
| T7 | Nom du jeu dans le dépôt | `games/complete_universe/`. |
