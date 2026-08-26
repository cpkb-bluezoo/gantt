/*
 * ivy_resolve.c
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
 * The resolution engine: resolver-chain traversal, cache fetch/hit, the
 * transitive dependency walk (cycle guard, exclude filtering, "latest
 * revision wins" conflict resolution, caller/eviction tracking for
 * ivy:report), and the ivy:resolve / ivy:retrieve task entry points.
 * ivy:cachepath/ivy:cachefileset/ivy:report live in separate files but
 * share ivy_resolve_run() as their only entry point into this engine -
 * every ivy:* task is stateless (see ivy_resolve_run()'s own doc comment).
 */

#include "ivy.h"

/* ========================================================================
 * Descriptor cache: every module descriptor fetched during a walk is
 * memoized here by "org:name:rev", regardless of whether it turns out to
 * be a conflict-resolution winner or an evicted loser - a losing
 * revision's own transitive dependencies still had to be fetched and
 * parsed to explore the graph. finalize_revision() runs once per entry
 * here (winners and losers alike) at the end of the walk, extracting the
 * fields ivy_revision_report_t needs and freeing the descriptor uniformly.
 * ======================================================================== */

typedef struct descriptor_cache_entry {
    ivy_module_descriptor_t *descriptor;  /* owned until finalize_revision frees it */
    ivy_resolver_t *resolver;             /* non-owning: owned by settings->resolvers */
    ivy_module_id_t id;                     /* the id this was fetched for */
} descriptor_cache_entry_t;

typedef struct walk_ctx {
    ivy_settings_t *settings;
    hashtable_t *ancestor_path;      /* "org:name" -> non-NULL while on the current DFS stack (cycle guard) */
    hashtable_t *descriptor_cache;   /* "org:name:rev" -> descriptor_cache_entry_t* */
    hashtable_t *modules;             /* "org:name" -> ivy_module_report_t* (== resolution->modules) */
    hashtable_t *revision_index;       /* "org:name:rev" -> ivy_revision_report_t*, non-owning */
    slist_t *requested_confs;        /* slist of char*, the confs being resolved */
    task_t *task;
    project_t *project;
} walk_ctx_t;

/* ========================================================================
 * Small helpers: conf-mapping, exclude matching, report bookkeeping
 * ======================================================================== */

/*
 * Returns a newly allocated, comma-joined list of members of
 * requested_confs that conf_mapping (raw "a->b;c->d" attribute, or NULL
 * for the implicit "*->default") actually maps from, in requested_confs
 * order. Returns NULL if none match (the dependency doesn't participate
 * in any requested conf at all - same semantics the old boolean
 * conf_mapping_matches() had).
 */
static char *conf_mapping_matching_confs(const char *conf_mapping, slist_t *requested_confs)
{
    char *arrow = NULL;
    char *lhs = NULL;
    char **tokens = NULL;
    bool wildcard = !conf_mapping;
    string_t *result;
    bool any = false;
    slist_t *p;
    int i;

    if (conf_mapping) {
        arrow = strstr(conf_mapping, "->");
        lhs = arrow ? strndup(conf_mapping, (size_t)(arrow - conf_mapping)) : strdup(conf_mapping);
        tokens = str_split(lhs, ",", -1);
        for (i = 0; tokens && tokens[i]; i++) {
            if (strcmp(str_strip(tokens[i]), "*") == 0) {
                wildcard = true;
                break;
            }
        }
    }

    result = string_new("");
    for (p = requested_confs; p; p = slist_next(p)) {
        const char *rc = p->data;
        bool matched = wildcard;

        if (!matched) {
            for (i = 0; tokens && tokens[i]; i++) {
                if (strcmp(str_strip(tokens[i]), rc) == 0) {
                    matched = true;
                    break;
                }
            }
        }
        if (matched) {
            if (any) {
                string_append(result, ",");
            }
            string_append(result, rc);
            any = true;
        }
    }

    str_freev(tokens);
    free(lhs);

    if (!any) {
        string_free(result, true);
        return NULL;
    }
    return string_free(result, false);
}

static bool exclude_matches(ivy_exclude_t *ex, const ivy_module_id_t *id)
{
    bool org_match = (strcmp(ex->organisation, "*") == 0) ||
                      (id->organisation && strcmp(ex->organisation, id->organisation) == 0);
    bool mod_match = (strcmp(ex->module, "*") == 0) ||
                      (id->name && strcmp(ex->module, id->name) == 0);
    return org_match && mod_match;
}

static bool is_excluded(slist_t *excludes, const ivy_module_id_t *id)
{
    slist_t *p;
    for (p = excludes; p; p = slist_next(p)) {
        if (exclude_matches((ivy_exclude_t *)p->data, id)) {
            return true;
        }
    }
    return false;
}

/* Returns a new list chain sharing a's and b's ivy_exclude_t* data pointers
 * (never duplicated, never owned by the result) - free with slist_free()
 * only, never slist_free_full(). */
static slist_t *merge_excludes(slist_t *a, slist_t *b)
{
    slist_t *result = NULL;
    slist_t *tail = NULL;
    slist_t *p;

    for (p = a; p; p = slist_next(p)) {
        if (!tail) {
            result = slist_new(p->data);
            tail = result;
        } else {
            tail = slist_append(tail, p->data);
        }
    }
    for (p = b; p; p = slist_next(p)) {
        if (!tail) {
            result = slist_new(p->data);
            tail = result;
        } else {
            tail = slist_append(tail, p->data);
        }
    }
    return result;
}

/* Merges the comma-joined confs in `comma_joined` into the deduped `*list`
 * (slist of owned char*), appending only names not already present. */
static void merge_conf_string_into_list(slist_t **list, const char *comma_joined)
{
    char **tokens;
    int i;

    if (!comma_joined) {
        return;
    }
    tokens = str_split(comma_joined, ",", -1);
    for (i = 0; tokens && tokens[i]; i++) {
        char *tok = str_strip(tokens[i]);
        slist_t *p;
        bool found = false;

        for (p = *list; p; p = slist_next(p)) {
            if (strcmp((char *)p->data, tok) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            char *copy = strdup(tok);
            if (!*list) {
                *list = slist_new(copy);
            } else {
                slist_append(slist_last(*list), copy);
            }
        }
    }
    str_freev(tokens);
}

static ivy_module_report_t *get_or_create_module_report(walk_ctx_t *ctx,
                                                          const ivy_module_id_t *id,
                                                          const char *key2)
{
    ivy_module_report_t *mr = hashtable_lookup(ctx->modules, key2);
    if (!mr) {
        mr = calloc(1, sizeof(ivy_module_report_t));
        ivy_module_id_init(&mr->id, id->organisation, id->name, NULL);
        hashtable_insert(ctx->modules, key2, mr);
    }
    return mr;
}

static ivy_revision_report_t *get_or_create_revision_report(walk_ctx_t *ctx,
                                                              ivy_module_report_t *mr,
                                                              const char *key3,
                                                              const char *revision)
{
    ivy_revision_report_t *rr = hashtable_lookup(ctx->revision_index, key3);
    if (!rr) {
        rr = calloc(1, sizeof(ivy_revision_report_t));
        rr->revision = strdup(revision ? revision : "");
        hashtable_insert(ctx->revision_index, key3, rr);
        if (!mr->revisions) {
            mr->revisions = slist_new(rr);
        } else {
            slist_append(slist_last(mr->revisions), rr);
        }
    }
    return rr;
}

static void add_caller_edge(ivy_revision_report_t *rr, const ivy_module_id_t *caller_id,
                             const char *caller_conf)
{
    ivy_caller_t *c = calloc(1, sizeof(ivy_caller_t));
    ivy_module_id_init(&c->id, caller_id->organisation, caller_id->name, caller_id->revision);
    c->conf = caller_conf ? strdup(caller_conf) : NULL;
    if (!rr->callers) {
        rr->callers = slist_new(c);
    } else {
        slist_append(slist_last(rr->callers), c);
    }
    merge_conf_string_into_list(&rr->confs, caller_conf);
}

static slist_t *copy_licenses(slist_t *src)
{
    slist_t *result = NULL;
    slist_t *tail = NULL;
    slist_t *p;

    for (p = src; p; p = slist_next(p)) {
        ivy_license_t *s = p->data;
        ivy_license_t *c = calloc(1, sizeof(ivy_license_t));
        c->name = s->name ? strdup(s->name) : NULL;
        c->url = s->url ? strdup(s->url) : NULL;
        if (!tail) {
            result = slist_new(c);
            tail = result;
        } else {
            tail = slist_append(tail, c);
        }
    }
    return result;
}

/* ========================================================================
 * Cache fetch
 * ======================================================================== */

/* Joins resolver->root and rel_path. If the resolver has no root (a
 * filesystem/url resolver whose pattern is already a full absolute path,
 * as is conventional when its <artifact pattern=".."/> bakes in the
 * location directly rather than using a separate root= attribute),
 * rel_path is already the complete source location. */
static char *build_source_location(ivy_resolver_t *resolver, const char *rel_path)
{
    size_t root_len;

    if (!resolver->root || !*resolver->root) {
        return strdup(rel_path);
    }
    root_len = strlen(resolver->root);
    if (resolver->root[root_len - 1] == '/') {
        return str_concat(resolver->root, rel_path, NULL);
    }
    return str_concat(resolver->root, "/", rel_path, NULL);
}

/* Builds settings->cache_dir/[organisation]/[module]/<subdir>/[artifact]-[revision].[ext] */
static char *build_cache_path(ivy_settings_t *settings, const ivy_module_id_t *id,
                               const char *subdir, const char *ext, const char *artifact_name)
{
    ivy_pattern_tokens_t tok = {0};
    char *pattern;
    char *rel;
    char *full;

    tok.organisation = id->organisation;
    tok.module = id->name;
    tok.revision = id->revision;
    tok.artifact = artifact_name;
    tok.ext = ext;

    pattern = str_concat("[organisation]/[module]/", subdir, "/[artifact]-[revision].[ext]", NULL);
    rel = ivy_pattern_substitute(pattern, &tok);
    full = str_concat(settings->cache_dir, "/", rel, NULL);

    free(pattern);
    free(rel);
    return full;
}

/* Fetches `source` (an http(s):// URL, a file:// URL, or a bare local path)
 * to `cache_dest`, creating any missing parent directories. A cache hit
 * (cache_dest already exists) is a no-op success - this is what makes
 * reruns, and a standalone ivy:retrieve after a prior ivy:resolve, cheap
 * and offline. If out_fresh is non-NULL, it is set to false on a cache hit
 * and true after an actual fetch/copy completes (left untouched on
 * failure) - used only by the winning artifact fetch, to populate
 * ivy_revision_report_t.downloaded for ivy:report. */
static bool ivy_fetch_to_cache(project_t *project, task_t *task,
                                const char *source, const char *cache_dest,
                                bool *out_fresh)
{
    char *dest_copy;
    char *last_sep;
    bool ok;

    if (file_exists(cache_dest)) {
        if (out_fresh) {
            *out_fresh = false;
        }
        return true;
    }

    dest_copy = strdup(cache_dest);
    last_sep = strrchr(dest_copy, DIR_SEPARATOR);
    if (last_sep) {
        *last_sep = '\0';
        if (*dest_copy && !file_is_directory(dest_copy)) {
            char *mkdir_argv[] = {"mkdir", "-p", dest_copy, NULL};
            spawn_sync(NULL, mkdir_argv, NULL, NULL);
        }
    }
    free(dest_copy);

    if (str_has_prefix(source, "http://") || str_has_prefix(source, "https://")) {
        /* Reuse <get>'s fetch-to-disk logic (retries/timestamping/auth)
         * via a throwaway task_t, the same technique project_init() uses
         * to invoke property_invoke() programmatically. */
        task_t *get_task = task_alloc(NULL);
        char *src_val = strdup(source);
        char *dest_val = strdup(cache_dest);
        char *skip_val = strdup("true");

        get_task->name = strdup("get"); /* task_log() requires a non-NULL name */
        hashtable_insert(get_task->attribute_dict, "src", src_val);
        hashtable_insert(get_task->attribute_dict, "dest", dest_val);
        hashtable_insert(get_task->attribute_dict, "skipexisting", skip_val);

        ok = get_invoke(get_task, project);

        free(src_val);
        free(dest_val);
        free(skip_val);
        task_free(get_task);
    } else {
        const char *local = str_has_prefix(source, "file://") ? source + 7 : source;
        if (!file_exists(local)) {
            ok = false;
        } else {
            char *cp_argv[] = {"cp", (char *)local, (char *)cache_dest, NULL};
            spawn_result_t result = {0};
            ok = spawn_sync(NULL, cp_argv, NULL, &result) && result.exit_status == 0;
            spawn_result_free(&result);
        }
    }

    if (ok && out_fresh) {
        *out_fresh = true;
    }

    (void)task;
    return ok;
}

/* ========================================================================
 * Resolver-chain traversal
 * ======================================================================== */

/*
 * Attempts to locate and fetch id's module descriptor via `resolver`
 * (recursing into chain members, v1 returnFirst=true semantics: the first
 * chain member with a hit wins). On success, returns a newly parsed
 * descriptor (caller owns it) and sets *out_resolver to the specific leaf
 * resolver that produced it - needed later to fetch the matching artifact
 * for whichever revision turns out to be the conflict-resolution winner.
 * Returns NULL if this resolver (or none of its chain members) has it.
 */
static ivy_module_descriptor_t *resolver_find_module(ivy_resolver_t *resolver,
                                                       const ivy_module_id_t *id,
                                                       ivy_settings_t *settings,
                                                       project_t *project, task_t *task,
                                                       ivy_resolver_t **out_resolver)
{
    ivy_pattern_tokens_t tok = {0};
    const char *pattern_to_use;
    char *rel_path;
    char *source;
    char *cache_dest;
    ivy_module_descriptor_t *md = NULL;

    if (resolver->kind == IVY_RESOLVER_CHAIN) {
        slist_t *p;
        for (p = resolver->chain_resolvers; p; p = slist_next(p)) {
            md = resolver_find_module((ivy_resolver_t *)p->data, id, settings, project,
                                       task, out_resolver);
            if (md) {
                return md;
            }
        }
        return NULL;
    }

    tok.organisation = id->organisation;
    tok.module = id->name;
    tok.revision = id->revision;
    tok.artifact = id->name;
    tok.type = resolver->m2compatible ? "pom" : "ivy";
    tok.ext = resolver->m2compatible ? "pom" : "xml";

    pattern_to_use = resolver->m2compatible ? resolver->pattern : resolver->ivy_pattern;
    rel_path = ivy_pattern_substitute(pattern_to_use, &tok);
    source = build_source_location(resolver, rel_path);
    free(rel_path);

    /* Cache layout matches real Ivy: poms/[module]-[revision].pom, but
     * ivys/ivy-[revision].xml (literally "ivy", not module-prefixed). */
    cache_dest = build_cache_path(settings, id, resolver->m2compatible ? "poms" : "ivys",
                                   resolver->m2compatible ? "pom" : "xml",
                                   resolver->m2compatible ? id->name : "ivy");

    if (ivy_fetch_to_cache(project, task, source, cache_dest, NULL)) {
        md = resolver->m2compatible ? ivy_pom_parse_file(cache_dest, id)
                                     : ivy_descriptor_parse_file(cache_dest);
        if (md && out_resolver) {
            *out_resolver = resolver;
        }
    }

    free(source);
    free(cache_dest);
    return md;
}

/* ========================================================================
 * Transitive walk
 * ======================================================================== */

/*
 * Resolves `id` (fetching+parsing its descriptor if not already cached),
 * records the caller edge and "latest revision wins" bookkeeping, and -
 * only if `recurse` is true - walks its own dependencies in turn.
 * `recurse` reflects the *incoming* edge's transitive attribute (false
 * for a dependency declared transitive="false": it is still resolved
 * itself, just not explored further), not id's own preference.
 * `caller_id`/`caller_conf` describe the edge that reached `id` - the
 * calling module's own id, and the comma-joined subset of requested confs
 * this specific edge maps into (see conf_mapping_matching_confs()).
 */
static void walk_module(walk_ctx_t *ctx, const ivy_module_id_t *id,
                         slist_t *inherited_excludes, bool recurse,
                         const ivy_module_id_t *caller_id, const char *caller_conf)
{
    char *key2;
    char *key3;
    ivy_module_report_t *mr;
    ivy_revision_report_t *rr;
    descriptor_cache_entry_t *entry;

    key2 = ivy_module_id_key(id);
    key3 = str_concat(key2, ":", id->revision ? id->revision : "", NULL);

    mr = get_or_create_module_report(ctx, id, key2);
    rr = get_or_create_revision_report(ctx, mr, key3, id->revision);

    /* Record the caller edge unconditionally, before the cycle check, so a
     * cyclic edge still shows up in a report even though it isn't walked
     * further. */
    add_caller_edge(rr, caller_id, caller_conf);

    if (hashtable_lookup(ctx->ancestor_path, key2)) {
        if (ctx->task) {
            char msg[512];
            snprintf(msg, sizeof(msg), "cyclic dependency at %s, skipping", key2);
            task_log(ctx->task, LOG_WARNING, msg);
        }
        free(key2);
        free(key3);
        return;
    }

    entry = hashtable_lookup(ctx->descriptor_cache, key3);
    if (!entry) {
        ivy_resolver_t *hit_resolver = NULL;
        ivy_module_descriptor_t *md = resolver_find_module(ctx->settings->default_resolver, id,
                                                             ctx->settings, ctx->project,
                                                             ctx->task, &hit_resolver);
        if (!md) {
            if (!rr->error) {
                rr->error = strdup("not found");
                if (ctx->task) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "unable to resolve %s:%s:%s",
                             id->organisation ? id->organisation : "?",
                             id->name ? id->name : "?",
                             id->revision ? id->revision : "?");
                    task_log(ctx->task, LOG_WARNING, msg);
                }
            }
            free(key2);
            free(key3);
            return;   /* rr->error entries never compete to become mr->id.revision */
        }
        entry = calloc(1, sizeof(descriptor_cache_entry_t));
        entry->descriptor = md;
        entry->resolver = hit_resolver;
        ivy_module_id_init(&entry->id, id->organisation, id->name, id->revision);
        hashtable_insert(ctx->descriptor_cache, key3, entry);
    }
    free(key3);

    /* "Latest revision wins" - identical logic to before, now living on
     * mr->id.revision instead of a separate winners-table value. */
    if (!mr->id.revision || ivy_compare_revisions(id->revision, mr->id.revision) > 0) {
        free(mr->id.revision);
        mr->id.revision = strdup(id->revision ? id->revision : "");
    }

    if (recurse) {
        slist_t *dep_ptr;

        hashtable_insert(ctx->ancestor_path, key2, (void *)1);

        for (dep_ptr = entry->descriptor->dependencies; dep_ptr; dep_ptr = slist_next(dep_ptr)) {
            ivy_dependency_t *dep = dep_ptr->data;
            char *matching_confs;
            slist_t *combined;

            matching_confs = conf_mapping_matching_confs(dep->conf_mapping, ctx->requested_confs);
            if (!matching_confs) {
                continue;
            }
            if (is_excluded(inherited_excludes, &dep->id)) {
                free(matching_confs);
                continue;
            }

            combined = merge_excludes(inherited_excludes, dep->excludes);
            walk_module(ctx, &dep->id, combined, dep->transitive, id, matching_confs);
            slist_free(combined);
            free(matching_confs);
        }

        hashtable_remove(ctx->ancestor_path, key2);
    }

    free(key2);
}

/* ========================================================================
 * Finalization: extract report fields, fetch the winning artifact, and
 * free every descriptor - winners and evicted losers alike, in one pass.
 * ======================================================================== */

typedef struct finalize_ctx {
    walk_ctx_t *walk_ctx;
} finalize_ctx_t;

static void finalize_revision(walk_ctx_t *ctx, descriptor_cache_entry_t *entry)
{
    char *key3;
    char *key2;
    ivy_revision_report_t *rr;
    ivy_module_report_t *mr;

    key2 = ivy_module_id_key(&entry->id);
    key3 = str_concat(key2, ":", entry->id.revision ? entry->id.revision : "", NULL);
    rr = hashtable_lookup(ctx->revision_index, key3);
    mr = hashtable_lookup(ctx->modules, key2);
    free(key2);
    free(key3);

    if (!rr || !mr) {
        /* Unreachable: walk_module always creates both before ever
         * creating the corresponding descriptor_cache entry. */
        ivy_module_descriptor_free(entry->descriptor);
        ivy_module_id_clear(&entry->id);
        free(entry);
        return;
    }

    rr->status = entry->descriptor->status ? strdup(entry->descriptor->status) : NULL;
    rr->homepage = entry->descriptor->homepage ? strdup(entry->descriptor->homepage) : NULL;
    rr->pubdate = entry->descriptor->pubdate ? strdup(entry->descriptor->pubdate) : NULL;
    rr->licenses = copy_licenses(entry->descriptor->licenses);
    rr->resolver_name = (entry->resolver && entry->resolver->name)
                             ? strdup(entry->resolver->name) : NULL;

    if (mr->id.revision && strcmp(rr->revision, mr->id.revision) == 0) {
        rr->is_default = true;

        if (entry->resolver) {
            ivy_pattern_tokens_t tok = {0};
            char *rel_path, *source, *cache_dest;
            bool fresh = false;

            tok.organisation = entry->id.organisation;
            tok.module = entry->id.name;
            tok.revision = entry->id.revision;
            tok.artifact = entry->id.name;
            tok.type = "jar";
            tok.ext = "jar";

            rel_path = ivy_pattern_substitute(entry->resolver->pattern, &tok);
            source = build_source_location(entry->resolver, rel_path);
            cache_dest = build_cache_path(ctx->settings, &entry->id, "jars", "jar", entry->id.name);
            free(rel_path);

            if (ivy_fetch_to_cache(ctx->project, ctx->task, source, cache_dest, &fresh)) {
                ivy_artifact_t *art = calloc(1, sizeof(ivy_artifact_t));
                ivy_module_id_init(&art->id, entry->id.organisation, entry->id.name,
                                    entry->id.revision);
                art->type = strdup("jar");
                art->ext = strdup("jar");
                art->cached_path = strdup(cache_dest);
                rr->artifacts = slist_new(art);
                rr->downloaded = fresh;
            } else if (ctx->task) {
                char msg[512];
                snprintf(msg, sizeof(msg), "unable to fetch artifact for %s:%s:%s",
                         entry->id.organisation ? entry->id.organisation : "?",
                         entry->id.name ? entry->id.name : "?",
                         entry->id.revision ? entry->id.revision : "?");
                task_log(ctx->task, LOG_WARNING, msg);
            }

            free(source);
            free(cache_dest);
        }
    } else if (mr->id.revision) {
        rr->evicted = true;
        rr->evicted_by_rev = strdup(mr->id.revision);
    }

    ivy_module_descriptor_free(entry->descriptor);
    ivy_module_id_clear(&entry->id);
    free(entry);
}

static void finalize_revision_cb(const char *key, void *value, void *user_data)
{
    finalize_ctx_t *fc = user_data;
    (void)key;
    finalize_revision(fc->walk_ctx, (descriptor_cache_entry_t *)value);
}

/* ========================================================================
 * Top-level entry point
 * ======================================================================== */

bool ivy_resolve_run(project_t *project, task_t *task,
                      const char *ivy_file, const char *settings_file,
                      const char *conf_filter, ivy_resolution_t **out_resolution)
{
    ivy_settings_t *settings;
    ivy_module_descriptor_t *root_md;
    ivy_resolution_t *resolution;
    walk_ctx_t ctx;
    finalize_ctx_t fctx;
    slist_t *requested_confs = NULL;
    slist_t *conf_tail = NULL;
    slist_t *dep_ptr;

    *out_resolution = NULL;

    settings = settings_file ? ivy_settings_load(settings_file, project)
                              : ivy_settings_default(project);
    if (!settings) {
        return false;
    }

    root_md = ivy_descriptor_parse_file(ivy_file);
    if (!root_md) {
        ivy_settings_free(settings);
        return false;
    }

    if (!conf_filter || strcmp(conf_filter, "*") == 0) {
        slist_t *p;
        for (p = root_md->configurations; p; p = slist_next(p)) {
            ivy_configuration_t *c = p->data;
            char *name = strdup(c->name);
            if (!conf_tail) {
                requested_confs = slist_new(name);
                conf_tail = requested_confs;
            } else {
                conf_tail = slist_append(conf_tail, name);
            }
        }
    } else {
        char **tokens = str_split(conf_filter, ",", -1);
        int i;
        for (i = 0; tokens && tokens[i]; i++) {
            char *name = strdup(str_strip(tokens[i]));
            if (!conf_tail) {
                requested_confs = slist_new(name);
                conf_tail = requested_confs;
            } else {
                conf_tail = slist_append(conf_tail, name);
            }
        }
        str_freev(tokens);
    }

    resolution = calloc(1, sizeof(ivy_resolution_t));
    resolution->modules = hashtable_new();
    resolution->conf_names = requested_confs;
    resolution->cache_dir = strdup(settings->cache_dir);
    ivy_module_id_init(&resolution->root_id, root_md->id.organisation,
                        root_md->id.name, root_md->id.revision);

    ctx.settings = settings;
    ctx.ancestor_path = hashtable_new();
    ctx.descriptor_cache = hashtable_new();
    ctx.modules = resolution->modules;
    ctx.revision_index = hashtable_new();
    ctx.requested_confs = requested_confs;
    ctx.task = task;
    ctx.project = project;

    for (dep_ptr = root_md->dependencies; dep_ptr; dep_ptr = slist_next(dep_ptr)) {
        ivy_dependency_t *dep = dep_ptr->data;
        char *matching_confs = conf_mapping_matching_confs(dep->conf_mapping, requested_confs);

        if (!matching_confs) {
            continue;
        }
        /* dep->excludes seeds inherited_excludes: <exclude> declared
         * directly on a root dependency scopes to that dependency's own
         * transitive subtree (see walk_module). */
        walk_module(&ctx, &dep->id, dep->excludes, dep->transitive, &root_md->id, matching_confs);
        free(matching_confs);
    }

    fctx.walk_ctx = &ctx;
    hashtable_foreach(ctx.descriptor_cache, finalize_revision_cb, &fctx);

    hashtable_free(ctx.descriptor_cache);   /* entries already freed by finalize_revision */
    hashtable_free(ctx.revision_index);      /* non-owning index */
    hashtable_free(ctx.ancestor_path);
    ivy_module_descriptor_free(root_md);
    ivy_settings_free(settings);

    *out_resolution = resolution;
    return true;
}

/* ========================================================================
 * ivy:resolve task
 * ======================================================================== */

/* Resolves settingsfile discovery shared by ivy:resolve/ivy:retrieve:
 * an explicit attribute always wins; otherwise ${basedir}/ivysettings.xml
 * is used if it exists, else NULL (zero-config default, see
 * ivy_settings_default()). Caller frees the result. */
char *discover_settings_file(task_t *task, project_t *project)
{
    const char *attr = hashtable_lookup(task->attribute_dict, "settingsfile");
    char *settings_file;

    if (attr) {
        settings_file = resolve_variables(strdup(attr), project);
        return expand_location(project, settings_file);
    }

    settings_file = expand_location(project, strdup("ivysettings.xml"));
    if (file_exists(settings_file)) {
        return settings_file;
    }
    free(settings_file);
    return NULL;
}

bool ivy_resolve_invoke(task_t *task, project_t *project)
{
    const char *file_attr;
    const char *conf_attr;
    char *file;
    char *settings_file;
    char *conf;
    bool failonerror;
    bool ret;
    ivy_resolution_t *resolution = NULL;

    file_attr = hashtable_lookup(task->attribute_dict, "file");
    file = resolve_variables(strdup(file_attr ? file_attr : "ivy.xml"), project);
    file = expand_location(project, file);

    settings_file = discover_settings_file(task, project);

    conf_attr = hashtable_lookup(task->attribute_dict, "conf");
    conf = conf_attr ? resolve_variables(strdup(conf_attr), project) : NULL;

    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);

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
        char msg[128];
        snprintf(msg, sizeof(msg), "resolved %zu module(s)", resolution->modules->count);
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
 * ivy:retrieve task
 * ======================================================================== */

/* Small self-contained copy loop - fileops.c's copy_file() is `static`
 * inside a standalone multi-main() binary (see the Makefile's per-DTASK_*
 * builds) and isn't linkable from here. Creates any missing parent
 * directories first. */
static bool copy_file_to(const char *src, const char *dest)
{
    char *dest_copy;
    char *last_sep;
    FILE *in;
    FILE *out;
    char buf[8192];
    size_t n;
    bool ok = true;

    dest_copy = strdup(dest);
    last_sep = strrchr(dest_copy, DIR_SEPARATOR);
    if (last_sep) {
        *last_sep = '\0';
        if (*dest_copy && !file_is_directory(dest_copy)) {
            char *mkdir_argv[] = {"mkdir", "-p", dest_copy, NULL};
            spawn_sync(NULL, mkdir_argv, NULL, NULL);
        }
    }
    free(dest_copy);

    in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    out = fopen(dest, "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }

    fclose(in);
    fclose(out);
    return ok;
}

typedef struct retrieve_ctx {
    project_t *project;
    task_t *task;
    const char *pattern;
    int count;
} retrieve_ctx_t;

/* Writes one copy of the winning revision's artifacts per configuration it
 * belongs to. Modules with no winner (every attempt at them errored) are
 * silently skipped - same effective behaviour as before this file tracked
 * error entries at all, since such a module never appeared in
 * resolution->modules in the first place under the old model. An existing
 * destination is left alone rather than re-copied - dest paths are
 * revision-qualified by construction, so an existing file there is already
 * the right content, the same reasoning that makes the cache itself skip
 * re-fetching. */
static void retrieve_module_cb(const char *key, void *value, void *user_data)
{
    retrieve_ctx_t *rctx = user_data;
    ivy_module_report_t *mr = value;
    ivy_revision_report_t *rr;
    slist_t *conf_ptr;

    (void)key;

    rr = ivy_module_report_find_default(mr);
    if (!rr) {
        return;
    }

    for (conf_ptr = rr->confs; conf_ptr; conf_ptr = slist_next(conf_ptr)) {
        const char *conf_name = conf_ptr->data;
        slist_t *art_ptr;

        for (art_ptr = rr->artifacts; art_ptr; art_ptr = slist_next(art_ptr)) {
            ivy_artifact_t *art = art_ptr->data;
            ivy_pattern_tokens_t tok = {0};
            char *dest;

            tok.organisation = mr->id.organisation;
            tok.module = mr->id.name;
            tok.revision = mr->id.revision;
            tok.artifact = mr->id.name;
            tok.type = art->type;
            tok.ext = art->ext;
            tok.conf = conf_name;

            dest = ivy_pattern_substitute(rctx->pattern, &tok);
            dest = expand_location(rctx->project, dest);

            if (file_exists(dest)) {
                rctx->count++;
            } else if (copy_file_to(art->cached_path, dest)) {
                rctx->count++;
            } else if (rctx->task) {
                char msg[512];
                snprintf(msg, sizeof(msg), "unable to retrieve %s to %s",
                         art->cached_path, dest);
                task_log(rctx->task, LOG_WARNING, msg);
            }

            free(dest);
        }
    }
}

bool ivy_retrieve_invoke(task_t *task, project_t *project)
{
    const char *file_attr;
    const char *conf_attr;
    const char *pattern_attr;
    char *file;
    char *settings_file;
    char *conf;
    char *pattern;
    bool failonerror;
    bool ret;
    ivy_resolution_t *resolution = NULL;

    file_attr = hashtable_lookup(task->attribute_dict, "file");
    file = resolve_variables(strdup(file_attr ? file_attr : "ivy.xml"), project);
    file = expand_location(project, file);

    settings_file = discover_settings_file(task, project);

    conf_attr = hashtable_lookup(task->attribute_dict, "conf");
    conf = conf_attr ? resolve_variables(strdup(conf_attr), project) : NULL;

    pattern_attr = hashtable_lookup(task->attribute_dict, "pattern");
    pattern = resolve_variables(
        strdup(pattern_attr ? pattern_attr
                             : "[organisation]/[module]/[conf]/[artifact]-[revision].[ext]"),
        project);

    if (parse_boolean(hashtable_lookup(task->attribute_dict, "sync"), false)) {
        task_log(task, LOG_WARNING,
                 "sync=\"true\" is not yet supported - stale files from a previous "
                 "retrieve will not be removed, proceeding as a plain copy");
    }

    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);

    if (!file_exists(file)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "ivy file not found: %s", file);
        task_log(task, LOG_ERROR, msg);
        free(file);
        free(settings_file);
        free(conf);
        free(pattern);
        return !failonerror;
    }

    ret = ivy_resolve_run(project, task, file, settings_file, conf, &resolution);

    if (ret && resolution) {
        retrieve_ctx_t rctx;
        char msg[128];

        rctx.project = project;
        rctx.task = task;
        rctx.pattern = pattern;
        rctx.count = 0;
        hashtable_foreach(resolution->modules, retrieve_module_cb, &rctx);

        snprintf(msg, sizeof(msg), "retrieved %d file(s)", rctx.count);
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
    free(pattern);

    return ret || !failonerror;
}
