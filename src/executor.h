#pragma once
#include <gio/gio.h>

typedef struct AaExecutor AaExecutor;
typedef void (*AaExecutorOutput)(const char *line, gboolean is_stderr, gpointer data);
typedef void (*AaExecutorFinished)(gboolean success, int exit_code,
                                   const char *message, gpointer data);

/* Non-blocking Python runner. Script runs with cwd=project_dir and stdin closed.
 * The chosen download is copied through O_NOFOLLOW into a private 0700 directory
 * before spawn, protecting against filename replacement during execution.
 * Callback data must remain alive until AaExecutorFinished is called.
 */
AaExecutor *aa_executor_start(const char *watch_dir, const char *basename,
                              const char *project_dir, AaExecutorOutput output,
                              AaExecutorFinished finished, gpointer data,
                              GError **error);
/* Interrupt the Python process, and finish asynchronously. */
void aa_executor_cancel(AaExecutor *executor);
/* Optionally delete the exact downloaded source after exit 0 AND successful
 * applied-history persistence. Call only from AaExecutorFinished, before
 * the executor is freed; refuse changed/replaced files or unsuccessful runs.
 * Uses best-effort identity validation, not atomic conditional unlink.
 */
gboolean aa_executor_remove_source(AaExecutor *executor, GError **error);
