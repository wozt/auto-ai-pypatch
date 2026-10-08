#include "history.h"
#include <errno.h>
#include <glib/gstdio.h>

struct AaHistory {
    GKeyFile *key;
    char *file;
    char *group;
    gboolean applied;
};

static char *profile_group(const AaConfig *c, gboolean applied) {
    g_autofree char *watch = g_canonicalize_filename(c->watch_dir ? c->watch_dir : "", NULL);
    g_autofree char *project = g_canonicalize_filename(c->project_dir ? c->project_dir : "", NULL);
    g_autofree char *profile = g_strdup_printf("%s\n%s\n%s\n%s\n%s\n%s\n%s\n%d",
        watch, project, c->regex ? c->regex : "", c->prefix ? c->prefix : "",
        c->suffix ? c->suffix : "", c->extension ? c->extension : "",
        c->numbering_regex ? c->numbering_regex : "", c->numbering);
    g_autofree char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, profile, -1);
    return g_strconcat(applied ? "applied-" : "preview-", digest, NULL);
}

static AaHistory *open_at(const AaConfig *c, const char *state_root,
                          gboolean applied, GError **error) {
    AaHistory *h = g_new0(AaHistory, 1);
    h->key = g_key_file_new();
    h->applied = applied;
    h->group = profile_group(c, applied);
    h->file = g_build_filename(state_root, "auto-ai-pypatch",
             applied ? "applied-history.ini" : "preview-history.ini", NULL);
    if (g_file_test(h->file, G_FILE_TEST_EXISTS) &&
        !g_key_file_load_from_file(h->key, h->file, G_KEY_FILE_NONE, error)) {
        aa_history_free(h);
        return NULL;
    }
    return h;
}

AaHistory *aa_history_open_at(const AaConfig *c, const char *state_root, GError **error) {
    return open_at(c, state_root, FALSE, error);
}
AaHistory *aa_history_open(const AaConfig *c, GError **error) {
    return open_at(c, g_get_user_state_dir(), FALSE, error);
}
AaHistory *aa_history_open_applied_at(const AaConfig *c, const char *state_root, GError **error) {
    return open_at(c, state_root, TRUE, error);
}
AaHistory *aa_history_open_applied(const AaConfig *c, GError **error) {
    return open_at(c, g_get_user_state_dir(), TRUE, error);
}

void aa_history_free(AaHistory *h) {
    if (!h) return;
    g_clear_pointer(&h->key, g_key_file_unref);
    g_free(h->file);
    g_free(h->group);
    g_free(h);
}

static char *seen_key(const char *basename) {
    g_autofree char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, basename, -1);
    return g_strconcat("seen-", digest, NULL);
}

gboolean aa_history_seen(const AaHistory *h, const char *basename) {
    g_autofree char *key = seen_key(basename);
    return g_key_file_has_key(h->key, h->group, key, NULL);
}

gboolean aa_history_last_number(const AaHistory *h, guint64 *out_number) {
    g_autofree char *value = g_key_file_get_string(h->key, h->group, "last-number", NULL);
    if (!value || !*value) return FALSE;
    char *end = NULL;
    errno = 0;
    guint64 n = g_ascii_strtoull(value, &end, 10);
    if (errno || !end || *end) return FALSE;
    if (out_number) *out_number = n;
    return TRUE;
}

static gboolean persist(AaHistory *h, const char *old_data, GError **error) {
    g_autofree char *directory = g_path_get_dirname(h->file);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create state directory %s: %s", directory, g_strerror(errno));
    } else {
        g_autofree char *content = g_key_file_to_data(h->key, NULL, error);
        if (content && g_file_set_contents_full(h->file, content, -1,
                    G_FILE_SET_CONTENTS_CONSISTENT, 0600, error)) return TRUE;
    }
    g_key_file_unref(h->key);
    h->key = g_key_file_new();
    if (old_data && *old_data)
        g_key_file_load_from_data(h->key, old_data, -1, G_KEY_FILE_NONE, NULL);
    return FALSE;
}

static char *attempt_key(const char *basename) {
    g_autofree char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, basename, -1);
    return g_strconcat("attempt-", digest, NULL);
}

gboolean aa_history_needs_confirmation(const AaHistory *h, const char *basename) {
    g_autofree char *key = attempt_key(basename);
    return h->applied && g_key_file_has_key(h->key, h->group, key, NULL);
}

gboolean aa_history_record_attempt(AaHistory *h, const char *basename, GError **error) {
    g_return_val_if_fail(h->applied, FALSE);
    g_autofree char *old_data = g_key_file_to_data(h->key, NULL, NULL);
    g_autofree char *key = attempt_key(basename);
    g_key_file_set_string(h->key, h->group, key, basename);
    return persist(h, old_data, error);
}

static gboolean record_success(AaHistory *h, const char *basename,
                               gboolean numbered, guint64 number, GError **error) {
    if (aa_history_seen(h, basename)) return TRUE;
    g_autofree char *old_data = g_key_file_to_data(h->key, NULL, NULL);
    g_autofree char *key = seen_key(basename);
    g_autofree char *attempt = attempt_key(basename);
    g_key_file_set_string(h->key, h->group, key, basename);
    if (h->applied) g_key_file_remove_key(h->key, h->group, attempt, NULL);
    if (numbered) {
        g_autofree char *n = g_strdup_printf("%" G_GUINT64_FORMAT, number);
        g_key_file_set_string(h->key, h->group, "last-number", n);
    }
    return persist(h, old_data, error);
}

gboolean aa_history_record_preview(AaHistory *h, const char *basename,
                                   gboolean numbered, guint64 number, GError **error) {
    g_return_val_if_fail(!h->applied, FALSE);
    return record_success(h, basename, numbered, number, error);
}
gboolean aa_history_record_applied(AaHistory *h, const char *basename,
                                   gboolean numbered, guint64 number, GError **error) {
    g_return_val_if_fail(h->applied, FALSE);
    return record_success(h, basename, numbered, number, error);
}
