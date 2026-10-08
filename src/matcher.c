#include "matcher.h"
#include <string.h>

static char *normalized_extension(const char *input) {
    if (!input || !*input) return g_strdup("");
    return g_str_has_prefix(input, ".") ? g_strdup(input) : g_strconcat(".", input, NULL);
}

/* Prefix, suffix, and extension are literals; only the number field is regex. */
static char *build_filename_pattern(const AaConfig *c) {
    g_autofree char *pre = g_regex_escape_string(c->prefix ? c->prefix : "", -1);
    g_autofree char *post = g_regex_escape_string(c->suffix ? c->suffix : "", -1);
    g_autofree char *ext_raw = normalized_extension(c->extension);
    g_autofree char *ext = g_regex_escape_string(ext_raw, -1);
    if (c->numbering) return g_strdup_printf("^%s(?:%s)%s%s$", pre, c->numbering_regex ? c->numbering_regex : "", post, ext);
    return g_strdup_printf("^%s.*%s%s$", pre, post, ext);
}

static gboolean compiled(const char *pattern, GError **error) {
    g_autoptr(GRegex) re = g_regex_new(pattern, G_REGEX_OPTIMIZE, 0, error);
    return re != NULL;
}

gboolean aa_matcher_validate(const AaConfig *c, GError **error) {
    if (c->regex && *c->regex && !compiled(c->regex, error)) return FALSE;
    if (c->numbering && (!c->numbering_regex || !*c->numbering_regex)) {
        g_set_error_literal(error, G_REGEX_ERROR, G_REGEX_ERROR_COMPILE,
                            "La regex de numérotation ne peut pas être vide.");
        return FALSE;
    }
    g_autofree char *pattern = build_filename_pattern(c);
    return compiled(pattern, error);
}

char *aa_matcher_preview(const AaConfig *c, GError **error) {
    if (!aa_matcher_validate(c, error)) return NULL;
    return build_filename_pattern(c);
}

gboolean aa_matcher_matches(const AaConfig *c, const char *basename) {
    if (!basename || !*basename || strchr(basename, '/') || strchr(basename, '\\')) return FALSE;
    g_autofree char *pattern = build_filename_pattern(c);
    g_autoptr(GError) error = NULL;
    g_autoptr(GRegex) constructed = g_regex_new(pattern, G_REGEX_OPTIMIZE, 0, &error);
    if (!constructed || !g_regex_match(constructed, basename, 0, NULL)) return FALSE;
    if (!c->regex || !*c->regex) return TRUE;
    g_autoptr(GRegex) custom = g_regex_new(c->regex, G_REGEX_OPTIMIZE, 0, &error);
    return custom && g_regex_match(custom, basename, 0, NULL);
}
