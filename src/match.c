/*
 * match.c
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

#include "gantt.h"

/**
 * Match the specified input path string against the pattern.
 */
bool match_path(const char *pattern, const char *string, bool case_sensitive)
{
    dlist_t *pat_list;
    dlist_t *str_list;
    dlist_t *pat_ptr;
    dlist_t *str_ptr;
    dlist_t *pat_end;
    dlist_t *str_end;
    string_t *pat_tmp;
    string_t *str_tmp;
    bool result = true;
    char *modified_pattern = NULL;
    size_t pattern_len;
    
    assert(pattern);
    
    if (!string) {
        return (strcmp(pattern, "**") == 0 || strcmp(pattern, "*") == 0);
    }
    
    /* Handle patterns ending with "/" - in Ant this means "all files in dir" */
    pattern_len = strlen(pattern);
    if (pattern_len > 0 && pattern[pattern_len - 1] == '/') {
        /* Append ** to make it match all files in the directory */
        modified_pattern = malloc(pattern_len + 3);
        memcpy(modified_pattern, pattern, pattern_len);
        modified_pattern[pattern_len] = '*';
        modified_pattern[pattern_len + 1] = '*';
        modified_pattern[pattern_len + 2] = '\0';
        pattern = modified_pattern;
    }
    
    pat_ptr = pat_list = path_as_list(pattern);
    str_ptr = str_list = path_as_list(string);
    
    if (modified_pattern) {
        free(modified_pattern);
    }
    
    /* Match list elements directly up to first "**" */
    while (pat_ptr && str_ptr) {
        assert(pat_ptr->data);
        assert(str_ptr->data);
        
        if (strcmp(pat_ptr->data, "**") == 0) {
            break;
        }
        
        if (!match(pat_ptr->data, str_ptr->data, case_sensitive)) {
            dlist_free_full(pat_list, free);
            dlist_free_full(str_list, free);
            return false;
        }
        
        pat_ptr = dlist_next(pat_ptr);
        str_ptr = dlist_next(str_ptr);
    }
    
    /* End of input? */
    if (!str_ptr) {
        while (pat_ptr) {
            if (strcmp(pat_ptr->data, "**") != 0) {
                dlist_free_full(pat_list, free);
                dlist_free_full(str_list, free);
                return false;
            }
            pat_ptr = dlist_next(pat_ptr);
        }
        dlist_free_full(pat_list, free);
        dlist_free_full(str_list, free);
        return true;
    } else if (!pat_ptr) {
        /* End of pattern with input remaining */
        dlist_free_full(pat_list, free);
        dlist_free_full(str_list, free);
        return false;
    }
    
    /* Match elements directly backwards to last "**" */
    pat_end = dlist_last(pat_list);
    str_end = dlist_last(str_list);
    
    while (pat_end != pat_ptr && str_end != str_ptr) {
        if (strcmp(pat_end->data, "**") == 0) {
            break;
        }
        
        if (!match(pat_end->data, str_end->data, case_sensitive)) {
            dlist_free_full(pat_list, free);
            dlist_free_full(str_list, free);
            return false;
        }
        
        pat_end = dlist_prev(pat_end);
        str_end = dlist_prev(str_end);
    }
    
    /* End of input? */
    if (str_end == str_ptr) {
        if (pat_ptr != pat_end) {
            if (strcmp(pat_ptr->data, "**") != 0) {
                dlist_free_full(pat_list, free);
                dlist_free_full(str_list, free);
                return false;
            }
        }
        while (pat_ptr != pat_end) {
            pat_ptr = dlist_next(pat_ptr);
            if (strcmp(pat_ptr->data, "**") != 0) {
                dlist_free_full(pat_list, free);
                dlist_free_full(str_list, free);
                return false;
            }
        }
        dlist_free_full(pat_list, free);
        dlist_free_full(str_list, free);
        return true;
    }
    
    if (pat_ptr != pat_end) {
        /* Construct strings to match */
        pat_tmp = string_new(pat_ptr->data);
        str_tmp = string_new(str_ptr->data);
        
        pat_ptr = dlist_next(pat_ptr);
        str_ptr = dlist_next(str_ptr);
        
        while (pat_ptr != pat_end) {
            string_append_c(pat_tmp, PATH_SEPARATOR);
            if (strcmp(pat_ptr->data, "**") == 0) {
                string_append(pat_tmp, "*");  /* Convert "**" to "*" */
            } else {
                string_append(pat_tmp, pat_ptr->data);
            }
            pat_ptr = dlist_next(pat_ptr);
        }
        
        while (str_ptr != str_end) {
            string_append_c(str_tmp, PATH_SEPARATOR);
            string_append(str_tmp, str_ptr->data);
            str_ptr = dlist_next(str_ptr);
        }
        
        if (!match(pat_tmp->str, str_tmp->str, case_sensitive)) {
            result = false;
        }
        
        string_free(pat_tmp, true);
        string_free(str_tmp, true);
        
        if (!result) {
            dlist_free_full(pat_list, free);
            dlist_free_full(str_list, free);
            return false;
        }
    }
    
    while (pat_ptr != pat_end) {
        if (strcmp(pat_ptr->data, "**") != 0) {
            dlist_free_full(pat_list, free);
            dlist_free_full(str_list, free);
            return false;
        }
        pat_ptr = dlist_next(pat_ptr);
    }
    
    dlist_free_full(pat_list, free);
    dlist_free_full(str_list, free);
    return true;
}

/**
 * Matches a string against the given pattern, optionally ignoring case.
 */
bool match(const char *pattern, const char *string, bool case_sensitive)
{
    char *pat_tmp;
    char *str_tmp;
    bool ret;
    int flags;
    
    if (case_sensitive) {
        pat_tmp = (char *)pattern;
        str_tmp = (char *)string;
        flags = 0;
    } else {
        pat_tmp = str_down(pattern);
        str_tmp = str_down(string);
        flags = 0;
    }
    
    /* Use fnmatch for glob pattern matching */
    ret = (fnmatch(pat_tmp, str_tmp, flags) == 0);
    
    if (!case_sensitive) {
        free(pat_tmp);
        free(str_tmp);
    }
    
    return ret;
}

/**
 * Converts a filesystem path specification to a list containing the
 * components of the path.
 */
dlist_t *path_as_list(const char *path)
{
    dlist_t *list = NULL;
    dlist_t *list_ptr = NULL;
    char **tokens;
    
    assert(path);
    
    tokens = str_split(path, DIR_SEPARATOR_S, -1);
    
    for (int i = 0; tokens && tokens[i]; i++) {
        if (!list) {
            list = dlist_new(strdup(tokens[i]));
            list_ptr = list;
        } else {
            list_ptr = dlist_append(list_ptr, strdup(tokens[i]));
        }
    }
    
    str_freev(tokens);
    return list;
}
