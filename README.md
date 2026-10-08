# Auto-AI-PyPatch

Application Linux native en C11 / GTK4 / libadwaita pour orchestrer des patchs Python produits par un LLM.

## Phase 2 : interface + validation des noms

- Barre de titre libadwaita avec boutons de fenêtre (fermer/réduire/maximiser selon le bureau)
- Dossiers à sélectionner avec une boîte de dialogue GTK4 (`Parcourir…`)
- Prompt multilingue (21 langues) avec sélection visible en haut et persistance du choix
- Exemples de regex de nom de fichier et de numérotation à appliquer en un clic
- Regex de numérotation distincte (par défaut `[0-9]{4}`), combinée par ET avec le filtre global
- Aperçu **EXPECTED FILENAME** en vert : caractères fixes échappés, partie variable en regex
- Validation des regex : erreur à l'écran, démarrage impossible si regex invalide
- Détection `inotify` : seuls les fichiers correspondant aux filtres sont journalisés
- Configuration sauvegardée dans `~/.config/auto-ai-pypatch/config.ini`

**Important :** les filtres de nom ne sécurisent pas l'exécution arbitraire de scripts. L'exécution Python et les opérations Git restent volontairement **désactivées**. Le contrôle d'ordre strict des numéros (dernier numéro traité), les règles d'ambiguïté et l'historique anti-doublon viendront avec le moteur d'exécution.

## Debian 13

```bash
sudo apt install build-essential meson ninja-build pkg-config libgtk-4-dev libadwaita-1-dev
meson setup build --reconfigure  # utiliser "meson setup build" si premier build
meson compile -C build
meson test -C build --print-errorlogs
./build/auto-ai-pypatch
```

### Exemple de réglages

| Option | Valeur |
|---|---|
| Préfixe | `patch_` |
| Numérotation | activée |
| Regex de numérotation | `[0-9]{4}` |
| Suffixe | `_fix` |
| Extension | `.py` |
| Regex globale | `^patch_[0-9]{4}_fix\.py$` (mettre un seul antislash dans l'interface) |

L'aperçu affichera : `^patch_(?:[0-9]{4})_fix\.py$` (avec un seul antislash dans l'interface). Ce motif accepte `patch_0001_fix.py` mais pas `patch_1_fix.py`.
