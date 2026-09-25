/*
 * parser.c
 * Direct SAX-style parser for Ant build files.
 * 
 * Builds typed project/target/task structures directly without
 * intermediate DOM representation.
 *
 * Copyright (C) 2026 Chris Burdess <dog@gnu.org>
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
#include <expat.h>
#include <libgen.h>
#include <time.h>

/* ========================================================================
 * Parse State Machine
 * ======================================================================== */

typedef enum {
    PARSE_ROOT,           /* Before <project> */
    PARSE_PROJECT,        /* Inside <project> */
    PARSE_TARGET,         /* Inside <target> */
    PARSE_TASK,           /* Inside a task (building XML node tree) */
    PARSE_PATH,           /* Inside a top-level <path> */
    PARSE_FILESET,        /* Inside a top-level <fileset> */
    PARSE_SELECTOR,       /* Inside a top-level <selector> */
    PARSE_SKIP            /* Skipping ignored elements (description, etc.) */
} parse_state_t;

/* Stack entry for tracking nested parsing state */
typedef struct state_entry {
    parse_state_t state;
    void *data;                /* Current object (task, path, fileset, etc.) */
    struct state_entry *next;
} state_entry_t;

/* Parse context passed to callbacks */
typedef struct {
    project_t *project;
    char *filename;
    
    /* Current parsing state */
    parse_state_t state;
    int skip_depth;            /* Depth counter for skipping nested elements */
    
    /* Current objects being built */
    target_t *current_target;
    task_t *current_task;
    slist_t *current_path;     /* For path definitions */
    fileset_t *current_fileset;
    selector_t *current_selector;
    
    /* Selector stack for direct parsing (LIFO) */
    slist_t *selector_stack;       /* Stack of parent selectors */
    char *selector_id;             /* ID for top-level named selector */
    
    /* Fileset stack for direct parsing */
    slist_t *fileset_stack;        /* Stack of parent filesets */
    char *fileset_id;              /* ID for top-level named fileset */
    int fileset_depth;             /* Depth within fileset for nested children */
    
    /* Path stack for direct parsing */
    slist_t *path_stack;           /* Stack of parent paths */
    char *path_id;                 /* ID for top-level named path */
    slist_t *current_path_list;    /* For building path lists */
    int path_depth;                /* Depth within path for nested children */
    
    /* XML node building for tasks that need it */
    xml_node_t *current_xml_node;  /* Current node being built */
    int xml_node_depth;            /* Depth within task XML */
    bool direct_child_parsing;     /* true = process children directly, false = build XML */
    
    /* Stack for nested elements */
    state_entry_t *state_stack;
    
    /* List pointers for appending */
    slist_t *target_list_tail;
    slist_t *task_list_tail;
    slist_t *fileset_list_tail;
    slist_t *selector_list_tail;
    slist_t *path_list_tail;
    
    /* Text accumulator for elements that need it */
    string_t *text_buf;
    
    /* Error tracking */
    bool error;
    char error_msg[256];
} parse_context_t;

/* ========================================================================
 * State Stack Management
 * ======================================================================== */

static void push_state(parse_context_t *ctx, parse_state_t state, void *data)
{
    state_entry_t *entry = malloc(sizeof(state_entry_t));
    if (!entry) {
        return;
    }
    
    entry->state = ctx->state;
    entry->data = data;
    entry->next = ctx->state_stack;
    ctx->state_stack = entry;
    ctx->state = state;
}

static void pop_state(parse_context_t *ctx)
{
    if (!ctx->state_stack) {
        return;
    }
    
    state_entry_t *entry = ctx->state_stack;
    ctx->state = entry->state;
    ctx->state_stack = entry->next;
    free(entry);
}

/* ========================================================================
 * Attribute Helpers
 * ======================================================================== */

static const char *get_attr(const char **attrs, const char *name)
{
    if (!attrs || !name) {
        return NULL;
    }
    for (int i = 0; attrs[i]; i += 2) {
        if (strcmp(attrs[i], name) == 0) {
            return attrs[i + 1];
        }
    }
    return NULL;
}

static char *get_attr_dup(const char **attrs, const char *name)
{
    const char *val = get_attr(attrs, name);
    return val ? strdup(val) : NULL;
}

/* ========================================================================
 * Element Handlers for Project Level
 * ======================================================================== */

static void handle_project_start(parse_context_t *ctx, const char **attrs)
{
    project_t *project = ctx->project;
    
    const char *val = get_attr(attrs, "name");
    if (val) {
        project->name = strdup(val);
    }
    
    val = get_attr(attrs, "default");
    if (val) {
        project->default_target = strdup(val);
    }
    
    /* basedir is handled separately in parse_project() before parsing */
    
    ctx->state = PARSE_PROJECT;
}

static void handle_target_start(parse_context_t *ctx, const char **attrs)
{
    target_t *target = target_alloc(ctx->project);
    if (!target) {
        ctx->error = true;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Failed to allocate target");
        return;
    }
    
    /* Get attributes */
    target->name = get_attr_dup(attrs, "name");
    target->description = get_attr_dup(attrs, "description");
    target->if_condition = get_attr_dup(attrs, "if");
    target->unless_condition = get_attr_dup(attrs, "unless");
    
    /* Parse depends attribute */
    const char *depends = get_attr(attrs, "depends");
    if (depends) {
        char **tokens = str_split(depends, ",", -1);
        slist_t *dep_tail = NULL;
        for (int i = 0; tokens && tokens[i]; i++) {
            char *token = str_strip(tokens[i]);
            if (token[0]) {
                if (!dep_tail) {
                    target->depends_list = slist_new(strdup(token));
                    dep_tail = target->depends_list;
                } else {
                    dep_tail = slist_append(dep_tail, strdup(token));
                }
            }
        }
        str_freev(tokens);
    }
    
    /* Add to project */
    if (!ctx->target_list_tail) {
        ctx->project->target_list = slist_new(target);
        ctx->target_list_tail = ctx->project->target_list;
    } else {
        ctx->target_list_tail = slist_append(ctx->target_list_tail, target);
    }
    
    if (target->name) {
        hashtable_insert(ctx->project->target_dict, target->name, target);
    }
    
    ctx->current_target = target;
    ctx->task_list_tail = NULL;
    ctx->state = PARSE_TARGET;
}

static void handle_property_element(parse_context_t *ctx, const char **attrs)
{
    /* Process property directly without creating a task object */
    const char *name = get_attr(attrs, "name");
    const char *value = get_attr(attrs, "value");
    const char *location = get_attr(attrs, "location");
    const char *file = get_attr(attrs, "file");
    const char *environment = get_attr(attrs, "environment");
    
    project_t *project = ctx->project;
    
    if (name && value) {
        /* Simple name/value property */
        char *resolved_value = resolve_variables(strdup(value), project);
        if (!hashtable_lookup(project->property_dict, name)) {
            hashtable_insert(project->property_dict, strdup(name), resolved_value);
        } else {
            free(resolved_value);
        }
    }
    else if (name && location) {
        /* Location property - resolve to absolute path */
        char *resolved = resolve_variables(strdup(location), project);
        char *expanded = expand_location(project, resolved);
        if (!hashtable_lookup(project->property_dict, name)) {
            hashtable_insert(project->property_dict, strdup(name), expanded);
        } else {
            free(expanded);
        }
    }
    else if (file) {
        /* Load properties from file */
        char *resolved = resolve_variables(strdup(file), project);
        char *expanded = expand_location(project, resolved);
        const char *prefix = get_attr(attrs, "prefix");
        
        /* Read properties file */
        FILE *fp = fopen(expanded, "r");
        if (fp) {
            char line[4096];
            while (fgets(line, sizeof(line), fp)) {
                /* Skip comments and empty lines */
                char *trimmed = str_strip(line);
                if (!trimmed[0] || trimmed[0] == '#' || trimmed[0] == '!') {
                    continue;
                }
                
                /* Find = separator */
                char *eq = strchr(trimmed, '=');
                if (!eq) {
                    continue;
                }
                
                *eq = '\0';
                char *prop_name = str_strip(trimmed);
                char *prop_value = str_strip(eq + 1);
                
                /* Apply prefix if specified */
                char *full_name;
                if (prefix) {
                    full_name = str_concat(prefix, prop_name, NULL);
                } else {
                    full_name = strdup(prop_name);
                }
                
                if (!hashtable_lookup(project->property_dict, full_name)) {
                    char *resolved_value = resolve_variables(strdup(prop_value), project);
                    hashtable_insert(project->property_dict, full_name, resolved_value);
                } else {
                    free(full_name);
                }
            }
            fclose(fp);
        }
        free(expanded);
    }
    else if (environment) {
        /* Load environment variables with prefix */
        extern char **environ;
        for (char **env = environ; *env; env++) {
            char *eq = strchr(*env, '=');
            if (!eq) {
                continue;
            }
            
            size_t name_len = eq - *env;
            char *env_name = strndup(*env, name_len);
            char *env_value = strdup(eq + 1);
            
            char *full_name = str_concat(environment, env_name, NULL);
            
            if (!hashtable_lookup(project->property_dict, full_name)) {
                hashtable_insert(project->property_dict, full_name, env_value);
            } else {
                free(env_value);
            }
            
            free(full_name);
            free(env_name);
        }
    }
}

static void handle_path_definition_start(parse_context_t *ctx, const char *name, const char **attrs)
{
    (void)name;
    
    /* Store ID if present (for top-level named paths) */
    const char *id = get_attr(attrs, "id");
    ctx->path_id = id ? strdup(id) : NULL;
    
    /* Check for path attribute on the container */
    const char *path_attr = get_attr(attrs, "path");
    if (path_attr) {
        /* Create a simple path element from the attribute */
        path_t *path = path_alloc();
        if (path) {
            path->type = PATH_ELEMENT;
            path->data = strdup(path_attr);
            ctx->current_path_list = slist_new(path);
        } else {
            ctx->current_path_list = NULL;
        }
    } else {
        /* No path attribute - children will be added directly */
        ctx->current_path_list = NULL;
    }
    
    ctx->path_stack = NULL;  /* Start fresh stack */
    ctx->current_path = NULL;
    ctx->path_depth = 1;     /* Start at depth 1 (the <path> element) */
    
    push_state(ctx, PARSE_PATH, NULL);
}

static void handle_fileset_definition_start(parse_context_t *ctx, const char *name, const char **attrs)
{
    /* Create fileset directly - no XML node needed */
    fileset_t *fileset = fileset_create(name, attrs);
    if (!fileset) {
        return;
    }
    
    /* Store ID if present (for top-level named filesets) */
    const char *id = get_attr(attrs, "id");
    ctx->fileset_id = id ? strdup(id) : NULL;
    
    ctx->current_fileset = fileset;
    ctx->fileset_stack = NULL;  /* Start fresh stack */
    ctx->fileset_depth = 1;     /* Start at depth 1 */
    
    push_state(ctx, PARSE_FILESET, NULL);
}

static void handle_selector_definition_start(parse_context_t *ctx, const char *name, const char **attrs)
{
    /* Create selector directly - no XML node needed */
    selector_t *selector = selector_create(name, attrs);
    if (!selector) {
        return;
    }
    
    /* Store the ID if present (for top-level named selectors) */
    const char *id = get_attr(attrs, "id");
    ctx->selector_id = id ? strdup(id) : NULL;
    
    ctx->current_selector = selector;
    ctx->selector_stack = NULL;  /* Start fresh stack */
    
    push_state(ctx, PARSE_SELECTOR, NULL);
}

/* ========================================================================
 * Element Handlers for Target Level (Tasks)
 * ======================================================================== */

/* Check if a task needs XML node at runtime (for nested condition evaluation) */
static bool task_needs_xml_node(const char *name)
{
    return xml_streq(name, "condition");
}

static void handle_task_start(parse_context_t *ctx, const char *name, const char **attrs)
{
    /* Create task directly with name and attributes */
    task_t *task = task_create(ctx->current_target, name, attrs);
    if (!task) {
        ctx->error = true;
        return;
    }
    
    ctx->current_task = task;
    
    /* Check if this task needs XML node at runtime */
    bool needs_xml = task_needs_xml_node(name);
    
    if (needs_xml) {
        /* Create XML node for children - needed for runtime evaluation */
        xml_node_t *node = xml_node_new(name);
        if (!node) {
            task_free(task);
            ctx->error = true;
            return;
        }
        
        /* Copy attributes to XML node */
        for (int i = 0; attrs[i]; i += 2) {
            xml_node_add_attr(node, attrs[i], attrs[i + 1]);
        }
        
        ctx->current_xml_node = node;
        ctx->xml_node_depth = 1;
        ctx->direct_child_parsing = false;
        push_state(ctx, PARSE_TASK, node);
    } else {
        /* Direct parsing mode - process children directly */
        ctx->current_xml_node = NULL;
        ctx->xml_node_depth = 1;  /* Track nesting depth even without XML node */
        ctx->direct_child_parsing = true;
        push_state(ctx, PARSE_TASK, NULL);  /* No XML node stored */
    }
}

/* ========================================================================
 * Element Handlers for Task Children
 * ======================================================================== */

static void handle_task_child_start(parse_context_t *ctx, const char *name, const char **attrs)
{
    task_t *task = ctx->current_task;
    
    /* Direct parsing mode - process children without building XML nodes */
    if (ctx->direct_child_parsing) {
        
        /* Fileset/dirset children - they have their own depth tracking */
        if (xml_streq(name, FILESET) || xml_streq(name, DIRSET) ||
            xml_streq(name, "zipfileset") || xml_streq(name, "tarfileset")) {
            fileset_t *fileset = fileset_create(name, attrs);
            if (fileset && task) {
                task_add_fileset(task, fileset);
                ctx->current_fileset = fileset;
                ctx->fileset_depth = 1;
                /* Push state for fileset's children (include/exclude) */
                push_state(ctx, PARSE_FILESET, fileset);
            }
            return;
        }
        
        /* Path/classpath children - they have their own depth tracking */
        if (xml_streq(name, PATH) || xml_streq(name, CLASSPATH) ||
            xml_streq(name, "sourcepath") || xml_streq(name, "bootclasspath") ||
            xml_streq(name, "extdirs") || xml_streq(name, "modulepath") ||
            xml_streq(name, "upgrademodulepath")) {
            const char *refid = get_attr(attrs, "refid");
            if (refid && task) {
                /* Reference to existing path - lookup and add */
                slist_t *path_list = hashtable_lookup(task->target->project->path_dict, refid);
                if (path_list) {
                    /* Make a copy of the path name for the task */
                    hashtable_insert(task->path_dict, strdup(name), path_list);
                } else {
                    fprintf(stderr, "Warning: Unable to locate path: %s\n", refid);
                }
                /* Increment depth for this simple child */
                ctx->xml_node_depth++;
                return;
            }
            /* Inline path - start collecting path elements */
            ctx->current_path_list = NULL;
            ctx->path_stack = NULL;
            ctx->path_depth = 1;
            push_state(ctx, PARSE_PATH, strdup(name));  /* Store path name */
            return;
        }
        
        /* Arg/jvmarg/compilerarg children - simple, increment depth */
        if (xml_streq(name, ARG) || xml_streq(name, JVMARG) || 
            xml_streq(name, "compilerarg") || xml_streq(name, "sysproperty")) {
            const char *val = get_attr(attrs, "value");
            if (!val) {
                val = get_attr(attrs, "file");
            }
            if (!val) {
                val = get_attr(attrs, "path");
            }
            if (!val) {
                val = get_attr(attrs, "line");
            }
            
            if (val && task) {
                slist_t *arg_list = hashtable_lookup(task->attribute_dict, name);
                char *arg_value = strdup(val);
                
                /* Handle file attribute - expand to full path */
                if (get_attr(attrs, "file")) {
                    char *expanded = expand_location(task->target->project, arg_value);
                    free(arg_value);
                    arg_value = expanded;
                }
                /* Handle path attribute - convert separators */
                else if (get_attr(attrs, "path")) {
                    str_delimit(arg_value, ":;", PATH_SEPARATOR);
                }
                
                if (!arg_list) {
                    arg_list = slist_new(arg_value);
                    hashtable_insert(task->attribute_dict, strdup(name), arg_list);
                } else {
                    slist_append(slist_last(arg_list), arg_value);
                }
            }
            ctx->xml_node_depth++;
            return;
        }
        
        /* Include/exclude selectors - simple, increment depth */
        if (xml_streq(name, INCLUDE) || xml_streq(name, EXCLUDE) ||
            xml_streq(name, "includesfile") || xml_streq(name, "excludesfile")) {
            selector_t *selector = selector_create(name, attrs);
            if (selector) {
                if (ctx->current_fileset) {
                    /* Add to current fileset being built */
                    fileset_add_selector(ctx->current_fileset, selector);
                } else if (task && task->fileset_list) {
                    /* Add to first fileset in task */
                    fileset_t *fs = task->fileset_list->data;
                    if (fs) {
                        fileset_add_selector(fs, selector);
                    } else {
                        selector_free(selector);
                    }
                } else if (task) {
                    /* Create implicit fileset for the task using srcdir attribute if present */
                    const char *srcdir = hashtable_lookup(task->attribute_dict, "srcdir");
                    const char *dir_attrs[] = {"dir", srcdir, NULL};
                    fileset_t *fileset = fileset_create(task->name, srcdir ? dir_attrs : NULL);
                    if (fileset) {
                        /* Add default include pattern first */
                        selector_t *include_sel = selector_alloc();
                        if (include_sel) {
                            include_sel->type = SELECTOR_INCLUDE;
                            const char *pattern = xml_streq(task->name, "javac") ? 
                                                  "**/*.java" : "**";
                            hashtable_insert(include_sel->attribute_dict, 
                                           strdup("name"), strdup(pattern));
                            fileset_add_selector(fileset, include_sel);
                        }
                        /* Then add the user's selector */
                        fileset_add_selector(fileset, selector);
                        task_add_fileset(task, fileset);
                        ctx->current_fileset = fileset;
                    } else {
                        selector_free(selector);
                    }
                } else {
                    selector_free(selector);
                }
            }
            ctx->xml_node_depth++;
            return;
        }
        
        /* Compound selectors - they have their own depth tracking */
        if (xml_streq(name, "selector") || xml_streq(name, "or") ||
            xml_streq(name, "and") || xml_streq(name, "not") ||
            xml_streq(name, "filename")) {
            selector_t *selector = selector_create(name, attrs);
            if (selector) {
                ctx->current_selector = selector;
                ctx->selector_stack = NULL;
                push_state(ctx, PARSE_SELECTOR, ctx->current_fileset);  /* Store parent fileset */
            }
            return;
        }
        
        /* Unknown children in direct mode - skip them with depth tracking */
        ctx->xml_node_depth++;
        return;
    }
    
    /* XML node mode - build XML tree for task_init_children */
    ctx->xml_node_depth++;
    
    xml_node_t *node = xml_node_new(name);
    if (!node) {
        return;
    }
    
    /* Copy attributes */
    for (int i = 0; attrs[i]; i += 2) {
        xml_node_add_attr(node, attrs[i], attrs[i + 1]);
    }
    
    /* Add as child of current node */
    xml_node_add_child(ctx->current_xml_node, node);
    
    /* Make this the current node */
    ctx->current_xml_node = node;
}

/* ========================================================================
 * Main SAX Callbacks
 * ======================================================================== */

static void XMLCALL start_element(void *user_data, const char *name, const char **attrs)
{
    parse_context_t *ctx = user_data;

    if (ctx->error) {
        return;
    }

    /* Handle skip state */
    if (ctx->state == PARSE_SKIP) {
        ctx->skip_depth++;
        return;
    }

    char *normalized_name = xml_ns_normalize(name);
    name = normalized_name;

    switch (ctx->state) {
    case PARSE_ROOT:
        if (xml_streq(name, "project")) {
            handle_project_start(ctx, attrs);
        } else {
            /* Skip non-project elements at root level (e.g., processing instructions) */
            push_state(ctx, PARSE_SKIP, NULL);
            ctx->skip_depth = 1;
        }
        break;
        
    case PARSE_PROJECT:
        if (xml_streq(name, "target")) {
            handle_target_start(ctx, attrs);
        }
        else if (xml_streq(name, "property")) {
            handle_property_element(ctx, attrs);
            /* Property is self-contained, no state change */
        }
        else if (xml_streq(name, "path")) {
            handle_path_definition_start(ctx, name, attrs);
        }
        else if (xml_streq(name, "fileset")) {
            handle_fileset_definition_start(ctx, name, attrs);
        }
        else if (xml_streq(name, "selector")) {
            handle_selector_definition_start(ctx, name, attrs);
        }
        else if (xml_streq(name, "description")) {
            /* Skip description content */
            push_state(ctx, PARSE_SKIP, NULL);
            ctx->skip_depth = 1;
        }
        else {
            /* Unknown top-level element - could be a taskdef, macrodef, etc. */
            /* Skip for now */
            push_state(ctx, PARSE_SKIP, NULL);
            ctx->skip_depth = 1;
        }
        break;
        
    case PARSE_TARGET:
        handle_task_start(ctx, name, attrs);
        break;
        
    case PARSE_TASK:
        /* Building XML node tree for task - add children */
        handle_task_child_start(ctx, name, attrs);
        break;
        
    case PARSE_PATH:
        /* Create child path element directly */
        ctx->path_depth++;
        
        /* Handle include/exclude inside a fileset within a path */
        if (ctx->current_fileset && 
            (xml_streq(name, INCLUDE) || xml_streq(name, EXCLUDE) ||
             xml_streq(name, "includesfile") || xml_streq(name, "excludesfile"))) {
            selector_t *selector = selector_create(name, attrs);
            if (selector) {
                fileset_add_selector(ctx->current_fileset, selector);
            }
            break;
        }
        
        {
            path_t *child = path_create(name, attrs);
            if (child) {
                /* Add to current path list */
                if (!ctx->current_path_list) {
                    ctx->current_path_list = slist_new(child);
                } else {
                    slist_append(slist_last(ctx->current_path_list), child);
                }
                
                /* If this is an inline path, push current list to stack and start new list */
                if (child->type == PATH_INLINE) {
                    slist_t *stack_node = slist_new(ctx->current_path_list);
                    stack_node->next = ctx->path_stack;
                    ctx->path_stack = stack_node;
                    /* Children will be added to child->data */
                    ctx->current_path_list = NULL;
                    ctx->current_path = slist_last(stack_node->data);  /* Track the inline path */
                }
                
                /* If this is a fileset, handle nested selectors */
                if (child->type == PATH_FILESET) {
                    ctx->current_fileset = (fileset_t *)child->data;
                    /* Don't change state - stay in PARSE_PATH but track fileset */
                }
            }
        }
        break;
        
    case PARSE_FILESET:
        /* Create child selector directly */
        ctx->fileset_depth++;
        if (xml_streq(name, INCLUDE) || xml_streq(name, EXCLUDE) ||
            xml_streq(name, "includesfile") || xml_streq(name, "excludesfile") ||
            xml_streq(name, "or") || xml_streq(name, "and") || 
            xml_streq(name, "not") || xml_streq(name, "filename") ||
            xml_streq(name, "selector")) {
            selector_t *selector = selector_create(name, attrs);
            if (selector && ctx->current_fileset) {
                /* For compound selectors, need to track parent */
                if (selector->type == SELECTOR_OR || selector->type == SELECTOR_AND ||
                    selector->type == SELECTOR_NOT) {
                    /* Push to selector stack, change to PARSE_SELECTOR state */
                    ctx->current_selector = selector;
                    ctx->selector_stack = NULL;
                    push_state(ctx, PARSE_SELECTOR, ctx->current_fileset);
                } else {
                    /* Simple selector - add directly */
                    fileset_add_selector(ctx->current_fileset, selector);
                }
            }
        }
        break;
        
    case PARSE_SELECTOR:
        /* Create child selector directly */
        {
            selector_t *child = selector_create(name, attrs);
            if (child && ctx->current_selector) {
                /* Push current selector onto stack and make child the current */
                slist_t *node = slist_new(ctx->current_selector);
                node->next = ctx->selector_stack;
                ctx->selector_stack = node;
                ctx->current_selector = child;
            } else if (child) {
                selector_free(child);
            }
        }
        break;
        
    case PARSE_SKIP:
        ctx->skip_depth++;
        break;
    }

    free(normalized_name);
}

static void XMLCALL end_element(void *user_data, const char *name)
{
    parse_context_t *ctx = user_data;
    
    if (ctx->error) {
        return;
    }
    
    (void)name; /* Often unused */
    
    switch (ctx->state) {
    case PARSE_SKIP:
        ctx->skip_depth--;
        if (ctx->skip_depth == 0) {
            pop_state(ctx);
        }
        break;
        
    case PARSE_PROJECT:
        /* Only end project state if this is the </project> tag */
        if (xml_streq(name, "project")) {
            ctx->state = PARSE_ROOT;
        }
        /* Otherwise stay in PARSE_PROJECT (e.g., for </property>) */
        break;
        
    case PARSE_TARGET:
        ctx->current_target = NULL;
        ctx->task_list_tail = NULL;  /* Reset for next target */
        ctx->state = PARSE_PROJECT;
        break;
        
    case PARSE_TASK:
        ctx->xml_node_depth--;
        
        /* Handle closing of directly-processed children in direct mode */
        if (ctx->direct_child_parsing && ctx->xml_node_depth > 0) {
            /* Child element closed - handle specific types */
            if (xml_streq(name, FILESET) || xml_streq(name, DIRSET) ||
                xml_streq(name, "zipfileset") || xml_streq(name, "tarfileset")) {
                ctx->current_fileset = NULL;
                ctx->fileset_depth = 0;
            }
            /* Other children (args, includes, etc.) need no special cleanup */
            break;
        }
        
        if (ctx->xml_node_depth == 0) {
            /* Task element closed - add task to target */
            task_t *task = ctx->current_task;
            
            if (ctx->direct_child_parsing) {
                /* Direct parsing mode - children already processed */
                if (task) {
                    /* Handle buffered body text */
                    if (ctx->text_buf && ctx->text_buf->len > 0) {
                        /* Make a copy to strip - str_strip modifies in place */
                        char *body_copy = strdup(ctx->text_buf->str);
                        if (body_copy) {
                            char *body = str_strip(body_copy);
                            if (body && body[0]) {
                                task_set_text(task, strdup(body));
                            }
                            free(body_copy);
                        }
                    }
                    
                    /* Create implicit fileset for javac/jar if none provided */
                    if (!task->fileset_list &&
                        (xml_streq(task->name, "javac") || xml_streq(task->name, "jar"))) {
                    const char *srcdir = hashtable_lookup(task->attribute_dict, "srcdir");
                    if (!srcdir) {
                        srcdir = hashtable_lookup(task->attribute_dict, "basedir");
                    }
                        if (srcdir) {
                            const char *attrs[] = {"dir", srcdir, NULL};
                            fileset_t *fileset = fileset_create(task->name, attrs);
                            if (fileset) {
                                /* Check for includes attribute (overrides default pattern) */
                                const char *includes = hashtable_lookup(task->attribute_dict, "includes");
                                if (includes) {
                                    /* Parse comma-separated includes patterns */
                                    char *patterns = strdup(includes);
                                    char *pattern = strtok(patterns, ",");
                                    while (pattern) {
                                        pattern = str_strip(pattern);
                                        if (pattern[0]) {
                                            selector_t *selector = selector_alloc();
                                            if (selector) {
                                                selector->type = SELECTOR_INCLUDE;
                                                hashtable_insert(selector->attribute_dict, 
                                                               strdup("name"), strdup(pattern));
                                                fileset_add_selector(fileset, selector);
                                            }
                                        }
                                        pattern = strtok(NULL, ",");
                                    }
                                    free(patterns);
                                } else {
                                    /* Add default include selector */
                                    selector_t *selector = selector_alloc();
                                    if (selector) {
                                        selector->type = SELECTOR_INCLUDE;
                                        const char *pattern = xml_streq(task->name, "javac") ? 
                                                              "**/*.java" : "**";
                                        hashtable_insert(selector->attribute_dict, 
                                                       strdup("name"), strdup(pattern));
                                        fileset_add_selector(fileset, selector);
                                    }
                                }
                                
                                /* Check for excludes attribute */
                                const char *excludes = hashtable_lookup(task->attribute_dict, "excludes");
                                if (excludes) {
                                    char *patterns = strdup(excludes);
                                    char *pattern = strtok(patterns, ",");
                                    while (pattern) {
                                        pattern = str_strip(pattern);
                                        if (pattern[0]) {
                                            selector_t *selector = selector_alloc();
                                            if (selector) {
                                                selector->type = SELECTOR_EXCLUDE;
                                                hashtable_insert(selector->attribute_dict, 
                                                               strdup("name"), strdup(pattern));
                                                fileset_add_selector(fileset, selector);
                                            }
                                        }
                                        pattern = strtok(NULL, ",");
                                    }
                                    free(patterns);
                                }
                                
                                task_add_fileset(task, fileset);
                            }
                        }
                    }
                    
                    if (!ctx->task_list_tail) {
                        ctx->current_target->task_list = slist_new(task);
                        ctx->task_list_tail = ctx->current_target->task_list;
                    } else {
                        ctx->task_list_tail = slist_append(ctx->task_list_tail, task);
                    }
                }
                
                /* Clear text buffer */
                if (ctx->text_buf) {
                    string_free(ctx->text_buf, true);
                    ctx->text_buf = NULL;
                }
            } else {
                /* XML mode - process children using the XML node */
                xml_node_t *task_node = ctx->state_stack ? ctx->state_stack->data : NULL;
                
                if (task && task_node) {
                    /* Set body text if any */
                    char *body_text = xml_node_get_text(task_node);
                    if (body_text && body_text[0] != '\0') {
                        task_set_text(task, body_text);
                    }
                    free(body_text);
                    
                    /* Process children using the XML node */
                    /* NOTE: task_init_children stores node in task->xml_node, so don't free it */
                    if (task_init_children(task, task_node)) {
                        /* Add to target's task list */
                        if (!ctx->task_list_tail) {
                            ctx->current_target->task_list = slist_new(task);
                            ctx->task_list_tail = ctx->current_target->task_list;
                        } else {
                            ctx->task_list_tail = slist_append(ctx->task_list_tail, task);
                        }
                    } else {
                        task_free(task);
                        xml_node_free(task_node);  /* Only free on failure */
                    }
                } else if (task) {
                    /* No XML children - add task directly */
                    if (!ctx->task_list_tail) {
                        ctx->current_target->task_list = slist_new(task);
                        ctx->task_list_tail = ctx->current_target->task_list;
                    } else {
                        ctx->task_list_tail = slist_append(ctx->task_list_tail, task);
                    }
                }
            }
            
            ctx->current_xml_node = NULL;
            ctx->current_task = NULL;
            ctx->current_fileset = NULL;
            ctx->direct_child_parsing = false;
            pop_state(ctx);
        } else if (!ctx->direct_child_parsing) {
            /* XML mode - move back to parent node */
            ctx->current_xml_node = ctx->current_xml_node->parent;
        }
        break;
        
    case PARSE_PATH:
        ctx->path_depth--;
        if (ctx->path_depth == 0) {
            /* Path closed - check context */
            state_entry_t *prev = ctx->state_stack;
            char *path_name = prev ? prev->data : NULL;  /* Path name stored when pushing state */
            
            if (prev && prev->state == PARSE_TASK && ctx->current_task && ctx->current_path_list) {
                /* Came from task - add path to task */
                hashtable_insert(ctx->current_task->path_dict, 
                               path_name ? strdup(path_name) : strdup("classpath"), 
                               ctx->current_path_list);
                free(path_name);
                ctx->current_path_list = NULL;
            } else if (ctx->path_id && ctx->current_path_list) {
                /* Top-level path with ID - add to project dict */
                hashtable_insert(ctx->project->path_dict, ctx->path_id, ctx->current_path_list);
                ctx->path_id = NULL;
            } else if (ctx->current_path_list) {
                /* No ID and not from task - discard path list */
                slist_free_full(ctx->current_path_list, (void (*)(void *))path_free);
                free(path_name);
            }
            free(ctx->path_id);
            ctx->path_id = NULL;
            ctx->current_path_list = NULL;
            ctx->current_path = NULL;
            ctx->current_fileset = NULL;
            pop_state(ctx);
        } else if (ctx->path_stack) {
            /* Nested inline path closed - set children and restore parent list */
            slist_t *stack_node = ctx->path_stack;
            slist_t *parent_list = stack_node->data;
            ctx->path_stack = stack_node->next;
            free(stack_node);
            
            /* Get the inline path (last element of parent list) */
            if (ctx->current_path && ctx->current_path->data) {
                path_t *inline_path = ctx->current_path->data;
                if (inline_path->type == PATH_INLINE) {
                    /* Set the children we collected */
                    inline_path->data = ctx->current_path_list;
                }
            }
            
            /* Restore parent list */
            ctx->current_path_list = parent_list;
            ctx->current_path = NULL;
            ctx->current_fileset = NULL;
        }
        /* else: just a child element closing (like fileset), nothing to do */
        break;
        
    case PARSE_FILESET:
        ctx->fileset_depth--;
        if (ctx->fileset_depth == 0) {
            /* Fileset closed - check context */
            state_entry_t *prev = ctx->state_stack;
            
            if (ctx->fileset_id && ctx->current_fileset) {
                /* Top-level named fileset - add to project dict */
                hashtable_insert(ctx->project->fileset_dict, ctx->fileset_id, ctx->current_fileset);
                ctx->fileset_id = NULL;
            } else if (prev && prev->state == PARSE_TASK) {
                /* Came from task - fileset already added to task, don't free */
                /* Note: we did NOT increment xml_node_depth when pushing PARSE_FILESET,
                   so we don't need to adjust it here */
            } else if (ctx->current_fileset) {
                /* Orphan fileset - free it */
                fileset_free(ctx->current_fileset);
            }
            free(ctx->fileset_id);
            ctx->fileset_id = NULL;
            ctx->current_fileset = NULL;
            pop_state(ctx);
        }
        /* else: just a child element closing (like include), nothing to do */
        break;
        
    case PARSE_SELECTOR:
        if (!ctx->selector_stack) {
            /* Top-level selector in its context closed */
            /* Check what state we came from (stored in state_stack BEFORE pop) */
            state_entry_t *prev = ctx->state_stack;
            fileset_t *parent_fileset = NULL;
            
            /* Data stored during push can be a fileset pointer (from PARSE_TASK) */
            if (prev && prev->data) {
                parent_fileset = prev->data;
            }
            
            if (ctx->selector_id && ctx->current_selector) {
                /* Named selector - add to project dict */
                hashtable_insert(ctx->project->selector_dict, ctx->selector_id, ctx->current_selector);
                ctx->selector_id = NULL;
            } else if (parent_fileset && ctx->current_selector) {
                /* Came from fileset - add selector to fileset */
                fileset_add_selector(parent_fileset, ctx->current_selector);
            } else if (ctx->current_task && ctx->current_selector) {
                /* Came from task - add to task's selector list */
                task_add_selector(ctx->current_task, ctx->current_selector);
            } else if (ctx->current_selector) {
                selector_free(ctx->current_selector);
            }
            
            free(ctx->selector_id);
            ctx->selector_id = NULL;
            ctx->current_selector = NULL;
            pop_state(ctx);
        } else {
            /* Nested selector closed - add as child of parent */
            selector_t *child = ctx->current_selector;
            
            /* Pop parent from selector stack */
            slist_t *stack_node = ctx->selector_stack;
            selector_t *parent = stack_node->data;
            ctx->selector_stack = stack_node->next;
            free(stack_node);
            
            /* Add child to parent */
            selector_add_child(parent, child);
            ctx->current_selector = parent;
        }
        break;
        
    case PARSE_ROOT:
        /* Shouldn't happen */
        break;
    }
}

static void XMLCALL char_data(void *user_data, const char *data, int len)
{
    parse_context_t *ctx = user_data;
    
    /* Handle task body text */
    if (ctx->state == PARSE_TASK) {
        if (ctx->current_xml_node) {
            /* XML mode - add to XML node */
            xml_node_append_text(ctx->current_xml_node, data, len);
        } else if (ctx->direct_child_parsing && ctx->xml_node_depth == 1) {
            /* Direct mode at task level (not in child) - buffer text for task */
            if (!ctx->text_buf) {
                ctx->text_buf = string_new(NULL);
            }
            if (ctx->text_buf) {
                string_append_len(ctx->text_buf, data, len);
            }
        }
    }
    
    /* Also handle text_buf if needed for other contexts */
    else if (ctx->text_buf) {
        string_append_len(ctx->text_buf, data, len);
    }
}

/* ========================================================================
 * Main Parse Function
 * ======================================================================== */

/* Profiling stats for parsing */
static double total_parse_time = 0;
static int parse_profile_enabled = -1;

static double parse_get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

__attribute__((destructor))
static void print_parse_stats(void) {
    if (parse_profile_enabled == 1 && total_parse_time > 0) {
        fprintf(stderr, "\n=== Parse Profiling ===\n");
        fprintf(stderr, "  Total parse time: %.2f ms\n", total_parse_time);
    }
}

/**
 * Parse a build file directly into a project structure.
 * 
 * @param filename     Path to the build.xml file
 * @param cmd_props    Command-line properties to apply (can be NULL)
 * @return             Parsed project, or NULL on error
 */
project_t *parse_project(const char *filename, slist_t *cmd_props)
{
    double start_time = 0;
    if (parse_profile_enabled < 0) {
        parse_profile_enabled = (getenv("GANTT_PROFILE") != NULL) ? 1 : 0;
    }
    if (parse_profile_enabled) {
        start_time = parse_get_time_ms();
    }
    
    if (!filename) {
        return NULL;
    }
    
    /* Read file contents */
    size_t length;
    char *contents = file_get_contents(filename, &length);
    if (!contents) {
        fprintf(stderr, "Unable to read file: %s\n", filename);
        return NULL;
    }
    
    /* Create project */
    project_t *project = project_alloc();
    if (!project) {
        free(contents);
        return NULL;
    }
    
    project->filename = strdup(filename);
    
    /* Apply command-line properties first (they take precedence) */
    slist_t *props_ptr = cmd_props;
    while (props_ptr) {
        char *data = (char *)props_ptr->data;
        char **values = str_split(data, "=", 2);
        if (values && values[0]) {
            hashtable_insert(project->property_dict, strdup(values[0]), 
                           values[1] ? strdup(values[1]) : strdup(""));
        }
        str_freev(values);
        props_ptr = slist_next(props_ptr);
    }
    
    /* Set up parse context */
    parse_context_t ctx = {
        .project = project,
        .filename = strdup(filename),
        .state = PARSE_ROOT,
        .skip_depth = 0,
        .current_target = NULL,
        .current_task = NULL,
        .current_path = NULL,
        .current_fileset = NULL,
        .current_selector = NULL,
        .selector_stack = NULL,
        .selector_id = NULL,
        .fileset_stack = NULL,
        .fileset_id = NULL,
        .fileset_depth = 0,
        .path_stack = NULL,
        .path_id = NULL,
        .current_path_list = NULL,
        .path_depth = 0,
        .current_xml_node = NULL,
        .xml_node_depth = 0,
        .direct_child_parsing = false,
        .state_stack = NULL,
        .target_list_tail = NULL,
        .task_list_tail = NULL,
        .fileset_list_tail = NULL,
        .selector_list_tail = NULL,
        .path_list_tail = NULL,
        .text_buf = NULL,
        .error = false
    };
    
    /* Create parser */
    XML_Parser parser = XML_ParserCreateNS(NULL, XML_NS_SEP);
    if (!parser) {
        free(contents);
        free(ctx.filename);
        project_free(project);
        return NULL;
    }
    
    XML_SetUserData(parser, &ctx);
    XML_SetElementHandler(parser, start_element, end_element);
    XML_SetCharacterDataHandler(parser, char_data);
    
    /* Do a first pass to get basedir from project element */
    /* We need basedir set before parsing properties that might reference it */
    {
        char *filename_copy = strdup(filename);
        char *dir = dirname(filename_copy);
        
        /* Quick scan for basedir attribute */
        char *basedir_value = NULL;
        char *project_start = strstr(contents, "<project");
        if (project_start) {
            char *basedir_attr = strstr(project_start, "basedir");
            if (basedir_attr && basedir_attr < strchr(project_start, '>')) {
                /* Extract basedir value */
                char *eq = strchr(basedir_attr, '=');
                if (eq) {
                    char *quote = strchr(eq, '"');
                    if (!quote) {
                        quote = strchr(eq, '\'');
                    }
                    if (quote) {
                        char quote_char = *quote;
                        char *end = strchr(quote + 1, quote_char);
                        if (end) {
                            basedir_value = strndup(quote + 1, end - quote - 1);
                        }
                    }
                }
            }
        }
        
        char *resolved_basedir;
        if (basedir_value && basedir_value[0]) {
            if (basedir_value[0] == '/') {
                resolved_basedir = basedir_value;
            } else if (strcmp(basedir_value, ".") == 0) {
                free(basedir_value);
                resolved_basedir = (dir && dir[0] && strcmp(dir, ".") != 0) 
                                  ? strdup(dir) : get_current_dir();
            } else {
                resolved_basedir = str_concat(dir, "/", basedir_value, NULL);
                free(basedir_value);
            }
        } else {
            resolved_basedir = (dir && dir[0] && strcmp(dir, ".") != 0) 
                              ? strdup(dir) : get_current_dir();
        }
        
        /* Normalize path */
        char *real_basedir = realpath(resolved_basedir, NULL);
        if (real_basedir) {
            free(resolved_basedir);
            resolved_basedir = real_basedir;
        }
        
        hashtable_insert(project->property_dict, strdup("basedir"), resolved_basedir);
        project->base_dir = strdup(resolved_basedir);
        
        /* Set standard built-in properties */
        char *home = getenv("HOME");
        if (home) {
            hashtable_insert(project->property_dict, strdup("user.home"), strdup(home));
        }
        
        char *cwd = get_current_dir();
        if (cwd) {
            hashtable_insert(project->property_dict, strdup("user.dir"), cwd);
        }
        
        char *user = getenv("USER");
        if (user) {
            hashtable_insert(project->property_dict, strdup("user.name"), strdup(user));
        }
        
        free(filename_copy);
    }
    
    /* Parse */
    if (XML_Parse(parser, contents, length, XML_TRUE) == XML_STATUS_ERROR) {
        fprintf(stderr, "XML parse error at line %lu: %s\n",
                XML_GetCurrentLineNumber(parser),
                XML_ErrorString(XML_GetErrorCode(parser)));
        XML_ParserFree(parser);
        free(contents);
        free(ctx.filename);
        project_free(project);
        return NULL;
    }
    
    XML_ParserFree(parser);
    free(contents);
    free(ctx.filename);
    
    /* Check for parse errors */
    if (ctx.error) {
        fprintf(stderr, "Parse error: %s\n", ctx.error_msg);
        project_free(project);
        return NULL;
    }
    
    /* Resolve target dependencies */
    slist_t *target_list = project->target_list;
    while (target_list) {
        target_t *target = target_list->data;
        slist_t *deps = target->depends_list;
        while (deps) {
            char *dep_name = deps->data;
            target_t *dep_target = hashtable_lookup(project->target_dict, dep_name);
            if (!dep_target) {
                fprintf(stderr, "\nBUILD FAILED\n");
                fprintf(stderr, "Target `%s' does not exist. Used from target `%s'.\n",
                       dep_name, target->name);
                project_free(project);
                return NULL;
            }
            /* Replace name string with pointer to target */
            free(dep_name);
            deps->data = dep_target;
            deps = slist_next(deps);
        }
        target_list = slist_next(target_list);
    }
    
    /* Clean up state stack */
    while (ctx.state_stack) {
        state_entry_t *entry = ctx.state_stack;
        ctx.state_stack = entry->next;
        free(entry->data);
        free(entry);
    }
    
    if (parse_profile_enabled) {
        total_parse_time = parse_get_time_ms() - start_time;
    }
    
    return project;
}

