/*
 * gantt.c
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
#include <libgen.h>  /* for dirname() */

/**
 * Converts a list containing string values to a string.
 */
char *list_to_string(slist_t *list, const char *delimiter)
{
    string_t *acc = NULL;
    
    while (list) {
        if (!acc) {
            acc = string_new(list->data);
        } else {
            string_append(acc, list->data);
        }
        list = slist_next(list);
        if (list) {
            string_append(acc, delimiter);
        }
    }
    
    if (!acc) {
        return NULL;
    }
    return string_free(acc, false);
}

/**
 * Print information about the current project.
 */
void print_project_help(project_t *project)
{
    slist_t *list;
    target_t *target;
    
    if (project->description) {
        printf("%s", project->description);
    }
    printf("\nMain targets:\n\n");
    /* TODO: identify main targets */
    printf("\nSubtargets:\n\n");
    
    list = project->target_list;
    while (list) {
        target = (target_t *)list->data;
        printf(" %s\n", target->name);
        if (target->description) {
            printf("  %s\n\n", target->description);
        }
        list = slist_next(list);
    }
    printf("\nDefault target: %s\n", project->default_target);
}

/**
 * Print the software version and build information.
 */
void print_version(void)
{
    printf("gantt version %s\n", GANTT_VERSION);
    printf("  Copyright (C) 2005, 2026 Chris Burdess\n");
    printf("  License: GPLv3+\n");
    printf("  Homepage: %s\n", PACKAGE_URL);
}

/**
 * Program start.
 */
int main(int argc, char **argv)
{
    char *filename = "build.xml";
    int i;
    project_t *project;
    target_t *target;
    slist_t *targets = NULL;
    slist_t *props = NULL;
    slist_t *props_ptr = NULL;
    slist_t *list;
    bool opt_projecthelp = false;
    bool expect_filename = false;
    bool use_direct_parser = true;  /* Use new direct parser by default */
    char *key;
    long t1, t2;
    
    /* Parse command line */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            /* Option */
            if (argv[i][1] == 'D') {
                /* Project property */
                if (props) {
                    props_ptr = slist_append(props_ptr, argv[i] + 2);
                } else {
                    props = slist_new(argv[i] + 2);
                    props_ptr = props;
                }
            }
            else if (strcmp(argv[i], "-projecthelp") == 0) {
                opt_projecthelp = true;
            } else if (strcmp(argv[i], "-version") == 0) {
                print_version();
                return 0;
            } else if (strcmp(argv[i], "-buildfile") == 0 ||
                       strcmp(argv[i], "-file") == 0 ||
                       strcmp(argv[i], "-f") == 0) {
                expect_filename = true;
            } else if (strcmp(argv[i], "--legacy-parser") == 0) {
                use_direct_parser = false;
            }
        } else {
            if (expect_filename) {
                filename = argv[i];
            } else {
                /* Target */
                if (!targets) {
                    targets = slist_new(argv[i]);
                } else {
                    slist_append(targets, argv[i]);
                }
            }
            expect_filename = false;
        }
    }
    
    /* Parse buildfile */
    if (use_direct_parser) {
        /* New direct parser - builds typed structures without DOM */
        project = parse_project(filename, props);
        slist_free(props);
        
        if (!project) {
            fprintf(stderr, "Unable to parse: %s\n", filename);
            return 1;
        }
    } else {
        /* Legacy DOM-based parser */
        xml_doc_t *doc = xml_parse_file(filename);
        if (!doc) {
            fprintf(stderr, "Unable to parse: %s\n", filename);
            return 1;
        }
        
        xml_node_t *root = xml_doc_get_root(doc);
        if (!root) {
            fprintf(stderr, "Document is empty: %s\n", filename);
            xml_doc_free(doc);
            return 2;
        }
        
        if (!xml_streq(root->name, PROJECT)) {
            fprintf(stderr, "Document root element is not 'project'.\n");
            xml_doc_free(doc);
            return 2;
        }
        
        project = project_alloc();
        if (!project) {
            fprintf(stderr, "Unable to allocate project.\n");
            xml_doc_free(doc);
            return 3;
        }
        
        /* Apply command-line properties */
        props_ptr = props;
        while (props_ptr) {
            char *data = (char *)props_ptr->data;
            char **values = str_split(data, "=", 2);
            if (values && values[0]) {
                hashtable_insert(project->property_dict, values[0], 
                               values[1] ? values[1] : "");
            }
            str_freev(values);
            props_ptr = slist_next(props_ptr);
        }
        slist_free(props);
        
        project->filename = strdup(filename);
        
        /* Set basedir property BEFORE project_init so properties can reference it. */
        {
            char *basedir_value;
            char *filename_copy = strdup(filename);
            char *dir = dirname(filename_copy);
            
            const char *xml_basedir = xml_node_get_attr(root, "basedir");
            
            if (xml_basedir && xml_basedir[0] != '\0') {
                if (xml_basedir[0] == '/') {
                    basedir_value = strdup(xml_basedir);
                } else if (strcmp(xml_basedir, ".") == 0) {
                    if (dir && dir[0] != '\0' && strcmp(dir, ".") != 0) {
                        basedir_value = strdup(dir);
                    } else {
                        basedir_value = get_current_dir();
                    }
                } else {
                    basedir_value = str_concat(dir, "/", xml_basedir, NULL);
                }
            } else {
                if (dir && dir[0] != '\0' && strcmp(dir, ".") != 0) {
                    basedir_value = strdup(dir);
                } else {
                    basedir_value = get_current_dir();
                }
            }
            free(filename_copy);
            
            char *real_basedir = realpath(basedir_value, NULL);
            if (real_basedir) {
                free(basedir_value);
                basedir_value = real_basedir;
            }
            
            hashtable_insert(project->property_dict, strdup("basedir"), basedir_value);
            project->base_dir = strdup(basedir_value);
            
            char *home_dir = getenv("HOME");
            if (home_dir) {
                hashtable_insert(project->property_dict, strdup("user.home"), strdup(home_dir));
            }
            char *cwd = get_current_dir();
            if (cwd) {
                hashtable_insert(project->property_dict, strdup("user.dir"), cwd);
            }
            char *user = getenv("USER");
            if (user) {
                hashtable_insert(project->property_dict, strdup("user.name"), strdup(user));
            }
        }
        
        if (!project_init(project, root)) {
            fprintf(stderr, "Unable to parse project.\n");
            xml_doc_free(doc);
            project_free(project);
            return 3;
        }
        
        xml_doc_free(doc);
    }
    
    /* Process project */
    printf("Buildfile: %s\n", project->filename);
    
    if (opt_projecthelp) {
        print_project_help(project);
    } else {
        if (targets) {
            /* Resolve targets */
            list = targets;
            while (list) {
                key = list->data;
                target = hashtable_lookup(project->target_dict, key);
                if (!target) {
                    printf("\nBUILD FAILED");
                    printf("\nTarget `%s' does not exist in this project.\n", key);
                    slist_free(targets);
                    project_free(project);
                    return 4;
                }
                list->data = target;
                list = slist_next(list);
            }
        } else {
            /* Use default target */
            key = project->default_target;
            target = hashtable_lookup(project->target_dict, key);
            if (!target) {
                printf("\nBUILD FAILED");
                printf("\nTarget `%s' does not exist in this project.\n", key);
                slist_free(targets);
                project_free(project);
                return 4;
            }
            targets = slist_new(target);
        }
        
        /* Invoke targets */
        hashtable_t *completed = hashtable_new();
        hashtable_t *context = hashtable_new();
        
        t1 = get_current_time_sec();
        
        list = targets;
        while (list) {
            target = (target_t *)list->data;
            if (!target_invoke(target, project, context, completed)) {
                hashtable_free(context);
                hashtable_free(completed);
                slist_free(targets);
                project_free(project);
                return 4;
            }
            list = slist_next(list);
        }
        
        t2 = get_current_time_sec();
        
        hashtable_free(context);
        hashtable_free(completed);
        
        printf("\nBUILD SUCCESSFUL\n");
        
        long elapsed = t2 - t1;
        if (elapsed == 1L) {
            printf("Total time: 1 second\n");
        } else {
            printf("Total time: %ld seconds\n", elapsed);
        }
    }
    
    slist_free(targets);
    project_free(project);
    
    return EXIT_SUCCESS;
}
