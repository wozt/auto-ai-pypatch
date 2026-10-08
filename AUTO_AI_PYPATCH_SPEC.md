# Auto-AI-PyPatch — Cahier des charges

> **Statut :** spécification initiale (v0.1)  
> **Dépôt :** https://github.com/wozt/auto-ai-pypatch  
> **Application :** C11, GTK4 et libadwaita (Linux)  
> **Scripts appliqués :** Python 3, générés par un LLM

## 1. Objectif

Auto-AI-PyPatch automatise l'application locale de scripts de patch Python produits par un assistant IA. L'utilisateur demande une modification à son LLM, télécharge le fichier `.py` proposé et l'application, déjà ouverte, le détecte dans un répertoire surveillé. Après vérification des critères configurés, elle exécute ce fichier dans le dossier du projet choisi, affiche ses sorties en direct et peut réaliser un `git add` / `git commit` / `git push`.

L'objectif est de raccourcir le cycle **demande au LLM → téléchargement du patch → application → compilation/tests manuels → itération**, sans perdre la traçabilité ni le contrôle utilisateur.

**Principe de sécurité :** un script Python téléchargé exécute du code arbitraire avec les droits de l'utilisateur. L'exécution automatique est donc une fonctionnalité explicitement activée, réservée à un environnement de confiance ; les filtres de nom ne constituent **pas** un mécanisme de sécurité.

## 2. Périmètre et plateformes

- **Cible initiale :** Linux (priorité Debian 13, bureau GTK).
- **Langage de l'application :** **C11** ; aucun code applicatif Python.
- **Langage des patchs :** Python 3 (`.py`).
- **Interface :** GTK4 + libadwaita ; terminal intégré avec une version de VTE compatible GTK4 (`vte-2.91-gtk4` lorsque disponible).
- **Surveillance :** API `inotify` Linux intégrée à la boucle principale GLib (`GSource`/`GIOChannel` ou descripteur de fichier surveillé), avec `GFileMonitor` comme abstraction envisageable. **Pas de polling périodique `ls`.**
- **Processus externes :** `GSubprocess` / `GSubprocessLauncher` pour Python et Git ; affichage des sorties, codes de retour et erreurs.
- **Configuration :** `GKeyFile` dans `~/.config/auto-ai-pypatch/config.ini`, permissions adaptées.
- **Build :** Meson + pkg-config (ou CMake si décision ultérieure).

## 3. Interface utilisateur

### 3.1 Disposition

Fenêtre redimensionnable, deux zones principales :

```text
┌────────────────────────────────────────────────────────────────────────┐
│ Auto-AI-PyPatch                    [Démarrer / Arrêter]  [Paramètres]   │
├────────────────────────────────────────────────────────────────────────┤
│ Prompt suggéré au LLM [Français ▾] [Copier]                          │
│ ┌────────────────────────────────────────────────────────────────────┐ │
│ │ Texte du prompt adapté aux réglages du projet et aux conventions  │ │
│ │ de nommage des patchs.                                           │ │
│ └────────────────────────────────────────────────────────────────────┘ │
├───────────────────────────────────┬────────────────────────────────────┤
│ Configuration                     │ Terminal / journal                 │
│ Dossier surveillé [Parcourir]     │                                    │
│ Dossier projet   [Parcourir]      │ [heure] Surveillance active        │
│ Regex            [            ]   │ [heure] Patch détecté…             │
│ Préfixe          [            ]   │ [heure] Validation…                │
│ Suffixe          [            ]   │ > python3 /.../patch.py            │
│ Extension        [.py         ]   │ stdout / stderr en direct          │
│ Numérotation     [activée ?  ]    │ ...                                │
│ Âge max          [      ] secondes│                                    │
│ Exécution        [Confirmer/Auto] │                                    │
│ Limite auto      [0           ]   │                                    │
│ [ ] Git add + commit + push       │                                    │
│                                   │                                    │
│ État / dernier fichier            │                                    │
└───────────────────────────────────┴────────────────────────────────────┘
```

Le panneau de gauche peut défiler indépendamment. Le panneau de droite affiche le terminal et les événements d'orchestration. Le texte du prompt en haut est **éditable/copiable**, et peut être régénéré à partir des paramètres. Le bouton principal indique clairement l'état **Surveillance arrêtée / active / patch en cours / erreur**.

### 3.2 Paramètres

| Paramètre | Description | Valeur initiale proposée |
|---|---|---|
| Dossier surveillé | Chemin des téléchargements, modifiable | Dossier XDG `DOWNLOAD`, sinon sélection manuelle |
| Dossier de travail | Racine du projet auquel appliquer les scripts | À sélectionner obligatoirement |
| Expression régulière | Filtre regex appliqué au **nom de fichier** | `.*\.py$` (à resserrer avant le mode automatique) |
| Préfixe | Préfixe exigé du nom du patch | `patch_` |
| Suffixe | Suffixe exigé, **avant l'extension** | vide |
| Extension | Extension admise (sans ou avec point) | `.py` |
| Numérotation incrémentale | Accepter les noms contenant un numéro croissant | désactivé |
| Âge maximum | Temps depuis la dernière modification au-delà duquel le patch est ignoré | 120 secondes (proposition) |
| Mode d'exécution | Confirmation manuelle ou exécution automatique | **Confirmation** |
| Limite d'auto-patchs | Nombre d'exécutions automatiques avant nouvelle confirmation ; `0` = illimité | 1 (proposition) |
| Git automatique | Exécuter `git add`, `git commit`, puis `git push` après succès | désactivé |
| Langue du prompt | Langue de suggestion et valeur par défaut | Français |

Les valeurs marquées « proposition » sont ajustables et ne sont pas des décisions définitives.

## 4. Détection et sélection des fichiers

### 4.1 Détection événementielle

1. Au clic sur **Démarrer**, vérifier l'existence/l'accès du dossier surveillé et du dossier projet.
2. Installer une surveillance `inotify` sur les événements pertinents (`IN_CLOSE_WRITE`, `IN_MOVED_TO`, et gestion des suppressions/déplacements du répertoire).
3. Les fichiers téléchargés sous un nom temporaire (`.part`, `.crdownload`, etc.) ne deviennent candidats qu'après leur renommage final, ou leur fermeture après écriture.
4. Refuser un fichier qui est encore en cours d'écriture ; si nécessaire, effectuer une vérification différée de stabilité (taille/mtime), **sans parcourir tout le dossier périodiquement**.
5. Déclencher une analyse initiale **unique** au démarrage afin d'identifier d'éventuels fichiers récents déjà présents ; ne jamais les exécuter sans appliquer les mêmes règles que les nouveaux événements.
6. Si le répertoire surveillé disparaît, arrêter la surveillance et signaler l'erreur ; permettre un redémarrage explicite.

### 4.2 Critères combinés

Un candidat doit respecter **tous** les critères activés : extension, regex, préfixe, suffixe, règle éventuelle de numérotation et âge maximum. Les filtres sont indépendants et combinés par un **ET logique**. Une regex invalide bloque le démarrage et produit un message intelligible.

- Une règle de numérotation peut, dans une première version, interpréter `patch_0001.py`, `patch_0002.py`, etc., et mémoriser le dernier numéro exécuté **par profil de projet**.
- Le format exact de la numérotation doit être précisé avant l'implémentation (voir Questions ouvertes).
- La limite d'âge repose sur `mtime` (`stat`), pas sur une date simplement présente dans le nom.
- Les chemins sont canonicalisés. Par défaut, **rejeter les liens symboliques** et les fichiers non réguliers, pour éviter le remplacement du chemin par une cible inattendue.

### 4.3 Ambiguïté : plusieurs candidats

**Règle impérative :** au moment de prendre une décision, si **deux fichiers ou plus** satisfont les critères dans le dossier surveillé, **erreur, aucune exécution**. Afficher les noms concurrents et demander à l'utilisateur de supprimer/renommer/déplacer les éléments ambigus, puis de relancer l'analyse.

Cette règle s'applique aussi en mode automatique. Ne jamais choisir arbitrairement le plus récent ou le plus grand numéro pour contourner l'ambiguïté. Un fichier précédemment exécuté est exclu des candidats **uniquement** si son identité et son état sont encore reconnus dans l'historique local ; les comportements de réexécution doivent rester explicites.

### 4.4 Anti-double exécution

- Un seul patch à la fois ; verrou logique tant qu'il est en cours.
- Événements `inotify` dupliqués regroupés pour un même fichier.
- Enregistrer le fichier traité (chemin, taille, `mtime`, idéalement empreinte SHA-256), l'heure, le projet, le résultat et le statut Git.
- Un fichier déjà exécuté n'est pas rejoué après redémarrage, sauf action explicite **Réexécuter**.
- Si de nouveaux fichiers arrivent pendant l'exécution, les réévaluer après la fin, en appliquant à nouveau la règle « plusieurs candidats = erreur ».

## 5. Exécution des patchs

### 5.1 Cycle d'exécution

1. Recevoir un événement de fichier et constituer la liste des candidats valides.
2. Vérifier qu'il y a **exactement un** candidat.
3. Vérifier l'âge, la régularité du fichier, la stabilité du contenu et les chemins.
4. Dans le mode **Confirmer**, afficher le chemin du patch, la cible, le message de commit éventuel et demander **Exécuter / Annuler**.
5. Dans le mode **Auto**, vérifier que le quota d'exécutions automatiques n'est pas épuisé ; sinon demander à l'utilisateur **Continuer / Arrêter** et réarmer le quota après confirmation.
6. Lancer `python3 /chemin/absolu/patch.py` avec le **répertoire courant du processus positionné sur le dossier projet** ; ne pas passer par `sh -c`.
7. Capturer la sortie standard, la sortie d'erreur, le code de retour et la durée ; permettre l'annulation (signal propre, puis arrêt forcé si nécessaire, avec gestion du groupe de processus).
8. Si le patch échoue (code non nul), afficher l'échec et **ne pas lancer Git**.
9. Si le patch réussit, lancer éventuellement les opérations Git, puis enregistrer l'exécution dans l'historique.
10. Revenir à l'état d'attente ou d'arrêt selon la demande de l'utilisateur.

**Important :** un code de sortie 0 signifie seulement que le script s'est terminé sans erreur déclarée ; il ne garantit pas que les modifications sont correctes. La compilation et les tests ne sont pas implicitement réussis.

### 5.2 Terminal intégré

Privilégier un terminal VTE compatible GTK4 pour l'affichage. Deux modes possibles :

- **V1 :** journal textuel colorisé et horodaté, lisant `stdout` et `stderr` via GLib ; plus simple, déterministe et suffisant pour les scripts non interactifs.
- **V2 :** vrai terminal VTE/PTy pour les programmes qui nécessitent les séquences ANSI ou des interactions utilisateur, à condition de gérer proprement le cycle de vie et les codes de sortie.

Le terminal doit montrer **la commande réellement lancée**, le projet cible, la sortie des processus Python/Git et le résultat. L'interface ne doit jamais se figer durant une exécution.

## 6. Gestion de Git

Si la case **Git add + commit + push** est activée :

1. Vérifier que le dossier projet est dans un dépôt Git et identifier sa racine réelle.
2. Vérifier l'état Git **avant le patch**, signaler les modifications préexistantes et les protéger d'un commit automatique involontaire.
3. Après succès du patch, récupérer le message de commit depuis un **commentaire en tête du script Python**, selon une convention stable :

```python
# Auto-AI-PyPatch: commit-message: Corrige l'extraction des sprites de Samus
# Auto-AI-PyPatch: project: metroidvania

from pathlib import Path
# ... script de patch ...
```

4. Parser le commentaire initial **sans exécuter le script pour découvrir le message** ; refuser les messages vides et les formats invalides. Enregistrer un avertissement s'il manque.
5. Ajouter **uniquement les fichiers modifiés par le patch et validés pour ce commit** : ne pas utiliser aveuglément `git add -A` dans un dépôt contenant d'autres travaux. La stratégie exacte de sélection sera explicitée avant de coder cette fonction.
6. Effectuer `git commit -m "..."`, puis `git push` vers l'upstream configuré, en appelant Git avec un tableau d'arguments plutôt qu'une commande shell concaténée.
7. Ne jamais écraser l'historique (`push --force` interdit), ni pousser si le patch ou le commit a échoué.
8. Si `push` échoue, afficher distinctement **patch appliqué**, **commit créé**, **push échoué** ; ne pas relancer automatiquement le patch.

**Recommandation V1 :** si le dépôt contient des changements préexistants, désactiver le commit automatique pour cette exécution et demander une intervention manuelle. Le programme ne doit pas faire de `reset`, `stash`, `checkout` ou suppression de fichiers de sa propre initiative.

## 7. Prompt suggéré pour le LLM

Un panneau en haut fournit un prompt prêt à copier. La langue est sélectionnable (au minimum **français** et **anglais**) et enregistrée dans les préférences. Le prompt est **généré dynamiquement** à partir des réglages (dossier projet, nommage, numérotation, convention de métadonnées, workflow Git).

### Exemple en français

> Tu travailles sur le projet « {nom_projet} ». Pour toute modification, fournis un script **Python 3 autonome** qui applique le patch aux fichiers du projet. Le script sera téléchargé et exécuté depuis la racine du projet par Auto-AI-PyPatch. Il doit être compatible avec une seconde exécution sans dupliquer les modifications, vérifier ses préconditions, échouer avec un code non nul si le contenu attendu est absent, et ne modifier aucun fichier hors de la racine du projet. Place au début du fichier les commentaires :
>
> `# Auto-AI-PyPatch: commit-message: <message impératif et précis>`
>
> `# Auto-AI-PyPatch: project: {nom_projet}`
>
> Nomme le script selon cette convention : `{convention_nom_fichier}`. N'inclus aucune opération Git dans le script : Auto-AI-PyPatch s'en chargera si configuré. Fournis le script sous forme d'un fichier `.py` téléchargeable, sans autre commande à exécuter pour appliquer le patch.

**Précision :** « idempotent » est une exigence donnée au LLM, mais ne doit pas être supposée vraie par le programme. L'app doit toujours empêcher les doubles exécutions accidentelles.

### Exemple en anglais

> You are working on project “{project_name}”. For each change, provide a standalone **Python 3** patch script, designed to run from the project's root directory. The script must check preconditions, fail with a non-zero exit code on unexpected content, avoid modifying paths outside the project, and be safe to rerun. Add these header comments:
>
> `# Auto-AI-PyPatch: commit-message: <clear imperative commit message>`
>
> `# Auto-AI-PyPatch: project: {project_name}`
>
> Name it using `{filename_convention}`. Do not run Git commands in the patch; Auto-AI-PyPatch handles Git separately when enabled. Provide a downloadable `.py` file.

Le texte est mis à jour lorsque les paramètres changent. Si le champ « chemin projet » contient une information locale sensible, proposer l'option de ne transmettre au LLM que le **nom du projet** plutôt que son chemin absolu.

## 8. Architecture C proposée

```text
auto-ai-pypatch/
├── README.md
├── docs/
│   └── SPEC.md
├── meson.build
├── src/
│   ├── main.c               # Entrée de l'application
│   ├── app.c/.h             # Cycle de vie GTK/libadwaita
│   ├── window.c/.h          # Fenêtre, contrôles, état UI
│   ├── config.c/.h          # Lecture/écriture de GKeyFile
│   ├── watcher.c/.h         # inotify et événements
│   ├── matcher.c/.h         # Regex, filtres, ambiguïtés
│   ├── executor.c/.h        # GSubprocess, annulation, sorties
│   ├── git.c/.h             # Statut, commit, push
│   ├── patch_meta.c/.h      # En-têtes des scripts Python
│   ├── history.c/.h         # Anti-doublon, résultats
│   ├── prompt.c/.h          # Génération FR/EN
│   └── log.c/.h             # Journalisation UI
├── data/
│   ├── resources.gresource.xml
│   └── ui/
└── tests/
    ├── test_matcher.c
    ├── test_patch_meta.c
    └── test_watcher.c
```

### États principaux

`STOPPED → WATCHING → CANDIDATE → WAIT_CONFIRMATION / RUNNING → GIT_RUNNING → WATCHING`

États d'erreur possibles : `AMBIGUOUS`, `INVALID_CONFIG`, `EXEC_FAILED`, `GIT_FAILED`. L'état `STOPPED` annule la surveillance mais **ne doit pas tuer silencieusement un patch déjà démarré** : offrir explicitement « arrêter la surveillance » et « interrompre l'exécution ».

## 9. Sécurité, robustesse et ergonomie

- Mode **confirmation** activé par défaut ; avertissement explicite lors du passage en **auto**.
- Aucun privilège root ; aucune demande de `sudo` automatique.
- Dossiers de projet surveillés/configurés séparément ; vérification d'appartenance au dépôt avant Git.
- Éviter les courses entre validation et exécution : conserver une identité stable de fichier, refuser les symlinks, revérifier la signature du fichier juste avant exécution. Si nécessaire, utiliser une copie immuable temporaire du script validé.
- Aucun `system()` ni commande shell construite avec des noms de fichiers non fiables.
- Les fichiers de configuration et l'historique ne doivent pas inclure de secrets provenant des sorties du script sans une politique de conservation claire.
- Confirmation avant push automatique initial ; vérifier les remotes et l'upstream. Ne jamais forcer un push.
- Consigner les raisons d'exclusion d'un candidat et les erreurs de regex, d'âge, de numérotation ou de permissions.
- Gérer les chemins Unicode/espaces, les téléchargements atomiques, les événements rapprochés, les déconnexions et les redémarrages.
- Option future : lancer les scripts dans un **sandbox** (bubblewrap, Flatpak, conteneur) avec accès limité au dossier projet ; hors MVP car cela demande une conception dédiée.

## 10. Critères d'acceptation (MVP)

- [ ] L'application démarre sur Linux avec une fenêtre GTK4/libadwaita et une configuration persistante.
- [ ] Le dossier à surveiller et le dossier projet sont configurables.
- [ ] Les événements de création/fin de téléchargement sont détectés sans polling `ls`.
- [ ] Regex, préfixe, suffixe, extension et âge maximum fonctionnent ensemble.
- [ ] Deux fichiers correspondants produisent une erreur et **aucun n'est exécuté**.
- [ ] Un script n'est pas exécuté deux fois à cause d'événements dupliqués ou d'un redémarrage.
- [ ] Modes **Confirmation** et **Auto**, limite d'auto-patchs (dont `0` = illimité), bouton Start/Stop fonctionnent.
- [ ] Commande Python, sortie, erreurs et code de retour sont affichés sans bloquer GTK.
- [ ] Le message de commit est extrait correctement du commentaire d'en-tête.
- [ ] Git est optionnel et n'est exécuté qu'après succès du script, sans embarquer des changements préexistants.
- [ ] Le prompt est copiable, localisé en français et anglais, et reflète le projet et les conventions sélectionnées.
- [ ] Des tests automatisés couvrent la sélection, les événements dupliqués, les fichiers ambigus et le parsing des en-têtes.

## 11. Phases de développement

1. **Socle** — Meson, GTK4/libadwaita, interface à deux colonnes, préférences persistantes, Start/Stop.
2. **Surveillance** — inotify/GLib, filtres, fichiers temporaires, âge maximum, ambiguïté, anti-doublon, tests.
3. **Exécution** — confirmation/auto/quota, `GSubprocess`, sortie temps réel, annulation, historique.
4. **Prompt** — génération FR/EN, conventions de fichier et de métadonnées, bouton Copier.
5. **Git** — lecture d'en-tête, détection des changements préexistants, commit sûr et push facultatif, scénarios d'erreur.
6. **Finition** — terminal VTE si retenu, ergonomie, journal historique, packaging et documentation.

## 12. Questions ouvertes (ne bloquent pas le socle)

1. **Numérotation :** veux-tu imposer `patch_0001.py` puis `patch_0002.py`, ou simplement accepter tout suffixe numérique et choisir uniquement le numéro attendu ? Que faire si un numéro manque ?
2. **Plusieurs candidats :** faut-il considérer tous les fichiers correspondants présents dans le dossier (même anciens), ou uniquement les fichiers non traités et sous l'âge maximum ? Le présent document recommande la seconde interprétation.
3. **Quota auto :** la confirmation doit-elle réarmer un nouveau lot de N patchs, ou uniquement autoriser le patch suivant ? Proposition : réarmer N.
4. **Git :** souhaites-tu obligatoirement exécuter un **build/test** entre patch et commit/push ? Actuellement non prévu dans le MVP, mais un hook configurable serait prudent.
5. **Terminal :** un journal texte en temps réel suffit-il au MVP, ou faut-il un vrai terminal interactif VTE dès la première version ?
6. **Nommage de projet :** faut-il prendre automatiquement le nom du dossier ou permettre un identifiant de projet personnalisé dans la métadonnée `project` ?
7. **Multi-projets :** une seule configuration active à la fois (MVP), ou plusieurs profils avec surveillance simultanée ?

---

**Décision déjà prise :** application écrite en **C**, **pas en Python** ; les scripts `.py` sont seulement les patchs entrants. Le présent document est conçu comme base de travail pour Codex et peut devenir `docs/SPEC.md` dans le dépôt GitHub.
