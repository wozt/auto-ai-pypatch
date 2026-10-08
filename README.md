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

## Phase 3 : sélection et historique de simulation

- Le watcher `inotify` groupe les événements rapprochés pendant 400 ms, sans boucle `ls`.
- Un scan est également lancé au démarrage (âge maximal respecté).
- Si au moins deux fichiers admissibles **non vus** sont présents : erreur, aucune validation.
- Un seul candidat entraîne une observation « VALIDÉ EN SIMULATION » et est enregistré pour prévenir les doublons après redémarrage.
- Numérotation : le premier numéro peut être quelconque ; ensuite, seul `dernier + 1` est accepté. Segment numérique ASCII, limité à 64 bits.
- `Âge maximal = 0` désactive la limite d'âge. Liens symboliques et répertoires ignorés.
- Supprimer ou déplacer un candidat débloque automatiquement une situation ambiguë.
- Modifier un filtre ou l'âge pendant la surveillance provoque son arrêt (redémarrage manuel).
- Historique de **simulation**, par projet et par filtres : `~/.local/state/auto-ai-pypatch/preview-history.ini`. Pour réinitialiser les essais, arrêter l'app puis déplacer ce fichier.

**Aucun patch Python n'est exécuté et aucun Git automatique n'est lancé** dans cette phase. Le moteur réel devra utiliser un historique d'exécution séparé et revérifier les fichiers juste avant leur exécution.


## Phase 4 — Python runner (no automatic Git yet)

- Select a **dedicated, disposable watch directory** and a **test project** initially.
  Press Start only after checking the active filename filters.
- Manual mode shows an explicit **Exécuter** confirmation for each new script.
  Automatic mode runs without prompting until `auto_limit` successful automatic
  scripts (0 means unlimited); the next candidate needs confirmation.
- Execution is asynchronous (`GSubprocess`) with live stdout/stderr and exit status,
  working directory set to the project root. Stdin is closed; interactive scripts
  cannot request terminal input. Press **Interrompre Python** to kill the child.
- Only exit code 0 is recorded as applied. A crash, kill or failure leaves a
  persistent *attempt* marker so that retrying the same filename requires manual
  confirmation. Applied history is separate from the phase-3 simulation history.
- A private snapshot of the downloaded file is taken using `O_NOFOLLOW` and a
  regular-file check (4 MiB maximum); the snapshot is deleted afterwards.
- **IMPORTANT:** Scripts execute with the full privileges of your user account,
  **without any sandbox**. Never auto-run downloaded code from untrusted sources.
  `Git add/commit/push` remains disabled until a later phase.

## Phase 4b — Suppression facultative des patchs téléchargés

- Paramètre **Supprimer le patch après exécution réussie**, désactivé par défaut et
  conservé dans `~/.config/auto-ai-pypatch/config.ini` (`delete_after_success=true/false`).
- Le téléchargement original n'est supprimé **qu'après** fin Python `exit 0`
  **et** persistance de l'historique des patchs appliqués. Le snapshot privé
  utilisé pour l'exécution est supprimé indépendamment de cette option.
- Aucun effacement sur échec, interruption, erreur d'historique ou absence de
  fichier original. Si le fichier a été remplacé/modifié, le journal indique
  pourquoi il a été conservé. L'échec de suppression ne réexécute pas un patch
  déjà appliqué.
- Le dossier est tenu ouvert pendant l'exécution et les métadonnées, l'inode
  ainsi que le SHA-256 sont vérifiés avant suppression. Cela ne constitue pas
  une suppression conditionnelle atomique face à un processus concurrent
  malveillant (limitation POSIX). Ne surveille pas de dossiers non fiables.
- Git automatique n'est pas encore implémenté : lorsque Git sera activé,
  supprimer la source devra rester conditionné au succès du workflow final.
