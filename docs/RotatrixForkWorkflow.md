# Rotatrix Maintained Fork Git / CI Spec

## Goals

Maintain a small Rotatrix/OpenAxis patch stack on top of official upstream releases, while:

- staying as close to upstream as practical;
- allowing rapid disposable development;
- preserving clean published history;
- producing usable cross-platform test and release builds;
- supporting normal public contributions.

## Branches

Use official upstream release tags as bases whenever possible.

Mirror the upstream tag exactly after `rotatrix/`:
```text
upstream v2.4.3  -> rotatrix/v2.4.3
upstream 2.4.3   -> rotatrix/2.4.3
```

Use:
```text
rotatrix/work/*              disposable development branches
rotatrix/<upstream-tag>      maintained branches
```

Examples:
```text
rotatrix/work/v2.5.0
rotatrix/work/v2.5.0-pivot-fix

rotatrix/v2.4.3
rotatrix/v2.5.0
```

`rotatrix/work/*` may be freely rebased, squashed, and force-pushed.

Once a maintained branch has been published or used by contributors, do not rewrite its existing history. Append clean commits instead.

Set the current maintained Rotatrix branch as the GitHub default branch.

Normally maintain only the current upstream release and, where useful, one previous release.

## Development and porting

For a new upstream release:
```text
upstream/v2.5.0
      ↓
rotatrix/work/v2.5.0
```

Iterate freely on the work branch.

When the implementation is ready, reorganize/squash it into a clean downstream patch stack and create:
```text
rotatrix/v2.5.0
```

When moving to another upstream release, create a new branch from that new upstream tag. Do not rewrite the old maintained branch.

Use the previous upstream/Rotatrix pair as the reference for what needs to be ported. `git range-diff` may be used to compare downstream patch stacks.

For changes to an already-published version, branch from that maintained branch into `rotatrix/work/*`, iterate there, then squash only the new work and append the resulting clean commit(s) to the maintained branch.

## Contributions

External contributors should branch from the relevant maintained branch and open PRs directly against it:
```text
contributor branch
      ↓
PR -> rotatrix/v2.5.0
```

Their own branch serves as their work branch.

## CI

Prefer adapting and reusing the project's existing upstream CI/build/package workflows rather than replacing them.

Preserve upstream build commands, platform setup, dependency handling, and packaging wherever practical.

Add Rotatrix-specific triggering and artifact behavior with minimal divergence.

Normal CI should run for:
```yaml
on:
  push:
    branches:
      - 'rotatrix/**'

  pull_request:
    branches:
      - 'rotatrix/*'
```

Build/test all platforms that upstream meaningfully supports.

Where upstream already produces usable packages/bundles, preserve that behavior.

Where upstream CI only compiles/tests, add packaging only as necessary to provide practical Rotatrix test builds.

CI runs should upload temporary downloadable artifacts when useful. Include the source commit SHA in artifact names.

Typical retention: 7–14 days.

These CI artifacts are disposable test/nightly builds and do not require tags.

### Upstream workflow baseline

Use the CI/build/package workflows from the same official upstream release tag as the source base. Preserve that release's dependencies, build options, precision variants, platform coverage and packaging wherever practical.

Do not combine release-tag source with workflows from upstream main without reviewing their differences and required compatibility changes.

The original build environment may no longer be available: hosted runner images change, moving labels such as macos-latest change meaning, and older runners are retired.

When adaptation is necessary:

- Prefer explicit, supported runner and toolchain versions over moving labels.
- Backport the smallest relevant upstream compatibility fixes where available.
- Preserve supported platforms and features; do not silently disable them to obtain a passing build.
- Document the original baseline, environment changes, backported commits and reasons.
- Validate the affected build and packaging paths before considering the adaptation complete.

Keep these compatibility changes distinguishable from the Rotatrix/OpenAxis integration patch stack. When porting to another upstream release, reassess them against that release's own workflows.

## Releases

Final Rotatrix release tags are:
```text
<upstream-tag>-rotatrix.N
```

Examples:
```text
v2.4.3-rotatrix.1
v2.4.3-rotatrix.2
2.4.3-rotatrix.1
```

Reset `N` when the upstream version changes.

Release tags are immutable.

Adapt the upstream release workflow where practical. A release build should produce a usable distribution comparable to upstream, including platform signing/notarization where applicable.

Release CI is triggered by:
```yaml
on:
  push:
    tags:
      - '*-rotatrix.*'
```

Routine testing uses CI artifacts.

If a test build needs to be permanently published, use a prerelease tag such as:
```text
v2.5.0-rotatrix.1-beta.1
```

and publish it as a GitHub prerelease.

## Core rules
```text
rotatrix/work/*
    disposable development history

rotatrix/<upstream-tag>
    clean maintained history
    append-only after publication
    normal PR target

CI artifact
    temporary test build

<upstream-tag>-rotatrix.N
    immutable shipped release
```

The standard lifecycle is:
```text
upstream release
      ↓
rotatrix/work/<tag>
      ↓
iterate + CI + test artifacts
      ↓
clean patch stack
      ↓
rotatrix/<tag>
      ↓
release tag
      ↓
upstream-like packaged/signed GitHub Release
```
