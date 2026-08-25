/*
 * task.c
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

static hashtable_t *invoke_dict = NULL;

/**
 * Create a new task.
 */
task_t *task_alloc(target_t *target)
{
    task_t *task = malloc(sizeof(task_t));
    if (!task) {
        return NULL;
    }
    
    task->target = target;
    task->name = NULL;
    task->attribute_list = NULL;
    task->attribute_dict = hashtable_new();
    task->fileset_list = NULL;
    task->path_dict = hashtable_new();
    task->nested_tasks = NULL;
    task->selector_list = NULL;
    task->xml_node = NULL;
    
    return task;
}

/**
 * Destroy a task.
 */
void task_free(task_t *task)
{
    if (!task) {
        return;
    }
    
    free(task->name);
    slist_free(task->attribute_list);
    hashtable_free(task->attribute_dict);
    
    /* Free filesets */
    slist_t *list = task->fileset_list;
    while (list) {
        fileset_free((fileset_t *)list->data);
        list = slist_next(list);
    }
    slist_free(task->fileset_list);
    
    /* Free nested tasks */
    list = task->nested_tasks;
    while (list) {
        task_free((task_t *)list->data);
        list = slist_next(list);
    }
    slist_free(task->nested_tasks);
    
    /* Free selectors */
    list = task->selector_list;
    while (list) {
        selector_free((selector_t *)list->data);
        list = slist_next(list);
    }
    slist_free(task->selector_list);
    
    hashtable_free(task->path_dict);
    
    /* Free XML node if present (created by parser for child processing) */
    if (task->xml_node) {
        xml_node_free(task->xml_node);
    }
    
    free(task);
}

/**
 * Create a task directly from element name and attributes (SAX-style).
 * This is used by the direct parser to avoid building XML node trees.
 * 
 * @param target Parent target
 * @param name Element name
 * @param attrs Expat-style attribute array [name1, value1, name2, value2, ..., NULL]
 * @return New task_t structure with name and attributes populated
 */
task_t *task_create(target_t *target, const char *name, const char **attrs)
{
    task_t *task = task_alloc(target);
    if (!task) {
        return NULL;
    }
    
    task->name = strdup(name);
    
    /* Copy attributes */
    slist_t *list_ptr = NULL;
    for (int i = 0; attrs && attrs[i]; i += 2) {
        char *attr_name = strdup(attrs[i]);
        char *attr_value = attrs[i + 1] ? strdup(attrs[i + 1]) : NULL;
        
        /* Add name to list and name->value to dict */
        if (!list_ptr) {
            task->attribute_list = slist_new(attr_name);
            list_ptr = task->attribute_list;
        } else {
            list_ptr = slist_append(list_ptr, attr_name);
        }
        hashtable_insert(task->attribute_dict, attr_name, attr_value);
    }
    
    return task;
}

/**
 * Set body text for a task (for tasks like echo that can have inline text).
 * This supports <echo>text content</echo> as well as <echo message="..."/>
 */
void task_set_text(task_t *task, const char *text)
{
    if (!task || !text || text[0] == '\0') {
        return;
    }
    
    /* Store as "message" attribute if no "message" or "text" is present */
    if (!hashtable_lookup(task->attribute_dict, "message") &&
        !hashtable_lookup(task->attribute_dict, "text")) {
        char *name = strdup("message");
        if (!task->attribute_list) {
            task->attribute_list = slist_new(name);
        } else {
            slist_append(slist_last(task->attribute_list), name);
        }
        hashtable_insert(task->attribute_dict, name, strdup(text));
    }
}

/**
 * Add a fileset to a task.
 */
void task_add_fileset(task_t *task, fileset_t *fileset)
{
    if (!task || !fileset) {
        return;
    }
    
    if (!task->fileset_list) {
        task->fileset_list = slist_new(fileset);
    } else {
        slist_append(slist_last(task->fileset_list), fileset);
    }
}

/**
 * Add a path to a task.
 */
void task_add_path(task_t *task, const char *name, slist_t *path_list)
{
    if (!task || !name || !path_list) {
        return;
    }
    hashtable_insert(task->path_dict, strdup(name), path_list);
}

/**
 * Add a selector to a task.
 */
void task_add_selector(task_t *task, selector_t *selector)
{
    if (!task || !selector) {
        return;
    }
    
    if (!task->selector_list) {
        task->selector_list = slist_new(selector);
    } else {
        slist_append(slist_last(task->selector_list), selector);
    }
}

/**
 * Add a nested task (for parallel/sequential).
 */
void task_add_nested(task_t *task, task_t *nested)
{
    if (!task || !nested) {
        return;
    }
    
    if (!task->nested_tasks) {
        task->nested_tasks = slist_new(nested);
    } else {
        slist_append(slist_last(task->nested_tasks), nested);
    }
}

/**
 * Process children of a task from an XML node.
 * This handles filesets, paths, args, selectors, nested tasks, and manifest entries.
 */
bool task_init_children(task_t *task, xml_node_t *node)
{
    const char *val;
    char *name;
    fileset_t *fileset;
    slist_t *path_list;
    
    if (!node) {
        return true;  /* No children to process */
    }
    
    /* Store reference to XML node for complex tasks like condition */
    task->xml_node = node;
    
    /* Process children */
    xml_node_t *cur = node->children;
    while (cur) {
        /* Tasks may act as implicit filesets */
        if (xml_streq(cur->name, INCLUDE) ||
            xml_streq(cur->name, EXCLUDE) ||
            xml_streq(cur->name, INCLUDES_FILE) ||
            xml_streq(cur->name, EXCLUDES_FILE)) {
            /* Locate or create the default fileset for the task */
            if (!task->fileset_list) {
                fileset = fileset_alloc();
                fileset_init(fileset, node);
                task->fileset_list = slist_new(fileset);
            }
        }
        else if (xml_streq(cur->name, FILESET)) {
            /* Nested <fileset> element */
            fileset = fileset_alloc();
            if (fileset && fileset_init(fileset, cur)) {
                task_add_fileset(task, fileset);
            } else {
                fileset_free(fileset);
            }
        }
        else if (xml_streq(cur->name, PATH) ||
                 xml_streq(cur->name, CLASSPATH)) {
            /* Path-like structures */
            val = xml_node_get_attr(cur, REFID);
            if (val) {
                /* Locate the existing path in the project */
                path_list = hashtable_lookup(task->target->project->path_dict, val);
                if (!path_list) {
                    fprintf(stderr, "Unable to locate path structure: %s\n", val);
                    return false;
                }
                task_add_path(task, cur->name, path_list);
            } else {
                path_list = path_list_init(cur);
                if (path_list) {
                    /* Save in the task path dict */
                    task_add_path(task, cur->name, path_list);
                    
                    /* If identified by an ID, save in the project path dict */
                    val = xml_node_get_attr(cur, ID);
                    if (val) {
                        name = strdup(val);
                        hashtable_insert(task->target->project->path_dict, name, path_list);
                    }
                }
            }
        }
        else if (xml_streq(cur->name, ARG) ||
                 xml_streq(cur->name, JVMARG) ||
                 xml_streq(cur->name, "compilerarg")) {
            /* Nested <arg>, <jvmarg>, or <compilerarg> element */
            slist_t *arg_list = hashtable_lookup(task->attribute_dict, cur->name);
            val = xml_node_get_attr(cur, "value");
            
            if (!val) {
                val = xml_node_get_attr(cur, "file");
                if (val) {
                    char *expanded = expand_location(task->target->project, strdup(val));
                    val = expanded;
                } else {
                    val = xml_node_get_attr(cur, "path");
                    if (val) {
                        /* Replace : or ; with path separator */
                        char *path_copy = strdup(val);
                        str_delimit(path_copy, ":;", PATH_SEPARATOR);
                        val = path_copy;
                    } else {
                        val = xml_node_get_attr(cur, "line");
                    }
                }
            }
            
            if (val) {
                if (!arg_list) {
                    arg_list = slist_new(strdup(val));
                    hashtable_insert(task->attribute_dict, strdup(cur->name), arg_list);
                } else {
                    slist_append(slist_last(arg_list), strdup(val));
                }
            }
        }
        else if (xml_streq(cur->name, "selector")) {
            /* Nested selector - parse and add to task */
            selector_t *selector = selector_parse(cur, task->target->project);
            if (selector) {
                task_add_selector(task, selector);
            }
        }
        else if (xml_streq(cur->name, MANIFEST)) {
            /* Nested <manifest> element for jar task
             * Contains <attribute name="..." value="..."/> children
             * Store as list of "name: value" strings
             */
            slist_t *manifest_list = hashtable_lookup(task->attribute_dict, MANIFEST_ATTR);
            
            /* Process <attribute> children */
            xml_node_t *attr_node = cur->children;
            while (attr_node) {
                if (xml_streq(attr_node->name, "attribute")) {
                    const char *attr_name = xml_node_get_attr(attr_node, "name");
                    const char *attr_value = xml_node_get_attr(attr_node, "value");
                    
                    if (attr_name && attr_value) {
                        /* Format: "Name: Value" */
                        char *entry = str_concat(attr_name, ": ", attr_value, NULL);
                        if (!manifest_list) {
                            manifest_list = slist_new(entry);
                            hashtable_insert(task->attribute_dict, strdup(MANIFEST_ATTR), manifest_list);
                        } else {
                            slist_append(slist_last(manifest_list), entry);
                        }
                    }
                }
                else if (xml_streq(attr_node->name, "section")) {
                    /* Named section: Name: section-name */
                    const char *section_name = xml_node_get_attr(attr_node, "name");
                    if (section_name) {
                        char *section_header = str_concat("\nName: ", section_name, NULL);
                        if (!manifest_list) {
                            manifest_list = slist_new(section_header);
                            hashtable_insert(task->attribute_dict, strdup(MANIFEST_ATTR), manifest_list);
                        } else {
                            slist_append(slist_last(manifest_list), section_header);
                        }
                        
                        /* Process attributes within section */
                        xml_node_t *sect_attr = attr_node->children;
                        while (sect_attr) {
                            if (xml_streq(sect_attr->name, "attribute")) {
                                const char *attr_name = xml_node_get_attr(sect_attr, "name");
                                const char *attr_value = xml_node_get_attr(sect_attr, "value");
                                if (attr_name && attr_value) {
                                    char *entry = str_concat(attr_name, ": ", attr_value, NULL);
                                    slist_append(slist_last(manifest_list), entry);
                                }
                            }
                            sect_attr = sect_attr->next;
                        }
                    }
                }
                attr_node = attr_node->next;
            }
        }
        /* Handle nested tasks for parallel/sequential */
        else if (xml_streq(task->name, PARALLEL) || 
                 xml_streq(task->name, SEQUENTIAL)) {
            /* Any unrecognized child element is a nested task */
            task_t *nested = task_alloc(task->target);
            if (nested && task_init(nested, cur)) {
                task_add_nested(task, nested);
            } else {
                task_free(nested);
            }
        }
        
        cur = cur->next;
    }
    
    /* Tasks that are implicit filesets */
    if (!task->fileset_list) {
        if (xml_streq(task->name, JAVAC) ||
            xml_streq(task->name, JAR)) {
            fileset = fileset_alloc();
            fileset_init(fileset, node);
            task->fileset_list = slist_new(fileset);
        }
    }
    
    return true;
}

/**
 * Initialise a task from an XML node (legacy interface).
 * This function populates all task fields from the XML node.
 */
bool task_init(task_t *task, xml_node_t *node)
{
    char *name;
    char *value;
    slist_t *list_ptr = NULL;
    
    /* Name */
    task->name = strdup(node->name);
    
    /* Get attributes */
    for (xml_attr_t *attr = node->attrs; attr; attr = attr->next) {
        name = strdup(attr->name);
        value = attr->value ? strdup(attr->value) : NULL;
        
        /* Add name to list and name->value to dict */
        if (!list_ptr) {
            task->attribute_list = slist_new(name);
            list_ptr = task->attribute_list;
        } else {
            list_ptr = slist_append(list_ptr, name);
        }
        hashtable_insert(task->attribute_dict, name, value);
    }
    
    /* Handle body text content - for tasks like echo that can have inline text */
    char *body_text = xml_node_get_text(node);
    if (body_text && body_text[0] != '\0') {
        task_set_text(task, body_text);
    }
    free(body_text);
    
    /* Process children */
    return task_init_children(task, node);
}

/**
 * Build command-line arguments for a fallback system command.
 * Converts task attributes to the format expected by standard Unix commands.
 *
 * @param task     The task to build arguments for
 * @param project  The project (for variable resolution)
 * @param argc     Output: number of arguments
 * @return         NULL-terminated argument array, or NULL on failure
 */
static char **build_fallback_argv(task_t *task, project_t *project, unsigned int *argc)
{
    char **argv = NULL;
    char *value;
    
    /* Each task has its own argument convention */
    if (strcmp(task->name, "mkdir") == 0) {
        /* mkdir <dir> */
        value = hashtable_lookup(task->attribute_dict, "dir");
        if (value) {
            value = resolve_variables(value, project);
            argv = malloc(sizeof(char *) * 4);
            argv[0] = NULL;  /* Will be filled with executable */
            argv[1] = strdup("-p");  /* Create parent dirs */
            argv[2] = strdup(value);
            argv[3] = NULL;
            *argc = 3;
        }
    } else if (strcmp(task->name, "delete") == 0) {
        /* rm -rf <dir> or rm <file> */
        value = hashtable_lookup(task->attribute_dict, "dir");
        if (!value) {
            value = hashtable_lookup(task->attribute_dict, "file");
        }
        if (value) {
            value = resolve_variables(value, project);
            argv = malloc(sizeof(char *) * 4);
            argv[0] = NULL;
            argv[1] = strdup("-rf");
            argv[2] = strdup(value);
            argv[3] = NULL;
            *argc = 3;
        }
    } else if (strcmp(task->name, "copy") == 0) {
        /* cp <file> <todir>/<basename> or cp <file> <tofile> */
        char *file = hashtable_lookup(task->attribute_dict, "file");
        char *todir = hashtable_lookup(task->attribute_dict, "todir");
        char *tofile = hashtable_lookup(task->attribute_dict, "tofile");
        
        if (file) {
            file = resolve_variables(strdup(file), project);
            if (tofile) {
                tofile = resolve_variables(strdup(tofile), project);
                argv = malloc(sizeof(char *) * 4);
                argv[0] = NULL;
                argv[1] = file;
                argv[2] = tofile;
                argv[3] = NULL;
                *argc = 3;
            } else if (todir) {
                todir = resolve_variables(strdup(todir), project);
                argv = malloc(sizeof(char *) * 4);
                argv[0] = NULL;
                argv[1] = file;
                argv[2] = todir;
                argv[3] = NULL;
                *argc = 3;
            }
        }
    } else if (strcmp(task->name, "move") == 0) {
        /* mv <file> <todir> or mv <file> <tofile> */
        char *file = hashtable_lookup(task->attribute_dict, "file");
        char *todir = hashtable_lookup(task->attribute_dict, "todir");
        char *tofile = hashtable_lookup(task->attribute_dict, "tofile");
        
        if (file) {
            file = resolve_variables(strdup(file), project);
            if (tofile) {
                tofile = resolve_variables(strdup(tofile), project);
                argv = malloc(sizeof(char *) * 4);
                argv[0] = NULL;
                argv[1] = file;
                argv[2] = tofile;
                argv[3] = NULL;
                *argc = 3;
            } else if (todir) {
                todir = resolve_variables(strdup(todir), project);
                argv = malloc(sizeof(char *) * 4);
                argv[0] = NULL;
                argv[1] = file;
                argv[2] = todir;
                argv[3] = NULL;
                *argc = 3;
            }
        }
    } else if (strcmp(task->name, "touch") == 0) {
        /* touch <file> */
        value = hashtable_lookup(task->attribute_dict, "file");
        if (value) {
            value = resolve_variables(value, project);
            argv = malloc(sizeof(char *) * 3);
            argv[0] = NULL;
            argv[1] = strdup(value);
            argv[2] = NULL;
            *argc = 2;
        }
    } else if (strcmp(task->name, "chmod") == 0) {
        /* chmod <perm> <file> */
        char *perm = hashtable_lookup(task->attribute_dict, "perm");
        char *file = hashtable_lookup(task->attribute_dict, "file");
        if (perm && file) {
            perm = resolve_variables(strdup(perm), project);
            file = resolve_variables(strdup(file), project);
            argv = malloc(sizeof(char *) * 4);
            argv[0] = NULL;
            argv[1] = perm;
            argv[2] = file;
            argv[3] = NULL;
            *argc = 3;
        }
    } else if (strcmp(task->name, "echo") == 0) {
        /* echo <message> */
        value = hashtable_lookup(task->attribute_dict, "message");
        if (value) {
            value = resolve_variables(value, project);
            argv = malloc(sizeof(char *) * 3);
            argv[0] = NULL;
            argv[1] = strdup(value);
            argv[2] = NULL;
            *argc = 2;
        }
    }
    
    return argv;
}

/**
 * Execute a task.
 */
bool task_invoke(task_t *task, project_t *project)
{
    unsigned int argc, envc;
    char *executable;
    char *dir;
    char **argv;
    char **envp;
    slist_t *list;
    char *name;
    char *value;
    int i;
    bool ret;
    task_invoke_fn f;
    bool is_gantt_executable;
    
    /* Built-in tasks */
    if (!invoke_dict) {
        /* Populate invoke function hashtable */
        invoke_dict = hashtable_new();
        hashtable_insert(invoke_dict, JAVAC, javac_invoke);
        hashtable_insert(invoke_dict, JAR, jar_invoke);
        hashtable_insert(invoke_dict, JAVA, java_invoke);
        hashtable_insert(invoke_dict, JAVADOC, javadoc_invoke);
        hashtable_insert(invoke_dict, TSTAMP, tstamp_invoke);
        hashtable_insert(invoke_dict, PROPERTY, property_invoke);
        hashtable_insert(invoke_dict, EXEC, exec_invoke);
        hashtable_insert(invoke_dict, AVAILABLE, available_invoke);
        hashtable_insert(invoke_dict, GET, get_invoke);
        hashtable_insert(invoke_dict, LOADFILE, loadfile_invoke);
        hashtable_insert(invoke_dict, TEMPFILE, tempfile_invoke);
        hashtable_insert(invoke_dict, LENGTH, length_invoke);
        hashtable_insert(invoke_dict, UPTODATE, uptodate_invoke);
        hashtable_insert(invoke_dict, CONDITION, condition_invoke);
        hashtable_insert(invoke_dict, ANTCALL, antcall_invoke);
        hashtable_insert(invoke_dict, INPUT, input_invoke);
        hashtable_insert(invoke_dict, PATHCONVERT, pathconvert_invoke);
        hashtable_insert(invoke_dict, SEQUENTIAL, sequential_invoke);
        hashtable_insert(invoke_dict, PARALLEL, parallel_invoke);
        hashtable_insert(invoke_dict, XMLPROPERTY, xmlproperty_invoke);
        hashtable_insert(invoke_dict, APPLY, apply_invoke);
        hashtable_insert(invoke_dict, "execon", apply_invoke);  /* alias */
        hashtable_insert(invoke_dict, BASENAME, basename_invoke);
        hashtable_insert(invoke_dict, DIRNAME, dirname_invoke);
        hashtable_insert(invoke_dict, FAIL, fail_invoke);
        hashtable_insert(invoke_dict, "condition", condition_invoke);
        hashtable_insert(invoke_dict, "local", local_invoke);
        hashtable_insert(invoke_dict, "native2ascii", native2ascii_invoke);
        hashtable_insert(invoke_dict, "ivy:resolve", ivy_resolve_invoke);
    }
    
    f = (task_invoke_fn)hashtable_lookup(invoke_dict, task->name);
    if (f) {
        return f(task, project);
    }
    
    dir = get_current_dir();
    
    executable = find_gantt_executable(task->name, &is_gantt_executable);
    if (!executable) {
        task_log(task, LOG_ERROR, "command not found");
        free(dir);
        return false;
    }
    
    if (is_gantt_executable) {
        /* Gantt executables receive attributes via environment variables */
        argc = 1;
        /* Extra slots: GANTT_TASK_NAME, filelist (if filesets present), basedir */
        envc = slist_length(task->attribute_list) + 3;
        
        /* Prepare argv */
        argv = malloc(sizeof(char *) * (argc + 1));
        argv[0] = executable;
        argv[1] = NULL;
        
        /* Copy attributes into environment */
        envp = malloc(sizeof(char *) * (envc + 1));
        i = 0;
        list = task->attribute_list;
        while (list) {
            name = list->data;
            value = hashtable_lookup(task->attribute_dict, name);
            value = resolve_variables(value, project);
            envp[i++] = str_concat(name, "=", value ? value : "", NULL);
            list = slist_next(list);
        }
        
        /* Add basedir for executables that need it (only if task doesn't have one) */
        if (!hashtable_lookup(task->attribute_dict, "basedir")) {
            value = hashtable_lookup(project->property_dict, "basedir");
            if (value) {
                envp[i++] = str_concat("basedir", "=", value, NULL);
            }
        }
        
        /* Resolve filesets and pass as filelist */
        if (task->fileset_list) {
            string_t *filelist_str = string_new("");
            slist_t *fs_ptr = task->fileset_list;
            while (fs_ptr) {
                fileset_t *fs = fs_ptr->data;
                slist_t *files = slist_new(NULL);  /* Dummy head */
                if (resolve_fileset(fs, files, project)) {
                    slist_t *file_ptr = slist_next(files);
                    while (file_ptr) {
                        if (filelist_str->len > 0) {
                            string_append(filelist_str, "\n");
                        }
                        string_append(filelist_str, (char *)file_ptr->data);
                        file_ptr = slist_next(file_ptr);
                    }
                }
                slist_free_full(files, free);
                fs_ptr = slist_next(fs_ptr);
            }
            if (filelist_str->len > 0) {
                char *filelist_value = string_free(filelist_str, false);
                envp[i++] = str_concat("filelist", "=", filelist_value, NULL);
                free(filelist_value);
            } else {
                string_free(filelist_str, true);
            }
        }
        
        /* Extra attributes */
        envp[i++] = str_concat(GANTT_TASK_NAME, "=", task->name, NULL);
        envp[i] = NULL;
    } else {
        /* Fallback to system command - pass attributes as command-line arguments */
        argv = build_fallback_argv(task, project, &argc);
        if (!argv) {
            task_log(task, LOG_ERROR, "unable to build arguments for fallback command");
            free(executable);
            free(dir);
            return false;
        }
        argv[0] = executable;
        envp = NULL;  /* Inherit environment */
    }
    
    ret = task_spawn(task, dir, argv, envp);
    
    free(executable);
    free(dir);
    
    /* Free argv strings (skip argv[0] which was executable) */
    if (!is_gantt_executable) {
        for (i = 1; argv[i]; i++) {
            free(argv[i]);
        }
    }
    free(argv);
    
    /* Free envp strings */
    if (envp) {
        for (i = 0; envp[i]; i++) {
            free(envp[i]);
        }
        free(envp);
    }
    
    return ret;
}

/**
 * Logs a message for a task.
 */
void task_log(task_t *task, int level, const char *message)
{
    int pos, i;
    char *p_buf;
    
    (void)level;  /* Currently unused */
    
    pos = 9 - strlen(task->name);
    if (pos < 0) {
        pos = 0;
    }
    
    p_buf = malloc(sizeof(char) * (pos + 1));
    for (i = 0; i < pos; i++) {
        p_buf[i] = ' ';
    }
    p_buf[pos] = '\0';
    
    printf("%s[%s] %s\n", p_buf, task->name, message);
    free(p_buf);
}
