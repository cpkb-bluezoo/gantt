/*
 * project.c
 * Copyright (C) 2005, 2013, 2026 Chris Burdess <dog@gnu.org>
 * 
 * This file is part of gantt.
 * 
 * gantt is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gantt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gantt.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "gantt.h"

/**
 * Create a new project.
 */
project_t *project_alloc(void)
{
    project_t *project = malloc(sizeof(project_t));
    if (!project) {
        return NULL;
    }
    
    project->filename = NULL;
    project->default_target = NULL;
    project->name = NULL;
    project->description = NULL;
    project->base_dir = NULL;
    project->target_list = NULL;
    project->target_dict = hashtable_new();
    project->property_dict = hashtable_new();
    project->path_dict = hashtable_new();
    project->fileset_dict = hashtable_new();
    project->selector_dict = hashtable_new();
    
    return project;
}

/**
 * Destroy a project.
 */
void project_free(project_t *project)
{
    if (!project) {
        return;
    }
    
    free(project->filename);
    free(project->default_target);
    free(project->name);
    free(project->description);
    free(project->base_dir);
    
    /* Free targets */
    slist_t *list = project->target_list;
    while (list) {
        target_free((target_t *)list->data);
        list = slist_next(list);
    }
    slist_free(project->target_list);
    
    hashtable_free(project->target_dict);
    hashtable_free(project->property_dict);
    hashtable_free(project->path_dict);
    hashtable_free(project->fileset_dict);
    hashtable_free(project->selector_dict);
    
    free(project);
}

/**
 * Initialise a project from an XML node.
 */
bool project_init(project_t *project, xml_node_t *node)
{
    const char *val;
    target_t *target;
    target_t *target_dep;
    slist_t *target_ptr = NULL;
    slist_t *deps;
    slist_t *path_list;
    
    /* Get attributes */
    val = xml_node_get_attr(node, NAME);
    if (val) {
        project->name = strdup(val);
    }
    
    val = xml_node_get_attr(node, BASE_DIR);
    if (val) {
        project->base_dir = strdup(val);
    }
    
    val = xml_node_get_attr(node, DEFAULT);
    if (val) {
        project->default_target = strdup(val);
    }
    
    /* Process children */
    xml_node_t *cur = node->children;
    while (cur) {
        if (xml_streq(cur->name, TARGET)) {
            /* Create a new target */
            target = target_alloc(project);
            if (!target) {
                return false;
            }
            if (!target_init(target, cur)) {
                target_free(target);
                return false;
            }
            
            /* Add to list */
            if (!project->target_list) {
                project->target_list = slist_new(target);
                target_ptr = project->target_list;
            } else {
                target_ptr = slist_append(target_ptr, target);
            }
            
            /* Add to dict */
            hashtable_insert(project->target_dict, target->name, target);
        }
        else if (xml_streq(cur->name, PROPERTY)) {
            /* Create dummy property task and apply */
            task_t *property_task = task_alloc(NULL);
            task_init(property_task, cur);
            property_invoke(property_task, project);
            task_free(property_task);
        }
        else if (xml_streq(cur->name, PATH)) {
            /* Parse path definition */
            val = xml_node_get_attr(cur, ID);
            if (val) {
                path_list = path_list_init(cur);
                if (path_list) {
                    char *key = strdup(val);
                    hashtable_insert(project->path_dict, key, path_list);
                }
            }
        }
        else if (xml_streq(cur->name, FILESET)) {
            /* Parse fileset definition */
            val = xml_node_get_attr(cur, ID);
            if (val) {
                fileset_t *fileset = fileset_alloc();
                if (fileset && fileset_init(fileset, cur)) {
                    char *key = strdup(val);
                    hashtable_insert(project->fileset_dict, key, fileset);
                }
            }
        }
        else if (xml_streq(cur->name, "selector")) {
            /* Parse named selector definition */
            val = xml_node_get_attr(cur, ID);
            if (val) {
                selector_t *selector = selector_parse(cur, project);
                if (selector) {
                    char *key = strdup(val);
                    hashtable_insert(project->selector_dict, key, selector);
                }
            }
        }
        else if (xml_streq(cur->name, DESCRIPTION)) {
            project->description = xml_node_get_text(cur);
        }
        
        cur = cur->next;
    }
    
    /* Resolve target dependencies */
    slist_t *target_list = project->target_list;
    while (target_list) {
        target = target_list->data;
        deps = target->depends_list;
        while (deps) {
            char *key = deps->data;
            target_dep = hashtable_lookup(project->target_dict, key);
            if (!target_dep) {
                fprintf(stderr, "\nBUILD FAILED\n");
                fprintf(stderr, "Target `%s' does not exist in this project.", key);
                fprintf(stderr, " It is used from target `%s'.\n", target->name);
                return false;
            }
            deps->data = target_dep;
            deps = slist_next(deps);
        }
        target_list = slist_next(target_list);
    }
    
    return true;
}

/**
 * Replaces all the variable references in the specified string with their
 * values, using the specified project's property dictionary.
 */
char *resolve_variables(char *value, project_t *project)
{
    if (!value) {
        return NULL;
    }
    assert(project);
    
    size_t len = strlen(value);
    char last = '\0';
    bool in_variable = false;
    bool replaced = false;
    string_t *buf = NULL;
    string_t *var_name = NULL;
    
    for (size_t i = 0; i < len && !replaced; i++) {
        char c = value[i];
        
        if (in_variable) {
            if (c == '}') {
                char *replacement = hashtable_lookup(project->property_dict, var_name->str);
                if (replacement) {
                    string_append(buf, replacement);
                    if ((i + 1) < len) {
                        string_append(buf, value + i + 1);
                    }
                    value = string_free(buf, false);
                    buf = NULL;
                    replaced = true;
                }
                string_free(var_name, true);
                var_name = NULL;
                in_variable = false;
            } else {
                string_append_c(var_name, c);
            }
        } else {
            if (c == '{' && last == '$') {
                if (i == 1) {
                    buf = string_new(NULL);
                } else {
                    buf = string_new_len(value, i - 1);
                }
                var_name = string_new(NULL);
                in_variable = true;
            }
        }
        last = c;
    }
    
    /* Clean up if we didn't complete a replacement */
    if (buf && !replaced) {
        string_free(buf, true);
    }
    if (var_name) {
        string_free(var_name, true);
    }
    
    if (replaced) {
        /* There may be further replacements */
        return resolve_variables(value, project);
    }
    
    return value;
}
