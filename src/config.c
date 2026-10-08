#include "config.h"
#include <glib/gstdio.h>
#include <errno.h>

static char *config_file(void) {
    return g_build_filename(g_get_user_config_dir(), "auto-ai-pypatch", "config.ini", NULL);
}

void aa_config_init(AaConfig *c) {
    *c = (AaConfig){0};
    const char *download = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    c->watch_dir = g_strdup(download ? download : g_get_home_dir());
    c->project_dir = g_strdup("");
    c->regex = g_strdup(".*\\.py$");
    c->prefix = g_strdup("patch_");
    c->suffix = g_strdup("");
    c->extension = g_strdup(".py");
    c->language = g_strdup("fr");
    c->numbering_regex = g_strdup("[0-9]{4}");
    c->max_age = 120;
    c->auto_limit = 1;
}

void aa_config_clear(AaConfig *c) {
    g_clear_pointer(&c->watch_dir, g_free);
    g_clear_pointer(&c->project_dir, g_free);
    g_clear_pointer(&c->regex, g_free);
    g_clear_pointer(&c->prefix, g_free);
    g_clear_pointer(&c->suffix, g_free);
    g_clear_pointer(&c->extension, g_free);
    g_clear_pointer(&c->language, g_free);
    g_clear_pointer(&c->numbering_regex, g_free);
}

static char *get_string(GKeyFile *key, const char *name, const char *fallback) {
    g_autofree char *s = g_key_file_get_string(key, "settings", name, NULL);
    return s ? g_steal_pointer(&s) : g_strdup(fallback);
}
static guint get_uint(GKeyFile *key, const char *name, guint fallback) {
    g_autoptr(GError) err = NULL;
    gint64 n = g_key_file_get_int64(key, "settings", name, &err);
    return err || n < 0 || n > G_MAXUINT ? fallback : (guint)n;
}
static gboolean get_bool(GKeyFile *key, const char *name, gboolean fallback) {
    g_autoptr(GError) err = NULL;
    gboolean v = g_key_file_get_boolean(key, "settings", name, &err);
    return err ? fallback : v;
}

gboolean aa_config_load(AaConfig *c, GError **error) {
    g_autofree char *path = config_file();
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) return TRUE;
    g_autoptr(GKeyFile) key = g_key_file_new();
    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, error)) return FALSE;
#define READ(field, name) do { char *v = get_string(key, name, c->field); g_free(c->field); c->field = v; } while (0)
    READ(watch_dir, "watch_dir"); READ(project_dir, "project_dir");
    READ(regex, "regex"); READ(prefix, "prefix"); READ(suffix, "suffix");
    READ(extension, "extension"); READ(language, "language");
    READ(numbering_regex, "numbering_regex");
#undef READ
    c->numbering = get_bool(key, "numbering", c->numbering);
    c->automatic = get_bool(key, "automatic", c->automatic);
    c->git_enabled = get_bool(key, "git_enabled", c->git_enabled);
    c->max_age = get_uint(key, "max_age", c->max_age);
    c->auto_limit = get_uint(key, "auto_limit", c->auto_limit);
    return TRUE;
}

gboolean aa_config_save(const AaConfig *c, GError **error) {
    g_autoptr(GKeyFile) key = g_key_file_new();
#define WRITE(field, name) g_key_file_set_string(key, "settings", name, c->field)
    WRITE(watch_dir, "watch_dir"); WRITE(project_dir, "project_dir");
    WRITE(regex, "regex"); WRITE(prefix, "prefix"); WRITE(suffix, "suffix");
    WRITE(extension, "extension"); WRITE(language, "language");
    WRITE(numbering_regex, "numbering_regex");
#undef WRITE
    g_key_file_set_boolean(key, "settings", "numbering", c->numbering);
    g_key_file_set_boolean(key, "settings", "automatic", c->automatic);
    g_key_file_set_boolean(key, "settings", "git_enabled", c->git_enabled);
    g_key_file_set_uint64(key, "settings", "max_age", c->max_age);
    g_key_file_set_uint64(key, "settings", "auto_limit", c->auto_limit);
    g_autofree char *path = config_file();
    g_autofree char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot create %s", dir);
        return FALSE;
    }
    gsize len = 0;
    g_autofree char *content = g_key_file_to_data(key, &len, error);
    if (!content) return FALSE;
    return g_file_set_contents(path, content, (gssize)len, error);
}
