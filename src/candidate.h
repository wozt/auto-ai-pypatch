#pragma once
#include "config.h"
#include "history.h"

typedef enum {
    AA_CANDIDATE_NONE,
    AA_CANDIDATE_READY,
    AA_CANDIDATE_AMBIGUOUS,
    AA_CANDIDATE_TOO_OLD, /* visible to the UI; never automatically executed */
    AA_CANDIDATE_INVALID_NUMBER,
    AA_CANDIDATE_OUT_OF_ORDER
} AaCandidateStatus;

typedef struct {
    AaCandidateStatus status;
    char *filename; /* Set when exactly one eligible candidate exists. */
    char *detail;   /* Human-readable reason; caller frees it. */
    guint eligible_count;
    guint64 number;
    gint64 age_seconds; /* rounded-down age in seconds, meaningful for READY/TOO_OLD */
} AaCandidateResult;

/* Pure selection; never executes code or modifies history. */
gboolean aa_candidate_scan(const AaConfig *config, const AaHistory *history,
                           AaCandidateResult *result, GError **error);
/* UI/manual lookup: prefer normally eligible files, but surface an expired
 * matching file only if no fresh candidate exists. Never auto-execute TOO_OLD.
 */
gboolean aa_candidate_scan_preview(const AaConfig *config, const AaHistory *history,
                                   AaCandidateResult *result, GError **error);
void aa_candidate_result_clear(AaCandidateResult *result);
