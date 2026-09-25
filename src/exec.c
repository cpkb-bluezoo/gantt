/*
 * exec.c
 * Copyright (C) 2005, 2026 Chris Burdess <dog@gnu.org>
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
 * Implementation of the "exec" task.
 * 
 * Supported attributes:
 *   executable      - The command to execute
 *   command         - Deprecated, same as executable (command string tokenized)
 *   dir             - Working directory for execution
 *   spawn           - Run asynchronously (don't wait for completion)
 *   failonerror     - Fail build if command returns non-zero
 *   failifexecutionfails - Fail if command cannot be executed (default: true)
 *   output          - File to redirect stdout to
 *   error           - File to redirect stderr to
 *   append          - Append to output/error files instead of overwriting
 *   os              - Only run on specified OS (comma-separated, e.g., "Linux,Darwin")
 *   osfamily        - Only run on specified OS family ("unix", "mac", "windows")
 *   resultproperty  - Property to store the exit code
 *   outputproperty  - Property to store stdout
 *   errorproperty   - Property to store stderr
 *   input           - File to use for stdin
 *   inputstring     - String to send to stdin
 *   newenvironment  - Don't inherit parent environment (only use <env> elements)
 * 
 * Nested elements:
 *   <arg>           - Command line arguments
 *   <env>           - Environment variables (key="name" value="val")
 */
bool exec_invoke(task_t *task, project_t *project)
{
    char *os;
    char *osfamily;
    char *dir;
    char *executable;
    slist_t *args = NULL;
    slist_t *envs;
    char *stdout_file;
    int stdout_fd = -1;
    char *stderr_file;
    int stderr_fd = -1;
    char *input_file;
    char *input_string;
    char *input_data = NULL;
    size_t input_len = 0;
    unsigned int argc;
    char **argv;
    unsigned int envc;
    char **envp;
    char **ptr;
    bool ret;
    bool async, failonerror, failifexecutionfails, newenvironment;
    char *resultproperty, *outputproperty, *errorproperty;
    
    /* OS filter - skip task if OS doesn't match */
    os = hashtable_lookup(task->attribute_dict, "os");
    if (os && !os_matches(os)) {
        /* OS doesn't match, skip this task silently */
        return true;
    }
    
    /* OS family filter */
    osfamily = hashtable_lookup(task->attribute_dict, "osfamily");
    if (osfamily && !os_family_matches(osfamily)) {
        /* OS family doesn't match, skip this task silently */
        return true;
    }
    
    async = parse_boolean(hashtable_lookup(task->attribute_dict, "spawn"), false);
    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), false);
    failifexecutionfails = parse_boolean(hashtable_lookup(task->attribute_dict, "failifexecutionfails"), true);
    newenvironment = parse_boolean(hashtable_lookup(task->attribute_dict, "newenvironment"), false);
    
    /* Properties to store results */
    resultproperty = hashtable_lookup(task->attribute_dict, "resultproperty");
    outputproperty = hashtable_lookup(task->attribute_dict, "outputproperty");
    errorproperty = hashtable_lookup(task->attribute_dict, "errorproperty");
    
    /* Get executable and arguments */
    dir = hashtable_lookup(task->attribute_dict, "dir");
    if (dir) {
        dir = resolve_variables(strdup(dir), project);
    }
    
    executable = hashtable_lookup(task->attribute_dict, "executable");
    
    if (!executable) {
        char **tokens;
        slist_t *args_ptr = NULL;
        char *command = hashtable_lookup(task->attribute_dict, "command");
        
        if (!command) {
            task_log(task, LOG_ERROR, "no executable defined");
            free(dir);
            return !failifexecutionfails;
        }
        
        /* Tokenize */
        command = resolve_variables(strdup(command), project);
        tokens = str_split(command, " ", 0);
        free(command);
        for (int i = 0; tokens && tokens[i]; i++) {
            if (args_ptr) {
                args_ptr = slist_append(args_ptr, strdup(tokens[i]));
            } else {
                args = slist_new(strdup(tokens[i]));
                args_ptr = args;
            }
        }
        str_freev(tokens);
    } else {
        executable = resolve_variables(strdup(executable), project);
        args = hashtable_lookup(task->attribute_dict, "arg");
        args = slist_prepend(args, executable);
    }
    
    envs = hashtable_lookup(task->attribute_dict, "env");
    
    /* Create argv array */
    argc = slist_length(args);
    ptr = argv = malloc(sizeof(char *) * (argc + 1));
    slist_t *arg_list = args;
    while (arg_list) {
        *ptr++ = arg_list->data;
        arg_list = slist_next(arg_list);
    }
    *ptr = NULL;
    
    /* Create envp array */
    envc = slist_length(envs);
    if (newenvironment || envc > 0) {
        ptr = envp = malloc(sizeof(char *) * (envc + 1));
        while (envs) {
            *ptr++ = envs->data;
            envs = slist_next(envs);
        }
        *ptr = NULL;
    } else {
        envp = NULL;  /* Inherit parent environment */
    }
    
    /* Handle stdin input */
    input_file = hashtable_lookup(task->attribute_dict, "input");
    input_string = hashtable_lookup(task->attribute_dict, "inputstring");
    
    if (input_file) {
        input_file = resolve_variables(strdup(input_file), project);
        input_data = file_get_contents(input_file, &input_len);
        free(input_file);
        if (!input_data) {
            task_log(task, LOG_ERROR, "Can't read input file");
            free(argv);
            free(envp);
            free(dir);
            return !failifexecutionfails;
        }
    } else if (input_string) {
        input_data = resolve_variables(strdup(input_string), project);
        input_len = strlen(input_data);
    }
    
    /* Output and error files */
    int output_flags = O_CREAT | O_WRONLY;
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "append"), false)) {
        output_flags |= O_APPEND;
    } else {
        output_flags |= O_TRUNC;
    }
    
    stdout_file = hashtable_lookup(task->attribute_dict, "output");
    if (stdout_file) {
        stdout_file = resolve_variables(strdup(stdout_file), project);
        int fd = open(stdout_file, output_flags, 0644);
        free(stdout_file);
        if (fd == -1) {
            task_log(task, LOG_ERROR, "Can't open output file");
            free(argv);
            free(envp);
            free(dir);
            free(input_data);
            return !failifexecutionfails;
        }
        stdout_fd = fd;
    }
    
    stderr_file = hashtable_lookup(task->attribute_dict, "error");
    if (stderr_file) {
        stderr_file = resolve_variables(strdup(stderr_file), project);
        int fd = open(stderr_file, output_flags, 0644);
        free(stderr_file);
        if (fd == -1) {
            task_log(task, LOG_ERROR, "Can't open error file");
            if (stdout_fd >= 0) {
                close(stdout_fd);
            }
            free(argv);
            free(envp);
            free(dir);
            free(input_data);
            return !failifexecutionfails;
        }
        stderr_fd = fd;
    }
    
    /* Spawn */
    if (async) {
        /* Async mode - no stdin support, no property capture */
        pid_t pid = spawn_async(dir, argv, newenvironment ? envp : NULL, 
                                -1, stdout_fd, stderr_fd);
        ret = (pid > 0);
    } else {
        spawn_result_t result = {0};
        
        /* Use stdin-capable spawn if we have input data */
        if (input_data) {
            ret = spawn_sync_with_input(dir, argv, 
                                        (newenvironment || envc > 0) ? envp : NULL,
                                        input_data, input_len, &result);
        } else {
            ret = spawn_sync(dir, argv, 
                            (newenvironment || envc > 0) ? envp : NULL, &result);
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
                /* Store in property (trimmed) */
                char *trimmed = strdup(str_strip(result.stdout_data));
                hashtable_insert(project->property_dict, strdup(outputproperty), trimmed);
            }
            if (!outputproperty) {
                /* Log to console if not capturing to property */
                char **lines = str_split(result.stdout_data, "\n", 0);
                for (int i = 0; lines && lines[i]; i++) {
                    if (strlen(lines[i]) > 0 || lines[i + 1]) {
                        task_log(task, LOG_MESSAGE, lines[i]);
                    }
                }
                str_freev(lines);
            }
        }
        
        /* Store/log stderr */
        if (result.stderr_data && *result.stderr_data) {
            if (errorproperty) {
                /* Store in property (trimmed) */
                char *trimmed = strdup(str_strip(result.stderr_data));
                hashtable_insert(project->property_dict, strdup(errorproperty), trimmed);
            }
            if (!errorproperty) {
                /* Log to console if not capturing to property */
                char **lines = str_split(result.stderr_data, "\n", 0);
                for (int i = 0; lines && lines[i]; i++) {
                    if (strlen(lines[i]) > 0 || lines[i + 1]) {
                        task_log(task, LOG_MESSAGE, lines[i]);
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
    free(argv);
    free(envp);
    free(dir);
    free(input_data);
    
    if (stdout_fd >= 0) {
        close(stdout_fd);
    }
    if (stderr_fd >= 0) {
        close(stderr_fd);
    }
    
    if (!ret) {
        return !failonerror;
    }
    
    return true;
}

/**
 * Map a task name to its equivalent system command name.
 * Returns the system command name, or the original name if no mapping exists.
 */
static const char *map_task_to_system_command(const char *task_name)
{
    /* Map Ant task names to Unix command names */
    if (strcmp(task_name, "delete") == 0) {
        return "rm";
    }
    if (strcmp(task_name, "copy") == 0) {
        return "cp";
    }
    if (strcmp(task_name, "move") == 0) {
        return "mv";
    }
    if (strcmp(task_name, "concat") == 0) {
        return "cat";
    }
    /* These are the same on Unix */
    /* mkdir, touch, chmod, echo */
    return task_name;
}

/**
 * Attempts to locate an executable named "gantt_<name>".
 * Searches the PATH for gantt_<name>, then falls back to the equivalent
 * system command if not found.
 *
 * To use custom gantt scripts, add their directory to your PATH.
 *
 * @param name            The task name (e.g., "mkdir")
 * @param is_gantt_executable If not NULL, set to true if a gantt_* executable was found,
 *                            false if falling back to a system command
 * @return                Path to the executable, or NULL if not found
 */
char *find_gantt_executable(const char *name, bool *is_gantt_executable)
{
    char *gantt_name;
    char *path;
    
    if (is_gantt_executable) {
        *is_gantt_executable = false;
    }
    
    /* Search PATH for gantt_<name> */
    gantt_name = str_concat(GANTT_PREFIX, name, NULL);
    path = find_executable(gantt_name);
    free(gantt_name);
    
    if (path) {
        if (is_gantt_executable) {
            *is_gantt_executable = true;
        }
        return path;
    }
    
    /* Fall back to the equivalent system command */
    /* is_gantt_executable remains false */
    const char *sys_cmd = map_task_to_system_command(name);
    return find_executable(sys_cmd);
}

/**
 * Returns the full pathname of the specified executable, or NULL if no such
 * executable could be found in the PATH.
 */
char *find_executable(const char *name)
{
    char *env_path;
    char *test;
    char **paths;
    char *result = NULL;
    size_t len;
    
    /* Get PATH variable and split it into individual paths */
    env_path = getenv(ENV_PATH);
    if (!env_path) {
        return NULL;
    }
    
    paths = str_split(env_path, PATH_SEPARATOR_S, -1);
    
    for (int i = 0; paths && paths[i]; i++) {
        len = strlen(paths[i]);
        if (!len) {
            continue;
        }
        
        /* Construct the full executable name */
        if (paths[i][len - 1] == DIR_SEPARATOR) {
            test = str_concat(paths[i], name, NULL);
        } else {
            test = str_concat(paths[i], DIR_SEPARATOR_S, name, NULL);
        }
        
        /* Test for executability */
        if (file_is_executable(test)) {
            result = test;
            break;
        }
        
        free(test);
    }
    
    str_freev(paths);
    return result;
}

bool task_spawn(task_t *task, const char *dir, char **argv, char **envp)
{
    spawn_result_t result;
    char **lines;
    int i;
    
    bool ret = spawn_sync(dir, argv, envp, &result);
    
    if (ret) {
        if (result.stdout_data && *result.stdout_data) {
            lines = str_split(result.stdout_data, "\n", 0);
            for (i = 0; lines && lines[i]; i++) {
                if (strlen(lines[i]) == 0 && !lines[i + 1]) {
                    continue;  /* Ignore empty last line */
                }
                task_log(task, LOG_MESSAGE, lines[i]);
            }
            str_freev(lines);
        }
        
        if (result.stderr_data && *result.stderr_data) {
            lines = str_split(result.stderr_data, "\n", 0);
            for (i = 0; lines && lines[i]; i++) {
                if (strlen(lines[i]) == 0 && !lines[i + 1]) {
                    continue;  /* Ignore empty last line */
                }
                task_log(task, LOG_MESSAGE, lines[i]);
            }
            str_freev(lines);
        }
        
        if (result.exit_status != 0) {
            ret = false;
        }
    } else {
        task_log(task, LOG_ERROR, "Failed to execute command");
    }
    
    spawn_result_free(&result);
    return ret;
}
