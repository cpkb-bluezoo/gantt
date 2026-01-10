/*
 * fileset.c
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
#include <dirent.h>
#include <time.h>

/* Timing stats (enabled with GANTT_PROFILE env var) */
static double total_scan_time = 0;
static double total_match_time = 0;
static int total_files_scanned = 0;
static int total_dirs_scanned = 0;
static int profile_enabled = -1;

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

__attribute__((destructor))
static void print_profile_stats(void) {
    if (profile_enabled == 1 && (total_scan_time > 0 || total_match_time > 0)) {
        fprintf(stderr, "\n=== Fileset Profiling ===\n");
        fprintf(stderr, "  Directories scanned: %d\n", total_dirs_scanned);
        fprintf(stderr, "  Files scanned: %d\n", total_files_scanned);
        fprintf(stderr, "  Directory scan time: %.2f ms\n", total_scan_time);
        fprintf(stderr, "  Selector match time: %.2f ms\n", total_match_time);
        fprintf(stderr, "  Total fileset time: %.2f ms\n", total_scan_time + total_match_time);
    }
}

fileset_t *fileset_alloc(void)
{
    fileset_t *fileset = malloc(sizeof(fileset_t));
    if (!fileset) {
        return NULL;
    }
    
    fileset->name = NULL;
    fileset->dir = NULL;
    fileset->refid = NULL;
    fileset->default_excludes = false;
    fileset->case_sensitive = false;
    fileset->follow_symlinks = false;
    fileset->selector_list = NULL;
    
    return fileset;
}

void fileset_free(fileset_t *fileset)
{
    if (!fileset) {
        return;
    }
    
    free(fileset->name);
    free(fileset->dir);
    free(fileset->refid);
    
    /* Free selectors */
    slist_t *list = fileset->selector_list;
    while (list) {
        selector_free((selector_t *)list->data);
        list = slist_next(list);
    }
    slist_free(fileset->selector_list);
    
    free(fileset);
}

/**
 * Create a fileset directly from element name and attributes (SAX-style).
 * This is used by the direct parser to avoid building XML node trees.
 * 
 * @param name Element name (fileset, dirset, etc.)
 * @param attrs Expat-style attribute array [name1, value1, name2, value2, ..., NULL]
 * @return New fileset_t structure
 */
fileset_t *fileset_create(const char *name, const char **attrs)
{
    fileset_t *fileset = fileset_alloc();
    if (!fileset) {
        return NULL;
    }
    
    fileset->name = name ? strdup(name) : NULL;
    
    /* Process attributes */
    for (int i = 0; attrs && attrs[i]; i += 2) {
        const char *attr_name = attrs[i];
        const char *attr_value = attrs[i + 1];
        
        if (strcmp(attr_name, "dir") == 0) {
            fileset->dir = strdup(attr_value);
        } else if (strcmp(attr_name, "refid") == 0) {
            fileset->refid = strdup(attr_value);
        } else if (strcmp(attr_name, "defaultexcludes") == 0) {
            fileset->default_excludes = (strcmp(attr_value, "yes") == 0 ||
                                         strcmp(attr_value, "true") == 0);
        } else if (strcmp(attr_name, "casesensitive") == 0) {
            fileset->case_sensitive = (strcmp(attr_value, "yes") == 0 ||
                                       strcmp(attr_value, "true") == 0);
        } else if (strcmp(attr_name, "followsymlinks") == 0) {
            fileset->follow_symlinks = (strcmp(attr_value, "yes") == 0 ||
                                        strcmp(attr_value, "true") == 0);
        } else if (strcmp(attr_name, "includes") == 0) {
            /* Convert includes attribute to selector */
            selector_t *selector = selector_alloc();
            if (selector) {
                selector->type = SELECTOR_INCLUDE;
                hashtable_insert(selector->attribute_dict, strdup("name"), strdup(attr_value));
                fileset->selector_list = slist_new(selector);
            }
        } else if (strcmp(attr_name, "excludes") == 0) {
            /* Convert excludes attribute to selector */
            selector_t *selector = selector_alloc();
            if (selector) {
                selector->type = SELECTOR_EXCLUDE;
                hashtable_insert(selector->attribute_dict, strdup("name"), strdup(attr_value));
                if (fileset->selector_list) {
                    slist_append(slist_last(fileset->selector_list), selector);
                } else {
                    fileset->selector_list = slist_new(selector);
                }
            }
        }
    }
    
    return fileset;
}

/**
 * Add a selector to a fileset (used during SAX parsing).
 */
void fileset_add_selector(fileset_t *fileset, selector_t *selector)
{
    if (!fileset || !selector) {
        return;
    }
    
    if (!fileset->selector_list) {
        fileset->selector_list = slist_new(selector);
    } else {
        slist_append(slist_last(fileset->selector_list), selector);
    }
}

bool fileset_init(fileset_t *fileset, xml_node_t *node)
{
    const char *val;
    slist_t *selector_ptr = NULL;
    selector_t *selector;
    const char *root_dir_attribute_name;
    
    /* The name of the element that defines the fileset */
    fileset->name = strdup(node->name);
    
    /* Check for refid - if present, this is a reference to another fileset */
    val = xml_node_get_attr(node, REFID);
    if (val) {
        fileset->refid = strdup(val);
        return true;  /* Reference fileset - will be resolved later */
    }
    
    /* Get attributes - the root dir attribute depends on the element type */
    if (xml_streq(fileset->name, JAVAC)) {
        root_dir_attribute_name = SRC_DIR;
    } else if (xml_streq(fileset->name, JAR)) {
        root_dir_attribute_name = BASE_DIR;
    } else {
        root_dir_attribute_name = ROOT_DIR;
    }
    
    val = xml_node_get_attr(node, root_dir_attribute_name);
    if (val) {
        fileset->dir = strdup(val);
    }
    
    val = xml_node_get_attr(node, DEFAULT_EXCLUDES);
    if (val) {
        fileset->default_excludes = parse_boolean(val, true);
    }
    
    val = xml_node_get_attr(node, CASE_SENSITIVE);
    if (val) {
        fileset->case_sensitive = parse_boolean(val, true);
    }
    
    val = xml_node_get_attr(node, FOLLOW_SYMLINKS);
    if (val) {
        fileset->follow_symlinks = parse_boolean(val, true);
    }
    
    /* Handle includes/excludes attributes (comma-separated patterns) */
    val = xml_node_get_attr(node, "includes");
    if (val) {
        char **patterns = str_split(val, ",", 0);
        for (int i = 0; patterns && patterns[i]; i++) {
            char *pattern = str_strip(patterns[i]);
            if (pattern[0]) {
                selector = selector_alloc();
                if (selector) {
                    selector->type = SELECTOR_INCLUDE;
                    hashtable_insert(selector->attribute_dict, strdup(NAME), strdup(pattern));
                    if (!selector_ptr) {
                        fileset->selector_list = slist_new(selector);
                        selector_ptr = fileset->selector_list;
                    } else {
                        selector_ptr = slist_append(selector_ptr, selector);
                    }
                }
            }
        }
        str_freev(patterns);
    }
    
    val = xml_node_get_attr(node, "excludes");
    if (val) {
        char **patterns = str_split(val, ",", 0);
        for (int i = 0; patterns && patterns[i]; i++) {
            char *pattern = str_strip(patterns[i]);
            if (pattern[0]) {
                selector = selector_alloc();
                if (selector) {
                    selector->type = SELECTOR_EXCLUDE;
                    hashtable_insert(selector->attribute_dict, strdup(NAME), strdup(pattern));
                    if (!selector_ptr) {
                        fileset->selector_list = slist_new(selector);
                        selector_ptr = fileset->selector_list;
                    } else {
                        selector_ptr = slist_append(selector_ptr, selector);
                    }
                }
            }
        }
        str_freev(patterns);
    }
    
    /* Process children */
    xml_node_t *cur = node->children;
    while (cur) {
        if (xml_streq(cur->name, INCLUDE) ||
            xml_streq(cur->name, EXCLUDE) ||
            xml_streq(cur->name, INCLUDES_FILE) ||
            xml_streq(cur->name, EXCLUDES_FILE)) {
            /* Create selector */
            selector = selector_alloc();
            if (!selector) {
                return false;
            }
            if (!selector_init(selector, cur)) {
                selector_free(selector);
                return false;
            }
            
            /* Add selector to list */
            if (!selector_ptr) {
                fileset->selector_list = slist_new(selector);
                selector_ptr = fileset->selector_list;
            } else {
                selector_ptr = slist_append(selector_ptr, selector);
            }
        }
        /* TODO: other selector types */
        
        cur = cur->next;
    }
    
    /* Add default include selector if needed */
    /* For javac, we always need an include pattern for *.java files */
    bool has_include = false;
    slist_t *check_sel = fileset->selector_list;
    while (check_sel) {
        selector_t *s = check_sel->data;
        if (s->type == SELECTOR_INCLUDE) {
            has_include = true;
            break;
        }
        check_sel = slist_next(check_sel);
    }
    
    if (!has_include) {
        selector = selector_alloc();
        if (!selector) {
            return false;
        }
        selector->type = SELECTOR_INCLUDE;
        
        if (xml_streq(fileset->name, JAVAC)) {
            hashtable_insert(selector->attribute_dict, NAME, strdup("**/*.java"));
        } else {
            hashtable_insert(selector->attribute_dict, NAME, strdup("**"));
        }
        
        /* Prepend to selector list so includes are checked first */
        slist_t *new_head = slist_new(selector);
        new_head->next = fileset->selector_list;
        fileset->selector_list = new_head;
    }
    
    /* Note: validation happens at resolution time, not here */
    /* Some tasks like fixcrlf use srcdir instead of dir */
    
    return true;
}

/**
 * Resolves the specified fileset, adding to the specified list of files.
 * Expands property references and resolves relative paths against basedir.
 */
bool resolve_fileset(fileset_t *fileset, slist_t *acc, project_t *project)
{
    char *dirname;
    bool ret;
    fileset_t *ref_fileset;
    
    /* Handle fileset reference */
    if (fileset->refid) {
        ref_fileset = hashtable_lookup(project->fileset_dict, fileset->refid);
        if (!ref_fileset) {
            fprintf(stderr, "Unable to locate fileset: %s\n", fileset->refid);
            return false;
        }
        return resolve_fileset(ref_fileset, acc, project);
    }
    
    /* Check for missing directory */
    if (!fileset->dir) {
        fprintf(stderr, "Warning: Fileset '%s' has no directory set.\n", 
                fileset->name ? fileset->name : "(unknown)");
        return true;  /* Empty fileset */
    }
    
    /* Expand property references */
    dirname = resolve_variables(strdup(fileset->dir), project);
    
    /* Resolve relative paths against basedir */
    dirname = expand_location(project, dirname);
    
    ret = add_files(dirname, acc, fileset, project);
    
    free(dirname);
    
    return ret;
}

bool add_files(const char *dirname, slist_t *acc, fileset_t *fileset,
               project_t *project)
{
    DIR *dir;
    struct dirent *entry;
    char *filename;
    slist_t *sel_ptr;
    selector_t *selector;
    bool file_match;
    double start_time, match_start;
    
    /* Check if profiling is enabled */
    if (profile_enabled < 0) {
        profile_enabled = (getenv("GANTT_PROFILE") != NULL) ? 1 : 0;
    }
    
    if (profile_enabled) {
        start_time = get_time_ms();
    }
    
    dir = opendir(dirname);
    if (!dir) {
        fprintf(stderr, "Can't open `%s'\n", dirname);
        return false;
    }
    
    if (profile_enabled) {
        total_scan_time += get_time_ms() - start_time;
        total_dirs_scanned++;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        if (profile_enabled) {
            start_time = get_time_ms();
        }
        filename = str_concat(dirname, DIR_SEPARATOR_S, entry->d_name, NULL);
        if (profile_enabled) {
            total_scan_time += get_time_ms() - start_time;
        }
        
        if (file_is_directory(filename)) {
            /* Retrieve subdirectory contents */
            if (!add_files(filename, acc, fileset, project)) {
                free(filename);
                closedir(dir);
                return false;
            }
            free(filename);
        } else {
            if (profile_enabled) {
                total_files_scanned++;
                match_start = get_time_ms();
            }
            
            /* Apply each selector - includes first, then excludes */
            file_match = false;
            sel_ptr = fileset->selector_list;
            
            /* First pass: check includes */
            while (sel_ptr) {
                selector = sel_ptr->data;
                if (selector->type == SELECTOR_INCLUDE) {
                    if (selector_match_file(filename, selector, fileset, project)) {
                        file_match = true;
                        break;  /* Stop on first include match */
                    }
                }
                sel_ptr = slist_next(sel_ptr);
            }
            
            /* Second pass: check excludes (only if file was included) */
            if (file_match) {
                sel_ptr = fileset->selector_list;
                while (sel_ptr) {
                    selector = sel_ptr->data;
                    if (selector->type == SELECTOR_EXCLUDE) {
                        if (selector_match_file(filename, selector, fileset, project)) {
                            file_match = false;  /* Excluded */
                            break;
                        }
                    }
                    sel_ptr = slist_next(sel_ptr);
                }
            }
            
            if (profile_enabled) {
                total_match_time += get_time_ms() - match_start;
            }
            
            /* Add to list if all selectors match */
            if (file_match) {
                slist_append(acc, filename);
            } else {
                free(filename);
            }
        }
    }
    
    closedir(dir);
    return true;
}
