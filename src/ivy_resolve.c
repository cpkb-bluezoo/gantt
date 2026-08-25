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
 * revision wins" conflict resolution), and the ivy:resolve / ivy:retrieve
 * task entry points (added in a later commit - this one only adds
 * ivy_resolve_run(), not wired to any task yet).
 */

#include "ivy.h"

/* ========================================================================
 * Descriptor cache: every module descriptor fetched during a walk is
 * memoized here by "org:name:rev", regardless of whether it turns out to
 * be a conflict-resolution winner or loser - a losing revision's own
 * transitive dependencies still had to be fetched and parsed to explore
 * the graph. Winners have their descriptor's ownership transferred out to
 * the corresponding ivy_resolved_module_t at the end of the walk (see
 * finalize_winners); whatever remains here afterwards (losers) is freed
 * as a batch.
 * ======================================================================== */

typedef struct descriptor_cache_entry {
    ivy_module_descriptor_t *descriptor;  /* owned until ownership transfers */
    ivy_resolver_t *resolver;             /* non-owning: owned by settings->resolvers */
} descriptor_cache_entry_t;

static void descriptor_cache_entry_free(void *p)
{
    descriptor_cache_entry_t *e = p;
    if (!e) {
        return;
    }
    ivy_module_descriptor_free(e->descriptor);
    free(e);
}

typedef struct walk_ctx {
    ivy_settings_t *settings;
    hashtable_t *ancestor_path;      /* "org:name" -> non-NULL while on the current DFS stack (cycle guard) */
    hashtable_t *descriptor_cache;   /* "org:name:rev" -> descriptor_cache_entry_t* */
    hashtable_t *winners;            /* "org:name" -> ivy_resolved_module_t* (== resolution->modules) */
    slist_t *requested_confs;        /* slist of char*, the confs being resolved */
    task_t *task;
    project_t *project;
} walk_ctx_t;

/* ========================================================================
 * Small helpers: conf-mapping and exclude matching
 * ======================================================================== */

/*
 * v1 simplification (see plan): dependency conf mapping is checked as a
 * flat filter against the single top-level requested_confs list at every
 * level of the walk, not propagated/remapped per edge into a real
 * per-configuration dependency graph.
 */
static bool conf_mapping_matches(const char *conf_mapping, slist_t *requested_confs)
{
    char *arrow;
    char *lhs;
    char **tokens;
    bool matched = false;
    int i;

    if (!conf_mapping) {
        return true; /* default "*->default" - always participates */
    }

    arrow = strstr(conf_mapping, "->");
    lhs = arrow ? strndup(conf_mapping, (size_t)(arrow - conf_mapping)) : strdup(conf_mapping);

    tokens = str_split(lhs, ",", -1);
    for (i = 0; tokens && tokens[i] && !matched; i++) {
        char *tok = str_strip(tokens[i]);
        slist_t *p;

        if (strcmp(tok, "*") == 0) {
            matched = true;
            break;
        }
        for (p = requested_confs; p; p = slist_next(p)) {
            if (strcmp(tok, (char *)p->data) == 0) {
                matched = true;
                break;
            }
        }
    }
    str_freev(tokens);
    free(lhs);

    return matched;
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
 * and offline. */
static bool ivy_fetch_to_cache(project_t *project, task_t *task,
                                const char *source, const char *cache_dest)
{
    char *dest_copy;
    char *last_sep;
    bool ok;

    if (file_exists(cache_dest)) {
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

    if (ivy_fetch_to_cache(project, task, source, cache_dest)) {
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
 * records/merges it into the winners table by "latest revision wins", and
 * - only if `recurse` is true - walks its own dependencies in turn.
 * `recurse` reflects the *incoming* edge's transitive attribute (false
 * for a dependency declared transitive="false": it is still resolved
 * itself, just not explored further), not id's own preference.
 */
static void walk_module(walk_ctx_t *ctx, const ivy_module_id_t *id,
                         slist_t *inherited_excludes, bool recurse)
{
    char *key2;
    char *key3;
    descriptor_cache_entry_t *entry;
    ivy_resolved_module_t *existing;

    key2 = ivy_module_id_key(id);

    if (hashtable_lookup(ctx->ancestor_path, key2)) {
        if (ctx->task) {
            char msg[512];
            snprintf(msg, sizeof(msg), "cyclic dependency at %s, skipping", key2);
            task_log(ctx->task, LOG_WARNING, msg);
        }
        free(key2);
        return;
    }

    key3 = str_concat(key2, ":", id->revision ? id->revision : "", NULL);
    entry = hashtable_lookup(ctx->descriptor_cache, key3);
    if (!entry) {
        ivy_resolver_t *hit_resolver = NULL;
        ivy_module_descriptor_t *md = resolver_find_module(ctx->settings->default_resolver, id,
                                                             ctx->settings, ctx->project,
                                                             ctx->task, &hit_resolver);
        if (!md) {
            if (ctx->task) {
                char msg[512];
                snprintf(msg, sizeof(msg), "unable to resolve %s:%s:%s",
                         id->organisation ? id->organisation : "?",
                         id->name ? id->name : "?",
                         id->revision ? id->revision : "?");
                task_log(ctx->task, LOG_WARNING, msg);
            }
            free(key2);
            free(key3);
            return;
        }
        entry = calloc(1, sizeof(descriptor_cache_entry_t));
        entry->descriptor = md;
        entry->resolver = hit_resolver;
        hashtable_insert(ctx->descriptor_cache, key3, entry);
    }
    free(key3);

    existing = hashtable_lookup(ctx->winners, key2);
    if (!existing) {
        ivy_resolved_module_t *rm = calloc(1, sizeof(ivy_resolved_module_t));
        ivy_module_id_init(&rm->id, id->organisation, id->name, id->revision);
        hashtable_insert(ctx->winners, key2, rm);
    } else if (ivy_compare_revisions(id->revision, existing->id.revision) > 0) {
        ivy_module_id_clear(&existing->id);
        ivy_module_id_init(&existing->id, id->organisation, id->name, id->revision);
    }

    if (recurse) {
        slist_t *dep_ptr;

        hashtable_insert(ctx->ancestor_path, key2, (void *)1);

        for (dep_ptr = entry->descriptor->dependencies; dep_ptr; dep_ptr = slist_next(dep_ptr)) {
            ivy_dependency_t *dep = dep_ptr->data;
            slist_t *combined;

            if (!conf_mapping_matches(dep->conf_mapping, ctx->requested_confs)) {
                continue;
            }
            if (is_excluded(inherited_excludes, &dep->id)) {
                continue;
            }

            combined = merge_excludes(inherited_excludes, dep->excludes);
            walk_module(ctx, &dep->id, combined, dep->transitive);
            slist_free(combined);
        }

        hashtable_remove(ctx->ancestor_path, key2);
    }

    free(key2);
}

/* ========================================================================
 * Finalization: artifact fetch for winners, descriptor ownership transfer
 * ======================================================================== */

static void finalize_winner(walk_ctx_t *ctx, const char *key2, ivy_resolved_module_t *rm)
{
    char *key3;
    descriptor_cache_entry_t *entry;
    slist_t *p, *tail;

    key3 = str_concat(key2, ":", rm->id.revision ? rm->id.revision : "", NULL);
    entry = hashtable_remove(ctx->descriptor_cache, key3);
    free(key3);

    if (!entry) {
        /* Every winner was recorded by walk_module only after successfully
         * populating descriptor_cache for that exact "org:name:rev", so
         * this should be unreachable. */
        return;
    }

    rm->descriptor = entry->descriptor; /* ownership transferred */

    if (entry->resolver) {
        ivy_pattern_tokens_t tok = {0};
        char *rel_path, *source, *cache_dest;

        tok.organisation = rm->id.organisation;
        tok.module = rm->id.name;
        tok.revision = rm->id.revision;
        tok.artifact = rm->id.name;
        tok.type = "jar";
        tok.ext = "jar";

        rel_path = ivy_pattern_substitute(entry->resolver->pattern, &tok);
        source = build_source_location(entry->resolver, rel_path);
        cache_dest = build_cache_path(ctx->settings, &rm->id, "jars", "jar", rm->id.name);
        free(rel_path);

        if (ivy_fetch_to_cache(ctx->project, ctx->task, source, cache_dest)) {
            ivy_artifact_t *art = calloc(1, sizeof(ivy_artifact_t));
            ivy_module_id_init(&art->id, rm->id.organisation, rm->id.name, rm->id.revision);
            art->type = strdup("jar");
            art->ext = strdup("jar");
            art->cached_path = strdup(cache_dest);
            rm->artifacts = slist_new(art);
        } else if (ctx->task) {
            char msg[512];
            snprintf(msg, sizeof(msg), "unable to fetch artifact for %s:%s:%s",
                     rm->id.organisation ? rm->id.organisation : "?",
                     rm->id.name ? rm->id.name : "?",
                     rm->id.revision ? rm->id.revision : "?");
            task_log(ctx->task, LOG_WARNING, msg);
        }

        free(source);
        free(cache_dest);
    }

    /* v1 doesn't track precise per-module conf provenance (see the flat
     * conf-mapping filter above) - every resolved module is associated
     * with the full requested-conf set. */
    tail = NULL;
    for (p = ctx->requested_confs; p; p = slist_next(p)) {
        char *c = strdup((char *)p->data);
        if (!tail) {
            rm->confs = slist_new(c);
            tail = rm->confs;
        } else {
            tail = slist_append(tail, c);
        }
    }

    free(entry);
}

typedef struct finalize_ctx {
    walk_ctx_t *walk_ctx;
} finalize_ctx_t;

static void finalize_winners_cb(const char *key, void *value, void *user_data)
{
    finalize_ctx_t *fc = user_data;
    finalize_winner(fc->walk_ctx, key, (ivy_resolved_module_t *)value);
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

    ctx.settings = settings;
    ctx.ancestor_path = hashtable_new();
    ctx.descriptor_cache = hashtable_new();
    ctx.winners = resolution->modules;
    ctx.requested_confs = requested_confs;
    ctx.task = task;
    ctx.project = project;

    for (dep_ptr = root_md->dependencies; dep_ptr; dep_ptr = slist_next(dep_ptr)) {
        ivy_dependency_t *dep = dep_ptr->data;

        if (!conf_mapping_matches(dep->conf_mapping, requested_confs)) {
            continue;
        }
        /* dep->excludes seeds inherited_excludes: <exclude> declared
         * directly on a root dependency scopes to that dependency's own
         * transitive subtree (see walk_module). */
        walk_module(&ctx, &dep->id, dep->excludes, dep->transitive);
    }

    fctx.walk_ctx = &ctx;
    hashtable_foreach(resolution->modules, finalize_winners_cb, &fctx);

    /* Whatever remains in descriptor_cache is losing revisions - their
     * artifacts were never fetched, only their descriptors (needed to walk
     * their own transitive dependencies while exploring the graph). */
    hashtable_free_full(ctx.descriptor_cache, descriptor_cache_entry_free);
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
static char *discover_settings_file(task_t *task, project_t *project)
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

/* Writes one copy of each of a resolved module's artifacts per
 * configuration it belongs to (in v1 every resolved module belongs to the
 * full requested-conf set - see the "v1 doesn't track precise per-module
 * conf provenance" note in finalize_winner). An existing destination is
 * left alone rather than re-copied - dest paths are revision-qualified by
 * construction, so an existing file there is already the right content,
 * the same reasoning that makes the cache itself skip re-fetching. */
static void retrieve_module_cb(const char *key, void *value, void *user_data)
{
    retrieve_ctx_t *rctx = user_data;
    ivy_resolved_module_t *rm = value;
    slist_t *conf_ptr;

    (void)key;

    for (conf_ptr = rm->confs; conf_ptr; conf_ptr = slist_next(conf_ptr)) {
        const char *conf_name = conf_ptr->data;
        slist_t *art_ptr;

        for (art_ptr = rm->artifacts; art_ptr; art_ptr = slist_next(art_ptr)) {
            ivy_artifact_t *art = art_ptr->data;
            ivy_pattern_tokens_t tok = {0};
            char *dest;

            tok.organisation = rm->id.organisation;
            tok.module = rm->id.name;
            tok.revision = rm->id.revision;
            tok.artifact = rm->id.name;
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
