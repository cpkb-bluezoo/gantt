/*
 * path.c
 * Copyright (C) 2005, 2026 Chris Burdess <dog@gnu.org>
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

path_t *path_alloc(void)
{
    path_t *path = malloc(sizeof(path_t));
    if (!path) {
        return NULL;
    }
    
    path->type = PATH_ELEMENT;
    path->data = NULL;
    
    return path;
}

void path_free(path_t *path)
{
    if (!path) {
        return;
    }
    
    if (path->type == PATH_FILESET && path->data) {
        fileset_free((fileset_t *)path->data);
    } else if ((path->type == PATH_ELEMENT || path->type == PATH_REFERENCE) && path->data) {
        free(path->data);
    } else if (path->type == PATH_INLINE && path->data) {
        slist_free_full((slist_t *)path->data, (void (*)(void *))path_free);
    }
    
    free(path);
}

/**
 * Create a path element directly from element name and attributes (SAX-style).
 * This is used by the direct parser to avoid building XML node trees.
 * 
 * @param name Element name (pathelement, path, classpath, fileset, dirset)
 * @param attrs Expat-style attribute array [name1, value1, name2, value2, ..., NULL]
 * @return New path_t structure, or NULL if not a valid path element
 */
path_t *path_create(const char *name, const char **attrs)
{
    path_t *path = path_alloc();
    if (!path) {
        return NULL;
    }
    
    if (xml_streq(name, PATHELEMENT)) {
        /* Path element with path or location attribute */
        for (int i = 0; attrs && attrs[i]; i += 2) {
            if (strcmp(attrs[i], "path") == 0 || strcmp(attrs[i], "location") == 0) {
                path->data = strdup(attrs[i + 1]);
                path->type = PATH_ELEMENT;
                return path;
            }
        }
        /* Missing path/location attribute */
        fprintf(stderr, "pathelement missing path attribute.\n");
        path_free(path);
        return NULL;
    }
    else if (xml_streq(name, PATH) || xml_streq(name, CLASSPATH)) {
        /* Check for refid or inline children */
        for (int i = 0; attrs && attrs[i]; i += 2) {
            if (strcmp(attrs[i], "refid") == 0) {
                path->type = PATH_REFERENCE;
                path->data = strdup(attrs[i + 1]);
                return path;
            }
            if (strcmp(attrs[i], "path") == 0) {
                /* Simple path value */
                path->type = PATH_ELEMENT;
                path->data = strdup(attrs[i + 1]);
                return path;
            }
        }
        /* Will be PATH_INLINE with children added later */
        path->type = PATH_INLINE;
        path->data = NULL;  /* Children to be added via path_add_child */
        return path;
    }
    else if (xml_streq(name, FILESET) || xml_streq(name, DIRSET)) {
        /* Create fileset */
        path->type = PATH_FILESET;
        path->data = fileset_create(name, attrs);
        if (!path->data) {
            path_free(path);
            return NULL;
        }
        return path;
    }
    
    /* Unknown path element type */
    fprintf(stderr, "Unknown path element type: %s\n", name);
    path_free(path);
    return NULL;
}

/**
 * Add a child path element to an inline path (used during SAX parsing).
 */
void path_add_child(path_t *parent, path_t *child)
{
    if (!parent || !child) {
        return;
    }
    
    if (parent->type != PATH_INLINE) {
        /* Can only add children to inline paths */
        return;
    }
    
    slist_t *children = parent->data;
    if (!children) {
        parent->data = slist_new(child);
    } else {
        slist_append(slist_last(children), child);
    }
}

/**
 * Parses an individual path element.
 */
bool path_init(path_t *path, xml_node_t *node)
{
    const char *val;
    fileset_t *fileset;
    
    if (xml_streq(node->name, PATHELEMENT)) {
        val = xml_node_get_attr(node, PATH);
        if (!val) {
            val = xml_node_get_attr(node, LOCATION);
        }
        if (!val) {
            fprintf(stderr, "pathelement missing path attribute.\n");
            return false;
        }
        path->data = strdup(val);
    }
    else if (xml_streq(node->name, FILESET) ||
             xml_streq(node->name, DIRSET)) {
        path->type = PATH_FILESET;
        fileset = fileset_alloc();
        if (!fileset) {
            return false;
        }
        if (!fileset_init(fileset, node)) {
            fileset_free(fileset);
            return false;
        }
        path->data = fileset;
    }
    else if (xml_streq(node->name, PATH) ||
             xml_streq(node->name, CLASSPATH)) {
        /* Nested path: either <path refid="..."/> or inline <path><pathelement.../></path> */
        val = xml_node_get_attr(node, REFID);
        if (val) {
            path->type = PATH_REFERENCE;
            path->data = strdup(val);
        } else {
            /* Inline nested path - parse children and store as PATH_INLINE */
            path->type = PATH_INLINE;
            path->data = path_list_init(node);  /* Recursively parse nested path elements */
        }
    }
    else {
        fprintf(stderr, "Unknown path element type: %s\n", node->name);
        return false;
    }
    
    return true;
}

/**
 * Parses a <path> or <classpath> element, returning a list of path
 * elements.
 */
slist_t *path_list_init(xml_node_t *node)
{
    const char *val;
    slist_t *path_list = NULL;
    slist_t *path_ptr = NULL;
    path_t *path;
    
    /* path attribute */
    val = xml_node_get_attr(node, PATH);
    if (val) {
        path = path_alloc();
        if (!path) {
            return NULL;
        }
        path->data = strdup(val);
        path_list = slist_new(path);
        path_ptr = path_list;
    }
    
    /* Process children */
    xml_node_t *cur = node->children;
    while (cur) {
        /* Create the path element */
        path = path_alloc();
        if (!path) {
            return NULL;
        }
        if (!path_init(path, cur)) {
            path_free(path);
            return NULL;
        }
        
        if (!path_ptr) {
            path_list = slist_new(path);
            path_ptr = path_list;
        } else {
            path_ptr = slist_append(path_ptr, path);
        }
        
        cur = cur->next;
    }
    
    return path_list;
}

/**
 * Resolves a path definition to a list of directory entries.
 * Expands property references in path elements.
 */
slist_t *resolve_path(slist_t *path_list, project_t *project)
{
    slist_t *acc = NULL;
    slist_t *acc_ptr = NULL;
    slist_t *sub;
    slist_t *sub_ptr;
    slist_t *ref_path_list;
    slist_t *ref_resolved;
    path_t *path;
    char *resolved_data;
    char *refid;
    
    while (path_list) {
        path = path_list->data;
        
        switch (path->type) {
        case PATH_ELEMENT:
            /* Expand property references in path element */
            resolved_data = resolve_variables(strdup(path->data), project);
            if (!acc) {
                acc = slist_new(resolved_data);
                acc_ptr = acc;
            } else {
                acc_ptr = slist_append(acc_ptr, resolved_data);
            }
            break;
            
        case PATH_FILESET:
            sub = slist_new(NULL);  /* Dummy head */
            if (resolve_fileset(path->data, sub, project)) {
                sub_ptr = slist_next(sub);
                while (sub_ptr) {
                    if (!acc) {
                        acc = slist_new(sub_ptr->data);
                        acc_ptr = acc;
                    } else {
                        acc_ptr = slist_append(acc_ptr, sub_ptr->data);
                    }
                    sub_ptr = slist_next(sub_ptr);
                }
            }
            slist_free(sub);
            break;
            
        case PATH_REFERENCE:
            /* Look up the referenced path and recursively resolve it */
            refid = (char *)path->data;
            ref_path_list = hashtable_lookup(project->path_dict, refid);
            if (ref_path_list) {
                ref_resolved = resolve_path(ref_path_list, project);
                /* Append resolved elements to our accumulator */
                sub_ptr = ref_resolved;
                while (sub_ptr) {
                    if (!acc) {
                        acc = slist_new(sub_ptr->data);
                        acc_ptr = acc;
                    } else {
                        acc_ptr = slist_append(acc_ptr, sub_ptr->data);
                    }
                    sub_ptr = slist_next(sub_ptr);
                }
                /* Free the list structure but not the data (now owned by acc) */
                slist_free(ref_resolved);
            } else {
                fprintf(stderr, "Unable to locate path structure: %s\n", refid);
            }
            break;
            
        case PATH_INLINE:
            /* Inline nested path - recursively resolve */
            ref_path_list = (slist_t *)path->data;
            if (ref_path_list) {
                ref_resolved = resolve_path(ref_path_list, project);
                /* Append resolved elements to our accumulator */
                sub_ptr = ref_resolved;
                while (sub_ptr) {
                    if (!acc) {
                        acc = slist_new(sub_ptr->data);
                        acc_ptr = acc;
                    } else {
                        acc_ptr = slist_append(acc_ptr, sub_ptr->data);
                    }
                    sub_ptr = slist_next(sub_ptr);
                }
                /* Free the list structure but not the data (now owned by acc) */
                slist_free(ref_resolved);
            }
            break;
        }
        
        path_list = slist_next(path_list);
    }
    
    return acc;
}
