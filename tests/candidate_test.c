#define _POSIX_C_SOURCE 200809L
#include <glib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include "candidate.h"
#include "config.h"
#include "history.h"

typedef struct {
    AaConfig cfg;
    char *root;
    char *watch;
    char *state;
} Fixture;

static void fixture_setup(Fixture *f, gconstpointer data) {
    (void)data;
    aa_config_init(&f->cfg);
    g_autoptr(GError) error = NULL;
    f->root = g_dir_make_tmp("aap-candidate-XXXXXX", &error);
    g_assert_no_error(error);
    f->watch = g_build_filename(f->root, "watch", NULL);
    f->state = g_build_filename(f->root, "state", NULL);
    g_assert_cmpint(g_mkdir(f->watch, 0700), ==, 0);
    g_free(f->cfg.watch_dir);
    g_free(f->cfg.project_dir);
    f->cfg.watch_dir = g_strdup(f->watch);
    f->cfg.project_dir = g_strdup(f->root);
    f->cfg.numbering = TRUE;
}

static void fixture_teardown(Fixture *f, gconstpointer data) {
    (void)data;
    g_autofree char *statepath = g_build_filename(f->state, "auto-ai-pypatch", "preview-history.ini", NULL);
    g_remove(statepath);
    g_autofree char *statedir = g_path_get_dirname(statepath);
    g_rmdir(statedir);
    g_rmdir(f->state);
    g_autoptr(GDir) d = g_dir_open(f->watch, 0, NULL);
    const char *name;
    while (d && (name = g_dir_read_name(d))) {
        g_autofree char *path = g_build_filename(f->watch, name, NULL);
        g_remove(path);
    }
    g_rmdir(f->watch);
    g_rmdir(f->root);
    aa_config_clear(&f->cfg);
    g_free(f->watch);
    g_free(f->state);
    g_free(f->root);
}

static void touch(Fixture *f, const char *name) {
    g_autofree char *path = g_build_filename(f->watch, name, NULL);
    g_assert_true(g_file_set_contents(path, "# safe dry run\n", -1, NULL));
}

static AaHistory *open_history(Fixture *f) {
    g_autoptr(GError) error = NULL;
    AaHistory *h = aa_history_open_at(&f->cfg, f->state, &error);
    g_assert_no_error(error);
    g_assert_nonnull(h);
    return h;
}

static AaCandidateResult scan(Fixture *f, AaHistory *h) {
    AaCandidateResult result = {0};
    g_autoptr(GError) error = NULL;
    g_assert_true(aa_candidate_scan(&f->cfg, h, &result, &error));
    g_assert_no_error(error);
    return result;
}

static void test_sequence(Fixture *f, gconstpointer data) {
    (void)data;
    AaHistory *h = open_history(f);
    AaCandidateResult result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_NONE);
    aa_candidate_result_clear(&result);
    touch(f, "patch_0007.py");
    result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_READY);
    g_assert_cmpuint(result.number, ==, 7);
    g_assert_true(aa_history_record_preview(h, result.filename, TRUE, result.number, NULL));
    aa_candidate_result_clear(&result);
    result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_NONE);
    aa_candidate_result_clear(&result);
    aa_history_free(h);
    h = open_history(f); /* Duplicate survives a restart. */
    result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_NONE);
    aa_candidate_result_clear(&result);
    touch(f, "patch_0009.py");
    result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_OUT_OF_ORDER);
    aa_candidate_result_clear(&result);
    g_autofree char *path = g_build_filename(f->watch, "patch_0009.py", NULL);
    g_remove(path);
    touch(f, "patch_0008.py");
    result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_READY);
    g_assert_cmpuint(result.number, ==, 8);
    aa_candidate_result_clear(&result);
    aa_history_free(h);
}

static void test_ambiguity(Fixture *f, gconstpointer data) {
    (void)data;
    AaHistory *h = open_history(f);
    touch(f, "patch_0001.py");
    touch(f, "patch_0002.py");
    AaCandidateResult result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_AMBIGUOUS);
    g_assert_cmpuint(result.eligible_count, ==, 2);
    g_assert_false(aa_history_seen(h, "patch_0001.py"));
    aa_candidate_result_clear(&result);
    g_autofree char *path = g_build_filename(f->watch, "patch_0002.py", NULL);
    g_remove(path);
    result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_READY);
    aa_candidate_result_clear(&result);
    aa_history_free(h);
}

static void test_age_and_numbers(Fixture *f, gconstpointer data) {
    (void)data;
    AaHistory *h = open_history(f);
    touch(f, "patch_0003.py");
    g_autofree char *path = g_build_filename(f->watch, "patch_0003.py", NULL);
    struct utimbuf old = {.actime = 1000000000, .modtime = 1000000000};
    g_assert_cmpint(utime(path, &old), ==, 0);
    AaCandidateResult result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_NONE);
    aa_candidate_result_clear(&result);
    f->cfg.max_age = 0; /* 0 = no age limit */
    result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_READY);
    aa_candidate_result_clear(&result);
    g_remove(path);
    g_free(f->cfg.numbering_regex);
    f->cfg.numbering_regex = g_strdup("[A-Z0-9]{4}");
    touch(f, "patch_A012.py");
    result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_INVALID_NUMBER);
    aa_candidate_result_clear(&result);
    aa_history_free(h);
}

static void test_expired_preview(Fixture *f, gconstpointer data) {
    (void)data;
    AaHistory *h = open_history(f);
    touch(f, "patch_0001.py");
    g_autofree char *old_path = g_build_filename(f->watch, "patch_0001.py", NULL);
    struct utimbuf old = {.actime = 1000000000, .modtime = 1000000000};
    g_assert_cmpint(utime(old_path, &old), ==, 0);
    AaCandidateResult result = {0};
    g_assert_true(aa_candidate_scan_preview(&f->cfg, h, &result, NULL));
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_TOO_OLD);
    g_assert_cmpstr(result.filename, ==, "patch_0001.py");
    g_assert_cmpint(result.age_seconds, >, f->cfg.max_age);
    aa_candidate_result_clear(&result);

    touch(f, "patch_0002.py");
    result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_READY);
    aa_candidate_result_clear(&result);
    g_assert_true(aa_candidate_scan_preview(&f->cfg, h, &result, NULL));
    g_assert_cmpstr(result.filename, ==, "patch_0002.py");
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_READY);
    aa_candidate_result_clear(&result);

    g_autofree char *fresh_path = g_build_filename(f->watch, "patch_0002.py", NULL);
    g_assert_cmpint(utime(fresh_path, &old), ==, 0);
    g_assert_true(aa_candidate_scan_preview(&f->cfg, h, &result, NULL));
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_AMBIGUOUS);
    aa_candidate_result_clear(&result);
    aa_history_free(h);
}

static void test_symlink(Fixture *f, gconstpointer data) {
    (void)data;
    AaHistory *h = open_history(f);
    g_autofree char *link = g_build_filename(f->watch, "patch_0001.py", NULL);
    g_assert_cmpint(symlink(f->root, link), ==, 0);
    AaCandidateResult result = scan(f, h);
    g_assert_cmpint(result.status, ==, AA_CANDIDATE_NONE);
    aa_candidate_result_clear(&result);
    aa_history_free(h);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add("/candidates/sequence-and-persistence", Fixture, NULL, fixture_setup, test_sequence, fixture_teardown);
    g_test_add("/candidates/ambiguous", Fixture, NULL, fixture_setup, test_ambiguity, fixture_teardown);
    g_test_add("/candidates/age-and-number", Fixture, NULL, fixture_setup, test_age_and_numbers, fixture_teardown);
    g_test_add("/candidates/no-symlinks", Fixture, NULL, fixture_setup, test_symlink, fixture_teardown);
    g_test_add("/candidates/expired-preview", Fixture, NULL, fixture_setup, test_expired_preview, fixture_teardown);
    return g_test_run();
}
