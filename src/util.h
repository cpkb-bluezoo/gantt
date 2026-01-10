/*
 * util.h
 * Utility functions and data structures - replaces glib dependency
 *
 * Copyright (C) 2005, 2026 Chris Burdess <dog@bluezoo.org>
 * 
 * This file is part of gantt.
 * 
 * gantt is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * gantt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <assert.h>
#include <fnmatch.h>

/* ========================================================================
 * Platform-specific constants
 * ======================================================================== */

#ifdef _WIN32
    #define DIR_SEPARATOR '\\'
    #define DIR_SEPARATOR_S "\\"
    #define PATH_SEPARATOR ';'
    #define PATH_SEPARATOR_S ";"
#else
    #define DIR_SEPARATOR '/'
    #define DIR_SEPARATOR_S "/"
    #define PATH_SEPARATOR ':'
    #define PATH_SEPARATOR_S ":"
#endif

/* ========================================================================
 * Singly-linked list (replaces GSList)
 * ======================================================================== */

typedef struct slist {
    void *data;
    struct slist *next;
} slist_t;

/* Create a new list node with the given data */
slist_t *slist_new(void *data);

/* Append data to the end of the list, returns new tail node */
slist_t *slist_append(slist_t *list, void *data);

/* Prepend data to the beginning of the list, returns new head */
slist_t *slist_prepend(slist_t *list, void *data);

/* Get the last node in the list */
slist_t *slist_last(slist_t *list);

/* Get the next node (convenience macro) */
#define slist_next(list) ((list) ? (list)->next : NULL)

/* Get the number of elements in the list */
size_t slist_length(slist_t *list);

/* Free all nodes in the list (does not free data) */
void slist_free(slist_t *list);

/* Free all nodes and their data */
void slist_free_full(slist_t *list, void (*free_func)(void*));

/* ========================================================================
 * Doubly-linked list (replaces GList)
 * ======================================================================== */

typedef struct dlist {
    void *data;
    struct dlist *prev;
    struct dlist *next;
} dlist_t;

/* Create a new list node with the given data */
dlist_t *dlist_new(void *data);

/* Append data to the end of the list, returns new tail node */
dlist_t *dlist_append(dlist_t *list, void *data);

/* Prepend data to the beginning of the list, returns new head */
dlist_t *dlist_prepend(dlist_t *list, void *data);

/* Get the first node in the list */
dlist_t *dlist_first(dlist_t *list);

/* Get the last node in the list */
dlist_t *dlist_last(dlist_t *list);

/* Navigation macros */
#define dlist_next(list) ((list) ? (list)->next : NULL)
#define dlist_prev(list) ((list) ? (list)->prev : NULL)

/* Get the number of elements in the list */
size_t dlist_length(dlist_t *list);

/* Free all nodes in the list (does not free data) */
void dlist_free(dlist_t *list);

/* Free all nodes and their data */
void dlist_free_full(dlist_t *list, void (*free_func)(void*));

/* ========================================================================
 * Hash table (replaces GHashTable)
 * ======================================================================== */

#define HASHTABLE_INITIAL_SIZE 64
#define HASHTABLE_LOAD_FACTOR 0.75

typedef struct hashtable_entry {
    char *key;
    void *value;
    struct hashtable_entry *next;
} hashtable_entry_t;

typedef struct hashtable {
    hashtable_entry_t **buckets;
    size_t size;        /* Number of buckets */
    size_t count;       /* Number of entries */
} hashtable_t;

/* Create a new hash table */
hashtable_t *hashtable_new(void);

/* Destroy a hash table (does not free values) */
void hashtable_free(hashtable_t *ht);

/* Destroy a hash table and free all values */
void hashtable_free_full(hashtable_t *ht, void (*free_func)(void*));

/* Insert or update a key-value pair (key is copied) */
void hashtable_insert(hashtable_t *ht, const char *key, void *value);

/* Look up a value by key, returns NULL if not found */
void *hashtable_lookup(hashtable_t *ht, const char *key);

/* Remove a key-value pair, returns the value (or NULL) */
void *hashtable_remove(hashtable_t *ht, const char *key);

/* Check if a key exists */
bool hashtable_contains(hashtable_t *ht, const char *key);

/* Iterate over all entries */
typedef void (*hashtable_foreach_fn)(const char *key, void *value, void *user_data);
void hashtable_foreach(hashtable_t *ht, hashtable_foreach_fn fn, void *user_data);

/* ========================================================================
 * Dynamic string (replaces GString)
 * ======================================================================== */

typedef struct string {
    char *str;      /* The string data (always null-terminated) */
    size_t len;     /* Current length (excluding null terminator) */
    size_t alloc;   /* Allocated size */
} string_t;

/* Create a new string, optionally initialized with content */
string_t *string_new(const char *init);

/* Create a new string with initial content of given length */
string_t *string_new_len(const char *init, size_t len);

/* Free a string, optionally freeing the char* data too */
char *string_free(string_t *str, bool free_segment);

/* Append a C string */
string_t *string_append(string_t *str, const char *val);

/* Append a portion of a C string */
string_t *string_append_len(string_t *str, const char *val, size_t len);

/* Append a single character */
string_t *string_append_c(string_t *str, char c);

/* Append formatted text */
string_t *string_append_printf(string_t *str, const char *format, ...);

/* Truncate to zero length */
string_t *string_truncate(string_t *str);

/* ========================================================================
 * String utilities
 * ======================================================================== */

/* Split a string by delimiter, returns NULL-terminated array */
/* max_tokens <= 0 means no limit */
char **str_split(const char *str, const char *delim, int max_tokens);

/* Free a NULL-terminated string array */
void str_freev(char **str_array);

/* Concatenate multiple strings (NULL-terminated argument list) */
char *str_concat(const char *first, ...);

/* Strip leading and trailing whitespace (modifies in place) */
char *str_strip(char *str);

/* Replace characters in delimiters with new_char (modifies in place) */
char *str_delimit(char *str, const char *delimiters, char new_char);

/* Check if string has the given suffix */
bool str_has_suffix(const char *str, const char *suffix);

/* Check if string has the given prefix */
bool str_has_prefix(const char *str, const char *prefix);

/* Duplicate a string (wrapper for strdup with NULL check) */
char *str_dup(const char *str);

/* Duplicate up to n characters */
char *str_ndup(const char *str, size_t n);

/* Convert string to lowercase (returns new string) */
char *str_down(const char *str);

/* Convert string to uppercase (returns new string) */
char *str_up(const char *str);

/* ========================================================================
 * File utilities
 * ======================================================================== */

/* Test if a file exists */
bool file_exists(const char *path);

/* Test if a path is a directory */
bool file_is_directory(const char *path);

/* Test if a file is executable */
bool file_is_executable(const char *path);

/* Test if a path is a regular file */
bool file_is_regular(const char *path);

/* Get the current working directory (caller must free) */
char *get_current_dir(void);

/* Read entire file contents (caller must free) */
char *file_get_contents(const char *filename, size_t *length);

/* ========================================================================
 * Process spawning (replaces g_spawn_*)
 * ======================================================================== */

typedef struct {
    int exit_status;     /* Exit status of child, or -1 if signaled */
    char *stdout_data;   /* Captured stdout (caller must free) */
    char *stderr_data;   /* Captured stderr (caller must free) */
} spawn_result_t;

/* Synchronous process spawn with output capture */
bool spawn_sync(const char *working_dir,
                char **argv,
                char **envp,
                spawn_result_t *result);

/**
 * Extended synchronous spawn with stdin support.
 * 
 * @param working_dir  Working directory for child (NULL for inherit)
 * @param argv         Command and arguments (NULL-terminated)
 * @param envp         Environment variables (NULL to inherit)
 * @param stdin_data   Data to send to child's stdin (NULL for none)
 * @param stdin_len    Length of stdin_data (0 for strlen)
 * @param result       Output: exit status and captured stdout/stderr
 * @return true on success, false if fork/exec failed
 */
bool spawn_sync_with_input(const char *working_dir,
                           char **argv,
                           char **envp,
                           const char *stdin_data,
                           size_t stdin_len,
                           spawn_result_t *result);

/* Asynchronous process spawn, returns child PID or -1 on error */
/* Pass -1 for any fd to inherit from parent */
pid_t spawn_async(const char *working_dir,
                  char **argv,
                  char **envp,
                  int stdin_fd,
                  int stdout_fd,
                  int stderr_fd);

/* Free spawn result data */
void spawn_result_free(spawn_result_t *result);

/* ========================================================================
 * JAR/ZIP archive utilities
 * ======================================================================== */

/**
 * Check if a JAR/ZIP archive contains a specific entry.
 * Uses 'unzip' command which is available on most Unix systems.
 * 
 * @param archive_path Path to the JAR/ZIP file
 * @param entry_path Path within the archive (e.g., "com/example/Foo.class")
 * @return true if entry exists, false otherwise
 */
bool archive_contains_entry(const char *archive_path, const char *entry_path);

/**
 * Extract content of a file from a JAR/ZIP archive.
 * Uses 'unzip -p' to extract to memory without creating temp files.
 * 
 * @param archive_path Path to the JAR/ZIP file
 * @param entry_path Path within the archive
 * @param contents Output: pointer to extracted content (caller must free)
 * @param length Output: length of extracted content
 * @return true on success, false if entry not found or extraction failed
 */
bool archive_extract_entry(const char *archive_path, const char *entry_path,
                           char **contents, size_t *length);

/* ========================================================================
 * HTTP/URL utilities
 * ======================================================================== */

/**
 * Check which HTTP client (curl or wget) is available.
 * Warns once if neither is found.
 * 
 * @return 1 for curl, 2 for wget, -1 if none available
 */
int check_http_client_available(void);

/**
 * Fetch content from a URL.
 * Tries 'curl' first, then 'wget'. Warns once if neither is available.
 * 
 * @param url The URL to fetch
 * @param contents Output: pointer to fetched content (caller must free)
 * @param length Output: length of fetched content (can be NULL)
 * @return true on success, false on failure or if no HTTP client available
 */
bool url_fetch(const char *url, char **contents, size_t *length);

/* ========================================================================
 * OS detection utilities
 * ======================================================================== */

/**
 * Get the operating system name (e.g., "Linux", "Darwin", "FreeBSD").
 * Uses uname -s. Result is cached after first call.
 * 
 * @return OS name string (do not free), or "unknown" if detection fails
 */
const char *get_os_name(void);

/**
 * Get the OS family (e.g., "unix", "mac", "windows").
 * 
 * Mapping:
 *   - Darwin -> "mac" (also matches "unix")
 *   - Linux, *BSD, SunOS, AIX, HP-UX -> "unix"
 *   - CYGWIN*, MINGW*, MSYS* -> "windows"
 * 
 * @return OS family string (do not free)
 */
const char *get_os_family(void);

/**
 * Check if the current OS matches the given OS name or pattern.
 * Comparison is case-insensitive and supports comma-separated values.
 * 
 * @param os_filter Comma-separated list of OS names (e.g., "Linux,Darwin")
 * @return true if current OS matches any in the list
 */
bool os_matches(const char *os_filter);

/**
 * Check if the current OS family matches the given family.
 * Comparison is case-insensitive.
 * 
 * @param family_filter OS family (e.g., "unix", "mac", "windows")
 * @return true if current OS family matches
 */
bool os_family_matches(const char *family_filter);

/* ========================================================================
 * Time utilities
 * ======================================================================== */

/* Get current time in seconds since epoch */
long get_current_time_sec(void);

/* Get current time with microsecond precision */
void get_current_time(long *sec, long *usec);

/* ========================================================================
 * Miscellaneous
 * ======================================================================== */

/* Compute a hash value for a string */
unsigned int str_hash(const char *str);

/* Parse boolean value from string ("true", "yes", "on" -> true) */
bool parse_boolean(const char *str, bool default_value);

#endif /* UTIL_H */

