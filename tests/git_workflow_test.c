#include <gio/gio.h>
#include <glib/gstdio.h>
#include "git.h"
#include "patch_meta.h"

static void write_file(const char *path, const char *data) {
    g_assert_true(g_file_set_contents(path, data, -1, NULL));
}
static void test_metadata(void) {
    g_autofree char *dir = g_dir_make_tmp("aa-meta-test-XXXXXX", NULL);
    g_assert_nonnull(dir);
    g_autofree char *file = g_build_filename(dir, "patch.py", NULL);
    write_file(file, "#!/usr/bin/env python3\n# coding: utf-8\n"
        "# Auto-AI-PyPatch: commit-message: feat: add tests\n"
        "# Auto-AI-PyPatch: project: myproject\nprint('ok')\n");
    g_autoptr(GError) error = NULL;
    g_autofree char *message = aa_patch_meta_read(file, "/tmp/myproject", &error);
    g_assert_no_error(error);
    g_assert_cmpstr(message, ==, "feat: add tests");
    g_autofree char *wrong = aa_patch_meta_read(file, "/tmp/otherproject", &error);
    g_assert_null(wrong);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    write_file(file, "# Auto-AI-PyPatch: commit-message: <placeholder>\n"
        "# Auto-AI-PyPatch: project: myproject\n");
    g_autofree char *invalid = aa_patch_meta_read(file, "/tmp/myproject", &error);
    g_assert_null(invalid);
    g_clear_error(&error);
    write_file(file, "# Auto-AI-PyPatch: commit-message: fix: alpha\n"
        "# Auto-AI-PyPatch: commit-message: fix: beta\n"
        "# Auto-AI-PyPatch: project: myproject\n");
    g_autofree char *duplicate = aa_patch_meta_read(file, "/tmp/myproject", &error);
    g_assert_null(duplicate);
    g_clear_error(&error);
    g_remove(file);
    g_rmdir(dir);
}

static char *run(char *const argv[]) {
    g_autofree char *out = NULL;
    g_autofree char *err = NULL;
    gint status = 0;
    g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, &out, &err, &status, NULL));
    if (!g_spawn_check_wait_status(status, NULL))
        g_error("Git fixture command failed: %s", err ? err : "");
    return g_steal_pointer(&out);
}
typedef struct { GMainLoop *loop; gboolean ok; } GitResult;
static void git_done(gboolean success, const char *message, gpointer data) {
    GitResult *r = data;
    r->ok = success;
    g_test_message("Git result: %s", message);
    g_main_loop_quit(r->loop);
}
static void on_output(const char *message, gpointer data) {
    (void)data;
    g_test_message("Git log: %s", message);
}
static void test_git_flow(void) {
    g_autofree char *tmp = g_dir_make_tmp("aa-git-test-XXXXXX", NULL);
    g_autofree char *remote = g_build_filename(tmp, "remote.git", NULL);
    g_autofree char *work = g_build_filename(tmp, "work", NULL);
    char *create_bare[] = {"git", "init", "-q", "--bare", "--initial-branch=main", remote, NULL};
    g_free(run(create_bare));
    char *create_repo[] = {"git", "init", "-q", "-b", "main", work, NULL};
    g_free(run(create_repo));
    char *identity_name[] = {"git", "-C", work, "config", "user.name", "Test", NULL};
    char *identity_mail[] = {"git", "-C", work, "config", "user.email", "test@example.org", NULL};
    g_free(run(identity_name));
    g_free(run(identity_mail));
    g_autofree char *file = g_build_filename(work, "file.txt", NULL);
    write_file(file, "base\n");
    char *add[] = {"git", "-C", work, "add", ".", NULL};
    char *commit[] = {"git", "-C", work, "commit", "-qm", "initial", NULL};
    g_free(run(add)); g_free(run(commit));
    char *remote_add[] = {"git", "-C", work, "remote", "add", "origin", remote, NULL};
    char *push[] = {"git", "-C", work, "push", "-q", "-u", "origin", "main", NULL};
    g_free(run(remote_add)); g_free(run(push));
    g_autoptr(GError) error = NULL;
    g_assert_true(aa_git_preflight(work, &error));
    g_assert_no_error(error);
    gboolean dirty = TRUE;
    g_assert_true(aa_git_preflight_mode(work, TRUE, &dirty, &error));
    g_assert_false(dirty);
    write_file(file, "updated\n");
    g_assert_false(aa_git_preflight(work, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_true(aa_git_preflight_mode(work, TRUE, &dirty, &error));
    g_assert_no_error(error);
    g_assert_true(dirty);
    /* Non-tracked files also select manual Git mode. */
    g_autofree char *extra = g_build_filename(work, "untracked.txt", NULL);
    write_file(extra, "local-only\n");
    g_assert_true(aa_git_preflight_mode(work, TRUE, &dirty, &error));
    g_assert_true(dirty);
    g_assert_cmpint(g_remove(extra), ==, 0);
    GitResult result = {.loop = g_main_loop_new(NULL, FALSE)};
    AaGit *job = aa_git_start(work, "fix: update file", on_output, git_done, &result, &error);
    g_assert_nonnull(job);
    g_assert_no_error(error);
    g_main_loop_run(result.loop);
    g_assert_true(result.ok);
    g_main_loop_unref(result.loop);
    g_assert_true(aa_git_preflight(work, &error));
    char *read_remote[] = {"git", "--git-dir", remote, "show", "main:file.txt", NULL};
    g_autofree char *readback = run(read_remote);
    g_assert_cmpstr(readback, ==, "updated\n");
    g_assert_no_error(error);
    /* Simulate a push rejection, so Git has already committed locally. */
    g_autofree char *hook = g_build_filename(remote, "hooks", "pre-receive", NULL);
    write_file(hook, "#!/bin/sh\nexit 1\n");
    g_assert_cmpint(g_chmod(hook, 0755), ==, 0);
    write_file(file, "rejected\n");
    result.ok = TRUE;
    result.loop = g_main_loop_new(NULL, FALSE);
    job = aa_git_start(work, "fix: rejected push", on_output, git_done, &result, &error);
    g_assert_nonnull(job);
    g_main_loop_run(result.loop);
    g_assert_false(result.ok);
    g_main_loop_unref(result.loop);
    g_assert_false(aa_git_preflight(work, &error)); /* Branch is ahead. */
    g_clear_error(&error);
    /* Dirty override cannot bypass the upstream alignment requirement. */
    g_assert_false(aa_git_preflight_mode(work, TRUE, &dirty, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    /* Temp fixture left for OS cleanup, not part of the actual repository. */
}
int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/patch-meta/header", test_metadata);
    g_test_add_func("/git/local-bare-push", test_git_flow);
    return g_test_run();
}
