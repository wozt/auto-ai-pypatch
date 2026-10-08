#include "patch_meta.h"
#include <string.h>

char *aa_patch_meta_read(const char *path, const char *project_dir, GError **error) {
    g_autofree gchar *content = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &content, &length, error)) return NULL;
    if (length > 4u * 1024u * 1024u || !g_utf8_validate(content, length, NULL)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "En-tête du patch trop grand ou non UTF-8.");
        return NULL;
    }
    g_autofree char *expected_project = g_path_get_basename(project_dir);
    g_auto(GStrv) lines = g_strsplit(content, "\n", 48);
    const char *commit_marker = "# Auto-AI-PyPatch: commit-message:";
    const char *project_marker = "# Auto-AI-PyPatch: project:";
    g_autofree gchar *message = NULL;
    g_autofree gchar *project = NULL;
    for (guint i = 0; lines[i] && i < 40; i++) {
        char *line = g_strstrip(lines[i]);
        if (!i && g_str_has_prefix(line, "#!")) continue;
        if (!*line) continue;
        if (line[0] != '#') break; /* Metadata belongs to opening comments only. */
        if (g_str_has_prefix(line, commit_marker)) {
            if (message) goto invalid;
            message = g_strdup(g_strstrip(line + strlen(commit_marker)));
        } else if (g_str_has_prefix(line, project_marker)) {
            if (project) goto invalid;
            project = g_strdup(g_strstrip(line + strlen(project_marker)));
        }
    }
    if (!message || !*message || strlen(message) > 200 || strchr(message, '\r') ||
        strchr(message, '\n') || strchr(message, '\t') ||
        strchr(message, '<') || strchr(message, '>') ||
        !project || g_strcmp0(project, expected_project) != 0) goto invalid;
    for (const char *p = message; *p; p++)
        if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7f) goto invalid;
    return g_steal_pointer(&message);
invalid:
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                "Métadonnées invalides : exiger un unique commit-message (1–200 caractères) "
                "et project: %s au début du script Python.", expected_project);
    return NULL;
}
