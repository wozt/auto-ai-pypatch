# Auto-AI-PyPatch

Application Linux native en C11/GTK4/libadwaita pour orchestrer des patchs Python produits par un LLM.

## Phase 1 : squelette

- Fenêtre à deux colonnes, prompt FR/EN et journal horodaté
- Configuration persistante dans `~/.config/auto-ai-pypatch/config.ini`
- Start/Stop : surveillance `inotify` sans polling
- **Aucune exécution Python ni opération Git à ce stade** : les réglages correspondants sont préparatoires
- L'interface consigne seulement les événements de fichiers achevés / renommés

## Debian 13

```bash
sudo apt install build-essential meson ninja-build pkg-config libgtk-4-dev libadwaita-1-dev
meson setup build
meson compile -C build
./build/auto-ai-pypatch
```

Les prochaines étapes sont les filtres et leur validation, la détection des ambiguïtés,
l'historique anti-doublon, puis l'exécution sécurisée, et enfin Git.
