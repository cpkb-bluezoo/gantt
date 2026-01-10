/*
 * native2ascii.c
 * Copyright (C) 2026 Chris Burdess <dog@bluezoo.org>
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

#include "gantt.h"
#include <limits.h>

/**
 * Convert a UTF-8 byte sequence to a Unicode code point.
 * Returns the number of bytes consumed, or 0 on error.
 */
static int utf8_to_codepoint(const unsigned char *s, uint32_t *codepoint)
{
    if (s[0] < 0x80) {
        *codepoint = s[0];
        return 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80) {
            return 0;
        }
        *codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
            return 0;
        }
        *codepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) {
            return 0;
        }
        *codepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    return 0;
}

/**
 * Convert a single file from native encoding (UTF-8) to ASCII with Unicode escapes.
 */
static bool convert_file(const char *src_path, const char *dest_path)
{
    FILE *src = fopen(src_path, "rb");
    if (!src) {
        return false;
    }
    
    /* Create destination directory if needed */
    char *dest_dir = strdup(dest_path);
    char *last_slash = strrchr(dest_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        /* Create directory recursively */
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dest_dir);
        system(cmd);
    }
    free(dest_dir);
    
    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        fclose(src);
        return false;
    }
    
    unsigned char buffer[4096];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        size_t i = 0;
        while (i < bytes_read) {
            uint32_t codepoint;
            int len = utf8_to_codepoint(&buffer[i], &codepoint);
            
            if (len == 0) {
                /* Invalid UTF-8, just copy the byte */
                fputc(buffer[i], dest);
                i++;
            } else if (codepoint < 0x80) {
                /* ASCII character, copy as-is */
                fputc(codepoint, dest);
                i += len;
            } else if (codepoint <= 0xFFFF) {
                /* BMP character, use \\uXXXX escape */
                fprintf(dest, "\\u%04X", codepoint);
                i += len;
            } else {
                /* Supplementary character, use surrogate pair */
                uint32_t high = 0xD800 + ((codepoint - 0x10000) >> 10);
                uint32_t low = 0xDC00 + ((codepoint - 0x10000) & 0x3FF);
                fprintf(dest, "\\u%04X\\u%04X", high, low);
                i += len;
            }
        }
    }
    
    fclose(src);
    fclose(dest);
    return true;
}

/**
 * Invoke the native2ascii task.
 * 
 * <native2ascii src="srcdir" dest="destdir" includes="pattern" encoding="UTF-8"/>
 */
bool native2ascii_invoke(task_t *task, project_t *project)
{
    const char *src = hashtable_lookup(task->attribute_dict, "src");
    const char *dest = hashtable_lookup(task->attribute_dict, "dest");
    const char *includes = hashtable_lookup(task->attribute_dict, "includes");
    const char *excludes = hashtable_lookup(task->attribute_dict, "excludes");
    const char *encoding = hashtable_lookup(task->attribute_dict, "encoding");
    
    if (!src) {
        task_log(task, LOG_ERROR, "native2ascii: src attribute is required");
        return false;
    }
    
    if (!dest) {
        task_log(task, LOG_ERROR, "native2ascii: dest attribute is required");
        return false;
    }
    
    /* Currently only support UTF-8 encoding */
    if (encoding && strcasecmp(encoding, "UTF-8") != 0) {
        task_log(task, LOG_WARNING, "native2ascii: only UTF-8 encoding is supported");
    }
    
    /* Resolve paths */
    char *src_dir = resolve_variables(strdup(src), project);
    src_dir = expand_location(project, src_dir);
    
    char *dest_dir = resolve_variables(strdup(dest), project);
    dest_dir = expand_location(project, dest_dir);
    
    /* Create a fileset to find matching files */
    fileset_t *fileset = fileset_alloc();
    fileset->dir = src_dir;
    fileset->default_excludes = true;
    fileset->case_sensitive = true;
    fileset->follow_symlinks = true;
    
    /* Add include/exclude patterns */
    if (includes) {
        char *inc_pattern = resolve_variables(strdup(includes), project);
        selector_t *selector = selector_alloc();
        selector->type = SELECTOR_INCLUDE;
        hashtable_insert(selector->attribute_dict, strdup("name"), inc_pattern);
        fileset->selector_list = slist_new(selector);
    }
    
    if (excludes) {
        char *exc_pattern = resolve_variables(strdup(excludes), project);
        selector_t *selector = selector_alloc();
        selector->type = SELECTOR_EXCLUDE;
        hashtable_insert(selector->attribute_dict, strdup("name"), exc_pattern);
        if (fileset->selector_list) {
            slist_append(fileset->selector_list, selector);
        } else {
            fileset->selector_list = slist_new(selector);
        }
    }
    
    /* Resolve the fileset */
    slist_t *file_list = slist_new(NULL);
    if (!resolve_fileset(fileset, file_list, project)) {
        task_log(task, LOG_WARNING, "native2ascii: no files found");
        slist_free(file_list);
        fileset_free(fileset);
        free(dest_dir);
        return true;
    }
    
    /* Process each file */
    int count = 0;
    slist_t *file_ptr = slist_next(file_list);
    size_t src_len = strlen(src_dir);
    
    while (file_ptr) {
        const char *file_path = file_ptr->data;
        
        /* Calculate relative path */
        const char *rel_path = file_path;
        if (strncmp(file_path, src_dir, src_len) == 0) {
            rel_path = file_path + src_len;
            if (*rel_path == '/') {
                rel_path++;
            }
        }
        
        /* Build destination path */
        char dest_path[PATH_MAX];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, rel_path);
        
        /* Convert the file */
        if (convert_file(file_path, dest_path)) {
            count++;
        }
        
        file_ptr = slist_next(file_ptr);
    }
    
    /* Log result */
    char message[256];
    snprintf(message, sizeof(message), "Converting %d files from %s to %s",
             count, src_dir, dest_dir);
    task_log(task, LOG_MESSAGE, message);
    
    slist_free(file_list);
    fileset_free(fileset);
    free(dest_dir);
    
    return true;
}

