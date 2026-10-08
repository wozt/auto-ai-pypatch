#pragma once
#include "config.h"

guint aa_prompt_language_count(void);
const char *aa_prompt_language_name(guint index);
const char *aa_prompt_language_code(guint index);
guint aa_prompt_language_index(const char *code);
char *aa_prompt_generate(const AaConfig *config);
