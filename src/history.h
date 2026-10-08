#pragma once
#include "config.h"

typedef struct AaHistory AaHistory;

/* Dry-run observations live in their own file, isolated from future applied-patch history. */
AaHistory *aa_history_open(const AaConfig *config, GError **error);
/* Testing hook; state_root is the equivalent of $XDG_STATE_HOME. */
AaHistory *aa_history_open_at(const AaConfig *config, const char *state_root, GError **error);
void aa_history_free(AaHistory *history);
gboolean aa_history_seen(const AaHistory *history, const char *basename);
gboolean aa_history_last_number(const AaHistory *history, guint64 *out_number);
gboolean aa_history_record_preview(AaHistory *history, const char *basename,
                                   gboolean numbered, guint64 number, GError **error);

/* Applied-patch history is separate from dry-run observations. */
AaHistory *aa_history_open_applied(const AaConfig *config, GError **error);
AaHistory *aa_history_open_applied_at(const AaConfig *config, const char *state_root, GError **error);
/* Persist before spawn: crash or failed attempts require explicit reconfirmation. */
gboolean aa_history_record_attempt(AaHistory *history, const char *basename, GError **error);
gboolean aa_history_needs_confirmation(const AaHistory *history, const char *basename);
gboolean aa_history_record_applied(AaHistory *history, const char *basename,
                                   gboolean numbered, guint64 number, GError **error);
