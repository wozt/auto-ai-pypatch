#pragma once
#include "config.h"
#include "history.h"

typedef enum {
    AA_CANDIDATE_NONE,
    AA_CANDIDATE_READY,
    AA_CANDIDATE_AMBIGUOUS,
    AA_CANDIDATE_INVALID_NUMBER,
    AA_CANDIDATE_OUT_OF_ORDER
} AaCandidateStatus;

typedef struct {
    AaCandidateStatus status;
    char *filename; /* Set when exactly one eligible candidate exists. */
    char *detail;   /* Human-readable reason; caller frees it. */
    guint eligible_count;
    guint64 number;
} AaCandidateResult;

/* Pure selection; never executes code or modifies history. */
gboolean aa_candidate_scan(const AaConfig *config, const AaHistory *history,
                           AaCandidateResult *result, GError **error);
void aa_candidate_result_clear(AaCandidateResult *result);
