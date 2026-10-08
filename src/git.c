#include "git.h"
#include <string.h>

typedef enum { AA_GIT_ADD, AA_GIT_DIFF, AA_GIT_COMMIT, AA_GIT_PUSH } AaGitStep;
struct AaGit {
    char *dir;
    char *message;
    GSubprocess *process;
    AaGitStep step;
    gboolean cancelled;
    AaGitOutput output;
    AaGitFinished finished;
    gpointer data;
};

static gboolean git_local(const char *directory, const char *const args[],
                          gchar **stdout_text, GError **error) {
    g_autoptr(GPtrArray) argv = g_ptr_array_new();
    g_ptr_array_add(argv, "git");
    g_ptr_array_add(argv, "-C");
    g_ptr_array_add(argv, (gpointer)directory);
    for (guint i = 0; args[i]; ++i) g_ptr_array_add(argv, (gpointer)args[i]);
    g_ptr_array_add(argv, NULL);
    g_autofree gchar *out = NULL;
    g_autofree gchar *err = NULL;
    gint status = 0;
    if (!g_spawn_sync(NULL, (gchar **)argv->pdata, NULL, G_SPAWN_SEARCH_PATH,
                      NULL, NULL, &out, &err, &status, error)) return FALSE;
    if (!g_spawn_check_wait_status(status, NULL)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "git %s: %s", args[0], err && *err ? err : "commande échouée");
        return FALSE;
    }
    if (stdout_text) *stdout_text = g_steal_pointer(&out);
    return TRUE;
}

gboolean aa_git_preflight(const char *directory, GError **error) {
    g_autofree char *root = NULL;
    const char *const root_args[] = {"rev-parse", "--show-toplevel", NULL};
    if (!git_local(directory, root_args, &root, error)) return FALSE;
    g_strchomp(root);
    g_autofree char *expected = g_canonicalize_filename(directory, NULL);
    g_autofree char *actual = g_canonicalize_filename(root, NULL);
    if (g_strcmp0(expected, actual) != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Le dossier projet doit être la racine du dépôt Git.");
        return FALSE;
    }
    const char *const upstream_args[] = {"rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{upstream}", NULL};
    if (!git_local(directory, upstream_args, NULL, error)) {
        g_prefix_error(error, "Branche sans upstream : configure d'abord 'git push -u origin main'. ");
        return FALSE;
    }
    g_autofree char *distance = NULL;
    const char *const distance_args[] = {"rev-list", "--left-right", "--count", "HEAD...@{upstream}", NULL};
    if (!git_local(directory, distance_args, &distance, error)) return FALSE;
    g_strstrip(distance);
    if (g_strcmp0(distance, "0\t0") != 0 && g_strcmp0(distance, "0 0") != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
            "Branche différente de son upstream (commits locaux non poussés ou retard). Résous Git avant Python.");
        return FALSE;
    }
    g_autofree char *status = NULL;
    const char *const status_args[] = {"status", "--porcelain=v1", "--untracked-files=all", NULL};
    if (!git_local(directory, status_args, &status, error)) return FALSE;
    if (status && *status) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
           "Dépôt Git non propre (modifications ou fichiers non suivis) : commit/stash ou .gitignore avant Apply patch.");
        return FALSE;
    }
    return TRUE;
}

static void git_free(AaGit *g) {
    g_clear_object(&g->process);
    g_free(g->message);
    g_free(g->dir);
    g_free(g);
}
static void git_finish(AaGit *g, gboolean ok, const char *detail) {
    if (g->finished) g->finished(ok, detail, g->data);
    git_free(g);
}
static const char *step_label(AaGitStep step) {
    switch (step) {
    case AA_GIT_ADD: return "git add -A";
    case AA_GIT_DIFF: return "git diff --cached --quiet";
    case AA_GIT_COMMIT: return "git commit";
    case AA_GIT_PUSH: return "git push";
    }
    return "git";
}
static gboolean git_launch(AaGit *g, GError **error);
static void git_step_completed(GObject *source, GAsyncResult *res, gpointer user_data) {
    AaGit *g = user_data;
    g_autoptr(GError) error = NULL;
    gchar *out = NULL, *err = NULL;
    gboolean communicated = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), res,
                                                                   &out, &err, &error);
    g_autofree gchar *out_owned = out;
    g_autofree gchar *err_owned = err;
    int code = communicated && g_subprocess_get_if_exited(g->process) ?
        g_subprocess_get_exit_status(g->process) : -1;
    if (out && *out && g->output) {
        g_autofree gchar *line = g_strndup(out, 8192);
        g->output(line, g->data);
    }
    if (err && *err && g->output) {
        g_autofree gchar *line = g_strndup(err, 8192);
        g->output(line, g->data);
    }
    if (g->cancelled) { git_finish(g, FALSE, "Git interrompu ; source conservée."); return; }
    if (!communicated || (code != 0 && !(g->step == AA_GIT_DIFF && code == 1))) {
        g_autofree char *message = g_strdup_printf("%s échoué (exit %d) : %s",
             step_label(g->step), code, error ? error->message : (err && *err ? err : "voir le journal"));
        git_finish(g, FALSE, message);
        return;
    }
    if (g->step == AA_GIT_DIFF && code == 0) {
        if (g->output) g->output("Aucun changement nouveau ; vérification du push.", g->data);
        g->step = AA_GIT_PUSH;
    } else if (g->step == AA_GIT_PUSH) {
        git_finish(g, TRUE, "Git push terminé.");
        return;
    } else {
        g->step++;
    }
    g_clear_object(&g->process);
    if (!git_launch(g, &error)) {
        g_autofree gchar *detail = g_strdup_printf("%s impossible : %s",
            step_label(g->step), error ? error->message : "erreur de lancement");
        git_finish(g, FALSE, detail);
    }
}
static gboolean git_launch(AaGit *g, GError **error) {
    const char *const add_args[] = {"git", "add", "-A", "--", ".", NULL};
    const char *const diff_args[] = {"git", "diff", "--cached", "--quiet", "--exit-code", NULL};
    const char *const push_args[] = {"git", "push", NULL};
    const char *const commit_args[] = {"git", "commit", "-m", g->message, NULL};
    const char *const *args = g->step == AA_GIT_ADD ? add_args :
        g->step == AA_GIT_DIFF ? diff_args : g->step == AA_GIT_COMMIT ? commit_args : push_args;
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_set_cwd(launcher, g->dir);
    g_subprocess_launcher_setenv(launcher, "GIT_TERMINAL_PROMPT", "0", TRUE);
    g_subprocess_launcher_setenv(launcher, "GIT_SSH_COMMAND", "ssh -o BatchMode=yes", TRUE);
    g->process = g_subprocess_launcher_spawnv(launcher, args, error);
    if (!g->process) return FALSE;
    if (g->output) g->output(step_label(g->step), g->data);
    g_subprocess_communicate_utf8_async(g->process, NULL, NULL, git_step_completed, g);
    return TRUE;
}
AaGit *aa_git_start(const char *dir, const char *message, AaGitOutput output,
                    AaGitFinished finished, gpointer data, GError **error) {
    if (!dir || !*dir || !message || !*message) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Chemin Git ou message de commit vide.");
        return NULL;
    }
    AaGit *g = g_new0(AaGit, 1);
    g->dir = g_strdup(dir);
    g->message = g_strdup(message);
    g->output = output;
    g->finished = finished;
    g->data = data;
    if (!git_launch(g, error)) { git_free(g); return NULL; }
    return g;
}
void aa_git_cancel(AaGit *g) {
    if (!g || g->cancelled) return;
    g->cancelled = TRUE;
    if (g->process) g_subprocess_force_exit(g->process);
}
