# Tower defense : un jeu de stratégie fait de données

`games/tower_defense` (« Hold the Road ») est un tower defense complet, **sans une ligne de C++ propre au jeu** : des monstres suivent une route jusqu'à votre base, vous construisez des tours à côté avec l'or qu'ils laissent, vous les améliorez, vous les revendez, et vous tenez douze vagues. Les tours, les monstres, les vagues et la route sont des entités et des préfabs ; ce qui est acheté se choisit par des boutons.

```
geplay games/tower_defense/game.json
```

On touche (ou on clique) une case libre pour y construire la tour choisie (le canon au départ), une tour pour la sélectionner (améliorer, vendre). Les boutons du bas choisissent la tour (les touches 1 à 4 aussi), appellent la vague suivante (espace) ou rejouent (R).

## Les règles, et ce qui les réalise

| Dans le jeu | Dans les fichiers |
| --- | --- |
| Un terrain de 14 × 9 cases, toujours en entier à l'écran, avec une barre d'état en haut et la boutique en bas | `grid2d.Grid`, `camera2d.CameraFit` (marges), `ui.Anchor` |
| La route, qu'on ne peut pas couvrir de tours, et des rochers | une entité par case avec `grid2d.Blocked` ; la route elle-même est une suite de `path2d.Waypoint` |
| Un monstre suit la route à sa vitesse ; au bout, il blesse la base et disparaît | `path2d.PathFollower` (`end_target` : l'entité de `ui.Tag` « base », `end_damage`) ; la base est une `health.Health` qui ne disparaît pas |
| Quatre monstres : fantassin, coureur (rapide), blindé (solide, blesse deux fois), chef (400 points de vie, blesse dix fois) | quatre préfabs : `health.Health`, `economy.Bounty`, `path2d.PathFollower`, `collision2d.Collider` |
| Quatre tours à trois niveaux : canon (tir rapide), mortier (dégâts de zone), glace (ralentit), sniper (portée, vise le plus solide) | douze préfabs : `autofire.AutoFire` (portée, cadence, mode de visée), un tir par niveau (`combat2d.Projectile` : `splash_radius`, `on_hit_speed_factor`, `homing`) |
| Un ralentissement qui passe | `transform2d.SpeedScale` posé par le tir, lu par `path2d` |
| Douze vagues, la suivante à la demande | une entité `waves.WaveGroup` par groupe (« 6 fantassins, un toutes les 50 ticks »), un `waves.WaveController` ; une vague est finie quand tout est créé et que plus rien ne reste de ses groupes |
| L'or : primes des monstres, prix, remboursement | `economy.Wallet`, `Bounty` (payée une fois à la mort), `Upgradable`, `Sellable` |
| Construire, sélectionner, améliorer, vendre | `economy.Placer`, porté par la même entité que `grid2d.GridCursor` : un clic sur une case libre achète, un clic sur une tour la sélectionne ; l'anneau jaune suit la sélection |
| Le choix de la tour à construire | quatre boutons, chacun faisant deux `rules.OnAction` qui règlent `Placer.prefab` et `Placer.price` ; le bouton de la tour choisie change d'image |
| « Améliorer 40 », « Vendre 30 » : les prix de la tour touchée | `ui.Bind` sur `Placer.upgrade_price` et `Placer.sell_value` |
| Choisir une tour, une tour de dernier niveau, partie gagnée ou perdue | `phase` : `playing`, `tower`, `tower_final`, `won`, `lost` ; `phase.ShowIn` (boutons, textes) et `phase.ActiveIn` (plus rien ne bouge après la fin) |
| Nouvelle partie à tout moment | `scene_switch.GoToScene` sur `new_game` |

## Les images et les scènes sont générées

```
python3 games/tower_defense/tools/make_assets.py
```

écrit les douze tours (avec un point doré par niveau), les quatre monstres, la base, les rochers et les tirs (dessinés par formes dans le script), l'interface, les préfabs, les scènes, les scénarios, les textes et `game.json`. Les nombres du jeu (prix, portées, vitesses, vagues) sont au début du script : on équilibre le jeu en changeant une ligne et en relançant.

## Les scénarios

`gerun games/tower_defense/game.json --test` joue vingt-deux scénarios, la plupart par des touches à l'écran : l'état de départ, l'achat (le prix est pris, refusé faute d'or, impossible sur la route ou un rocher), le choix de la tour par touche et par bouton, une tour qui tue et dont la prime est payée, la sélection, l'amélioration (prix, remplacement, refus faute d'or), la vente, la tour de dernier niveau, un tir de mortier qui blesse tout un groupe, un ralentissement, une vague finie, la dernière vague qui fait gagner, le chef contre quatre snipers, la base qui tombe, rien n'est construit après la défaite, et la nouvelle partie (touche et bouton). Des scènes d'essai (`scenes/test_*.json`) sont le vrai jeu avec peu de monstres.

## Ce que le jeu a demandé au moteur

Quatre modules neufs et génériques (ADR 0013, G9b) : `path2d`, `autofire`, `waves`, `economy`. Les scénarios ont aussi montré un défaut de `health::deal` : deux monstres qui arrivent au même tick à la base ne comptaient qu'une fois leurs dégâts ; la demande est maintenant fusionnée au moment où la file est appliquée.

## Limites connues

- Pas de barre de vie par monstre ni d'anneau de portée à la sélection d'une tour.
- Pas de bouton « ×2 ».
- L'équilibre (douze vagues) est une première passe ; il se règle dans `make_assets.py`.
