/*
 * selector.c
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

selector_t *selector_alloc(void)
{
    selector_t *selector = malloc(sizeof(selector_t));
    if (!selector) {
        return NULL;
    }
    
    selector->type = SELECTOR_UNKNOWN;
    selector->attribute_dict = hashtable_new();
    selector->children = NULL;
    selector->refid = NULL;
    selector->unless_prop = NULL;
    selector->if_prop = NULL;
    
    return selector;
}

void selector_free(selector_t *selector)
{
    if (!selector) {
        return;
    }
    
    hashtable_free(selector->attribute_dict);
    
    /* Free children */
    slist_t *child = selector->children;
    while (child) {
        selector_free((selector_t *)child->data);
        child = slist_next(child);
    }
    slist_free(selector->children);
    
    free(selector->refid);
    free(selector->unless_prop);
    free(selector->if_prop);
    free(selector);
}

bool selector_init(selector_t *selector, xml_node_t *node)
{
    char *name;
    char *value;
    
    if (xml_streq(node->name, INCLUDE)) {
        selector->type = SELECTOR_INCLUDE;
    } else if (xml_streq(node->name, EXCLUDE)) {
        selector->type = SELECTOR_EXCLUDE;
    } else if (xml_streq(node->name, INCLUDES_FILE)) {
        selector->type = SELECTOR_INCLUDES_FILE;
    } else if (xml_streq(node->name, EXCLUDES_FILE)) {
        selector->type = SELECTOR_EXCLUDES_FILE;
    } else {
        fprintf(stderr, "Unknown selector type: %s\n", node->name);
        return false;
    }
    
    /* Get attributes */
    for (xml_attr_t *attr = node->attrs; attr; attr = attr->next) {
        name = strdup(attr->name);
        value = attr->value ? strdup(attr->value) : NULL;
        hashtable_insert(selector->attribute_dict, name, value);
        free(name);
    }
    
    return true;
}

/**
 * Create a selector directly from element name and attributes (SAX-style).
 * This is used by the direct parser to avoid building XML node trees.
 * 
 * @param name Element name (or, and, not, filename, selector, include, exclude)
 * @param attrs Expat-style attribute array [name1, value1, name2, value2, ..., NULL]
 * @return New selector_t structure
 */
selector_t *selector_create(const char *name, const char **attrs)
{
    selector_t *selector = selector_alloc();
    if (!selector) {
        return NULL;
    }
    
    /* Determine selector type from element name */
    if (xml_streq(name, "or")) {
        selector->type = SELECTOR_OR;
    } else if (xml_streq(name, "and")) {
        selector->type = SELECTOR_AND;
    } else if (xml_streq(name, "not")) {
        selector->type = SELECTOR_NOT;
    } else if (xml_streq(name, "filename")) {
        selector->type = SELECTOR_FILENAME;
    } else if (xml_streq(name, "selector")) {
        /* Check for refid attribute */
        for (int i = 0; attrs && attrs[i]; i += 2) {
            if (strcmp(attrs[i], "refid") == 0) {
                selector->type = SELECTOR_REFID;
                selector->refid = strdup(attrs[i + 1]);
                break;
            }
        }
        /* If no refid, treat as AND of children */
        if (selector->type != SELECTOR_REFID) {
            selector->type = SELECTOR_AND;
        }
    } else if (xml_streq(name, INCLUDE)) {
        selector->type = SELECTOR_INCLUDE;
    } else if (xml_streq(name, EXCLUDE)) {
        selector->type = SELECTOR_EXCLUDE;
    } else if (xml_streq(name, INCLUDES_FILE)) {
        selector->type = SELECTOR_INCLUDES_FILE;
    } else if (xml_streq(name, EXCLUDES_FILE)) {
        selector->type = SELECTOR_EXCLUDES_FILE;
    } else {
        selector->type = SELECTOR_UNKNOWN;
    }
    
    /* Copy all attributes */
    for (int i = 0; attrs && attrs[i]; i += 2) {
        const char *attr_name = attrs[i];
        const char *attr_value = attrs[i + 1];
        
        /* Handle special attributes */
        if (strcmp(attr_name, "if") == 0) {
            selector->if_prop = strdup(attr_value);
        } else if (strcmp(attr_name, "unless") == 0) {
            selector->unless_prop = strdup(attr_value);
        } else if (strcmp(attr_name, "refid") == 0 && selector->type == SELECTOR_REFID) {
            /* Already handled above */
        } else {
            /* Store in attribute dict */
            hashtable_insert(selector->attribute_dict, strdup(attr_name), 
                           attr_value ? strdup(attr_value) : NULL);
        }
    }
    
    return selector;
}

/**
 * Add a child selector to a parent (used during SAX parsing).
 */
void selector_add_child(selector_t *parent, selector_t *child)
{
    if (!parent || !child) {
        return;
    }
    
    if (!parent->children) {
        parent->children = slist_new(child);
    } else {
        slist_append(slist_last(parent->children), child);
    }
}

/**
 * Parse a selector element, including compound selectors (or, and, not).
 * This handles the full selector grammar with nested children.
 * NOTE: This function is kept for backward compatibility with DOM-based parsing.
 */
selector_t *selector_parse(xml_node_t *node, project_t *project)
{
    selector_t *selector;
    selector_t *child_selector;
    slist_t *child_ptr = NULL;
    const char *val;
    
    selector = selector_alloc();
    if (!selector) {
        return NULL;
    }
    
    /* Determine selector type */
    if (xml_streq(node->name, "or")) {
        selector->type = SELECTOR_OR;
    } else if (xml_streq(node->name, "and")) {
        selector->type = SELECTOR_AND;
    } else if (xml_streq(node->name, "not")) {
        selector->type = SELECTOR_NOT;
    } else if (xml_streq(node->name, "filename")) {
        selector->type = SELECTOR_FILENAME;
    } else if (xml_streq(node->name, "selector")) {
        /* <selector refid="..."/> or nested selector */
        val = xml_node_get_attr(node, REFID);
        if (val) {
            selector->type = SELECTOR_REFID;
            selector->refid = strdup(val);
        } else {
            /* Nested selector container - treat as AND of children */
            selector->type = SELECTOR_AND;
        }
    } else if (xml_streq(node->name, INCLUDE)) {
        selector->type = SELECTOR_INCLUDE;
    } else if (xml_streq(node->name, EXCLUDE)) {
        selector->type = SELECTOR_EXCLUDE;
    } else {
        /* Unknown - try to handle generically */
        selector->type = SELECTOR_UNKNOWN;
    }
    
    /* Get conditional attributes */
    val = xml_node_get_attr(node, "unless");
    if (val) {
        selector->unless_prop = strdup(val);
    }
    val = xml_node_get_attr(node, "if");
    if (val) {
        selector->if_prop = strdup(val);
    }
    
    /* Store all attributes */
    for (xml_attr_t *attr = node->attrs; attr; attr = attr->next) {
        hashtable_insert(selector->attribute_dict, strdup(attr->name), 
                        attr->value ? strdup(attr->value) : NULL);
    }
    
    /* Parse children for compound selectors */
    if (selector->type == SELECTOR_OR || selector->type == SELECTOR_AND ||
        selector->type == SELECTOR_NOT || 
        (selector->type == SELECTOR_REFID && !selector->refid)) {
        for (xml_node_t *cur = node->children; cur; cur = cur->next) {
            child_selector = selector_parse(cur, project);
            if (child_selector) {
                if (!child_ptr) {
                    selector->children = slist_new(child_selector);
                    child_ptr = selector->children;
                } else {
                    child_ptr = slist_append(child_ptr, child_selector);
                }
            }
        }
    }
    
    return selector;
}

/**
 * Check if a file matches a selector, handling compound selectors.
 * This is used for filtering in javac tasks.
 * 
 * @param filepath The file path to check (relative to source dir)
 * @param selector The selector to match against
 * @param project Project for property lookups
 * @return true if file matches the selector
 */
bool selector_matches(const char *filepath, selector_t *selector, project_t *project)
{
    char *pattern;
    slist_t *child;
    selector_t *child_selector;
    selector_t *ref_selector;
    bool result;
    
    if (!selector) {
        return false;
    }
    
    /* Check conditional attributes first */
    if (selector->unless_prop) {
        /* If the property IS set, this selector should not match (return false) */
        if (hashtable_lookup(project->property_dict, selector->unless_prop)) {
            return false;
        }
    }
    if (selector->if_prop) {
        /* If the property is NOT set, this selector should not match (return false) */
        if (!hashtable_lookup(project->property_dict, selector->if_prop)) {
            return false;
        }
    }
    
    switch (selector->type) {
    case SELECTOR_OR:
        /* Any child matches -> true */
        child = selector->children;
        while (child) {
            child_selector = (selector_t *)child->data;
            if (selector_matches(filepath, child_selector, project)) {
                return true;
            }
            child = slist_next(child);
        }
        return false;
        
    case SELECTOR_AND:
        /* All children must match -> true only if all match */
        child = selector->children;
        while (child) {
            child_selector = (selector_t *)child->data;
            if (!selector_matches(filepath, child_selector, project)) {
                return false;
            }
            child = slist_next(child);
        }
        return true;
        
    case SELECTOR_NOT:
        /* Invert the result of the first child */
        if (selector->children) {
            child_selector = (selector_t *)selector->children->data;
            return !selector_matches(filepath, child_selector, project);
        }
        return true;
        
    case SELECTOR_REFID:
        /* Look up the referenced selector and evaluate it */
        if (selector->refid && project->selector_dict) {
            ref_selector = hashtable_lookup(project->selector_dict, selector->refid);
            if (ref_selector) {
                return selector_matches(filepath, ref_selector, project);
            }
        }
        return false;
        
    case SELECTOR_FILENAME:
    case SELECTOR_INCLUDE:
        /* Match filename pattern */
        pattern = hashtable_lookup(selector->attribute_dict, NAME);
        if (!pattern) {
            pattern = hashtable_lookup(selector->attribute_dict, "name");
        }
        if (pattern) {
            pattern = resolve_variables(strdup(pattern), project);
            result = match_path(pattern, filepath, true);
            free(pattern);
            return result;
        }
        return false;
        
    case SELECTOR_EXCLUDE:
        /* Exclude patterns - match returns true if file should be excluded */
        pattern = hashtable_lookup(selector->attribute_dict, NAME);
        if (!pattern) {
            pattern = hashtable_lookup(selector->attribute_dict, "name");
        }
        if (pattern) {
            pattern = resolve_variables(strdup(pattern), project);
            result = match_path(pattern, filepath, true);
            free(pattern);
            return result;
        }
        return false;
        
    default:
        return false;
    }
}

/**
 * Does the specified selector match the given directory name?
 */
bool selector_match_dir(const char *dirname, selector_t *selector,
                        fileset_t *fileset, project_t *project)
{
    (void)dirname;
    (void)selector;
    (void)fileset;
    (void)project;
    return false;  /* TODO */
}

/**
 * Does the specified selector match the given file name?
 */
bool selector_match_file(const char *filename, selector_t *selector,
                         fileset_t *fileset, project_t *project)
{
    char *pattern;
    char *dir;
    bool ret;
    size_t len;
    const char *relative_filename;
    
    /* Strip the fileset base directory from the filename */
    /* Must expand the dir the same way we expand filenames in resolve_fileset */
    dir = resolve_variables(strdup(fileset->dir), project);
    dir = expand_location(project, dir);
    
    len = strlen(dir);
    
    /* Strip directory prefix from filename to get relative path */
    if (strncmp(filename, dir, len) == 0) {
        if (filename[len] == DIR_SEPARATOR) {
            len++;
        }
        relative_filename = filename + len;
    } else {
        /* Try to find the dir substring in the filename */
        const char *pos = strstr(filename, fileset->dir);
        if (pos) {
            len = strlen(fileset->dir);
            if (pos[len] == DIR_SEPARATOR) {
                len++;
            }
            relative_filename = pos + len;
        } else {
            relative_filename = filename;
        }
    }
    
    switch (selector->type) {
    case SELECTOR_INCLUDE:
        pattern = hashtable_lookup(selector->attribute_dict, NAME);
        if (!pattern) {
            return false;
        }
        /* Expand property references in pattern */
        pattern = resolve_variables(strdup(pattern), project);
        ret = match_path(pattern, relative_filename, fileset->case_sensitive);
        free(pattern);
        return ret;
        
    case SELECTOR_EXCLUDE:
        pattern = hashtable_lookup(selector->attribute_dict, NAME);
        if (!pattern) {
            return false;
        }
        /* Expand property references in pattern */
        pattern = resolve_variables(strdup(pattern), project);
        ret = match_path(pattern, relative_filename, fileset->case_sensitive);
        free(pattern);
        return ret;
        
    /* TODO: more selector types */
    default:
        return false;
    }
}
