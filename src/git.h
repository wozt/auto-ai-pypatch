#pragma once
#include <gio/gio.h>

typedef struct AaGit AaGit;
typedef void (*AaGitOutput)(const char *message, gpointer data);
typedef void (*AaGitFinished)(gboolean success, const char *message, gpointer data);

/* Local synchronous preflight (no shell/network). Refuse dirty repos and missing upstream.
 * Run BEFORE Python execution to avoid committing preexisting user changes.
 */
gboolean aa_git_preflight(const char *project_dir, GError **error);
/* Async workflow: git add -A -- ., git diff --cached --quiet, git commit -m, git push.
 * Callback occurs on main context. git push cannot prompt interactively.
 */
AaGit *aa_git_start(const char *project_dir, const char *commit_message,
                    AaGitOutput output, AaGitFinished finished,
                    gpointer data, GError **error);
void aa_git_cancel(AaGit *job);
