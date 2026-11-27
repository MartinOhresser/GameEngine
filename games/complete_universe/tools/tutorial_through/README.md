# Le scénario `tutorial_through`

Le scénario qui joue tout le tutoriel est généré : ses entrées sont celles de chaque épreuve,
comptées depuis le début de son étape (`generate.py`, `SEGMENTS`), et les débuts des étapes sont
mesurés en jouant (`starts.json` pour les épreuves, `all_starts.json` pour toutes les étapes).

Après un changement du tutoriel (une étape, une durée), depuis WSL, à la racine du dépôt :

```bash
python3 -u games/complete_universe/tools/tutorial_through/calibrate.py   # recale starts.json
python3 games/complete_universe/tools/tutorial_through/generate.py \
  games/complete_universe/tools/tutorial_through/starts.json \
  games/complete_universe/scenarios/tutorial_through.json
```

`calibrate.py` rejoue le scénario en mode mesure (la phase tous les 5 ticks) jusqu'à ce que les
débuts se stabilisent ; il faut `build/dev/runner/gerun.exe` compilé. Recopier ensuite les débuts de
toutes les étapes du journal (`build/tutorial_through_probe.log`) dans `all_starts.json` si les
démonstrations ont changé de durée.
