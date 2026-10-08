#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <string.h>

#define AA_MAX_PATCH_BYTES (4u * 1024u * 1024u)
#define AA_MAX_OUTPUT_BYTES (512u * 1024u)

typedef struct AaStream {
    struct AaExecutor *owner;
    GInputStream *input; /* Borrowed from subprocess. */
    GString *pending;
    gboolean stderr_stream;
    gboolean done;
} AaStream;

struct AaExecutor {
    GSubprocess *process;
    AaStream stdout_stream, stderr_stream;
    AaExecutorOutput output;
    AaExecutorFinished finished;
    gpointer user_data;
    gchar *stage_dir;
    gchar *stage_file;
    gboolean wait_done;
    gboolean wait_ok;
    gboolean interrupted;
    gboolean output_truncated;
    gsize output_size;
    int exit_code;
};

static void executor_free(AaExecutor *e) {
    if (e->stage_file) g_remove(e->stage_file);
    if (e->stage_dir) g_rmdir(e->stage_dir);
    g_clear_object(&e->process);
    if (e->stdout_stream.pending) g_string_free(e->stdout_stream.pending, TRUE);
    if (e->stderr_stream.pending) g_string_free(e->stderr_stream.pending, TRUE);
    g_free(e->stage_file);
    g_free(e->stage_dir);
    g_free(e);
}

static void emit_line(AaStream *s, const gchar *line, gsize length) {
    AaExecutor *e = s->owner;
    if (e->output_size >= AA_MAX_OUTPUT_BYTES) {
        if (!e->output_truncated) {
            e->output_truncated = TRUE;
            if (e->output) e->output("[Sortie limitée à 512 Kio, flux encore drainés]", TRUE, e->user_data);
        }
        return;
    }
    gsize n = MIN(length, AA_MAX_OUTPUT_BYTES - e->output_size);
    e->output_size += n;
    g_autofree gchar *utf8 = g_utf8_make_valid(line, (gssize)n);
    if (e->output) e->output(utf8, s->stderr_stream, e->user_data);
}

static void process_chunk(AaStream *s, const guint8 *buf, gsize n) {
    for (gsize i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            if (s->pending->len) emit_line(s, s->pending->str, s->pending->len);
            g_string_truncate(s->pending, 0);
        } else {
            g_string_append_c(s->pending, (gchar)buf[i]);
            if (s->pending->len >= 8192) {
                emit_line(s, s->pending->str, s->pending->len);
                g_string_truncate(s->pending, 0);
            }
        }
    }
}

static void try_finish(AaExecutor *e) {
    if (!e->wait_done || !e->stdout_stream.done || !e->stderr_stream.done) return;
    gboolean success = e->wait_ok && !e->interrupted &&
        g_subprocess_get_if_exited(e->process) && e->exit_code == 0;
    g_autofree gchar *message = success ? g_strdup("Python terminé avec succès (exit 0).") :
        (e->interrupted ? g_strdup("Exécution interrompue.") :
         (e->wait_ok && g_subprocess_get_if_signaled(e->process)) ?
         g_strdup_printf("Python terminé par signal %d.", g_subprocess_get_term_sig(e->process)) :
         g_strdup_printf("Python a échoué (exit %d).", e->exit_code));
    /* No I/O callback remains; safe for the application to free its callback data afterwards. */
    if (e->finished) e->finished(success, e->exit_code, message, e->user_data);
    executor_free(e);
}

static void stream_read(GObject *source, GAsyncResult *result, gpointer data);
static void schedule_read(AaStream *s) {
    g_input_stream_read_bytes_async(s->input, 4096, G_PRIORITY_DEFAULT, NULL, stream_read, s);
}
static void stream_read(GObject *source, GAsyncResult *result, gpointer data) {
    AaStream *s = data;
    AaExecutor *e = s->owner;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), result, &error);
    gsize size = 0;
    const guint8 *raw = bytes ? g_bytes_get_data(bytes, &size) : NULL;
    if (bytes && size) {
        process_chunk(s, raw, size);
        schedule_read(s);
        return;
    }
    if (error) emit_line(s, error->message, strlen(error->message));
    if (s->pending->len) emit_line(s, s->pending->str, s->pending->len);
    s->done = TRUE;
    try_finish(e);
}

static void wait_finished(GObject *source, GAsyncResult *result, gpointer data) {
    AaExecutor *e = data;
    g_autoptr(GError) error = NULL;
    e->wait_ok = g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error);
    if (!e->wait_ok && error) {
        AaStream *out = &e->stderr_stream;
        emit_line(out, error->message, strlen(error->message));
    }
    e->exit_code = (e->wait_ok && g_subprocess_get_if_exited(e->process)) ?
        g_subprocess_get_exit_status(e->process) : -1;
    e->wait_done = TRUE;
    try_finish(e);
}

static gboolean copy_snapshot(AaExecutor *e, const char *watch_dir,
                              const char *basename, GError **error) {
    if (!basename || !*basename || strchr(basename, '/') || strchr(basename, '\\')) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME, "Nom de fichier invalide.");
        return FALSE;
    }
    g_autofree gchar *path = g_build_filename(watch_dir, basename, NULL);
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Impossible d'ouvrir %s : %s", path, g_strerror(errno));
        return FALSE;
    }
    struct stat before, after;
    if (fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_size <= 0 || before.st_size > AA_MAX_PATCH_BYTES) {
        close(fd);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Patch non régulier, vide ou supérieur à 4 Mio.");
        return FALSE;
    }
    g_autoptr(GByteArray) contents = g_byte_array_sized_new((guint)before.st_size);
    guint8 chunk[16384];
    while (contents->len <= AA_MAX_PATCH_BYTES) {
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 || (n == 0 && contents->len != (gsize)before.st_size)) break;
        if (n == 0) break;
        g_byte_array_append(contents, chunk, (guint)n);
    }
    gboolean stable = fstat(fd, &after) == 0 &&
        contents->len == (gsize)before.st_size &&
        contents->len <= AA_MAX_PATCH_BYTES &&
        before.st_ino == after.st_ino && before.st_dev == after.st_dev &&
        before.st_size == after.st_size &&
        before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
        before.st_mtim.tv_nsec == after.st_mtim.tv_nsec;
    close(fd);
    if (!stable) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Téléchargement modifié pendant la lecture ; exécution refusée.");
        return FALSE;
    }
    e->stage_dir = g_dir_make_tmp("auto-ai-pypatch-XXXXXX", error);
    if (!e->stage_dir) return FALSE;
    e->stage_file = g_build_filename(e->stage_dir, "patch.py", NULL);
    return g_file_set_contents_full(e->stage_file, (const char *)contents->data,
                                    contents->len, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
}

AaExecutor *aa_executor_start(const char *watch_dir, const char *basename,
                              const char *project_dir, AaExecutorOutput output,
                              AaExecutorFinished finished, gpointer data, GError **error) {
    if (!project_dir || !g_file_test(project_dir, G_FILE_TEST_IS_DIR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, "Dossier projet inexistant.");
        return NULL;
    }
    AaExecutor *e = g_new0(AaExecutor, 1);
    e->output = output;
    e->finished = finished;
    e->user_data = data;
    if (!copy_snapshot(e, watch_dir, basename, error)) {
        executor_free(e);
        return NULL;
    }
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_set_cwd(launcher, project_dir);
    e->process = g_subprocess_launcher_spawn(launcher, error,
                                            "python3", "-u", e->stage_file, NULL);
    if (!e->process) { executor_free(e); return NULL; }
    /* Python scripts must not block waiting for input in unattended mode. */
    g_output_stream_close(g_subprocess_get_stdin_pipe(e->process), NULL, NULL);
    e->stdout_stream = (AaStream){.owner = e,
        .input = g_subprocess_get_stdout_pipe(e->process), .pending = g_string_new(NULL)};
    e->stderr_stream = (AaStream){.owner = e,
        .input = g_subprocess_get_stderr_pipe(e->process), .pending = g_string_new(NULL), .stderr_stream = TRUE};
    schedule_read(&e->stdout_stream);
    schedule_read(&e->stderr_stream);
    g_subprocess_wait_async(e->process, NULL, wait_finished, e);
    return e;
}

void aa_executor_cancel(AaExecutor *e) {
    if (!e || e->interrupted) return;
    e->interrupted = TRUE;
    g_subprocess_force_exit(e->process);
}
