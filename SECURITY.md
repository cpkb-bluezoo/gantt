# Security Policy

## Reporting a Vulnerability

Please report security vulnerabilities privately, not via a public GitHub
issue.

1. **Preferred**: open a [GitHub Security Advisory](https://github.com/cpkb-bluezoo/gantt/security/advisories/new)
   for this repository. This lets us discuss and fix the issue privately
   before disclosure.
2. **Fallback**: if you can't use GitHub Security Advisories, email
   Chris Burdess at <dog@gnu.org>.

Please include:

- A description of the vulnerability and its impact.
- Steps to reproduce, or a minimal `build.xml`/`ivysettings.xml`/`ivy.xml`
  that triggers it.
- The gantt version or commit you tested against (`gantt -version`).
- Any suggested fix, if you have one.

This project has no dedicated security team - it's maintained on a
best-effort basis. Expect an initial response within a few days, not
hours.

## Scope

gantt is a build tool: running it against a `build.xml` is expected to
execute the tasks that file declares, including arbitrary external
commands via `<exec>` and the `gantt_*` task executables, in the same way
`make` or Apache Ant do. **That gantt executes what you tell it to is by
design, not a vulnerability report on its own** - the trust boundary is
the same as running any other build tool against a build file you didn't
write yourself: don't run untrusted build files.

Security issues we *do* want to hear about include (non-exhaustively):

- Memory-safety bugs (buffer overflows, use-after-free, etc.) in the XML
  parser, pattern matching, or anywhere else - especially anything
  reachable just by *parsing* a build file, `ivy.xml`, `ivysettings.xml`,
  or a Maven POM, without any task in it actually running.
- Path traversal or unintended file access via crafted glob/Ant-style
  patterns, Ivy `[token]` pattern substitution, or archive extraction
  (`gantt_unzip`/`gantt_untar` and friends).
- Command construction that could let untrusted *data* (not the build
  file's own declared commands) be interpreted as additional commands or
  arguments - e.g. via unsanitised property expansion into a shell
  string, as opposed to gantt's normal `execve`-with-argv-array
  invocation of external tools.
- Credential handling: gantt uses `curl`/`wget` as subprocesses for
  `<get>` and the Ivy `ivy:*` tasks, including HTTP Basic Auth
  (`username`/`password` on an `ivysettings.xml` resolver). Note that
  command-line arguments - including `-u user:pass` - are visible to
  other local users on most Unix systems via `ps`. This is a known,
  inherent limitation of shelling out to curl/wget for authenticated
  fetch/publish rather than linking an HTTP library; we're interested in
  reports about credentials leaking anywhere *beyond* that already-known
  local-`ps`-visibility case (e.g. into log files, generated reports, or
  a remote party gantt didn't intend to talk to).

## Supported Versions

gantt does not yet maintain multiple supported release branches - please
report vulnerabilities against the latest commit on `main`.
