#include "window.h"
#include "config.h"
#include "watcher.h"
#include "prompt.h"
#include "log.h"
#include <errno.h>

typedef struct {
    AaConfig config;
    AaWatcher *watcher;
    GtkWidget *window;
    GtkWidget *start_button;
    GtkWidget *status;
    GtkTextBuffer *journal;
    GtkTextBuffer *prompt;
    GtkWidget *watch_entry, *project_entry, *regex_entry, *prefix_entry;
    GtkWidget *suffix_entry, *extension_entry, *age_spin, *limit_spin;
    GtkWidget *numbering_switch, *auto_switch, *git_switch, *language;
} Ui;

static GtkWidget *entry_row(GtkWidget *parent, const char *label, const char *value) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title = gtk_label_new(label);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), value);
    gtk_box_append(GTK_BOX(row), title);
    gtk_box_append(GTK_BOX(row), entry);
    gtk_box_append(GTK_BOX(parent), row);
    return entry;
}
static GtkWidget *switch_row(GtkWidget *parent, const char *label, gboolean state) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *title = gtk_label_new(label);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkWidget *toggle = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(toggle), state);
    gtk_box_append(GTK_BOX(row), title);
    gtk_box_append(GTK_BOX(row), toggle);
    gtk_box_append(GTK_BOX(parent), row);
    return toggle;
}
static GtkWidget *spin_row(GtkWidget *parent, const char *label, guint value) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *title = gtk_label_new(label);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkWidget *spin = gtk_spin_button_new_with_range(0, 86400, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_box_append(GTK_BOX(row), title);
    gtk_box_append(GTK_BOX(row), spin);
    gtk_box_append(GTK_BOX(parent), row);
    return spin;
}
static void replace_string(char **target, GtkWidget *entry) {
    g_free(*target);
    *target = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
}
static void collect(Ui *u) {
    replace_string(&u->config.watch_dir, u->watch_entry);
    replace_string(&u->config.project_dir, u->project_entry);
    replace_string(&u->config.regex, u->regex_entry);
    replace_string(&u->config.prefix, u->prefix_entry);
    replace_string(&u->config.suffix, u->suffix_entry);
    replace_string(&u->config.extension, u->extension_entry);
    g_free(u->config.language);
    u->config.language = g_strdup(gtk_drop_down_get_selected(GTK_DROP_DOWN(u->language)) == 1 ? "en" : "fr");
    u->config.numbering = gtk_switch_get_active(GTK_SWITCH(u->numbering_switch));
    u->config.automatic = gtk_switch_get_active(GTK_SWITCH(u->auto_switch));
    u->config.git_enabled = gtk_switch_get_active(GTK_SWITCH(u->git_switch));
    u->config.max_age = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->age_spin));
    u->config.auto_limit = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->limit_spin));
}
static void on_save(GtkButton *button, gpointer data) {
    (void)button;
    Ui *u = data;
    collect(u);
    g_autoptr(GError) error = NULL;
    if (!aa_config_save(&u->config, &error)) {
        g_autofree char *msg = g_strdup_printf("Erreur de configuration : %s", error->message);
        aa_log_append(u->journal, msg);
    } else aa_log_append(u->journal, "Configuration enregistrée.");
}
static void on_prompt(GtkButton *button, gpointer data) {
    (void)button;
    Ui *u = data;
    collect(u);
    g_autofree char *text = aa_prompt_generate(&u->config);
    gtk_text_buffer_set_text(u->prompt, text, -1);
}
static void on_copy(GtkButton *button, gpointer data) {
    (void)button;
    Ui *u = data;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(u->prompt, &start, &end);
    g_autofree char *text = gtk_text_buffer_get_text(u->prompt, &start, &end, FALSE);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(u->window), text);
    aa_log_append(u->journal, "Prompt copié.");
}
static void on_file(const char *filename, gpointer data) {
    Ui *u = data;
    g_autofree char *message = g_strdup_printf("Fichier reçu : %s (détection uniquement, exécution désactivée)", filename);
    aa_log_append(u->journal, message);
}
static void on_start(GtkButton *button, gpointer data) {
    (void)button;
    Ui *u = data;
    if (u->watcher) {
        aa_watcher_stop(u->watcher);
        u->watcher = NULL;
        gtk_button_set_label(GTK_BUTTON(u->start_button), "Démarrer");
        gtk_label_set_text(GTK_LABEL(u->status), "Surveillance arrêtée");
        aa_log_append(u->journal, "Surveillance arrêtée.");
        return;
    }
    collect(u);
    if (!g_file_test(u->config.watch_dir, G_FILE_TEST_IS_DIR) ||
        !g_file_test(u->config.project_dir, G_FILE_TEST_IS_DIR)) {
        aa_log_append(u->journal, "Erreur : renseigner deux dossiers existants (surveillance et projet).");
        return;
    }
    g_autoptr(GError) error = NULL;
    u->watcher = aa_watcher_start(u->config.watch_dir, on_file, u, &error);
    if (!u->watcher) {
        g_autofree char *msg = g_strdup_printf("Surveillance impossible : %s", error->message);
        aa_log_append(u->journal, msg);
        return;
    }
    gtk_button_set_label(GTK_BUTTON(u->start_button), "Arrêter");
    gtk_label_set_text(GTK_LABEL(u->status), "Surveillance active (aucune exécution)");
    aa_log_append(u->journal, "Surveillance active. Les scripts ne seront PAS exécutés.");
}
static void on_close(GtkWindow *window, gpointer data) {
    (void)window;
    Ui *u = data;
    aa_watcher_stop(u->watcher);
    collect(u);
    g_autoptr(GError) error = NULL;
    if (!aa_config_save(&u->config, &error)) g_warning("Config save: %s", error->message);
    aa_config_clear(&u->config);
    g_free(u);
}

GtkWidget *aa_window_new(AdwApplication *app) {
    Ui *u = g_new0(Ui, 1);
    aa_config_init(&u->config);
    g_autoptr(GError) error = NULL;
    if (!aa_config_load(&u->config, &error)) g_warning("Config load: %s", error->message);

    u->window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(u->window), "Auto-AI-PyPatch");
    gtk_window_set_default_size(GTK_WINDOW(u->window), 1100, 780);
    g_signal_connect(u->window, "destroy", G_CALLBACK(on_close), u);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(outer, 12);
    gtk_widget_set_margin_bottom(outer, 12);
    gtk_widget_set_margin_start(outer, 12);
    gtk_widget_set_margin_end(outer, 12);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(u->window), outer);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(outer), toolbar);
    u->status = gtk_label_new("Surveillance arrêtée");
    gtk_widget_set_hexpand(u->status, TRUE);
    gtk_widget_set_halign(u->status, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(toolbar), u->status);
    u->start_button = gtk_button_new_with_label("Démarrer");
    gtk_box_append(GTK_BOX(toolbar), u->start_button);
    g_signal_connect(u->start_button, "clicked", G_CALLBACK(on_start), u);
    GtkWidget *save = gtk_button_new_with_label("Enregistrer");
    gtk_box_append(GTK_BOX(toolbar), save);
    g_signal_connect(save, "clicked", G_CALLBACK(on_save), u);

    GtkWidget *prompt_head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(outer), prompt_head);
    GtkWidget *prompt_title = gtk_label_new("Prompt suggéré au LLM");
    gtk_widget_set_hexpand(prompt_title, TRUE);
    gtk_widget_set_halign(prompt_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(prompt_head), prompt_title);
    GtkWidget *regen = gtk_button_new_with_label("Régénérer");
    GtkWidget *copy = gtk_button_new_with_label("Copier");
    gtk_box_append(GTK_BOX(prompt_head), regen);
    gtk_box_append(GTK_BOX(prompt_head), copy);
    g_signal_connect(regen, "clicked", G_CALLBACK(on_prompt), u);
    g_signal_connect(copy, "clicked", G_CALLBACK(on_copy), u);

    GtkWidget *prompt_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(prompt_scroll, -1, 155);
    gtk_box_append(GTK_BOX(outer), prompt_scroll);
    GtkWidget *prompt_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(prompt_view), GTK_WRAP_WORD_CHAR);
    u->prompt = gtk_text_view_get_buffer(GTK_TEXT_VIEW(prompt_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(prompt_scroll), prompt_view);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_position(GTK_PANED(paned), 445);
    gtk_box_append(GTK_BOX(outer), paned);

    GtkWidget *left_scroll = gtk_scrolled_window_new();
    gtk_paned_set_start_child(GTK_PANED(paned), left_scroll);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);
    GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_end(settings, 14);
    gtk_widget_set_margin_top(settings, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(left_scroll), settings);
    GtkWidget *heading = gtk_label_new("Configuration");
    gtk_widget_add_css_class(heading, "title-3");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(settings), heading);
    u->watch_entry = entry_row(settings, "Dossier surveillé", u->config.watch_dir);
    u->project_entry = entry_row(settings, "Dossier projet", u->config.project_dir);
    u->regex_entry = entry_row(settings, "Expression régulière", u->config.regex);
    u->prefix_entry = entry_row(settings, "Préfixe", u->config.prefix);
    u->suffix_entry = entry_row(settings, "Suffixe", u->config.suffix);
    u->extension_entry = entry_row(settings, "Extension", u->config.extension);
    u->age_spin = spin_row(settings, "Âge maximal (secondes)", u->config.max_age);
    u->limit_spin = spin_row(settings, "Limite automatique (0 = infini)", u->config.auto_limit);
    u->numbering_switch = switch_row(settings, "Numérotation incrémentale", u->config.numbering);
    u->auto_switch = switch_row(settings, "Exécution automatique (future)", u->config.automatic);
    u->git_switch = switch_row(settings, "Git add/commit/push (futur)", u->config.git_enabled);
    GtkWidget *language_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *language_label = gtk_label_new("Langue du prompt");
    gtk_widget_set_hexpand(language_label, TRUE);
    gtk_widget_set_halign(language_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(language_row), language_label);
    const char *languages[] = {"Français", "English", NULL};
    u->language = gtk_drop_down_new_from_strings(languages);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->language), g_strcmp0(u->config.language, "en") == 0 ? 1 : 0);
    gtk_box_append(GTK_BOX(language_row), u->language);
    gtk_box_append(GTK_BOX(settings), language_row);

    GtkWidget *right_scroll = gtk_scrolled_window_new();
    gtk_paned_set_end_child(GTK_PANED(paned), right_scroll);
    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
    u->journal = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(right_scroll), log_view);

    on_prompt(NULL, u);
    aa_log_append(u->journal, "Squelette prêt : détection inotify seulement, aucun script exécuté.");
    return u->window;
}
