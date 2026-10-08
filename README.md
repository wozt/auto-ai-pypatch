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

## Patch 004c — aperçu du candidat et exécution manuelle

- Une ligne `DETECTED FILENAME` sous `EXPECTED FILENAME` montre le candidat non appliqué.
  **Bleu** : candidat récent ; **orange** : fichier dépassant `max_age` ;
  **rouge** : ambiguïté, erreur de numérotation ou séquence invalide.
- `Apply patch` fonctionne même lorsque Start/Stop est arrêté. Sur un fichier
  récent, le clic valide directement l'exécution. Pour un fichier ancien, une
  boîte de dialogue explicite `No` / `Yes` (par défaut `No`) demande de passer
  outre **uniquement** le filtre d'âge. Un script précédemment tenté exige
  aussi une nouvelle confirmation.
- Les filtres regex, l'unicité du candidat (à âge comparable), les liens
  symboliques et l'ordre numérique restent contrôlés juste avant exécution.
  L'automatisation ne sélectionne jamais un fichier trop ancien.
- L'aperçu se met à jour via `GFileMonitor` sans polling, y compris lorsque la
  surveillance automatique est arrêtée ; un timer unique gère l'expiration.
- En présence de plusieurs fichiers anciens, l'interface affiche l'ambiguïté
  et refuse leur exécution. Les anciens fichiers sont exclus du scan auto normal.

**Sécurité** : `Apply patch` lance du code Python avec les privilèges complets
de l'utilisateur. Effectuer les premiers essais sur un dépôt jetable.


## Phase 5 — Git automatique

- Activation manuelle du switch **Git add / commit / push automatique** (désactivé par défaut).
- Avant Python : exige que le **dossier projet soit la racine d'un dépôt Git propre**, sans
  modifications, sans fichiers non suivis et avec une branche upstream synchronisée (`git push -u origin main`).
  Pense à exclure `build/` dans le `.gitignore` de ton projet si nécessaire.
- Au début du script Python, ajoute exactement :

  ```python
  # Auto-AI-PyPatch: commit-message: feat: describe changes
  # Auto-AI-PyPatch: project: myproject
  ```

  Le marqueur `project` correspond au nom du dossier projet. Les deux marqueurs doivent
  se trouver dans les premiers commentaires ; le message est limité à 200 caractères.
  L'en-tête est lu **depuis le snapshot exécuté**, avant de lancer Python.
- Après exit 0 Python et enregistrement de l'historique : `git add -A -- .`,
  `git diff --cached --quiet --exit-code`, `git commit -m <message>` si changements,
  puis `git push`. Ces commandes utilisent `GSubprocess` asynchrone, **sans shell**.
- Pour éviter une demande interactive de mot de passe, le push désactive les invites
  terminal/SSH. Configure préalablement ta clé SSH ou des credentials non interactifs.
- Si Git échoue : le script est marqué comme **déjà appliqué** ; aucun Python automatique
  ne recommence, aucun téléchargement n'est supprimé. Finalise le commit/push
  **manuellement**, après examen de `git status`. Un commit local peut déjà exister.
- Si l'option de suppression est active : sans Git, après Python+historique ;
  avec Git, **seulement après push réussi** et vérification du fichier d'origine inchangé.
- Limitations : les scripts Python ne sont **pas isolés**. `git add -A` stage toutes les
  modifications présentes après le patch ; des changements concurrents pourraient
  aussi être inclus. Utilise un dépôt dédié et évite les éditions simultanées.

### Test

```bash
meson setup build --reconfigure
meson compile -C build
meson test -C build --print-errorlogs
```

Le test Git utilise un dépôt bare local temporaire ; il ne pousse **rien sur GitHub**.

## Patch 005c — métadonnées et ordre de validation

- Le prompt est régénéré en changeant le dossier projet, les filtres ou la numérotation.
- Si aucun dossier projet n'est défini, aucun faux `project: project` n'est proposé.
- Exemple de message de commit valide, sans chevrons ; le nom du projet est celui du dossier choisi.
- En mode Git, l'en-tête est validé sur le snapshot privé **avant** l'enregistrement de la tentative.
- L'enregistrement demeure **avant** le lancement Python : en cas d'échec/crash du script,
  une nouvelle confirmation reste obligatoire. Une erreur d'en-tête n'inscrit plus de tentative.


## Patch 006 — dépôts modifiés : repli manuel sûr

- Nouveau réglage (désactivé par défaut) : « Dépôt Git modifié : autoriser Python manuel (Git à faire soi-même) ».
- Avec Git coché et dépôt **propre**, comportement identique : Python, puis commit/push automatique.
- Avec Git coché et dépôt **modifié**, le bouton **Apply patch** peut exécuter Python ; `git add/commit/push` est **désactivé pour cette exécution**, même si Git est coché. Le téléchargement est conservé.
- L'exécution *automatique* reste interdite si le dépôt est modifié. Le clic manuel est nécessaire ; jamais de `git add -A` sur un dépôt déjà sale.
- La racine du dépôt, l'upstream et la synchronisation de branche restent obligatoires, même en mode manuel ; les métadonnées d'en-tête sont toujours vérifiées.
- Après succès Python, contrôler `git status` et `git diff`, puis sélectionner/committer/pousser manuellement. Le script est mémorisé comme appliqué ; ne pas le relancer pour résoudre Git.
- Le mode `Git add/commit/push` automatique sélectif pour dépôts déjà sales **n'est pas encore implémenté**. Ce mode de repli privilégie l'absence de commits accidentels.
