/*
 * ivy_cache_tasks.c
 * Copyright (C) 2026 Chris Burdess <dog@bluezoo.org>
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

/*
 * ivy:cachepath and ivy:cachefileset - expose a resolution's artifacts to
 * the rest of the running build as a named <path>/<fileset>, the same way
 * a top-level <path id="x">/<fileset id="x"> registers into
 * project->path_dict/fileset_dict. Stateless like every other ivy:* task
 * (see ivy_resolve_run()'s doc comment): both independently call
 * ivy_resolve_run() themselves rather than depending on a prior
 * ivy:resolve having run in the same build.
 */

#include "ivy.h"

/* Shared file/settingsfile/conf/failonerror attribute discovery, identical
 * to ivy:resolve/ivy:retrieve's. Caller frees file/settings_file/conf. */
static void discover_common_attrs(task_t *task, project_t *project,
                                   char **out_file, char **out_settings_file,
                                   char **out_conf, bool *out_failonerror)
{
    const char *file_attr = hashtable_lookup(task->attribute_dict, "file");
    const char *conf_attr = hashtable_lookup(task->attribute_dict, "conf");

    *out_file = resolve_variables(strdup(file_attr ? file_attr : "ivy.xml"), project);
    *out_file = expand_location(project, *out_file);
    *out_settings_file = discover_settings_file(task, project);
    *out_conf = conf_attr ? resolve_variables(strdup(conf_attr), project) : NULL;
    *out_failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);
}

/* ========================================================================
 * ivy:cachepath
 * ======================================================================== */

typedef struct cachepath_ctx {
    slist_t *path_list;
    slist_t *path_tail;
} cachepath_ctx_t;

static void cachepath_module_cb(const char *key, void *value, void *user_data)
{
    cachepath_ctx_t *cctx = user_data;
    ivy_module_report_t *mr = value;
    ivy_revision_report_t *rr;
    slist_t *art_ptr;

    (void)key;

    rr = ivy_module_report_find_default(mr);
    if (!rr) {
        return;
    }

    for (art_ptr = rr->artifacts; art_ptr; art_ptr = slist_next(art_ptr)) {
        ivy_artifact_t *art = art_ptr->data;
        path_t *p = path_alloc();
        p->data = strdup(art->cached_path);
        if (!cctx->path_tail) {
            cctx->path_list = slist_new(p);
            cctx->path_tail = cctx->path_list;
        } else {
            cctx->path_tail = slist_append(cctx->path_tail, p);
        }
    }
}

bool ivy_cachepath_invoke(task_t *task, project_t *project)
{
    const char *pathid;
    char *file, *settings_file, *conf;
    bool failonerror;
    bool ret;
    ivy_resolution_t *resolution = NULL;

    pathid = hashtable_lookup(task->attribute_dict, "pathid");
    if (!pathid) {
        task_log(task, LOG_ERROR, "ivy:cachepath: pathid attribute is required");
        return false;
    }

    discover_common_attrs(task, project, &file, &settings_file, &conf, &failonerror);

    if (!file_exists(file)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "ivy file not found: %s", file);
        task_log(task, LOG_ERROR, msg);
        free(file);
        free(settings_file);
        free(conf);
        return !failonerror;
    }

    ret = ivy_resolve_run(project, task, file, settings_file, conf, &resolution);

    if (ret && resolution) {
        cachepath_ctx_t cctx = {0};
        char msg[128];

        hashtable_foreach(resolution->modules, cachepath_module_cb, &cctx);
        hashtable_insert(project->path_dict, pathid, cctx.path_list);

        snprintf(msg, sizeof(msg), "cachepath: registered path '%s' with %zu element(s)",
                 pathid, slist_length(cctx.path_list));
        task_log(task, LOG_INFO, msg);
    } else if (!ret) {
        char msg[512];
        snprintf(msg, sizeof(msg), "resolve of %s failed", file);
        task_log(task, LOG_ERROR, msg);
    }

    if (resolution) {
        ivy_resolution_free(resolution);
    }
    free(file);
    free(settings_file);
    free(conf);

    return ret || !failonerror;
}

/* ========================================================================
 * ivy:cachefileset
 * ======================================================================== */

typedef struct cachefileset_ctx {
    fileset_t *fileset;
    const char *cache_dir;
    size_t cache_dir_len;
    int count;
} cachefileset_ctx_t;

static void cachefileset_module_cb(const char *key, void *value, void *user_data)
{
    cachefileset_ctx_t *fctx = user_data;
    ivy_module_report_t *mr = value;
    ivy_revision_report_t *rr;
    slist_t *art_ptr;

    (void)key;

    rr = ivy_module_report_find_default(mr);
    if (!rr) {
        return;
    }

    for (art_ptr = rr->artifacts; art_ptr; art_ptr = slist_next(art_ptr)) {
        ivy_artifact_t *art = art_ptr->data;
        const char *rel;
        selector_t *selector;

        if (strncmp(art->cached_path, fctx->cache_dir, fctx->cache_dir_len) != 0) {
            continue; /* defensive - every artifact is fetched under cache_dir */
        }
        rel = art->cached_path + fctx->cache_dir_len;
        if (*rel == DIR_SEPARATOR) {
            rel++;
        }

        selector = selector_alloc();
        selector->type = SELECTOR_INCLUDE;
        hashtable_insert(selector->attribute_dict, NAME, strdup(rel));
        fileset_add_selector(fctx->fileset, selector);
        fctx->count++;
    }
}

bool ivy_cachefileset_invoke(task_t *task, project_t *project)
{
    const char *setid;
    char *file, *settings_file, *conf;
    bool failonerror;
    bool ret;
    ivy_resolution_t *resolution = NULL;

    setid = hashtable_lookup(task->attribute_dict, "setid");
    if (!setid) {
        task_log(task, LOG_ERROR, "ivy:cachefileset: setid attribute is required");
        return false;
    }

    discover_common_attrs(task, project, &file, &settings_file, &conf, &failonerror);

    if (!file_exists(file)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "ivy file not found: %s", file);
        task_log(task, LOG_ERROR, msg);
        free(file);
        free(settings_file);
        free(conf);
        return !failonerror;
    }

    ret = ivy_resolve_run(project, task, file, settings_file, conf, &resolution);

    if (ret && resolution) {
        cachefileset_ctx_t fctx = {0};
        char msg[128];

        fctx.fileset = fileset_alloc();
        fctx.fileset->dir = strdup(resolution->cache_dir);
        fctx.fileset->default_excludes = true;
        fctx.fileset->case_sensitive = true;
        fctx.fileset->follow_symlinks = true;
        fctx.cache_dir = resolution->cache_dir;
        fctx.cache_dir_len = strlen(resolution->cache_dir);

        hashtable_foreach(resolution->modules, cachefileset_module_cb, &fctx);
        hashtable_insert(project->fileset_dict, setid, fctx.fileset);

        snprintf(msg, sizeof(msg), "cachefileset: registered fileset '%s' with %d file(s)",
                 setid, fctx.count);
        task_log(task, LOG_INFO, msg);
    } else if (!ret) {
        char msg[512];
        snprintf(msg, sizeof(msg), "resolve of %s failed", file);
        task_log(task, LOG_ERROR, msg);
    }

    if (resolution) {
        ivy_resolution_free(resolution);
    }
    free(file);
    free(settings_file);
    free(conf);

    return ret || !failonerror;
}
