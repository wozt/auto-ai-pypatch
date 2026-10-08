#pragma once
#include <glib.h>

typedef struct {
    char *watch_dir;
    char *project_dir;
    char *regex;
    char *prefix;
    char *suffix;
    char *extension;
    char *language;
    gboolean numbering;
    gboolean automatic;
    gboolean git_enabled;
    guint max_age;
    guint auto_limit;
} AaConfig;

void aa_config_init(AaConfig *config);
void aa_config_clear(AaConfig *config);
gboolean aa_config_load(AaConfig *config, GError **error);
gboolean aa_config_save(const AaConfig *config, GError **error);
