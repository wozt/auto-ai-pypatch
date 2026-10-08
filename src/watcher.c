#include "watcher.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <glib-unix.h>

struct AaWatcher {
    int fd;
    guint source_id;
    AaWatcherEvent callback;
    gpointer user_data;
};

static gboolean on_inotify(gint fd, GIOCondition condition, gpointer user_data) {
    AaWatcher *w = user_data;
    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        w->source_id = 0;
        return G_SOURCE_REMOVE;
    }
    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n <= 0) break;
        for (size_t offset = 0; offset < (size_t)n;) {
            const struct inotify_event *e = (const void *)(buffer + offset);
            if (e->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
                if (!(e->mask & IN_ISDIR) && e->len && w->callback)
                    w->callback(e->name, w->user_data);
            }
            offset += sizeof(*e) + e->len;
        }
    }
    return G_SOURCE_CONTINUE;
}

AaWatcher *aa_watcher_start(const char *directory, AaWatcherEvent callback,
                             gpointer user_data, GError **error) {
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "inotify_init1 failed: %s", g_strerror(errno));
        return NULL;
    }
    int wd = inotify_add_watch(fd, directory, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF);
    if (wd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot watch %s: %s", directory, g_strerror(errno));
        close(fd);
        return NULL;
    }
    AaWatcher *w = g_new0(AaWatcher, 1);
    w->fd = fd;
    w->callback = callback;
    w->user_data = user_data;
    w->source_id = g_unix_fd_add(fd, G_IO_IN | G_IO_ERR | G_IO_HUP, on_inotify, w);
    return w;
}

void aa_watcher_stop(AaWatcher *w) {
    if (!w) return;
    if (w->source_id) g_source_remove(w->source_id);
    close(w->fd);
    g_free(w);
}
