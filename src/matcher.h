#pragma once
#include "config.h"

/* Combines all active rules: full-name regex AND prefix/number/suffix/extension. */
gboolean aa_matcher_validate(const AaConfig *config, GError **error);
gboolean aa_matcher_matches(const AaConfig *config, const char *basename);
/* Regex representing the deterministic parts of the expected file name. */
char *aa_matcher_preview(const AaConfig *config, GError **error);
