#pragma once
#include <gio/gio.h>
/* Strict, unambiguous metadata in the opening Python comments.
 * Caller owns the returned message. Project marker must equal the target basename.
 */
char *aa_patch_meta_read(const char *snapshot_path, const char *project_dir,
                         GError **error);
