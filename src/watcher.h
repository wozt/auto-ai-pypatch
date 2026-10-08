#pragma once
#include <glib.h>

typedef struct AaWatcher AaWatcher;
typedef void (*AaWatcherEvent)(const char *filename, gpointer user_data);
AaWatcher *aa_watcher_start(const char *directory, AaWatcherEvent callback,
                             gpointer user_data, GError **error);
void aa_watcher_stop(AaWatcher *watcher);
