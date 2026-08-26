/*
 * fileops.c
 * Unified file operations for gantt (delete, move, touch, chmod, mkdir, copy)
 * 
 * Compile with -DTASK_NAME to select operation:
 *   -DTASK_DELETE  -> gantt_delete
 *   -DTASK_MOVE    -> gantt_move
 *   -DTASK_TOUCH   -> gantt_touch
 *   -DTASK_CHMOD   -> gantt_chmod
 *   -DTASK_MKDIR   -> gantt_mkdir
 *   -DTASK_COPY    -> gantt_copy
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <utime.h>
#include <time.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define BUFFER_SIZE 65536

/* Global options from environment */
static int verbose = 0;
static int quiet = 0;
static int failonerror = 1;

/* ========================================================================
 * Utility functions
 * ======================================================================== */

static const char *getenv_default(const char *name, const char *def)
{
    const char *val = getenv(name);
    return val ? val : def;
}

static int env_is_true(const char *name)
{
    const char *val = getenv(name);
    return val && strcmp(val, "true") == 0;
}

/* Create directory and all parents */
__attribute__((unused))
static int mkdirp(const char *path, mode_t mode)
{
    char *tmp = strdup(path);
    char *p = tmp;
    int ret = 0;
    
    if (!tmp) {
        return -1;
    }
    if (tmp[0] == '/') {
        p++;
    }
    
    while (*p) {
        while (*p && *p != '/') {
            p++;
        }
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                ret = -1;
                break;
            }
            *p = '/';
            p++;
        }
    }
    
    if (ret == 0 && mkdir(tmp, mode) != 0 && errno != EEXIST) {
        ret = -1;
    }
    
    free(tmp);
    return ret;
}

/* Get directory portion of path */
__attribute__((unused))
static char *get_dirname(const char *path)
{
    char *copy = strdup(path);
    char *last_slash = strrchr(copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        return copy;
    }
    free(copy);
    return strdup(".");
}

/*
 * Recursively deletes the CONTENTS of an already-open directory (dfd),
 * operating entirely via *at() functions relative to dfd or a freshly
 * opened child fd - never re-resolving a path string from the filesystem
 * root once inside this function. This is what closes the "an ancestor
 * directory got swapped mid-walk" class of time-of-check/time-of-use race
 * (each entry is checked with fstatat() and then acted on with
 * unlinkat()/openat() using the exact same dfd+name pair, rather than a
 * path string that gets independently re-resolved for the check and for
 * the removal). A residual race on the leaf entry itself (something else
 * replaces exactly that name in the same instant) is inherent to
 * name-based removal on POSIX and isn't something any API closes - this
 * addresses the class of race that *is* fixable, matching the standard
 * "prefer fd-relative operations to path-based ones" mitigation.
 * Does not remove the directory dfd itself - the caller does that once
 * this returns, since dfd carries no name of its own for messages/rmdir().
 * Takes ownership of dfd (always closes it, via closedir()).
 */
__attribute__((unused))
static int rmdir_recursive_contents(int dfd, const char *display_path)
{
    DIR *dir;
    struct dirent *entry;
    int ret = 0;

    dir = fdopendir(dfd);
    if (!dir) {
        close(dfd);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        char child_display[PATH_MAX];

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (fstatat(dfd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            ret = -1;
            continue;
        }

        snprintf(child_display, PATH_MAX, "%s/%s", display_path, entry->d_name);

        if (S_ISDIR(st.st_mode)) {
            int child_dfd = openat(dfd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (child_dfd < 0) {
                ret = -1;
                continue;
            }
            if (rmdir_recursive_contents(child_dfd, child_display) != 0) {
                ret = -1;
            }
            if (unlinkat(dfd, entry->d_name, AT_REMOVEDIR) != 0) {
                if (!quiet)
                    fprintf(stderr, "delete: cannot remove directory '%s': %s\n",
                            child_display, strerror(errno));
                ret = -1;
            } else if (verbose) {
                printf("Deleted directory: %s\n", child_display);
            }
        } else {
            if (unlinkat(dfd, entry->d_name, 0) != 0) {
                if (!quiet)
                    fprintf(stderr, "delete: cannot remove '%s': %s\n",
                            child_display, strerror(errno));
                ret = -1;
            } else if (verbose) {
                printf("Deleted: %s\n", child_display);
            }
        }
    }

    closedir(dir);  /* also closes dfd */
    return ret;
}

/* Recursive directory deletion */
__attribute__((unused))
static int rmdir_recursive(const char *path)
{
    int dfd;
    int ret;

    dfd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dfd < 0) {
        if (errno == ENOENT) {
            return 0;  /* Already gone */
        }
        return -1;
    }

    ret = rmdir_recursive_contents(dfd, path);

    if (rmdir(path) != 0) {
        if (!quiet)
            fprintf(stderr, "delete: cannot remove directory '%s': %s\n",
                    path, strerror(errno));
        ret = -1;
    } else if (verbose) {
        printf("Deleted directory: %s\n", path);
    }

    return ret;
}

/* ========================================================================
 * DELETE implementation
 * ======================================================================== */

#ifdef TASK_DELETE

static int do_delete_file(const char *path)
{
    struct stat st;
    
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) {
            if (verbose && !quiet)
                fprintf(stderr, "delete: not found (skipping): %s\n", path);
            return 0;  /* Not an error - Ant behavior */
        }
        if (!quiet)
            fprintf(stderr, "delete: cannot stat '%s': %s\n", path, strerror(errno));
        return failonerror ? -1 : 0;
    }
    
    if (S_ISDIR(st.st_mode)) {
        return rmdir_recursive(path);
    } else {
        if (unlink(path) != 0) {
            if (!quiet)
                fprintf(stderr, "delete: cannot remove '%s': %s\n", path, strerror(errno));
            return failonerror ? -1 : 0;
        }
        if (verbose)
            printf("Deleted: %s\n", path);
    }
    
    return 0;
}

static int delete_main(void)
{
    const char *file = getenv("file");
    const char *dir = getenv("dir");
    const char *filelist = getenv("filelist");
    int errors = 0;
    
    /* Directory mode */
    if (dir) {
        struct stat st;
        if (stat(dir, &st) != 0) {
            if (verbose && !quiet)
                fprintf(stderr, "delete: directory not found (skipping): %s\n", dir);
            return 0;  /* Ant behavior: silently succeed */
        }
        if (!quiet && !verbose)
            printf("Deleting directory %s\n", dir);
        return rmdir_recursive(dir);
    }
    
    /* Single file mode */
    if (file) {
        return do_delete_file(file);
    }
    
    /* File list mode */
    if (filelist) {
        const char *p = filelist;
        const char *line_start = filelist;
        char path[PATH_MAX];
        
        while (*p) {
            if (*p == '\n' || *(p + 1) == '\0') {
                size_t line_len = (*p == '\n') ? (p - line_start) : (p - line_start + 1);
                if (line_len > 0 && line_len < PATH_MAX) {
                    strncpy(path, line_start, line_len);
                    path[line_len] = '\0';
                    if (path[0] && do_delete_file(path) != 0)
                        errors++;
                }
                line_start = p + 1;
            }
            p++;
        }
        return errors > 0 ? 1 : 0;
    }
    
    fprintf(stderr, "delete: must specify 'file', 'dir', or provide fileset\n");
    return 1;
}

#endif /* TASK_DELETE */

/* ========================================================================
 * MOVE implementation
 * ======================================================================== */

#ifdef TASK_MOVE

static int overwrite = 1;
static int flatten = 0;

static int do_move_file(const char *src, const char *dest)
{
    struct stat st;
    char *dest_dir;
    
    if (stat(src, &st) != 0) {
        if (!quiet)
            fprintf(stderr, "move: source not found: %s\n", src);
        return failonerror ? -1 : 0;
    }
    
    /* Check if dest exists */
    if (!overwrite && stat(dest, &st) == 0) {
        if (verbose)
            fprintf(stderr, "move: skipping (exists): %s\n", dest);
        return 0;
    }
    
    /* Create destination directory */
    dest_dir = get_dirname(dest);
    if (dest_dir && *dest_dir && mkdirp(dest_dir, 0755) != 0) {
        fprintf(stderr, "move: cannot create directory '%s': %s\n", 
                dest_dir, strerror(errno));
        free(dest_dir);
        return -1;
    }
    free(dest_dir);
    
    /* Rename (move) */
    if (rename(src, dest) != 0) {
        fprintf(stderr, "move: cannot move '%s' to '%s': %s\n", 
                src, dest, strerror(errno));
        return -1;
    }
    
    if (verbose)
        printf("%s -> %s\n", src, dest);
    
    return 0;
}

static int move_main(void)
{
    const char *file = getenv("file");
    const char *tofile = getenv("tofile");
    const char *todir = getenv("todir");
    const char *filelist = getenv("filelist");
    const char *basedir = getenv("basedir");
    int errors = 0;
    
    overwrite = !env_is_true("overwrite") || strcmp(getenv_default("overwrite", "true"), "true") == 0;
    flatten = env_is_true("flatten");
    
    /* Single file mode */
    if (file) {
        if (tofile) {
            return do_move_file(file, tofile);
        } else if (todir) {
            char dest[PATH_MAX];
            const char *basename = strrchr(file, '/');
            basename = basename ? basename + 1 : file;
            snprintf(dest, PATH_MAX, "%s/%s", todir, basename);
            return do_move_file(file, dest);
        } else {
            fprintf(stderr, "move: must specify 'tofile' or 'todir'\n");
            return 1;
        }
    }
    
    /* File list mode */
    if (filelist) {
        if (!todir) {
            fprintf(stderr, "move: must specify 'todir' when using fileset\n");
            return 1;
        }
        
        const char *p = filelist;
        const char *line_start = filelist;
        char src_path[PATH_MAX], dest_path[PATH_MAX];
        size_t basedir_len = basedir ? strlen(basedir) : 0;
        
        while (*p) {
            if (*p == '\n' || *(p + 1) == '\0') {
                size_t line_len = (*p == '\n') ? (p - line_start) : (p - line_start + 1);
                if (line_len > 0 && line_len < PATH_MAX) {
                    strncpy(src_path, line_start, line_len);
                    src_path[line_len] = '\0';
                    
                    if (src_path[0]) {
                        const char *rel_path = src_path;
                        if (flatten) {
                            rel_path = strrchr(src_path, '/');
                            rel_path = rel_path ? rel_path + 1 : src_path;
                        } else if (basedir && basedir_len > 0 &&
                                   strncmp(src_path, basedir, basedir_len) == 0 &&
                                   src_path[basedir_len] == '/') {
                            rel_path = src_path + basedir_len + 1;
                        }
                        
                        snprintf(dest_path, PATH_MAX, "%s/%s", todir, rel_path);
                        if (do_move_file(src_path, dest_path) != 0)
                            errors++;
                    }
                }
                line_start = p + 1;
            }
            p++;
        }
        return errors > 0 ? 1 : 0;
    }
    
    fprintf(stderr, "move: must specify 'file' or provide fileset\n");
    return 1;
}

#endif /* TASK_MOVE */

/* ========================================================================
 * TOUCH implementation
 * ======================================================================== */

#ifdef TASK_TOUCH

static int mkdirs = 0;
static time_t touch_time = 0;

static int do_touch_file(const char *path)
{
    struct stat st;
    int fd;
    struct utimbuf times;
    
    /* Create parent directories if requested */
    if (mkdirs) {
        char *dir = get_dirname(path);
        if (dir && *dir) {
            mkdirp(dir, 0755);
        }
        free(dir);
    }
    
    /* Check if file exists */
    if (stat(path, &st) != 0) {
        /* Create empty file */
        fd = open(path, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            fprintf(stderr, "touch: cannot create '%s': %s\n", path, strerror(errno));
            return failonerror ? -1 : 0;
        }
        close(fd);
    }
    
    /* Update timestamp */
    if (touch_time != 0) {
        times.actime = touch_time;
        times.modtime = touch_time;
        if (utime(path, &times) != 0) {
            fprintf(stderr, "touch: cannot set time on '%s': %s\n", path, strerror(errno));
            return failonerror ? -1 : 0;
        }
    } else {
        if (utime(path, NULL) != 0) {
            fprintf(stderr, "touch: cannot update time on '%s': %s\n", path, strerror(errno));
            return failonerror ? -1 : 0;
        }
    }
    
    if (verbose)
        printf("Touched: %s\n", path);
    
    return 0;
}

static int touch_main(void)
{
    const char *file = getenv("file");
    const char *filelist = getenv("filelist");
    const char *millis = getenv("millis");
    const char *datetime = getenv("datetime");
    int errors = 0;
    
    mkdirs = env_is_true("mkdirs");
    
    /* Parse timestamp */
    if (millis) {
        touch_time = atoll(millis) / 1000;
    } else if (datetime) {
        /* Try to parse YYYYMMDDHHMM format */
        if (strlen(datetime) >= 12) {
            struct tm tm = {0};
            int year, month, day, hour, min;
            if (sscanf(datetime, "%4d%2d%2d%2d%2d", &year, &month, &day, &hour, &min) == 5) {
                tm.tm_year = year - 1900;
                tm.tm_mon = month - 1;
                tm.tm_mday = day;
                tm.tm_hour = hour;
                tm.tm_min = min;
                touch_time = mktime(&tm);
            }
        }
    }
    
    /* Single file mode */
    if (file) {
        return do_touch_file(file);
    }
    
    /* File list mode */
    if (filelist) {
        const char *p = filelist;
        const char *line_start = filelist;
        char path[PATH_MAX];
        
        while (*p) {
            if (*p == '\n' || *(p + 1) == '\0') {
                size_t line_len = (*p == '\n') ? (p - line_start) : (p - line_start + 1);
                if (line_len > 0 && line_len < PATH_MAX) {
                    strncpy(path, line_start, line_len);
                    path[line_len] = '\0';
                    if (path[0] && do_touch_file(path) != 0)
                        errors++;
                }
                line_start = p + 1;
            }
            p++;
        }
        return errors > 0 ? 1 : 0;
    }
    
    fprintf(stderr, "touch: must specify 'file' or provide fileset\n");
    return 1;
}

#endif /* TASK_TOUCH */

/* ========================================================================
 * CHMOD implementation
 * ======================================================================== */

#ifdef TASK_CHMOD

static mode_t parse_mode(const char *mode_str)
{
    /* Try numeric mode first */
    if (mode_str[0] >= '0' && mode_str[0] <= '7') {
        return (mode_t)strtol(mode_str, NULL, 8);
    }
    /* TODO: Handle symbolic modes like u+x, a+r, etc. */
    /* For now, just return 0 and let chmod handle it */
    return 0;
}

static int do_chmod_file(const char *path, mode_t mode)
{
    if (chmod(path, mode) != 0) {
        fprintf(stderr, "chmod: cannot change mode of '%s': %s\n", path, strerror(errno));
        return failonerror ? -1 : 0;
    }

    if (verbose)
        printf("chmod %o %s\n", mode, path);

    return 0;
}

/* fchmodat() equivalent of do_chmod_file(), for a file reached during a
 * directory walk - see chmod_recursive_contents(). Flags 0 (not
 * AT_SYMLINK_NOFOLLOW) to match do_chmod_file()'s plain chmod(), which
 * always follows symlinks - AT_SYMLINK_NOFOLLOW is also not universally
 * supported by fchmodat() (notably absent on Linux, present on
 * macOS/BSD), so using it here would be a portability hazard for no
 * behavioural gain. */
static int do_chmod_at(int dfd, const char *name, const char *display_path, mode_t mode)
{
    if (fchmodat(dfd, name, mode, 0) != 0) {
        fprintf(stderr, "chmod: cannot change mode of '%s': %s\n", display_path, strerror(errno));
        return failonerror ? -1 : 0;
    }

    if (verbose)
        printf("chmod %o %s\n", mode, display_path);

    return 0;
}

/*
 * Recursively chmods everything inside an already-open directory (dfd),
 * operating via *at() functions relative to dfd or a freshly opened child
 * fd - never re-resolving a path string from the filesystem root once
 * inside this function. Each child's type is determined by *attempting*
 * to open it as a directory rather than by a separate stat() beforehand:
 * success means it's a directory (and the already-open fd is what gets
 * chmod'd/recursed into, not a path re-resolved afterwards); ENOTDIR
 * means it's a file, chmod'd via the same dfd+name pair with
 * do_chmod_at(). This closes the "an ancestor directory got swapped
 * mid-walk" class of time-of-check/time-of-use race, the same fix
 * applied to rmdir_recursive_contents() for the same underlying CodeQL
 * finding (cpp/toctou-race-condition). Takes ownership of dfd (always
 * closes it, via closedir()).
 */
static int chmod_recursive_contents(int dfd, const char *display_path, mode_t mode,
                                     int do_files, int do_dirs)
{
    DIR *dir;
    struct dirent *entry;
    int ret = 0;

    dir = fdopendir(dfd);
    if (!dir) {
        close(dfd);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char child_display[PATH_MAX];
        int child_dfd;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(child_display, PATH_MAX, "%s/%s", display_path, entry->d_name);

        child_dfd = openat(dfd, entry->d_name, O_RDONLY | O_DIRECTORY);
        if (child_dfd >= 0) {
            if (do_dirs && fchmod(child_dfd, mode) != 0) {
                fprintf(stderr, "chmod: cannot change mode of '%s': %s\n",
                        child_display, strerror(errno));
                if (failonerror) {
                    ret = -1;
                }
            } else if (do_dirs && verbose) {
                printf("chmod %o %s\n", mode, child_display);
            }
            if (chmod_recursive_contents(child_dfd, child_display, mode,
                                          do_files, do_dirs) != 0) {
                ret = -1;
            }
        } else if (errno == ENOTDIR) {
            if (do_files && do_chmod_at(dfd, entry->d_name, child_display, mode) != 0) {
                ret = -1;
            }
        } else {
            ret = -1;
        }
    }

    closedir(dir);  /* also closes dfd */
    return ret;
}

static int chmod_recursive(const char *path, mode_t mode, const char *type_filter)
{
    int dfd;
    int ret = 0;
    int do_files = !type_filter || strcmp(type_filter, "file") == 0 || strcmp(type_filter, "both") == 0;
    int do_dirs = !type_filter || strcmp(type_filter, "dir") == 0 || strcmp(type_filter, "both") == 0;

    dfd = open(path, O_RDONLY | O_DIRECTORY);
    if (dfd < 0) {
        if (errno == ENOTDIR) {
            /* Not a directory - treat as a plain file/symlink. open()
             * itself already tells us the type; no separate stat()
             * needed (and no TOCTOU gap between a check and this). */
            return do_files ? do_chmod_file(path, mode) : 0;
        }
        return -1;
    }

    if (do_dirs && fchmod(dfd, mode) != 0) {
        fprintf(stderr, "chmod: cannot change mode of '%s': %s\n", path, strerror(errno));
        if (failonerror) {
            ret = -1;
        }
    } else if (do_dirs && verbose) {
        printf("chmod %o %s\n", mode, path);
    }

    if (chmod_recursive_contents(dfd, path, mode, do_files, do_dirs) != 0) {
        ret = -1;
    }

    return ret;
}

static int chmod_main(void)
{
    const char *perm = getenv("perm");
    const char *file = getenv("file");
    const char *dir = getenv("dir");
    const char *filelist = getenv("filelist");
    const char *type_filter = getenv("type");
    mode_t mode;
    int errors = 0;
    
    if (!perm) {
        fprintf(stderr, "chmod: missing 'perm' attribute\n");
        return 1;
    }
    
    mode = parse_mode(perm);
    if (mode == 0) {
        /* Fall back to system chmod for symbolic modes */
        /* For now, report error */
        fprintf(stderr, "chmod: invalid mode '%s' (symbolic modes not yet supported)\n", perm);
        return 1;
    }
    
    /* Single file mode */
    if (file) {
        return do_chmod_file(file, mode);
    }
    
    /* Directory mode (recursive) */
    if (dir) {
        return chmod_recursive(dir, mode, type_filter);
    }
    
    /* File list mode */
    if (filelist) {
        const char *p = filelist;
        const char *line_start = filelist;
        char path[PATH_MAX];
        
        while (*p) {
            if (*p == '\n' || *(p + 1) == '\0') {
                size_t line_len = (*p == '\n') ? (p - line_start) : (p - line_start + 1);
                if (line_len > 0 && line_len < PATH_MAX) {
                    strncpy(path, line_start, line_len);
                    path[line_len] = '\0';
                    if (path[0] && do_chmod_file(path, mode) != 0)
                        errors++;
                }
                line_start = p + 1;
            }
            p++;
        }
        return errors > 0 ? 1 : 0;
    }
    
    fprintf(stderr, "chmod: must specify 'file', 'dir', or provide fileset\n");
    return 1;
}

#endif /* TASK_CHMOD */

/* ========================================================================
 * MKDIR implementation
 * ======================================================================== */

#ifdef TASK_MKDIR

static int mkdir_main(void)
{
    const char *dir = getenv("dir");
    struct stat st;
    
    if (!dir) {
        fprintf(stderr, "mkdir: missing 'dir' attribute\n");
        return 1;
    }
    
    /* Already exists */
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (verbose)
            printf("mkdir: directory exists: %s\n", dir);
        return 0;
    }
    
    if (mkdirp(dir, 0755) != 0) {
        fprintf(stderr, "mkdir: cannot create '%s': %s\n", dir, strerror(errno));
        return 1;
    }
    
    if (verbose)
        printf("Created directory: %s\n", dir);
    
    return 0;
}

#endif /* TASK_MKDIR */

/* ========================================================================
 * COPY implementation (imported from copy_task.c)
 * ======================================================================== */

#ifdef TASK_COPY

static int preserve_mtime = 0;
static int copy_overwrite = 1;

static int needs_copy(const char *src, const char *dest)
{
    struct stat src_stat, dest_stat;
    
    if (stat(src, &src_stat) != 0) {
        return 0;
    }
    
    if (stat(dest, &dest_stat) != 0) {
        return 1;
    }
    
    if (!copy_overwrite) {
        return 0;
    }
    
    return src_stat.st_mtime > dest_stat.st_mtime;
}

static int copy_file(const char *src, const char *dest)
{
    int src_fd, dest_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    struct stat src_stat;
    struct utimbuf times;
    char *dest_dir;
    
    if (!needs_copy(src, dest)) {
        if (verbose)
            fprintf(stderr, "skip: %s (up-to-date)\n", src);
        return 0;
    }
    
    if (stat(src, &src_stat) != 0) {
        fprintf(stderr, "copy: cannot stat '%s': %s\n", src, strerror(errno));
        return -1;
    }
    
    dest_dir = get_dirname(dest);
    if (dest_dir && *dest_dir && mkdirp(dest_dir, 0755) != 0) {
        fprintf(stderr, "copy: cannot create directory '%s': %s\n", 
                dest_dir, strerror(errno));
        free(dest_dir);
        return -1;
    }
    free(dest_dir);
    
    src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        fprintf(stderr, "copy: cannot open '%s': %s\n", src, strerror(errno));
        return -1;
    }
    
    dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, src_stat.st_mode);
    if (dest_fd < 0) {
        fprintf(stderr, "copy: cannot create '%s': %s\n", dest, strerror(errno));
        close(src_fd);
        return -1;
    }
    
    while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0) {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            fprintf(stderr, "copy: write error to '%s': %s\n", dest, strerror(errno));
            close(src_fd);
            close(dest_fd);
            return -1;
        }
    }
    
    close(src_fd);
    close(dest_fd);
    
    if (preserve_mtime) {
        times.actime = src_stat.st_atime;
        times.modtime = src_stat.st_mtime;
        utime(dest, &times);
    }
    
    if (verbose)
        printf("%s -> %s\n", src, dest);
    
    return 0;
}

static int copy_main(void)
{
    const char *file = getenv("file");
    const char *tofile = getenv("tofile");
    const char *todir = getenv("todir");
    const char *filelist = getenv("filelist");
    const char *basedir = getenv("basedir");
    int errors = 0;
    
    preserve_mtime = env_is_true("preservelastmodified");
    copy_overwrite = strcmp(getenv_default("overwrite", "true"), "false") != 0;
    
    /* Single file mode */
    if (file) {
        if (tofile) {
            return copy_file(file, tofile);
        } else if (todir) {
            char dest[PATH_MAX];
            const char *basename = strrchr(file, '/');
            basename = basename ? basename + 1 : file;
            snprintf(dest, PATH_MAX, "%s/%s", todir, basename);
            return copy_file(file, dest);
        } else {
            fprintf(stderr, "copy: must specify 'tofile' or 'todir'\n");
            return 1;
        }
    }
    
    /* File list mode */
    if (filelist) {
        if (!todir) {
            fprintf(stderr, "copy: must specify 'todir' when using fileset\n");
            return 1;
        }
        
        const char *p = filelist;
        const char *line_start = filelist;
        char src_path[PATH_MAX], dest_path[PATH_MAX];
        size_t basedir_len = basedir ? strlen(basedir) : 0;
        
        while (*p) {
            if (*p == '\n' || *(p + 1) == '\0') {
                size_t line_len = (*p == '\n') ? (p - line_start) : (p - line_start + 1);
                if (line_len > 0 && line_len < PATH_MAX) {
                    strncpy(src_path, line_start, line_len);
                    src_path[line_len] = '\0';
                    
                    if (src_path[0]) {
                        const char *rel_path = src_path;
                        if (basedir && basedir_len > 0 &&
                            strncmp(src_path, basedir, basedir_len) == 0 &&
                            src_path[basedir_len] == '/') {
                            rel_path = src_path + basedir_len + 1;
                        }
                        
                        snprintf(dest_path, PATH_MAX, "%s/%s", todir, rel_path);
                        if (copy_file(src_path, dest_path) != 0)
                            errors++;
                    }
                }
                line_start = p + 1;
            }
            p++;
        }
        return errors > 0 ? 1 : 0;
    }
    
    fprintf(stderr, "copy: must specify 'file' or provide fileset\n");
    return 1;
}

#endif /* TASK_COPY */

/* ========================================================================
 * CONCAT implementation
 * ======================================================================== */

#ifdef TASK_CONCAT

static int fixlastline = 0;
static const char *eol_mode = NULL;

static int concat_file(const char *path, int out_fd)
{
    int in_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    char last_char = '\n';
    
    in_fd = open(path, O_RDONLY);
    if (in_fd < 0) {
        fprintf(stderr, "concat: cannot open '%s': %s\n", path, strerror(errno));
        return failonerror ? -1 : 0;
    }
    
    while ((bytes_read = read(in_fd, buffer, BUFFER_SIZE)) > 0) {
        if (write(out_fd, buffer, bytes_read) != bytes_read) {
            fprintf(stderr, "concat: write error: %s\n", strerror(errno));
            close(in_fd);
            return -1;
        }
        last_char = buffer[bytes_read - 1];
    }
    
    close(in_fd);
    
    /* Ensure file ends with newline if fixlastline is set */
    if (fixlastline && last_char != '\n') {
        if (write(out_fd, "\n", 1) != 1) {
            fprintf(stderr, "concat: write error: %s\n", strerror(errno));
            return -1;
        }
    }
    
    return 0;
}

static int convert_eol(const char *destfile, const char *mode)
{
    int fd;
    struct stat st;
    char *content, *p, *out;
    ssize_t len, out_len;
    int i;
    
    if (stat(destfile, &st) != 0) {
        return -1;
    }
    
    content = malloc(st.st_size + 1);
    if (!content) {
        return -1;
    }
    
    fd = open(destfile, O_RDONLY);
    if (fd < 0) {
        free(content);
        return -1;
    }
    
    len = read(fd, content, st.st_size);
    close(fd);
    
    if (len < 0) {
        free(content);
        return -1;
    }
    content[len] = '\0';
    
    /* Allocate output buffer (worst case: every char becomes \r\n) */
    out = malloc(len * 2 + 1);
    if (!out) {
        free(content);
        return -1;
    }
    
    /* First, normalize all line endings to \n */
    out_len = 0;
    for (i = 0; i < len; i++) {
        if (content[i] == '\r') {
            if (i + 1 < len && content[i + 1] == '\n') {
                i++;  /* Skip \r in \r\n */
            }
            out[out_len++] = '\n';
        } else {
            out[out_len++] = content[i];
        }
    }
    out[out_len] = '\0';
    free(content);
    
    /* Now convert to desired EOL */
    fd = open(destfile, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        free(out);
        return -1;
    }
    
    if (strcmp(mode, "lf") == 0) {
        /* Already in LF format */
        write(fd, out, out_len);
    } else if (strcmp(mode, "crlf") == 0) {
        /* Convert LF to CRLF */
        for (p = out; *p; p++) {
            if (*p == '\n') {
                write(fd, "\r\n", 2);
            } else {
                write(fd, p, 1);
            }
        }
    } else if (strcmp(mode, "cr") == 0) {
        /* Convert LF to CR */
        for (p = out; *p; p++) {
            if (*p == '\n') {
                write(fd, "\r", 1);
            } else {
                write(fd, p, 1);
            }
        }
    }
    
    close(fd);
    free(out);
    return 0;
}

static int concat_main(void)
{
    const char *destfile = getenv("destfile");
    const char *file = getenv("file");
    const char *filelist = getenv("filelist");
    const char *header = getenv("header");
    const char *footer = getenv("footer");
    int out_fd;
    int append;
    int errors = 0;
    
    fixlastline = env_is_true("fixlastline");
    eol_mode = getenv("eol");
    append = env_is_true("append");
    
    /* Open output */
    if (destfile) {
        char *dir = get_dirname(destfile);
        if (dir && *dir) {
            mkdirp(dir, 0755);
        }
        free(dir);
        
        out_fd = open(destfile, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
        if (out_fd < 0) {
            fprintf(stderr, "concat: cannot create '%s': %s\n", destfile, strerror(errno));
            return 1;
        }
    } else {
        out_fd = STDOUT_FILENO;
    }
    
    /* Write header */
    if (header && *header) {
        write(out_fd, header, strlen(header));
        write(out_fd, "\n", 1);
    }
    
    /* Single file mode */
    if (file) {
        if (concat_file(file, out_fd) != 0)
            errors++;
    }
    
    /* File list mode */
    if (filelist) {
        const char *p = filelist;
        const char *line_start = filelist;
        char path[PATH_MAX];
        
        while (*p) {
            if (*p == '\n' || *(p + 1) == '\0') {
                size_t line_len = (*p == '\n') ? (p - line_start) : (p - line_start + 1);
                if (line_len > 0 && line_len < PATH_MAX) {
                    strncpy(path, line_start, line_len);
                    path[line_len] = '\0';
                    if (path[0] && concat_file(path, out_fd) != 0)
                        errors++;
                }
                line_start = p + 1;
            }
            p++;
        }
    }
    
    /* Write footer */
    if (footer && *footer) {
        write(out_fd, footer, strlen(footer));
        write(out_fd, "\n", 1);
    }
    
    /* Close output */
    if (destfile) {
        close(out_fd);
        
        /* EOL conversion */
        if (eol_mode && *eol_mode) {
            convert_eol(destfile, eol_mode);
        }
    }
    
    return errors > 0 ? 1 : 0;
}

#endif /* TASK_CONCAT */

/* ========================================================================
 * Main entry point
 * ======================================================================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    /* Parse common options */
    verbose = env_is_true("verbose");
    quiet = env_is_true("quiet");
    failonerror = strcmp(getenv_default("failonerror", "true"), "false") != 0;
    
#ifdef TASK_DELETE
    return delete_main();
#elif defined(TASK_MOVE)
    return move_main();
#elif defined(TASK_TOUCH)
    return touch_main();
#elif defined(TASK_CHMOD)
    return chmod_main();
#elif defined(TASK_MKDIR)
    return mkdir_main();
#elif defined(TASK_COPY)
    return copy_main();
#elif defined(TASK_CONCAT)
    return concat_main();
#else
    fprintf(stderr, "fileops: no task defined at compile time\n");
    return 1;
#endif
}

