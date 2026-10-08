#include "log.h"
void aa_log_append(GtkTextBuffer *buffer, const char *message) {
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree char *time = g_date_time_format(now, "%H:%M:%S");
    g_autofree char *line = g_strdup_printf("[%s] %s\n", time, message);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(buffer, &iter);
    gtk_text_buffer_insert(buffer, &iter, line, -1);
}
