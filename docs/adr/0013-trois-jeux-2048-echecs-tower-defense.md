# ADR 0013 : trois jeux pour éprouver le moteur (2048, échecs, tower defense)

Statut : proposé (plan ; les écarts constatés à l'implémentation s'ajoutent à la fin, comme pour les ADR 0011 et 0012). Révisé le 20 novembre : décisions de l'auteur (promotion à quatre pièces, tower defense plus grand avec amélioration des tours, 2048 en premier) et **revue de généricité** de `board_moves` et `combat2d` (sections 0, 1b et 3).

## Contexte

Après l'audit de généricité (modules sans « haut » ni « bas », jeu de référence `sideways_jump`), on éprouve le moteur avec trois jeux d'un autre genre que les plateformes. Règles du jeu :

- **Du JSON pur** : aucune ligne de C++ propre à un jeu. Ce qui manque devient un **module générique**, réutilisable par n'importe quel autre jeu.
- **Exportables partout où l'export existe** : zips Windows et Linux, APK Android, avec une vérification en CI pour chacun.
- **Jouables au clavier, à la souris et au doigt**, sur des écrans de formes différentes (portrait et paysage).
- **Déterministes** : mêmes entrées et même graine, même partie (indispensable aux scénarios, et plus tard au réseau).

| Jeu | Ce qu'on attend |
| --- | --- |
| **2048** | Le jeu complet : fusions, score, meilleur score gardé, écran de victoire à 2048, possibilité de continuer ensuite, fin de partie quand rien ne bouge, **recommencer à tout moment**. |
| **Échecs** | Deux joueurs **en local** : mouvements interdits refusés (y compris ceux qui laissent son roi en échec), roque, prise en passant, promotion, échec, **mat**, **pat**. Pas d'ordinateur adverse ni de réseau pour l'instant. |
| **Tower defense** | **Un niveau**, quelques ennemis et quelques tours, de l'argent gagné en battant les ennemis, des vagues, une défaite quand la base n'a plus de vies, une victoire à la dernière vague. |

## Ce que le moteur donne déjà (à réutiliser, pas à refaire)

| Besoin | Déjà là |
| --- | --- |
| Positions, vitesses, gravité, collisions, rebonds | `transform2d`, `gravity2d`, `collision2d`, `bounce2d` |
| Projectiles et points de vie | `combat2d` (`Projectile`, `Target.health`, `Mortal`) |
| Créer des entités depuis des préfabs, au hasard et de façon reproductible | `spawner2d` (`Generator`, `Shooter`) et la ressource `Random` |
| Dessin, texte, animation par images | `sprite2d`, `text2d`, `debug_draw` (carrés) |
| Interface : ancrage, grilles, boutons à focus, barres, textes liés à un nombre | `ui` (`Anchor`, `Grid`, `Menu`, `Button`, `Bind`, `Bar`) |
| Changer de scène, garder des entités entre deux scènes | `scene_switch` (`GoToScene`, `Persistent`) |
| Meilleur score gardé entre deux parties | `records` |
| Souris et clavier ; doigts ; boutons à l'écran | `input.json` (`mouse:`, `key:`, `touch:`), `touch_controls` |
| Vitesse du jeu (x2) | `transform2d.Params.speed_scale` |
| Fenêtre qui s'adapte à l'écran | `window.adapt` et `ui.Anchor` |
| Export Windows, Linux, Android ; lanceur | `gectl export`, `gelauncher` |

## Ce qui manque

| Besoin | 2048 | Échecs | Tower defense | Module |
| --- | :-: | :-: | :-: | --- |
| Plateau de cases, position en colonne/ligne, case sous le pointeur | oui | oui | oui | `grid2d` |
| Animer un nombre (glisser, grossir, fondre) avec une courbe | oui | oui | oui | `tween2d` |
| « Quand ce nombre dépasse X, lever une action » ; « sur cette action, changer ce nombre » | oui | oui | oui | `rules` |
| Phases du jeu (jouer, victoire, fin) et ce qui est visible dans chacune | oui | oui | oui | `phase` |
| Glissement au doigt ou à la souris | oui | | | `gesture` |
| Faire glisser et fusionner des pièces sur une grille | oui | | | `slide_merge` |
| Tour de jeu, joueur courant | | oui | | `turns` |
| Déplacements sur une grille décrits en données ; menaces ; « ne peut plus bouger » | | oui | | `grid_moves` |
| Suivre un chemin | | | oui | `path2d` |
| Viser et tirer sur une cible à portée | | | oui | `autofire` |
| Vagues d'ennemis | | | oui | `waves` |
| Argent, prix, récompense | | | oui | `economy` |
| Rectangles, cercles, traits sans image | oui | oui | oui | `debug_draw` (extension) |
| Adapter un plateau à l'écran | oui | oui | oui | `camera2d` (extension) |
| Cacher une entité ; attacher une entité à une autre | oui | oui | oui | `transform2d` (extension) |
| Une image qui dépend d'un nombre ; un texte lié à soi-même | oui | oui | oui | `sprite2d`, `ui` (extensions) |
| Points de vie, dégâts de toute source, mort | | | oui | `health` (reprise de `combat2d`) |
| Sortir d'un côté de l'écran déclenche une action | | | oui | `bounds2d` (reprise de `Cull` et de `fall_death`) |
| Dégâts de zone, ralentissement à l'impact | | | oui | `combat2d` (recentré) |

## Décisions

Chaque module reste **utile hors de son jeu d'origine** ; la colonne « Autres usages » est le test de généricité demandé à chaque pull request.

### 0. Règle : un module nomme un mécanisme, jamais un jeu

Le premier jet de ce plan nommait des modules d'après le jeu qui les motive (`board_moves` avec « roi », « échec », « mat », « roque » ; `towers` ; `combat2d` avec `Mortal` et `fall_death`). La revue de généricité fixe trois critères, vérifiés à chaque pull request :

1. **Aucun vocabulaire de jeu dans les noms.** Le module dit « menacé », « ne peut plus bouger », « tire sur sa cible » ; c'est le **jeu** (en JSON, avec `rules.Watch`) qui nomme « échec et mat ». Les composants parlent de mécanismes (garde, motif, zone, dégâts), pas de pièces ni de tours.
2. **Une règle propre à un jeu est décomposée en primitives** que d'autres jeux combinent (le roque devient « un second déplacement attaché au premier » ; la promotion devient « une zone, une action, un choix »), au lieu d'un cas particulier dans le code.
3. **Un test d'usage étranger** : chaque module a au moins un scénario ou un test qui l'emploie autrement que le jeu qui l'a motivé (pièces inventées sur un plateau 5×5, une tourelle qui tire sur des joueurs, une barre de vie sur un monstre de plateforme).

### 1. Les fondations, communes aux trois jeux

**`grid2d`** (nouveau). Un plateau rectangulaire de cases.
- `Grid { columns, rows, cell_width, cell_height, origin_x, origin_y }` : une entité par plateau.
- `GridPos { col, row, grid }` : la case d'une entité ; le système `snap` pose sa `Position` au centre de la case.
- Un index « qui est sur quelle case » (une entité au plus par case et par `layer`), consultable par les autres modules.
- `GridCursor { grid, col, row, over, pressed_action }` : la case sous le pointeur ou le doigt ; un clic lève l'action et note la case (`cell_col`, `cell_row`).
- `Blocked` : une case qui n'accepte rien (cases hors chemin constructible d'un tower defense).
- `Zone { grid, col_min, col_max, row_min, row_max, owner, action }` : une zone de cases ; une entité qui y arrive lève l'action (l'arrivée d'un pion au bout du plateau, une sortie de niveau, une case piégée).
- `Selectable` et `Selection { grid, selected, action }` : l'entité choisie sur le plateau par le curseur (une pièce, une tour déjà posée), que l'interface étiquette pour montrer ses boutons.
- Autres usages : match-3, Tetris, Sokoban, jeu de rôle au tour par tour, ferme.

**`tween2d`** (nouveau). Anime un nombre d'un composant vers une valeur.
- `Tween { field ("module.Component.field"), from, to, ticks, easing (linear, ease_out, ease_in_out, back), on_done (action), despawn_when_done }`, pour la position, l'échelle, la transparence, la couleur.
- Tourne dans `FixedUpdate` : déterministe, donc testable par scénario.
- Autres usages : transitions de menu, clignotement des dégâts, recul d'un canon, pièces qui volent vers le score.

**`rules`** (nouveau). Les petites règles de jeu en données.
- `Watch { source (tag), field, op (>=, <=, ==, !=, <, >), value, action, once }` : lève l'action quand la condition devient vraie (victoire à 2048, base à 0 point de vie).
- `OnAction { action, target (tag), field, set | add, value }` : sur une action, change un nombre (choisir une tour, passer la vitesse à x2, vider le score).
- Autres usages : portes, interrupteurs, quêtes, jauges, mode « pause ».

**`phase`** (nouveau). Les phases d'un jeu et ce qu'on voit dans chacune.
- `Phase { current }` (liste du manifeste `phase`), `Transition { from, to, action }`, `ShowIn { phases }`, `ActiveIn { phases }`.
- Une entité hors de ses phases reçoit `transform2d.Hidden` (voir plus bas) : plus dessinée, ses boutons plus actifs. L'écran de victoire, le menu de promotion et la pause sont des entités visibles dans une seule phase, **sans changer de scène** (la partie reste là, dessous).
- Autres usages : pause, tutoriel, boutique, dialogues.

**`gesture`** (nouveau). Du doigt ou de la souris vers des actions.
- `Swipe { min_distance, max_ticks, left, right, up, down (actions) }` : un glissé assez long lève l'action de sa direction ; `Tap` en option.
- Se bâtit sur les sources `touch:` et `mouse:` existantes, donc marche partout.
- Autres usages : menus qu'on fait défiler, lancer d'un objet, changement de page.

**Extensions de modules existants**
- `transform2d` : `Hidden` (ni dessinée ni cliquable, lu par `sprite2d`, `text2d`, `debug_draw`, `ui`), `Owner { entity }` (l'entité qui en a créé une autre, par un lien) et `Attach { offset_x, offset_y }` (suit son `Owner`, disparaît avec lui : barre de vie, portée d'une tour) et `SpeedScale { factor, ticks }` (multiplie la vitesse de cette entité un instant : ralentissement, gel, accélération).
- `debug_draw` : `Rect` (largeur, hauteur, plein ou contour), `Circle` (rayon, plein ou contour) et `Line`, pour dessiner des plateaux, des portées et des barres de vie sans image.
- `camera2d` : `CameraFit { width, height, center_x, center_y, margin_*, max_zoom }` règle le zoom et la position pour qu'une zone du monde (le plateau) tienne dans l'écran, quelle que soit sa forme ; `ui.Anchor` garde l'interface au bord.
- `sprite2d` : `FrameBind { field, first_frame, log2, frame_count }` choisit l'image ou la teinte d'après un nombre (la couleur d'une tuile d'après sa valeur).
- `ui.Bind` : champ `own` (l'entité lit son propre nombre : le texte sur une tuile, une barre de vie).

### 1b. Reprise de `combat2d` : `health`, `combat2d` recentré, `bounds2d`

Constat sur le code actuel :

- **Deux modèles de vie qui ne se parlent pas** : `Mortal` (meurt au premier contact, sans points de vie) et `Target.health` (ne baisse que par un `Projectile`). Un monstre à points de vie qui blesse au contact, un joueur à trois vies ou une base qu'on attaque sont impossibles.
- **`Mortal.fall_death`** (mourir en sortant de l'écran) est une idée de jeu de plateformes dans un module de combat ; et **`spawner2d.Cull`** fait la même chose (« au-delà d'un côté de l'écran »). La fonction « bord d'écran dans un sens » est écrite deux fois (dans `combat2d` et `spawner2d`).

Reprise :

- **`health`** (nouveau) : `Health { amount, max, on_zero (action), despawn_on_zero }` ; `Damage { amount }` est une **demande** que n'importe quelle source ajoute à une entité (le module l'applique au tick suivant et la retire) : les sources de dégâts n'ont pas à connaître les cibles. `Dead` marque l'entité à zéro (`economy` y paie sa prime, `rules` y réagit). Un joueur à trois vies : `max` 3 ; un monstre tué en un coup : `max` 1. **Remplace `Mortal` et `Target`.**
- **`combat2d`** (recentré sur les sources de dégâts 2D) : `ContactDamage { amount, layers }` (toucher blesse), `Projectile { damage, splash_radius, homing, pierce, on_hit_speed_factor, on_hit_speed_ticks }` (dégâts de zone, poursuite, traverse plusieurs cibles, ralentit ce qu'il touche par `transform2d.SpeedScale`). Plus de `Mortal` ni de `Target`.
- **`bounds2d`** (nouveau) : `OutOfView { side_x, side_y, margin, action, despawn }` : ce qui sort d'un côté de l'écran lève une action et/ou disparaît. **Remplace `spawner2d.Cull` et `Mortal.fall_death`** ; le calcul du bord d'écran n'existe qu'une fois. Autres usages : balles perdues, ennemis qui s'échappent, fin de niveau, objets laissés derrière.
- **Migration** : `doodle_jump` et `sideways_jump` passent aux nouveaux composants (changement de noms qui casse les anciens fichiers de jeu, noté dans le CHANGELOG) ; leurs scénarios doivent passer **sans modification de comportement**.

### 2. 2048 : `slide_merge`

**`slide_merge`** (nouveau, sur `grid2d`).
- `SlideBoard { grid, merge (sum | next), left, right, up, down (actions), score, max_value, moved, no_moves }`.
- `Tile { value, merged_this_move }` : une tuile par case.
- Système `slide` : à l'action d'une direction, chaque tuile glisse jusqu'à l'obstacle ; deux tuiles égales qui se rencontrent fusionnent **une seule fois par coup** (`sum` : la valeur double ; `next` : elle monte d'un cran, pour les variantes) ; le score gagne la valeur obtenue ; `moved` dit si le coup a changé quelque chose (un coup sans effet ne fait pas apparaître de tuile).
- `SpawnOnEmpty { grid, prefab, count, weights }` (dans `grid2d`) : sur l'action `moved`, pose une tuile sur une case libre tirée au hasard (2 à 90 %, 4 à 10 % avec deux poids).
- Les déplacements créent des `Tween` (glisse, apparition, « pop » de la fusion) : la logique est faite d'un coup, l'écran s'anime derrière.
- `no_moves` : plus de case libre ni de fusion possible.
- Autres usages : jeux de glissement et de fusion (variantes à nombres ou à lettres), puzzles de tuiles.

**Le jeu `games/game_2048`** assemble : un `SlideBoard` 4×4 (`CameraFit` dans l'écran), des tuiles dont l'image dépend de leur valeur (`FrameBind` log2) et qui portent leur nombre (`Bind` `self`), les touches fléchées, WASD et `Swipe`, `records` pour le meilleur score.
- **Victoire** : `Watch { max_value >= 2048, once }` lève `won` ; la phase `won` montre « Vous avez gagné » avec **Continuer** (retour à la phase `play`, la règle ne rejoue pas) et **Nouvelle partie**.
- **Recommencer à tout moment** : touche R et bouton, depuis n'importe quelle phase (rechargement de la scène).
- **Fin de partie** : `no_moves` lève `lost` ; la phase `lost` propose Nouvelle partie.
- Interface : score et meilleur score ancrés en haut, boutons en bas, adaptée aux écrans en portrait comme en paysage.

### 3. Échecs : `turns` et `grid_moves`

**`turns`** (nouveau). Le tour de jeu.
- `Players { count }`, `Turn { current, number }`, action `end_turn` : passe au joueur suivant, lève `turn_changed`, remet `moves_left` à son départ.
- `Team { player }` sur une entité : à quel joueur elle appartient (une pièce, une carte, une unité). Pas `Owner` : ce nom est pris par `transform2d.Owner`, le lien vers l'entité qui en a créé une autre.
- Autres usages : jeux de cartes, jeux de plateau, tactique au tour par tour, jeux à plusieurs en tournante.

**`grid_moves`** (nouveau, sur `grid2d` et `turns`). **Comment une entité se déplace sur une grille**, en données. Le module ne connaît ni les échecs ni les mots « roi », « échec » ou « mat ».
- `Mover { kind, moves_made, last_move_turn, last_move_distance }` : le type est un élément de la liste `mover_kind` du manifeste ; les compteurs disent combien de fois, quand et de combien de cases l'entité s'est déplacée (ce que les conditions ci-dessous lisent).
- `Pattern { kind, dx, dy, range, mode, jump }` : **une entité par façon de se déplacer** (les champs sont des nombres). Un glisseur a un sens `(dx, dy)` et une portée (0 : illimitée), un sauteur a `jump` et une portée de 1 ; `mode` : `move_or_capture`, `move_only` ou `capture_only`. Conditions facultatives : `first_move_only` (n'a jamais bougé), `needs_clear` (les cases traversées sont libres), `safe_path` (les cases traversées ne sont pas menacées).
- **Effets facultatifs d'un motif, pour les coups composés** : `victim_dx`, `victim_dy` (la cible prise n'est pas sur la case d'arrivée) avec `victim_needs_leap` (elle vient de faire un grand pas au tour précédent) ; `companion_kind`, `companion_from_dx`, `companion_to_dx` (un second déplacement a lieu en même temps). C'est avec ces champs que la prise en passant et le roque s'écrivent en données ; aucun cas particulier propre aux échecs n'est dans le code.
- `Guard { player }` : une entité qu'on ne doit pas laisser **menacée** (le roi, mais aussi un convoi à protéger ou un chef à garder). Un coup qui laisserait un `Guard` de son camp sur une case menacée est **illégal** : c'est ce filtre qui interdit les mouvements illégaux, en plus des motifs.
- Système `threats` : pour chaque camp, les cases que ses entités pourraient prendre. Système `legal_moves` : pour l'entité sélectionnée, les cases permises (éclairées par l'interface).
- `Side { player, status }` : **des faits, pas des noms de jeu** : `status` est un jeu de bits (1 : un de ses `Guard` est menacé, 2 : il ne peut plus faire aucun coup légal). Le jeu les nomme avec `rules.Watch` : « échec » = `status == 1`, « **échec et mat** » = `status == 3`, « **pat** » = `status == 2`.
- **Une seule porte d'entrée** : `MoveRequest { from_col, from_row, to_col, to_row }` ; la touche du joueur la remplit aujourd'hui, un adversaire réseau la remplira plus tard sans toucher aux règles. Le module lève `moved` avec la case.
- **La promotion n'est pas dans le module** : c'est une `grid2d.Zone` (les rangées du fond) qui lève une action, la phase `promotion` du jeu affiche **un menu de quatre boutons (dame, tour, fou, cavalier)**, et chaque bouton lève une action que `rules.OnAction` transforme en `Mover.kind`.
- Autres usages : dames (sans prise multiple obligatoire), variantes d'échecs et jeux apparentés, unités d'un jeu tactique (une portée de déplacement est un glisseur de portée n, les cases atteignables servent à éclairer), « tour du cavalier » et autres puzzles.
- **Test d'usage étranger** : des pièces inventées (un « chameau » qui saute en `(3, 1)`, une « amazone » qui glisse comme une dame et saute comme un cavalier) sur un plateau 5×5, plus un scénario tactique où une unité éclaire sa portée.

**Le jeu `games/chess`** assemble : un `Grid` 8×8 avec des `Rect` en damier (`CameraFit`), 32 pièces (images générées par un script d'assets, comme `doodle_jump`), `turns` pour alterner, `GridCursor` et `Selection` pour choisir.
- **Interaction** : on touche une pièce de son camp, ses coups légaux s'éclairent, on touche une case éclairée pour jouer (touche-puis-touche : marche à la souris et au doigt) ; un `Tween` déplace la pièce.
- **Règles** : tous les mouvements des pièces, coups interdits refusés (dont ceux qui laissent son roi menacé), **roque** des deux côtés, **prise en passant**, **promotion** par le menu des quatre pièces, **échec**, **mat** et **pat**.
- **Phases** : `play`, `promotion`, `over`. L'écran de fin dit « Échec et mat — les Blancs gagnent » ou « Pat », avec Nouvelle partie.
- **Interface** : joueur courant, « Échec ! », case du dernier coup, boutons Abandonner et Nouvelle partie.
- Local uniquement ; pas d'horloge, pas d'annulation, pas d'adversaire automatique.

### 4. Tower defense : `path2d`, `autofire`, `waves`, `economy`

Ces modules reposent sur `health`, `combat2d` et `bounds2d` de la section 1b.

**`path2d`** (nouveau). Suivre un chemin.
- `Waypoint { path, index }` : un point du chemin (une entité par point).
- `PathFollower { path, speed, index, progress, end_target, end_damage }` : suit les points dans l'ordre à sa vitesse (le ralentissement vient de `transform2d.SpeedScale`) ; l'entité se tourne vers le prochain point ; arrivée au dernier, elle lève `reached_end`, ajoute un `Damage { end_damage }` à l'entité étiquetée `end_target` (la base, un joueur, un convoi) et disparaît.
- Autres usages : véhicules sur une route, patrouilles, projectiles qui suivent une courbe.

**`autofire`** (nouveau, sur `health`). Viser et tirer.
- `AutoFire { range, cooldown, prefab (le projectile), projectile_speed, targeting (first | last | closest | strongest | weakest), target_layer, action }` : une tourelle, mais aussi un ennemi qui tire sur le joueur, un héros qui attaque seul ou un gardien.
- Système `aim_and_fire` : choisit une cible parmi les entités de la couche visée (qui ont une `Health`) dans la portée ; `first` utilise `path2d.PathFollower.progress` s'il existe (la plus avancée sur le chemin), sinon la plus proche ; tire un projectile dans sa direction et lève l'action (pour le son).
- Autres usages : tourelles d'un jeu de tir, ennemis qui visent, défense de base, tir automatique d'un héros.

**`waves`** (nouveau, sur `spawner2d`). Des vagues d'ennemis.
- `WaveGroup { wave, prefab, count, interval_ticks, delay_ticks }` : un groupe d'une vague (**une entité par groupe**, puisque les champs sont des nombres ; une vague en a plusieurs).
- `WaveController { current, auto_next, rest_ticks, spawn_x, spawn_y }` : lève `wave_started`, `wave_cleared` (plus aucune entité de la vague en vie), `all_waves_cleared` ; l'action `start_wave` lance la suivante.
- Autres usages : jeux de survie, arènes, vagues de monstres.

**`economy`** (nouveau, sur `health` et `grid2d`). Argent, prix, récompense, achat.
- `Wallet { amount }` (étiqueté), `Bounty { amount }` (versé quand l'entité meurt, par `Dead`), `Price { amount }`.
- `Placer { prefab, price }` : pose une entité sur la case cliquée si elle est libre et constructible, **retire le prix si le porte-monnaie suffit**, sinon lève `not_enough_money`.
- `Upgradable { next_prefab, price }` : **améliorer** (l'entité est remplacée par `next_prefab`, qui peut l'être encore : trois niveaux = deux améliorations) et `Sellable { refund }` : vendre rend une part du prix.
- Autres usages : boutiques, jeux de gestion et de construction, butin, coût d'une compétence, évolution d'un équipement.

**Le jeu `games/tower_defense`** assemble : **une carte 14×9** avec un chemin de points d'eau et des cases constructibles ; **4 tours** (canon rapide à un seul coup, mortier à dégâts de zone, tour de glace qui ralentit, tireur d'élite à longue portée), **chacune améliorable deux fois** ; **4 ennemis** (fantassin, coureur rapide, blindé lent et résistant, et un **chef** à la dernière vague) ; **12 vagues** de plus en plus fortes ; de l'argent au départ ; une **base** avec des points de vie.
- **Interface** : argent, vies et vague en haut (`Bind`) ; barre de boutiques en bas (choisir une tour, `rules.OnAction` règle le préfab du `Placer`) ; cercle de portée au choix de l'emplacement (`Circle`, `Attach`) ; barre de vie par ennemi (`Bar`, `Bind` `self`, `Attach`) ; bouton **Lancer la vague** et bouton **x2** (`speed_scale`) ; toucher une tour la sélectionne et la phase `tour_choisie` montre ses boutons **Améliorer** (prix) et **Vendre** (remboursement).
- **Fin** : la base à 0 point de vie lève `lost` (`Watch`) ; `all_waves_cleared` lève `won`. La phase de fin propose Nouvelle partie ; recommencer est possible à tout moment.

### 5. Un son d'accompagnement (facultatif)

`sfx` (nouveau, petit) : `Sfx { action, sound }` joue un son nommé quand l'action est levée (`AudioBackend` existe déjà, aucun module ne s'en sert). Ajoute le bruit des tuiles, des prises et des tirs sans changer les règles. À faire en dernier si le temps le permet.

### 6. Des jeux exportables partout

Les trois jeux ne contiennent que du JSON et des images : l'export copie les modules qu'ils utilisent, comme pour `doodle_jump`.
- **Windows et Linux** : `gectl export` (validation, scénarios rejoués, zip) ajouté à la matrice de CI et à la publication.
- **Android** : l'APK de chaque jeu, installé dans l'émulateur et joué par des gestes. Le script de CI reçoit une suite de touchers (`TAPS`) et de glissés (`adb shell input swipe`) pour jouer un coup de 2048, ouvrir une partie d'échecs et poser une tour.
- **macOS, iOS, Web** : la compilation de tous les modules est déjà vérifiée en CI ; l'**export** de ces cibles n'existe pas encore (ADR à part). Les trois jeux n'ajoutent aucune dépendance de plateforme.
- Le lanceur liste les trois jeux sans rien changer.
- Écrans de formes variées : `window.adapt`, `CameraFit` et `ui.Anchor` ; les scénarios n'adaptent pas la taille (déterminisme), la CI Android essaie des tailles d'écran différentes comme pour `doodle_jump`.

## Jalons

Chaque jalon est une pull request (ou deux) vers `dev`, avec ses tests, sa doc et son CHANGELOG. Les jeux arrivent dès que leurs modules sont prêts.

| | Jalon | Sortie |
| --- | --- | --- |
| G0 | **Plan** : cette ADR, la liste des modules dans le catalogue de la doc | le plan est relu et corrigé |
| G1 | **Petites extensions** : `transform2d.Hidden` et `Attach`, `debug_draw` (Rect, Circle, Line), `camera2d.CameraFit`, `sprite2d.FrameBind`, `ui.Bind` `self` | tests de chaque ajout ; un scénario qui dessine un plateau et le tient dans l'écran |
| G2 | **`tween2d`** | tests : courbes, fin, action de fin, déterminisme |
| G3 | **`grid2d`** (plateau, cases, curseur, `SpawnOnEmpty`) | tests ; clic sur une case sous la souris et au doigt |
| G4 | **`rules`** et **`phase`** | tests ; une démonstration de phase qui cache et montre un panneau |
| G5 | **`gesture`** | tests : glissé, assez long, assez rapide, dans les quatre sens |
| G6 | **`slide_merge`** et **le jeu `game_2048`** | scénarios : fusions, un coup sans effet ne crée rien, score, victoire à 2048, continuer, fin de partie, recommencer |
| G7 | **`turns`** et **`grid_moves`** (motifs, menaces, `Guard`, `Side`, `MoveRequest`) | tests : chaque motif, mouvements interdits, clouages ; pièces inventées sur un plateau 5×5 ; scénario tactique |
| G8 | **Coups composés** (`victim_*`, `companion_*`), `grid2d.Zone`, menu de promotion à quatre pièces et **le jeu `chess`** | scénarios : parties complètes (mat du berger, pat, roque des deux côtés, prise en passant, promotion à chacune des quatre pièces, coups illégaux refusés) |
| G9a | **Reprise de `combat2d`** : `health`, `bounds2d`, `combat2d` recentré, `transform2d.SpeedScale` ; `doodle_jump` et `sideways_jump` migrés | les scénarios existants passent sans changement de comportement ; test d'usage étranger (un monstre à trois vies qui blesse au contact) |
| G9b | **`path2d`, `autofire`, `waves`, `economy`** | tests de chaque module ; un niveau d'essai |
| G10 | **Le jeu `tower_defense`** | scénarios : une tour tue un ennemi et verse sa prime, achat refusé faute d'argent, amélioration et vente, dégâts de zone, ralentissement, fin de vague, chef, victoire, défaite |
| G11 | **Export et CI** : les trois jeux dans les matrices Windows, Linux, Android (touchers et glissés), la release, la doc | les trois APK installés, lancés et joués dans l'émulateur |
| G12 | **Son** (facultatif) : `sfx` | un son joué par action dans un scénario |

Ordre de dépendance : G1 à G5 sont les fondations ; 2048 (G6) ne demande qu'elles ; les échecs (G7, G8) ajoutent `turns` et `grid_moves` ; le tower defense (G9a, G9b, G10) ajoute la reprise de `combat2d` et ses quatre modules. **2048 est livré en premier** (G1 à G6) ; les deux autres jeux peuvent ensuite avancer en parallèle.

## Limites assumées

- **Beaucoup de modules** (quatorze nouveaux : `tween2d`, `grid2d`, `rules`, `phase`, `gesture`, `slide_merge`, `turns`, `grid_moves`, `health`, `bounds2d`, `path2d`, `autofire`, `waves`, `economy`, plus `sfx` facultatif). Chacun est petit et fait une chose ; c'est le prix d'éviter un module par jeu.
- **La reprise de `combat2d` casse des noms** : `Mortal`, `Target` et `spawner2d.Cull` disparaissent, les jeux existants sont migrés dans la même pull request.
- **Les échecs sont le gros morceau** : `grid_moves` reste le module le plus complexe du moteur, même décomposé en motifs, conditions, effets, gardes et zones. Son risque : des champs de plus pour un cas qu'un seul jeu emploie. Chaque champ doit avoir un second usage dans les tests, sinon il sort.
- **Échecs : règles de nulle partielles.** Pat : oui. Matériel insuffisant, triple répétition et règle des cinquante coups : plus tard (ils demandent de compter des pièces par type ou de se souvenir des positions passées).
- **Un champ de composant est un nombre.** Pas de liste dans un composant : les motifs d'une pièce, les groupes d'une vague et les points d'un chemin sont **une entité chacun**. Ce n'est pas un problème en JSON (un préfab peut regrouper), mais les fichiers sont plus verbeux.
- **Pas d'ordinateur adverse** (échecs) ni de réseau : prévu plus tard ; la porte d'entrée unique `MoveRequest` est faite pour ça.
- **Pas de manette** : le moteur n'a pas encore de source de manette ; ce n'est pas nécessaire pour ces jeux (clavier, souris, doigt).
- **Images** : les pièces d'échecs et les tours demandent des images générées par un script, comme `doodle_jump` (formes simples). Un habillage soigné est une autre tâche.
- **Export macOS, iOS, Web** : pas dans ce plan (la cible n'existe pas).

## Décidé le 20 novembre

- **Promotion** : un menu de quatre pièces (dame, tour, fou, cavalier), jamais automatique.
- **Tower defense** : plus grand que le premier jet : carte 14×9, 12 vagues, 4 tours, 4 ennemis dont un chef, **amélioration des tours** (deux niveaux) et vente.
- **Ordre** : **2048 en premier**, puis les échecs, puis le tower defense.
- **Généricité** : section 0 ; `board_moves` devient `grid_moves` (faits et primitives, pas de vocabulaire d'échecs), `towers` devient `autofire`, `combat2d` est repris (section 1b).

## Ce qui reste ouvert

Valeurs par défaut proposées ; à corriger si besoin.
- **Annuler le dernier coup** (2048 et échecs) : non, pour l'instant. Un `undo` demande de mémoriser l'état ; à discuter.
- **Nom des jeux** : les noms de jeux et de listes sont en `lower_snake_case` et doivent commencer par une lettre : le jeu s'appellera `game_2048` (dossier `games/game_2048`, `title` « 2048 »), les autres `chess` et `tower_defense`.
- **Matériel insuffisant aux échecs** : reporté (voir les limites).

## Écarts constatés à l'implémentation

### G1 : petites extensions

- **Le moteur n'avait aucun lien d'une entité à une autre**, et `Attach`, la cible d'une tourelle, la poursuite d'un projectile et la sélection d'une tour en ont besoin. Ajout minimal : `Entity::to_link()` / `from_link()` (l'identifiant plus un, 0 pour « aucune », avec sa génération : un lien vers une entité détruite ne retrouve jamais celle qui prend sa place), un champ `u64` pour le garder, et `SpawnRequest::owner` (`SpawnRequests::spawn_owned`) : la création depuis un préfab pose `transform2d.Owner` sur la nouvelle entité. Aucun nouveau type de champ.
- **`Attach` ne porte plus sa cible** : la cible est `Owner`, ce qui sert aussi à un projectile (son tireur) ou à un membre de vague (son contrôleur). `Attach` garde les décalages et `despawn_with_owner`.
- **Ordre de dessin** : `debug_draw` dessine ses formes à l'endroit de la liste des modules du jeu ; il n'y a pas de couche entre modules. Les formes passent donc avant les images si `debug_draw` est listé avant `sprite2d` (un plateau, une portée) ; ce qui doit être au-dessus est une image ou un contour dans le sens du dessin.
- `DrawCommand` gagne `x2`, `y2` et `thickness` ; le rendu SDL active le mélange alpha pour les formes.

### G2 : `tween2d`, `rules`, `phase`

- **Écrire un champ par son nom** n'existait pas (seulement `read`) ; `tween2d`, `rules.OnAction` et plus tard les prix en ont besoin. Chaque module généré a une fonction `write` (`ComponentInfo::write`), sœur de `read`.
- **Une valeur animée par entité n'est pas assez** : un composant ne peut être posé qu'une fois sur une entité. Un `Tween` anime donc le nombre de **sa propre entité** ou, avec `on_owner`, celui de l'entité dont il est le `Owner` : on peut en poser plusieurs sur une cible, par des entités d'effet. `Settle` (un nombre qui s'approche d'un but à chaque tick) remplace le « pop » d'une fusion : la tuile passe à 1,25 et revient à 1.
- **`transform2d.Scale`** (taille de dessin) a été ajouté : une seule animation de `Scale.factor` fait grossir à la fois l'image et le texte d'une tuile. **`transform2d.Disabled`** : une entité mise de côté (`phase.ActiveIn` la pose), lue par `rules` ; les autres modules la liront quand ils en auront besoin.
- **`rules` dépend de `ui`** pour `ui.Tag` (viser l'entité qui porte un nom). `Watch` regarde l'entité elle-même par défaut. Pas de comparaison à six opérateurs : un intervalle `min`–`max` dit « atteint 2048 » (`min`), « tombe à 0 » (`max`) ou « devient exactement » (les deux).
- **Les courbes de `Tween`** sont des nombres (0 linéaire, 1 départ lent, 2 arrivée lente, 3 les deux, 4 dépasse le but) : un champ de composant est un nombre.

### G3 et G5 : `grid2d` et `gesture`

- **Les cases ne sont pas des entités** : une `Grid` est une seule entité (sa `Position` est le centre du plateau), et ce qui est sur une case est une entité avec un `GridPos`. `ge/mod/grid2d/cells.hpp` (en-tête seul, dans un nouveau dossier `include/` du module, repris par CMake) donne aux modules qui en dépendent la mise en page (`Layout`) et qui est sur quelle case (`occupancy`) ; une entité `Disabled` ne compte pas comme occupant (c'est ce qui laisse une tuile fusionnée finir de glisser sans bloquer la case).
- **`GridPos.from_position`** : un préfab créé par un spawner a une position, pas une case ; il prend la case qui l'entoure à son premier tick. `SpawnOnEmpty` s'appuie dessus.
- **Une action « aucune » vaut 4294967295** dans `GridCursor`, `SpawnOnEmpty` et `Swipe` (comme pour `ui.Menu`) : un champ de renvoi à une action n'est pas obligé d'en avoir une.
- **`GridCursor` lit le pointeur à travers la caméra** et le `Viewport` ; il ne dépend pas du clavier ni de la souris, seulement d'actions (`mouse:` ou `touch:` selon `input.json`).
- **`Swipe` compte la durée depuis l'appui** (pas le tick de l'appui lui-même) : 10 ticks max veut dire 10 ticks entre l'appui et le relâchement.
- Reporté à G8 : `grid2d.Zone`, `Selectable` et `Selection` (les échecs et le tower defense en ont besoin, pas le 2048).

### G6 : `slide_merge` et `game_2048`

- **Le module** : `SlideBoard` (`grid`, quatre actions de direction, `mode` : 0 somme, 1 valeur suivante, 2 glisser sans fusionner ; `vanish_ticks`, `pop`, `moved_action`, `stuck_action`, et son état : `score`, `highest`, `moves`, `no_moves`) et `Tile` (`value`, `merged`, `dying`, `vanish_in`). Une tuile est une entité avec un `Tile` et un `GridPos` ; tout autre `GridPos` du plateau est un obstacle. Le glissement est calculé ligne par ligne par une fonction pure (`plan`), qui sert aussi à savoir si le plateau est bloqué (aucune direction ne change quoi que ce soit). La tuile absorbée va jusqu'à la case de l'autre en glissant, puis disparaît après `vanish_ticks` (elle est `Disabled` entre-temps, donc ne compte plus pour `grid2d`). `slide` passe avant `grid2d.spawn` : la case choisie pour la nouvelle tuile tient compte du coup qui vient d'être joué.
- **Le jeu n'a aucun C++** : la partie commence quand un `rules.Watch` voit `moves == 0` (un nombre déjà dans l'intervalle au départ « entre » : cela lève `deal`, qui fait poser deux tuiles) ; la victoire est un `Watch` sur `highest >= 2048` (une fois) qui fait passer `phase` à `won` ; `stuck` fait passer à `lost` ; `continue_game` revient à `playing`. Le plateau est `ActiveIn playing`. Recommencer recharge la scène `game` (R, N ou le bouton), à tout moment.
- **L'écran de victoire est une bannière sous le plateau**, pas un écran plein : `text2d` dessine toujours au-dessus des sprites, donc un voile sur le plateau laisserait lire les nombres des tuiles à travers. La bannière (message, bouton **Continuer**) est un `ShowIn won` ; le plateau reste visible et figé.
- **Les clics et les glissés** viennent de la même souris/doigt : un glissé lève la direction (`gesture.Swipe`), un appui bref sur un bouton est un clic ; le premier doigt étant aussi la souris, le jeu n'a aucune source `touch:`.
- **Les images** : 14 images de 32 pixels (`tiles`), une par puissance de 2 jusqu'à 8192 (au-delà, la dernière) ; `FrameBind` en logarithme choisit l'image, `ui.Bind own` écrit le nombre.
- **Les scénarios** (15) jouent des plateaux de test (`test_*`), la vraie scène avec des tuiles posées et sans la partie au hasard, plus la vraie scène (`deal_two_tiles`).

