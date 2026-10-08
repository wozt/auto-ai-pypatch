#include "window.h"
#include "config.h"
#include "watcher.h"
#include "prompt.h"
#include "matcher.h"
#include "log.h"
#include <gio/gio.h>

typedef struct {
    AaConfig config;
    AaWatcher *watcher;
    GtkWidget *window;
    GtkWidget *start_button;
    GtkWidget *status;
    GtkTextBuffer *journal;
    GtkTextBuffer *prompt;
    GtkWidget *watch_entry, *project_entry, *regex_entry, *prefix_entry;
    GtkWidget *suffix_entry, *extension_entry, *number_regex_entry;
    GtkWidget *age_spin, *limit_spin, *numbering_switch;
    GtkWidget *auto_switch, *git_switch, *language;
    GtkWidget *preview, *preview_extra, *number_examples;
} Ui;

static GtkWidget *entry_row(GtkWidget *parent, const char *label, const char *value) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title = gtk_label_new(label);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(entry), value ? value : "");
    gtk_box_append(GTK_BOX(row), title);
    gtk_box_append(GTK_BOX(row), entry);
    gtk_box_append(GTK_BOX(parent), row);
    return entry;
}

static void folder_chosen(GObject *source, GAsyncResult *result, gpointer user_data) {
    GWeakRef *target_ref = user_data;
    GtkWidget *target = GTK_WIDGET(g_weak_ref_get(target_ref));
    g_autoptr(GError) error = NULL;
    g_autoptr(GFile) folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);
    if (folder) {
        g_autofree char *path = g_file_get_path(folder);
        if (path && target) gtk_editable_set_text(GTK_EDITABLE(target), path);
    } else if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_warning("Dossier non sélectionné : %s", error->message);
    }
    if (target) g_object_unref(target);
    g_weak_ref_clear(target_ref);
    g_free(target_ref);
}

static void browse_folder(GtkButton *button, gpointer data) {
    GtkWidget *target = GTK_WIDGET(data);
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (!GTK_IS_WINDOW(root)) return;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Sélectionner un dossier");
    const char *path = gtk_editable_get_text(GTK_EDITABLE(target));
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_autoptr(GFile) folder = g_file_new_for_path(path);
        gtk_file_dialog_set_initial_folder(dialog, folder);
    }
    GWeakRef *target_ref = g_new0(GWeakRef, 1);
    g_weak_ref_init(target_ref, G_OBJECT(target));
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(root), NULL, folder_chosen, target_ref);
    g_object_unref(dialog);
}

static GtkWidget *folder_row(GtkWidget *parent, const char *label, const char *value) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title = gtk_label_new(label);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), title);
    GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(entry), value ? value : "");
    GtkWidget *browse = gtk_button_new_with_label("Parcourir…");
    g_signal_connect(browse, "clicked", G_CALLBACK(browse_folder), entry);
    gtk_box_append(GTK_BOX(line), entry);
    gtk_box_append(GTK_BOX(line), browse);
    gtk_box_append(GTK_BOX(row), line);
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
    replace_string(&u->config.numbering_regex, u->number_regex_entry);
    g_free(u->config.language);
    u->config.language = g_strdup(aa_prompt_language_code(gtk_drop_down_get_selected(GTK_DROP_DOWN(u->language))));
    u->config.numbering = gtk_switch_get_active(GTK_SWITCH(u->numbering_switch));
    u->config.automatic = gtk_switch_get_active(GTK_SWITCH(u->auto_switch));
    u->config.git_enabled = gtk_switch_get_active(GTK_SWITCH(u->git_switch));
    u->config.max_age = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->age_spin));
    u->config.auto_limit = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->limit_spin));
}

static void update_preview(Ui *u) {
    collect(u);
    gtk_widget_set_sensitive(u->number_regex_entry, u->config.numbering);
    if (u->number_examples) gtk_widget_set_sensitive(u->number_examples, u->config.numbering);
    g_autoptr(GError) error = NULL;
    g_autofree char *expected = aa_matcher_preview(&u->config, &error);
    if (expected) {
        g_autofree char *escaped = g_markup_escape_text(expected, -1);
        g_autofree char *markup = g_strdup_printf(
            "<span foreground='#2ab96e' weight='bold' font_family='monospace'>EXPECTED FILENAME: %s</span>", escaped);
        gtk_label_set_markup(GTK_LABEL(u->preview), markup);
        const char *extra = u->config.regex && *u->config.regex ? u->config.regex : ".*";
        g_autofree char *detail = g_strdup_printf("AND filename regex: %s", extra);
        gtk_label_set_text(GTK_LABEL(u->preview_extra), detail);
    } else {
        g_autofree char *escaped = g_markup_escape_text(error ? error->message : "unknown regex error", -1);
        g_autofree char *markup = g_strdup_printf(
            "<span foreground='#e04b4b' weight='bold' font_family='monospace'>INVALID REGEX: %s</span>", escaped);
        gtk_label_set_markup(GTK_LABEL(u->preview), markup);
        gtk_label_set_text(GTK_LABEL(u->preview_extra), "Corrige la regex avant de démarrer la surveillance.");
    }
}

static void on_field_changed(GtkEditable *editable, gpointer data) {
    (void)editable;
    update_preview(data);
}
static void on_numbering_changed(GObject *switch_obj, GParamSpec *pspec, gpointer data) {
    (void)switch_obj; (void)pspec;
    update_preview(data);
}

static const char *const filename_examples[] = {
    "^patch_[0-9]{4}\\.py$",
    "^patch_[0-9]+\\.py$",
    "^patch_(?:fix|feat)_[0-9]{4}\\.py$",
    "^ai_.*\\.py$",
    ".*\\.py$",
};
static const char *const number_examples[] = {
    "[0-9]{4}",
    "[0-9]+",
    "[1-9][0-9]*",
    "[0-9]{3,6}",
    "[0-9]{6}",
};

static void on_example_changed(GObject *source, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    GtkDropDown *dropdown = GTK_DROP_DOWN(source);
    guint index = gtk_drop_down_get_selected(dropdown);
    if (!index || index > G_N_ELEMENTS(filename_examples)) return;
    GtkWidget *entry = GTK_WIDGET(data);
    gboolean is_number = GPOINTER_TO_INT(g_object_get_data(source, "number-example"));
    gtk_editable_set_text(GTK_EDITABLE(entry), is_number ? number_examples[index - 1] : filename_examples[index - 1]);
    gtk_drop_down_set_selected(dropdown, 0);
}

static GtkWidget *example_picker(GtkWidget *parent, GtkWidget *entry, gboolean number) {
    const char *names[] = {
        "Exemples de regex ▾", "4 chiffres : [0-9]{4}", "1+ chiffres : [0-9]+",
        "Sans zéro initial : [1-9][0-9]*", "3 à 6 chiffres : [0-9]{3,6}",
        "6 chiffres : [0-9]{6}", NULL
    };
    const char *name_regex[] = {
        "Exemples de regex ▾", "Patch sur 4 chiffres", "Patch sur N chiffres",
        "Fix ou feat numéroté", "Préfixe ai_", "Tous les fichiers .py", NULL
    };
    GtkWidget *picker = gtk_drop_down_new_from_strings(number ? names : name_regex);
    g_object_set_data(G_OBJECT(picker), "number-example", GINT_TO_POINTER(number));
    gtk_widget_set_halign(picker, GTK_ALIGN_START);
    g_signal_connect(picker, "notify::selected", G_CALLBACK(on_example_changed), entry);
    gtk_box_append(GTK_BOX(parent), picker);
    return picker;
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

static void on_language_changed(GObject *object, GParamSpec *pspec, gpointer data) {
    (void)object; (void)pspec;
    on_prompt(NULL, data);
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
    if (!aa_matcher_matches(&u->config, filename)) return;
    g_autofree char *message = g_strdup_printf(
        "Fichier correspondant aux filtres : %s (détection uniquement ; exécution désactivée)", filename);
    aa_log_append(u->journal, message);
}

static void stop_watcher(Ui *u) {
    if (!u->watcher) return;
    aa_watcher_stop(u->watcher);
    u->watcher = NULL;
    gtk_button_set_label(GTK_BUTTON(u->start_button), "Démarrer");
    gtk_label_set_text(GTK_LABEL(u->status), "Surveillance arrêtée");
}

static void on_start(GtkButton *button, gpointer data) {
    (void)button;
    Ui *u = data;
    if (u->watcher) {
        stop_watcher(u);
        aa_log_append(u->journal, "Surveillance arrêtée.");
        return;
    }
    collect(u);
    g_autoptr(GError) error = NULL;
    if (!aa_matcher_validate(&u->config, &error)) {
        g_autofree char *message = g_strdup_printf("Erreur de regex : %s", error->message);
        aa_log_append(u->journal, message);
        return;
    }
    if (!g_file_test(u->config.watch_dir, G_FILE_TEST_IS_DIR) ||
        !g_file_test(u->config.project_dir, G_FILE_TEST_IS_DIR)) {
        aa_log_append(u->journal, "Erreur : renseigner deux dossiers existants (surveillance et projet).");
        return;
    }
    u->watcher = aa_watcher_start(u->config.watch_dir, on_file, u, &error);
    if (!u->watcher) {
        g_autofree char *msg = g_strdup_printf("Surveillance impossible : %s", error->message);
        aa_log_append(u->journal, msg);
        return;
    }
    gtk_button_set_label(GTK_BUTTON(u->start_button), "Arrêter");
    gtk_label_set_text(GTK_LABEL(u->status), "Surveillance active (aucune exécution)");
    aa_log_append(u->journal, "Surveillance active : seuls les noms qui passent TOUS les filtres sont journalisés.");
}

static gboolean on_close(GtkWindow *window, gpointer data) {
    (void)window;
    Ui *u = data;
    aa_watcher_stop(u->watcher);
    collect(u);
    GtkWidget *entries[] = {u->watch_entry, u->project_entry, u->regex_entry,
        u->prefix_entry, u->suffix_entry, u->extension_entry, u->number_regex_entry};
    for (guint i = 0; i < G_N_ELEMENTS(entries); i++)
        g_signal_handlers_disconnect_by_data(entries[i], u);
    g_signal_handlers_disconnect_by_data(u->numbering_switch, u);
    g_signal_handlers_disconnect_by_data(u->language, u);
    g_autoptr(GError) error = NULL;
    if (!aa_config_save(&u->config, &error)) g_warning("Config save: %s", error->message);
    aa_config_clear(&u->config);
    g_free(u);
    return FALSE; /* Allow GTK4 to close the window. */
}

GtkWidget *aa_window_new(AdwApplication *app) {
    Ui *u = g_new0(Ui, 1);
    aa_config_init(&u->config);
    g_autoptr(GError) error = NULL;
    if (!aa_config_load(&u->config, &error)) g_warning("Config load: %s", error->message);

    u->window = adw_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(u->window), "Auto-AI-PyPatch");
    gtk_window_set_default_size(GTK_WINDOW(u->window), 1150, 820);
    g_signal_connect(u->window, "close-request", G_CALLBACK(on_close), u);

    /* Libadwaita window controls, including the missing Close button. */
    GtkWidget *toolbar_view = adw_toolbar_view_new();
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(u->window), toolbar_view);
    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(header), TRUE);
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), TRUE);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), adw_window_title_new("Auto-AI-PyPatch", "LLM patch runner"));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(outer, 10);
    gtk_widget_set_margin_bottom(outer, 12);
    gtk_widget_set_margin_start(outer, 12);
    gtk_widget_set_margin_end(outer, 12);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), outer);

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
    GtkWidget *lang_text = gtk_label_new("Langue :");
    gtk_box_append(GTK_BOX(prompt_head), lang_text);
    const char **lang_names = g_new0(const char *, aa_prompt_language_count() + 1);
    for (guint i = 0; i < aa_prompt_language_count(); i++) lang_names[i] = aa_prompt_language_name(i);
    u->language = gtk_drop_down_new_from_strings(lang_names);
    g_free(lang_names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->language), aa_prompt_language_index(u->config.language));
    gtk_widget_set_size_request(u->language, 120, -1);
    gtk_box_append(GTK_BOX(prompt_head), u->language);
    g_signal_connect(u->language, "notify::selected", G_CALLBACK(on_language_changed), u);

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
    gtk_paned_set_position(GTK_PANED(paned), 470);
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
    u->watch_entry = folder_row(settings, "Dossier surveillé", u->config.watch_dir);
    u->project_entry = folder_row(settings, "Dossier projet", u->config.project_dir);
    u->regex_entry = entry_row(settings, "Expression régulière (filtre global)", u->config.regex);
    example_picker(settings, u->regex_entry, FALSE);
    u->prefix_entry = entry_row(settings, "Préfixe (texte littéral)", u->config.prefix);
    u->suffix_entry = entry_row(settings, "Suffixe (avant extension)", u->config.suffix);
    u->extension_entry = entry_row(settings, "Extension", u->config.extension);
    u->numbering_switch = switch_row(settings, "Numérotation incrémentale", u->config.numbering);
    u->number_regex_entry = entry_row(settings, "Regex de numérotation", u->config.numbering_regex);
    gtk_widget_set_tooltip_text(u->number_regex_entry, "Exemple : [0-9]{4} pour 0001, 0002, 0003… (motif regex, sans ^ ni $)");
    u->number_examples = example_picker(settings, u->number_regex_entry, TRUE);
    gtk_widget_set_sensitive(u->number_regex_entry, u->config.numbering);
    gtk_widget_set_sensitive(u->number_examples, u->config.numbering);
    u->age_spin = spin_row(settings, "Âge maximal (secondes)", u->config.max_age);
    u->limit_spin = spin_row(settings, "Limite automatique (0 = infini)", u->config.auto_limit);
    u->auto_switch = switch_row(settings, "Exécution automatique (future)", u->config.automatic);
    u->git_switch = switch_row(settings, "Git add/commit/push (futur)", u->config.git_enabled);

    GtkWidget *right_scroll = gtk_scrolled_window_new();
    gtk_paned_set_end_child(GTK_PANED(paned), right_scroll);
    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
    u->journal = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(right_scroll), log_view);

    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(outer), footer);
    u->preview = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(u->preview), 0);
    gtk_label_set_wrap(GTK_LABEL(u->preview), TRUE);
    gtk_label_set_selectable(GTK_LABEL(u->preview), TRUE);
    gtk_box_append(GTK_BOX(footer), u->preview);
    u->preview_extra = gtk_label_new(NULL);
    gtk_widget_add_css_class(u->preview_extra, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(u->preview_extra), 0);
    gtk_label_set_ellipsize(GTK_LABEL(u->preview_extra), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable(GTK_LABEL(u->preview_extra), TRUE);
    gtk_box_append(GTK_BOX(footer), u->preview_extra);

    GtkWidget *entries[] = {u->watch_entry, u->project_entry, u->regex_entry,
        u->prefix_entry, u->suffix_entry, u->extension_entry, u->number_regex_entry};
    for (guint i = 0; i < G_N_ELEMENTS(entries); i++)
        g_signal_connect(entries[i], "changed", G_CALLBACK(on_field_changed), u);
    g_signal_connect(u->numbering_switch, "notify::active", G_CALLBACK(on_numbering_changed), u);
    update_preview(u);
    on_prompt(NULL, u);
    aa_log_append(u->journal, "Interface prête : filtrage des noms actif, exécution Python/Git désactivée.");
    return u->window;
}
