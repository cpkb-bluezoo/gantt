/*
 * ivy_publish.c
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

/*
 * ivy:publish - uploads a module's locally-built artifacts, plus its
 * ivy.xml module descriptor, to a named resolver. This is the first
 * *write* path in the Ivy feature - everything else only reads.
 *
 * Publish targets are restricted to non-m2compatible filesystem/url
 * resolvers. ibiblio (always public read-only infrastructure like Maven
 * Central) and chain (ambiguous "publish to which member?") are rejected
 * outright; m2compatible filesystem/url targets are also rejected - real
 * Maven deploy semantics (checksums, maven-metadata.xml) are a materially
 * bigger feature than this warrants.
 *
 * gantt has no ivy.xml writer (only the unrelated ivy-report XML schema
 * has one, in ivy_report.c), so the descriptor is republished byte-for-
 * byte, not rewritten. `pubrevision`, if given, affects only the publish
 * *path* via pattern substitution - the uploaded file still declares its
 * own originally-authored <info revision="...">. This is a real fidelity
 * gap versus real Ivy (which delivers a resolved descriptor): gantt's own
 * resolution engine never reads a fetched descriptor's declared revision
 * back out (it keys everything off the *requested* dependency edge), so a
 * publish-then-resolve round-trip within gantt is unaffected - but a
 * human, or a real-Ivy/Maven consumer that does trust the descriptor's
 * own declared revision, will see a mismatched revision inside the
 * published file. Named plainly here, not glossed over.
 *
 * Unlike every other ivy:* task, this one does not call ivy_resolve_run()
 * - publishing only needs the local descriptor's own info/publications,
 * never the dependency graph.
 */

#include "ivy.h"

/* ========================================================================
 * Upload primitive
 * ======================================================================== */

/* Uploads local_file to dest (an http(s):// URL, a file:// URL, or a bare
 * local path). For a local destination, creates any missing parent
 * directories first, then shells to cp - the same convention already used
 * by ivy_fetch_to_cache() in ivy_resolve.c, just copying in the opposite
 * direction. For a URL destination, shells out to curl/wget for an HTTP
 * PUT, mirroring get_invoke()'s (src/property.c) argv-building style and
 * curl/wget dispatch via check_http_client_available() - but -T/
 * --method=PUT instead of -o/-O, since there is no existing upload code
 * anywhere in gantt to adapt from. No -L/follow-redirects on upload:
 * blindly resubmitting a PUT to a redirected location is the kind of
 * surprise an upload shouldn't risk. Every string this function builds is
 * freed before returning. */
static bool publish_upload(task_t *task, const char *local_file, const char *dest,
                            const char *username, const char *password)
{
    if (str_has_prefix(dest, "http://") || str_has_prefix(dest, "https://")) {
        int client = check_http_client_available();
        slist_t *arg_list = NULL;
        slist_t *arg_ptr = NULL;
        spawn_result_t result = {0};
        bool ok;
        char *auth = NULL;
        char *body_file_opt = NULL;
        char *user_opt = NULL;
        char *pass_opt = NULL;

        if (client < 0) {
            task_log(task, LOG_ERROR, "ivy:publish: no HTTP client (curl/wget) available");
            return false;
        }

        if (client == 1) {
            arg_list = slist_new("curl");
            arg_ptr = arg_list;
            arg_ptr = slist_append(arg_ptr, "-s");
            arg_ptr = slist_append(arg_ptr, "-f");
            if (username && password) {
                auth = str_concat(username, ":", password, NULL);
                arg_ptr = slist_append(arg_ptr, "-u");
                arg_ptr = slist_append(arg_ptr, auth);
            }
            arg_ptr = slist_append(arg_ptr, "-T");
            arg_ptr = slist_append(arg_ptr, (char *)local_file);
            arg_ptr = slist_append(arg_ptr, (char *)dest);
        } else {
            arg_list = slist_new("wget");
            arg_ptr = arg_list;
            arg_ptr = slist_append(arg_ptr, "-q");
            arg_ptr = slist_append(arg_ptr, "--method=PUT");
            body_file_opt = str_concat("--body-file=", local_file, NULL);
            arg_ptr = slist_append(arg_ptr, body_file_opt);
            if (username) {
                user_opt = str_concat("--http-user=", username, NULL);
                arg_ptr = slist_append(arg_ptr, user_opt);
            }
            if (password) {
                pass_opt = str_concat("--http-passwd=", password, NULL);
                arg_ptr = slist_append(arg_ptr, pass_opt);
            }
            /* wget still writes the server's response body somewhere under
             * --method=PUT; redirect it away rather than let it default to
             * a file derived from the URL. */
            arg_ptr = slist_append(arg_ptr, "-O");
            arg_ptr = slist_append(arg_ptr, "/dev/null");
            arg_ptr = slist_append(arg_ptr, (char *)dest);
        }

        {
            int argc = slist_length(arg_list);
            char **argv = malloc(sizeof(char *) * (argc + 1));
            int i = 0;
            for (arg_ptr = arg_list; arg_ptr; arg_ptr = slist_next(arg_ptr)) {
                argv[i++] = arg_ptr->data;
            }
            argv[i] = NULL;
            ok = spawn_sync(NULL, argv, NULL, &result) && result.exit_status == 0;
            free(argv);
        }

        spawn_result_free(&result);
        slist_free(arg_list);
        free(auth);
        free(body_file_opt);
        free(user_opt);
        free(pass_opt);
        return ok;
    } else {
        const char *local_dest = str_has_prefix(dest, "file://") ? dest + 7 : dest;
        char *dest_copy = strdup(local_dest);
        char *last_sep = strrchr(dest_copy, DIR_SEPARATOR);
        spawn_result_t result = {0};
        bool ok;

        if (last_sep) {
            *last_sep = '\0';
            if (*dest_copy && !file_is_directory(dest_copy)) {
                char *mkdir_argv[] = {"mkdir", "-p", dest_copy, NULL};
                spawn_sync(NULL, mkdir_argv, NULL, NULL);
            }
        }
        free(dest_copy);

        {
            char *cp_argv[] = {"cp", (char *)local_file, (char *)local_dest, NULL};
            ok = spawn_sync(NULL, cp_argv, NULL, &result) && result.exit_status == 0;
        }
        spawn_result_free(&result);
        return ok;
    }
}

/* ========================================================================
 * ivy:publish task
 * ======================================================================== */

/* Publishes one local file (an artifact or the descriptor itself) to
 * `dest`, honouring `overwrite`. For a local destination, an existing
 * file is detected via file_exists() and skipped with a warning when
 * overwrite is false. For a URL destination, no pre-check is possible
 * without an extra network round-trip - overwrite="false" can't be
 * honoured there, so a single warning is logged and the upload proceeds
 * regardless, rather than either silently ignoring the stated intent or
 * hard-failing something that isn't really an error. */
static void publish_one(task_t *task, const char *local_path, const char *dest,
                         const char *username, const char *password, bool overwrite,
                         const char *what, int *published, int *skipped, int *failed)
{
    bool is_remote_url = str_has_prefix(dest, "http://") || str_has_prefix(dest, "https://");

    if (!overwrite) {
        if (is_remote_url) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "ivy:publish: overwrite=\"false\" has no effect for a URL resolver - "
                     "a pre-existing remote file may still be overwritten");
            task_log(task, LOG_WARNING, msg);
        } else {
            const char *check = str_has_prefix(dest, "file://") ? dest + 7 : dest;
            if (file_exists(check)) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "ivy:publish: skipping %s, already exists at %s (overwrite=\"false\")",
                         what, dest);
                task_log(task, LOG_WARNING, msg);
                (*skipped)++;
                return;
            }
        }
    }

    if (publish_upload(task, local_path, dest, username, password)) {
        (*published)++;
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "ivy:publish: failed to publish %s to %s", what, dest);
        task_log(task, LOG_ERROR, msg);
        (*failed)++;
    }
}

bool ivy_publish_invoke(task_t *task, project_t *project)
{
    const char *resolver_name;
    const char *file_attr;
    const char *pubrev_attr;
    const char *pattern_attr;
    char *file, *settings_file, *pubrevision, *artifactspattern;
    bool overwrite, failonerror;
    ivy_settings_t *settings = NULL;
    ivy_resolver_t *resolver = NULL;
    ivy_module_descriptor_t *md = NULL;
    const char *effective_revision;
    int published = 0, skipped = 0, failed = 0;
    slist_t *p;
    bool ret;

    resolver_name = hashtable_lookup(task->attribute_dict, "resolver");
    if (!resolver_name) {
        task_log(task, LOG_ERROR, "ivy:publish: resolver attribute is required");
        return false;
    }

    file_attr = hashtable_lookup(task->attribute_dict, "file");
    file = resolve_variables(strdup(file_attr ? file_attr : "ivy.xml"), project);
    file = expand_location(project, file);

    settings_file = discover_settings_file(task, project);

    pubrev_attr = hashtable_lookup(task->attribute_dict, "pubrevision");
    pubrevision = pubrev_attr ? resolve_variables(strdup(pubrev_attr), project) : NULL;

    pattern_attr = hashtable_lookup(task->attribute_dict, "artifactspattern");
    artifactspattern = resolve_variables(
        strdup(pattern_attr ? pattern_attr : "[artifact]-[revision].[ext]"), project);

    overwrite = parse_boolean(hashtable_lookup(task->attribute_dict, "overwrite"), false);
    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);

    if (!file_exists(file)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "ivy file not found: %s", file);
        task_log(task, LOG_ERROR, msg);
        ret = false;
        goto cleanup;
    }

    settings = settings_file ? ivy_settings_load(settings_file, project)
                              : ivy_settings_default(project);
    if (!settings) {
        char msg[512];
        snprintf(msg, sizeof(msg), "ivy:publish: failed to load settings");
        task_log(task, LOG_ERROR, msg);
        ret = false;
        goto cleanup;
    }

    resolver = hashtable_lookup(settings->resolvers, resolver_name);
    if (!resolver) {
        char msg[256];
        snprintf(msg, sizeof(msg), "ivy:publish: resolver '%s' not found", resolver_name);
        task_log(task, LOG_ERROR, msg);
        ret = false;
        goto cleanup;
    }
    if (resolver->kind == IVY_RESOLVER_IBIBLIO || resolver->kind == IVY_RESOLVER_CHAIN) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "ivy:publish: resolver '%s' is a %s resolver and cannot be a publish target",
                 resolver_name, resolver->kind == IVY_RESOLVER_IBIBLIO ? "ibiblio" : "chain");
        task_log(task, LOG_ERROR, msg);
        ret = false;
        goto cleanup;
    }
    if (resolver->m2compatible) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "ivy:publish: resolver '%s' is m2compatible; publishing to a "
                 "Maven-layout repository is not supported", resolver_name);
        task_log(task, LOG_ERROR, msg);
        ret = false;
        goto cleanup;
    }

    md = ivy_descriptor_parse_file(file);
    if (!md) {
        char msg[512];
        snprintf(msg, sizeof(msg), "ivy:publish: failed to parse %s", file);
        task_log(task, LOG_ERROR, msg);
        ret = false;
        goto cleanup;
    }

    effective_revision = pubrevision ? pubrevision : md->id.revision;
    if (!effective_revision) {
        task_log(task, LOG_ERROR,
                 "ivy:publish: no revision available (pubrevision not given and "
                 "ivy.xml has none)");
        ret = false;
        goto cleanup;
    }

    for (p = md->publications; p; p = slist_next(p)) {
        ivy_publication_t *pub = p->data;
        ivy_pattern_tokens_t tok = {0};
        char *local_rel, *local_path, *remote_rel, *remote_dest;

        tok.organisation = md->id.organisation;
        tok.module = md->id.name;
        tok.artifact = pub->name;
        tok.type = pub->type;
        tok.ext = pub->ext;

        /* Local build output is named after whatever revision the project
         * currently declares (e.g. "1.0-SNAPSHOT" during development), not
         * necessarily pubrevision - a deliver/publish workflow re-labels
         * artifacts at publish time without needing to rename them first. */
        tok.revision = md->id.revision;
        local_rel = ivy_pattern_substitute(artifactspattern, &tok);
        local_path = expand_location(project, local_rel);

        if (!file_exists(local_path)) {
            char msg[512];
            snprintf(msg, sizeof(msg), "ivy:publish: local artifact not found: %s", local_path);
            task_log(task, LOG_ERROR, msg);
            failed++;
            free(local_path);
            continue;
        }

        /* The publish *destination* always uses the effective (possibly
         * pubrevision-stamped) revision, regardless of what the local file
         * was named after. */
        tok.revision = effective_revision;
        remote_rel = ivy_pattern_substitute(resolver->pattern, &tok);
        remote_dest = ivy_resolver_location(resolver, remote_rel);
        free(remote_rel);

        publish_one(task, local_path, remote_dest, resolver->username, resolver->password,
                    overwrite, pub->name, &published, &skipped, &failed);

        free(local_path);
        free(remote_dest);
    }

    {
        ivy_pattern_tokens_t tok = {0};
        char *desc_rel, *desc_dest;

        tok.organisation = md->id.organisation;
        tok.module = md->id.name;
        tok.revision = effective_revision;
        tok.artifact = md->id.name;
        tok.type = "ivy";
        tok.ext = "xml";

        desc_rel = ivy_pattern_substitute(resolver->ivy_pattern, &tok);
        desc_dest = ivy_resolver_location(resolver, desc_rel);
        free(desc_rel);

        publish_one(task, file, desc_dest, resolver->username, resolver->password,
                    overwrite, "module descriptor", &published, &skipped, &failed);

        free(desc_dest);
    }

    {
        char msg[256];
        snprintf(msg, sizeof(msg), "ivy:publish: published %d file(s), %d skipped, %d failed",
                 published, skipped, failed);
        task_log(task, failed ? LOG_WARNING : LOG_INFO, msg);
    }

    ret = (failed == 0);

cleanup:
    if (md) {
        ivy_module_descriptor_free(md);
    }
    if (settings) {
        ivy_settings_free(settings);
    }
    free(file);
    free(settings_file);
    free(pubrevision);
    free(artifactspattern);

    return ret || !failonerror;
}
