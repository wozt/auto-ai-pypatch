#define _POSIX_C_SOURCE 200809L
#include "candidate.h"
#include "matcher.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

static char *extension_with_dot(const char *ext) {
    if (!ext || !*ext) return g_strdup("");
    return g_str_has_prefix(ext, ".") ? g_strdup(ext) : g_strconcat(".", ext, NULL);
}

static gint compare_names(gconstpointer left, gconstpointer right) {
    return g_strcmp0(*(char * const *)left, *(char * const *)right);
}

static gboolean numeric_segment(const AaConfig *c, const char *basename, guint64 *out) {
    const char *prefix = c->prefix ? c->prefix : "";
    const char *suffix = c->suffix ? c->suffix : "";
    g_autofree char *extension = extension_with_dot(c->extension);
    gsize length = strlen(basename);
    gsize head = strlen(prefix);
    gsize tail = strlen(suffix) + strlen(extension);
    if (length <= head + tail || !g_str_has_prefix(basename, prefix) ||
        !g_str_has_suffix(basename, extension)) return FALSE;
    /* Matcher already verified the full constructed pattern. */
    g_autofree char *trailing = g_strconcat(suffix, extension, NULL);
    if (!g_str_has_suffix(basename, trailing)) return FALSE;
    g_autofree char *segment = g_strndup(basename + head, length - head - tail);
    if (!*segment) return FALSE;
    for (const char *p = segment; *p; p++)
        if (*p < '0' || *p > '9') return FALSE;
    errno = 0;
    char *end = NULL;
    guint64 number = g_ascii_strtoull(segment, &end, 10);
    if (errno == ERANGE || !end || *end) return FALSE;
    *out = number;
    return TRUE;
}

void aa_candidate_result_clear(AaCandidateResult *r) {
    if (!r) return;
    g_free(r->filename);
    g_free(r->detail);
    *r = (AaCandidateResult){0};
}

gboolean aa_candidate_scan(const AaConfig *c, const AaHistory *history,
                           AaCandidateResult *result, GError **error) {
    *result = (AaCandidateResult){0};
    g_autoptr(GDir) directory = g_dir_open(c->watch_dir, 0, error);
    if (!directory) return FALSE;
    gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    const char *name;
    g_autoptr(GPtrArray) choices = g_ptr_array_new_with_free_func(g_free);
    while ((name = g_dir_read_name(directory))) {
        if (!aa_matcher_matches(c, name) || aa_history_seen(history, name)) continue;
        g_autofree char *path = g_build_filename(c->watch_dir, name, NULL);
        GStatBuf st;
        if (g_lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        /* 0 disables age checking. Ignore grossly future-dated files. */
        gint64 age = now - (gint64)st.st_mtime;
        if (age < -300 || (c->max_age && age > (gint64)c->max_age)) continue;
        g_ptr_array_add(choices, g_strdup(name));
    }
    result->eligible_count = choices->len;
    if (!choices->len) return TRUE;
    if (choices->len > 1) {
        result->status = AA_CANDIDATE_AMBIGUOUS;
        g_ptr_array_sort(choices, compare_names);
        g_autoptr(GString) description = g_string_new(NULL);
        for (guint i = 0; i < choices->len && i < 4; i++) {
            if (i) g_string_append(description, ", ");
            g_string_append(description, g_ptr_array_index(choices, i));
        }
        if (choices->len > 4) g_string_append(description, ", …");
        result->detail = g_strdup_printf("%u candidats détectés (%s). Aucun fichier validé. Retire les doublons.",
                                          choices->len, description->str);
        return TRUE;
    }
    result->filename = g_strdup(g_ptr_array_index(choices, 0));
    if (c->numbering) {
        if (!numeric_segment(c, result->filename, &result->number)) {
            result->status = AA_CANDIDATE_INVALID_NUMBER;
            result->detail = g_strdup_printf("%s : le segment numéroté doit contenir uniquement des chiffres ASCII (0-9) sur 64 bits.", result->filename);
            return TRUE;
        }
        guint64 last;
        if (aa_history_last_number(history, &last) &&
            (last == G_MAXUINT64 || result->number != last + 1)) {
            result->status = AA_CANDIDATE_OUT_OF_ORDER;
            result->detail = g_strdup_printf("%s : numéro %" G_GUINT64_FORMAT
                ", attendu %" G_GUINT64_FORMAT " après %" G_GUINT64_FORMAT ".",
                result->filename, result->number, last == G_MAXUINT64 ? last : last + 1, last);
            return TRUE;
        }
    }
    result->status = AA_CANDIDATE_READY;
    return TRUE;
}
