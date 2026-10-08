#include <glib.h>
#include "config.h"
#include "matcher.h"
#include "prompt.h"

static void basic_preview(void) {
    AaConfig c;
    aa_config_init(&c);
    g_autoptr(GError) error = NULL;
    g_autofree char *preview = aa_matcher_preview(&c, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(preview, ==, "^patch_.*\\.py$");
    g_assert_true(aa_matcher_matches(&c, "patch_foo.py"));
    g_assert_false(aa_matcher_matches(&c, "else_foo.py"));
    g_assert_false(aa_matcher_matches(&c, "patch_foo.txt"));
    aa_config_clear(&c);
}

static void numbered_and_suffix(void) {
    AaConfig c;
    aa_config_init(&c);
    c.numbering = TRUE;
    g_free(c.suffix);
    c.suffix = g_strdup("_fix");
    g_free(c.extension);
    c.extension = g_strdup("py");
    g_autoptr(GError) error = NULL;
    g_autofree char *preview = aa_matcher_preview(&c, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(preview, ==, "^patch_(?:[0-9]{4})_fix\\.py$");
    g_assert_true(aa_matcher_matches(&c, "patch_0001_fix.py"));
    g_assert_false(aa_matcher_matches(&c, "patch_001_fix.py"));
    g_assert_false(aa_matcher_matches(&c, "patch_000a_fix.py"));
    g_assert_false(aa_matcher_matches(&c, "patch_0001.py"));
    aa_config_clear(&c);
}

static void generic_regex_and_literal(void) {
    AaConfig c;
    aa_config_init(&c);
    c.numbering = TRUE;
    g_free(c.prefix);
    c.prefix = g_strdup("patch.+_");
    g_free(c.regex);
    c.regex = g_strdup("^patch\\.\\+_00[0-9]{2}\\.py$");
    g_assert_true(aa_matcher_matches(&c, "patch.+_0012.py"));
    g_assert_false(aa_matcher_matches(&c, "patch.+_1234.py"));
    g_assert_false(aa_matcher_matches(&c, "patch123_0012.py"));
    aa_config_clear(&c);
}

static void invalid_regex(void) {
    AaConfig c;
    aa_config_init(&c);
    c.numbering = TRUE;
    g_free(c.numbering_regex);
    c.numbering_regex = g_strdup("[0-9{");
    g_autoptr(GError) error = NULL;
    g_assert_false(aa_matcher_validate(&c, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_free(c.numbering_regex);
    c.numbering_regex = g_strdup("");
    g_assert_false(aa_matcher_validate(&c, &error));
    g_assert_nonnull(error);
    aa_config_clear(&c);
}

static void languages(void) {
    AaConfig c;
    aa_config_init(&c);
    g_assert_cmpuint(aa_prompt_language_count(), >=, 16);
    const char *codes[] = {"fr", "en", "de", "it", "es", "ja", "zh", "ko", "ru", "vi", "tl", "pt", "ar", "hi", "id", "tr", "nl", "pl", "uk", "th", "sv"};
    for (guint i = 0; i < G_N_ELEMENTS(codes); i++) {
        g_free(c.language);
        c.language = g_strdup(codes[i]);
        g_assert_cmpstr(aa_prompt_language_code(aa_prompt_language_index(c.language)), ==, codes[i]);
        g_autofree char *prompt = aa_prompt_generate(&c);
        g_assert_nonnull(g_strstr_len(prompt, -1, "# Auto-AI-PyPatch: commit-message:"));
        g_assert_nonnull(g_strstr_len(prompt, -1, "^patch_.*\\.py$"));
    }
    aa_config_clear(&c);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/matcher/default", basic_preview);
    g_test_add_func("/matcher/numbered", numbered_and_suffix);
    g_test_add_func("/matcher/and-literal", generic_regex_and_literal);
    g_test_add_func("/matcher/invalid", invalid_regex);
    g_test_add_func("/prompt/languages", languages);
    return g_test_run();
}
