/*
 * java_task.c
 * Implementation of the <java> task - runs a Java class or JAR file
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

/**
 * Execute the java task.
 *
 * Runs a Java class or executable JAR file with the specified classpath,
 * JVM arguments, and program arguments.
 *
 * Supported attributes:
 *   classname       - The Java class to run (mutually exclusive with jar)
 *   jar             - The JAR file to run (mutually exclusive with classname)
 *   classpath       - The classpath for the JVM
 *   classpathref    - Reference to a path defined elsewhere
 *   jvm             - The java executable to use (default: "java")
 *   maxmemory       - Maximum memory for the JVM (e.g., "512m")
 *   dir             - Working directory
 *   output          - File to redirect stdout to
 *   error           - File to redirect stderr to
 *   append          - Append to output/error files instead of overwriting
 *   failonerror     - Fail the build if the Java process returns non-zero
 *   fork            - Always true for gantt (we always fork)
 *   resultproperty  - Property to store exit code
 *   outputproperty  - Property to store stdout
 *   errorproperty   - Property to store stderr
 *   input           - File to use for stdin
 *   inputstring     - String to send to stdin
 *
 * Nested elements:
 *   <arg>        - Program arguments
 *   <jvmarg>     - JVM arguments
 *   <classpath>  - Classpath elements
 */
bool java_invoke(task_t *task, project_t *project)
{
    char *jvm;
    char *executable;
    char *classname;
    char *jarfile;
    char *dir;
    char *maxmemory;
    char *output_file;
    char *error_file;
    char *input_file;
    char *input_string;
    char *input_data = NULL;
    size_t input_len = 0;
    int output_fd = -1;
    int error_fd = -1;
    bool failonerror;
    bool append;
    char *resultproperty, *outputproperty, *errorproperty;
    char **argv;
    slist_t *arg_list;
    slist_t *arg_ptr;
    slist_t *jvmarg_list;
    slist_t *progarg_list;
    slist_t *path_list;
    char *value;
    unsigned int argc;
    int i;
    bool ret;
    char message[512];
    
    /* Get the java executable */
    jvm = hashtable_lookup(task->attribute_dict, "jvm");
    if (!jvm) {
        jvm = hashtable_lookup(project->property_dict, "java.home");
        if (jvm) {
            jvm = str_concat(jvm, DIR_SEPARATOR_S, "bin", DIR_SEPARATOR_S, "java", NULL);
        } else {
            jvm = "java";
        }
    }
    
    executable = find_executable(jvm);
    if (!executable) {
        fprintf(stderr, "%s: java command not found\n", jvm);
        return false;
    }
    
    /* Get classname or jar - must have one */
    classname = hashtable_lookup(task->attribute_dict, "classname");
    jarfile = hashtable_lookup(task->attribute_dict, "jar");
    
    if (!classname && !jarfile) {
        task_log(task, LOG_ERROR, "must specify classname or jar attribute");
        free(executable);
        return false;
    }
    
    if (classname && jarfile) {
        task_log(task, LOG_ERROR, "cannot specify both classname and jar attributes");
        free(executable);
        return false;
    }
    
    /* Resolve variables in classname/jar */
    if (classname) {
        classname = resolve_variables(strdup(classname), project);
    }
    if (jarfile) {
        jarfile = resolve_variables(strdup(jarfile), project);
        jarfile = expand_location(project, jarfile);
    }
    
    /* Working directory */
    dir = hashtable_lookup(task->attribute_dict, "dir");
    if (dir) {
        dir = resolve_variables(strdup(dir), project);
        dir = expand_location(project, dir);
    } else {
        dir = get_current_dir();
    }
    
    /* Options */
    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);
    append = parse_boolean(hashtable_lookup(task->attribute_dict, "append"), false);
    
    /* Properties to store results */
    resultproperty = hashtable_lookup(task->attribute_dict, "resultproperty");
    outputproperty = hashtable_lookup(task->attribute_dict, "outputproperty");
    errorproperty = hashtable_lookup(task->attribute_dict, "errorproperty");
    
    /* Handle stdin input */
    input_file = hashtable_lookup(task->attribute_dict, "input");
    input_string = hashtable_lookup(task->attribute_dict, "inputstring");
    
    if (input_file) {
        input_file = resolve_variables(strdup(input_file), project);
        input_data = file_get_contents(input_file, &input_len);
        free(input_file);
        if (!input_data) {
            task_log(task, LOG_ERROR, "Can't read input file");
            free(executable);
            free(dir);
            if (classname) {
                free(classname);
            }
            if (jarfile) {
                free(jarfile);
            }
            return !failonerror;
        }
    } else if (input_string) {
        input_data = resolve_variables(strdup(input_string), project);
        input_len = strlen(input_data);
    }
    
    /* Build argument list */
    arg_list = slist_new(executable);
    arg_ptr = arg_list;
    
    /* JVM arguments from nested <jvmarg> elements */
    jvmarg_list = hashtable_lookup(task->attribute_dict, JVMARG);
    while (jvmarg_list) {
        value = jvmarg_list->data;
        if (value) {
            arg_ptr = slist_append(arg_ptr, strdup(value));
        }
        jvmarg_list = slist_next(jvmarg_list);
    }
    
    /* Max memory */
    maxmemory = hashtable_lookup(task->attribute_dict, "maxmemory");
    if (maxmemory) {
        maxmemory = resolve_variables(strdup(maxmemory), project);
        value = str_concat("-Xmx", maxmemory, NULL);
        arg_ptr = slist_append(arg_ptr, value);
        free(maxmemory);
    }
    
    /* Classpath */
    path_list = hashtable_lookup(task->path_dict, "classpath");
    if (!path_list) {
        /* Try classpathref */
        value = hashtable_lookup(task->attribute_dict, "classpathref");
        if (value) {
            path_list = hashtable_lookup(project->path_dict, value);
        }
    }
    if (!path_list) {
        /* Try classpath attribute */
        value = hashtable_lookup(task->attribute_dict, "classpath");
        if (value) {
            value = resolve_variables(strdup(value), project);
            arg_ptr = slist_append(arg_ptr, strdup("-classpath"));
            arg_ptr = slist_append(arg_ptr, value);
        }
    }
    if (path_list) {
        value = list_to_string(resolve_path(path_list, project), PATH_SEPARATOR_S);
        if (value && *value) {
            arg_ptr = slist_append(arg_ptr, strdup("-classpath"));
            arg_ptr = slist_append(arg_ptr, value);
        }
    }
    
    /* JAR or class */
    if (jarfile) {
        arg_ptr = slist_append(arg_ptr, strdup("-jar"));
        arg_ptr = slist_append(arg_ptr, jarfile);
    } else {
        arg_ptr = slist_append(arg_ptr, classname);
    }
    
    /* Program arguments from nested <arg> elements */
    progarg_list = hashtable_lookup(task->attribute_dict, ARG);
    while (progarg_list) {
        value = progarg_list->data;
        if (value) {
            arg_ptr = slist_append(arg_ptr, strdup(value));
        }
        progarg_list = slist_next(progarg_list);
    }
    
    /* Copy arg_list into argv */
    argc = slist_length(arg_list);
    argv = malloc(sizeof(char *) * (argc + 1));
    arg_ptr = arg_list;
    i = 0;
    while (arg_ptr) {
        argv[i++] = arg_ptr->data;
        arg_ptr = slist_next(arg_ptr);
    }
    argv[i] = NULL;
    
    slist_free(arg_list);
    
    /* Log what we're doing */
    if (jarfile) {
        snprintf(message, sizeof(message), "Executing JAR: %s", jarfile);
    } else {
        snprintf(message, sizeof(message), "Executing class: %s", classname);
    }
    task_log(task, LOG_MESSAGE, message);
    
    /* Handle output/error redirection */
    int output_flags = O_CREAT | O_WRONLY;
    if (append) {
        output_flags |= O_APPEND;
    } else {
        output_flags |= O_TRUNC;
    }
    
    output_file = hashtable_lookup(task->attribute_dict, "output");
    if (output_file) {
        output_file = resolve_variables(strdup(output_file), project);
        output_fd = open(output_file, output_flags, 0644);
        if (output_fd < 0) {
            task_log(task, LOG_ERROR, "cannot open output file");
            free(output_file);
            free(executable);
            free(dir);
            free(argv);
            return !failonerror;
        }
        free(output_file);
    }
    
    error_file = hashtable_lookup(task->attribute_dict, "error");
    if (error_file) {
        error_file = resolve_variables(strdup(error_file), project);
        error_fd = open(error_file, output_flags, 0644);
        if (error_fd < 0) {
            task_log(task, LOG_ERROR, "cannot open error file");
            if (output_fd >= 0) {
                close(output_fd);
            }
            free(error_file);
            free(executable);
            free(dir);
            free(argv);
            return !failonerror;
        }
        free(error_file);
    }
    
    /* Execute */
    if (output_fd >= 0 || error_fd >= 0) {
        /* Use spawn_async for output redirection, then wait */
        pid_t pid = spawn_async(dir, argv, NULL,
                                -1,
                                output_fd >= 0 ? output_fd : -1,
                                error_fd >= 0 ? error_fd : -1);
        
        if (output_fd >= 0) {
            close(output_fd);
        }
        if (error_fd >= 0) {
            close(error_fd);
        }
        
        if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            ret = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            
            /* Store exit code in property */
            if (resultproperty) {
                char exit_str[16];
                snprintf(exit_str, sizeof(exit_str), "%d", 
                         WIFEXITED(status) ? WEXITSTATUS(status) : -1);
                hashtable_insert(project->property_dict, strdup(resultproperty), strdup(exit_str));
            }
        } else {
            ret = false;
        }
    } else {
        /* Use spawn with output capture for property storage */
        spawn_result_t result = {0};
        
        if (input_data) {
            ret = spawn_sync_with_input(dir, argv, NULL, input_data, input_len, &result);
        } else {
            ret = spawn_sync(dir, argv, NULL, &result);
        }
        
        /* Store exit code in property */
        if (resultproperty && ret) {
            char exit_str[16];
            snprintf(exit_str, sizeof(exit_str), "%d", result.exit_status);
            hashtable_insert(project->property_dict, strdup(resultproperty), strdup(exit_str));
        }
        
        /* Store/log stdout */
        if (result.stdout_data && *result.stdout_data) {
            if (outputproperty) {
                char *trimmed = strdup(str_strip(result.stdout_data));
                hashtable_insert(project->property_dict, strdup(outputproperty), trimmed);
            }
            if (!outputproperty) {
                char **lines = str_split(result.stdout_data, "\n", 0);
                for (int j = 0; lines && lines[j]; j++) {
                    if (strlen(lines[j]) > 0 || lines[j + 1]) {
                        task_log(task, LOG_MESSAGE, lines[j]);
                    }
                }
                str_freev(lines);
            }
        }
        
        /* Store/log stderr */
        if (result.stderr_data && *result.stderr_data) {
            if (errorproperty) {
                char *trimmed = strdup(str_strip(result.stderr_data));
                hashtable_insert(project->property_dict, strdup(errorproperty), trimmed);
            }
            if (!errorproperty) {
                char **lines = str_split(result.stderr_data, "\n", 0);
                for (int j = 0; lines && lines[j]; j++) {
                    if (strlen(lines[j]) > 0 || lines[j + 1]) {
                        task_log(task, LOG_MESSAGE, lines[j]);
                    }
                }
                str_freev(lines);
            }
        }
        
        if (ret && result.exit_status != 0) {
            ret = false;
        }
        
        spawn_result_free(&result);
    }
    
    /* Clean up */
    free(executable);
    free(dir);
    free(argv);
    free(input_data);
    
    if (!ret && failonerror) {
        return false;
    }
    
    return true;
}

