#include "prompt.h"

char *aa_prompt_generate(const AaConfig *c) {
    g_autofree char *project = g_path_get_basename(c->project_dir && *c->project_dir ? c->project_dir : "project");
    g_autofree char *example = g_strdup_printf("%s0001%s", c->prefix, c->extension);
    if (g_strcmp0(c->language, "en") == 0) {
        return g_strdup_printf(
            "You are working on project '%s'. Provide a downloadable standalone Python 3 patch script, "
            "run from the project root. Check preconditions; exit nonzero if they fail; "
            "make changes idempotently. Never modify files outside the project. "
            "Start the file with:\n# Auto-AI-PyPatch: commit-message: <precise commit message>\n"
            "# Auto-AI-PyPatch: project: %s\n"
            "Use a filename matching the configured filters (e.g. %s). "
            "Do not run Git commands in the patch.", project, project, example);
    }
    return g_strdup_printf(
        "Tu travailles sur le projet « %s ». Fournis un script Python 3 autonome téléchargeable, "
        "exécuté depuis la racine du projet. Vérifie les préconditions, échoue avec un code non nul "
        "si elles ne sont pas respectées et rends les changements idempotents. "
        "Ne modifie aucun fichier hors du projet. Commence par :\n"
        "# Auto-AI-PyPatch: commit-message: <message de commit précis>\n"
        "# Auto-AI-PyPatch: project: %s\n"
        "Respecte les filtres de nommage (exemple : %s). N'exécute aucune commande Git.",
        project, project, example);
}
