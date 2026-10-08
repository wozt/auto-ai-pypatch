#define _POSIX_C_SOURCE 200809L
#include <gio/gio.h>
#include <glib/gstdio.h>
#include "executor.h"
#include "config.h"
#include "history.h"
#include <unistd.h>
#include <string.h>

typedef struct {
    GMainLoop *loop;
    AaExecutor *executor;
    GString *output;
    gboolean success;
    int exit_code;
    gboolean completed;
} Execution;

static void output_cb(const char *line, gboolean err, gpointer data) {
    Execution *run = data;
    g_string_append_printf(run->output, "%s%s\n", err ? "ERR:" : "OUT:", line);
}
static void complete_cb(gboolean success, int code, const char *msg, gpointer data) {
    Execution *run = data;
    (void)msg;
    run->success = success;
    run->exit_code = code;
    run->completed = TRUE;
    run->executor = NULL;
    g_main_loop_quit(run->loop);
}
static gboolean timeout_cb(gpointer data) {
    Execution *run = data;
    if (run->executor) aa_executor_cancel(run->executor);
    g_main_loop_quit(run->loop);
    return G_SOURCE_REMOVE;
}
static void run_python(const char *watch, const char *basename, const char *project,
                       gboolean expected_success, const char *expected_output) {
    Execution run = {.loop = g_main_loop_new(NULL, FALSE), .output = g_string_new(NULL)};
    g_autoptr(GError) error = NULL;
    run.executor = aa_executor_start(watch, basename, project, output_cb, complete_cb, &run, &error);
    g_assert_no_error(error);
    g_assert_nonnull(run.executor);
    guint timer = g_timeout_add_seconds(10, timeout_cb, &run);
    g_main_loop_run(run.loop);
    if (run.completed) g_source_remove(timer);
    g_assert_true(run.completed);
    g_assert_cmpint(run.success, ==, expected_success);
    g_assert_nonnull(strstr(run.output->str, expected_output));
    g_string_free(run.output, TRUE);
    g_main_loop_unref(run.loop);
}
static void write_patch(const char *dir, const char *basename, const char *code) {
    g_autofree char *name = g_build_filename(dir, basename, NULL);
    g_assert_true(g_file_set_contents(name, code, -1, NULL));
}
static void test_runner(void) {
    g_autoptr(GError) error = NULL;
    g_autofree char *root = g_dir_make_tmp("aap-exec-test-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *watch = g_build_filename(root, "watch", NULL);
    g_autofree char *project = g_build_filename(root, "project", NULL);
    g_assert_cmpint(g_mkdir(watch, 0700), ==, 0);
    g_assert_cmpint(g_mkdir(project, 0700), ==, 0);
    write_patch(watch, "patch_ok.py", "import pathlib,sys\nprint('output-ok',flush=True)\nprint('err-ok',file=sys.stderr,flush=True)\npathlib.Path('changed.txt').write_text('modified')\n");
    run_python(watch, "patch_ok.py", project, TRUE, "OUT:output-ok");
    g_autofree char *changed = g_build_filename(project, "changed.txt", NULL);
    g_assert_true(g_file_test(changed, G_FILE_TEST_EXISTS));
    write_patch(watch, "patch_fail.py", "import sys\nprint('exit-failed',file=sys.stderr)\nsys.exit(7)\n");
    run_python(watch, "patch_fail.py", project, FALSE, "ERR:exit-failed");
    g_autofree char *link_path = g_build_filename(watch, "patch_link.py", NULL);
    g_autofree char *original = g_build_filename(watch, "patch_ok.py", NULL);
    g_assert_cmpint(symlink("patch_ok.py", link_path), ==, 0);
    AaExecutor *unsafe = aa_executor_start(watch, "patch_link.py", project, NULL, NULL, NULL, &error);
    g_assert_null(unsafe);
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_cmpint(g_remove(link_path), ==, 0);
    g_assert_cmpint(g_remove(original), ==, 0);
    g_autofree char *fail = g_build_filename(watch, "patch_fail.py", NULL);
    g_remove(fail);
    g_remove(changed);
    g_rmdir(project);
    g_rmdir(watch);
    g_rmdir(root);
}

static gboolean cancel_cb(gpointer data) {
    Execution *run = data;
    if (run->executor) aa_executor_cancel(run->executor);
    return G_SOURCE_REMOVE;
}

static void test_cancel(void) {
    g_autoptr(GError) error = NULL;
    g_autofree char *root = g_dir_make_tmp("aap-cancel-test-XXXXXX", &error);
    g_assert_no_error(error);
    write_patch(root, "patch_sleep.py", "import time\nprint('running',flush=True)\ntime.sleep(20)\n");
    Execution run = {.loop = g_main_loop_new(NULL, FALSE), .output = g_string_new(NULL)};
    run.executor = aa_executor_start(root, "patch_sleep.py", root, output_cb, complete_cb, &run, &error);
    g_assert_no_error(error);
    g_assert_nonnull(run.executor);
    guint cancel_source = g_timeout_add(100, cancel_cb, &run);
    guint timeout_source = g_timeout_add_seconds(10, timeout_cb, &run);
    g_main_loop_run(run.loop);
    if (run.completed) g_source_remove(timeout_source);
    /* If Python exits extremely quickly due to an external failure, this also cancels timer. */
    if (g_main_context_find_source_by_id(NULL, cancel_source)) g_source_remove(cancel_source);
    g_assert_true(run.completed);
    g_assert_false(run.success);
    g_string_free(run.output, TRUE);
    g_main_loop_unref(run.loop);
    g_autofree char *script = g_build_filename(root, "patch_sleep.py", NULL);
    g_remove(script);
    g_rmdir(root);
}

static void test_applied_history(void) {
    AaConfig cfg;
    aa_config_init(&cfg);
    g_autoptr(GError) error = NULL;
    g_autofree char *root = g_dir_make_tmp("aap-history4-XXXXXX", &error);
    g_assert_no_error(error);
    AaHistory *preview = aa_history_open_at(&cfg, root, &error);
    AaHistory *applied = aa_history_open_applied_at(&cfg, root, &error);
    g_assert_no_error(error);
    g_assert_true(aa_history_record_preview(preview, "patch_0001.py", TRUE, 1, &error));
    g_assert_false(aa_history_seen(applied, "patch_0001.py"));
    g_assert_true(aa_history_record_attempt(applied, "patch_0001.py", &error));
    g_assert_true(aa_history_needs_confirmation(applied, "patch_0001.py"));
    g_assert_false(aa_history_seen(applied, "patch_0001.py"));
    aa_history_free(applied);
    applied = aa_history_open_applied_at(&cfg, root, &error);
    g_assert_true(aa_history_needs_confirmation(applied, "patch_0001.py"));
    g_assert_true(aa_history_record_applied(applied, "patch_0001.py", TRUE, 1, &error));
    g_assert_true(aa_history_seen(applied, "patch_0001.py"));
    g_assert_false(aa_history_needs_confirmation(applied, "patch_0001.py"));
    guint64 last = 0;
    g_assert_true(aa_history_last_number(applied, &last));
    g_assert_cmpuint(last, ==, 1);
    aa_history_free(preview);
    aa_history_free(applied);
    g_autofree char *p = g_build_filename(root, "auto-ai-pypatch", "preview-history.ini", NULL);
    g_autofree char *a = g_build_filename(root, "auto-ai-pypatch", "applied-history.ini", NULL);
    g_remove(p); g_remove(a);
    g_autofree char *state_dir = g_path_get_dirname(p);
    g_rmdir(state_dir); g_rmdir(root);
    aa_config_clear(&cfg);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/executor/python-success-failure-symlink", test_runner);
    g_test_add_func("/history/applied-separate-and-restart", test_applied_history);
    g_test_add_func("/executor/interrupt", test_cancel);
    return g_test_run();
}
