# MeshLab build-once promotion

The active work branch is the development/default branch. Mixed iterative commits
are acceptable. Cleanup for upstream submission is optional and must not move a
published tag. These workflows implement the source side of the workspace's
build-once policy; no tags or releases were created during implementation.

## Build contract

`BuildMeshLab.yml` builds all five single-precision targets with upstream
`ML_VERSION` metadata, full plugins and regression tests. It uploads short-named
packages directly, platform build records, and a source snapshot from Linux x64.
Each record includes the full SHA, original ref/event, run/attempt, version,
precision, 75-plugin inventory, submodule revisions, package hashes and Windows
signing status. Trusted repository branch builds attest packages and records;
public PRs have neither distribution signing credentials nor eligible attestations.

The source snapshot preserves tracked MeshLab source, checked-out submodules,
downloaded dependency sources and FetchContent source directories. It does not
prove completeness for every system-provided or redistributed dependency. Before
distribution, review corresponding-source/license obligations, including Qt and
platform libraries, and retain any additional required sources. The publication
input records that review; it is not automatically inferred from build success.

Windows signing, when configured, occurs during the trusted build. A beta can
explicitly accept unsigned Windows packages; stable promotion rejects them.
Promotion preserves embedded versions and package bytes. A desired metadata or
signing change requires another build, never a patch to an accepted package.

## Verify before publication

An authorized release operation first creates an immutable
`MeshLab-2025.07-rotatrix.N[-beta.M]` tag at the tested SHA. Tag creation itself is
outside these workflows. A tag push runs verification only; if there is not exactly
one eligible successful run at that SHA it asks for an explicit run ID.

Dispatch `Rotatrix release` from the default branch with the existing release tag
and original build run ID. Leave `publish` false to verify only. Verification rejects
PRs, another repository/workflow/SHA, incomplete platform jobs, missing/expired
artifacts, digest changes, missing attestations, and attestations from another
run/event/ref. It retains the signed attestation bundles and verification output.
Focused partial runs cannot currently be combined: select one full successful run.

Existing pre-transition CI packages have no eligible branch provenance. They cannot
be made promotable by adding a tag. Build the desired commit explicitly, validate
its packages and then retry; the promotion workflow never silently rebuilds.

## Central signer transition and draft publication

Keep repository variable `ROTATRIX_PROMOTION_ENABLED` unset/false until the central
signer's branch-artifact promotion changes are merged, deployed and tested against
this contract. Its source SHA/ref/event/run verification, tag recheck, signing,
notarization and output verification remain the central service's responsibility.
No shared signer source is modified by this repository.

After deployment and explicit release authorization, use a dispatch with `publish`
and `source-and-package-acceptance` true. The latter records package acceptance
and the source/license review for the selected SHA. The `signing-dispatch`
environment needs `SIGNER_DISPATCH_TOKEN`. The workflow rechecks the tag and bytes,
creates a draft with Windows/Linux packages, sources, checksums and provenance,
retains the original ad-hoc Mac inputs labeled as signing inputs, then dispatches
both Mac artifacts to the signer using their original build IDs.

The workflow does not publish the draft. Confirm both signed Mac outputs,
notarization, all expected assets and acceptance before an explicitly authorized
final publication. Existing releases/drafts cause retries to fail rather than
overwrite assets or dispatch duplicate signing blindly. Inspect and reconcile
existing asset hashes and signer runs before any recovery. Source tags are never
created, moved or deleted by these scripts.

## Validation

Run `python -m unittest discover -s tests/rotatrix -v` and actionlint after changes.
Tests exercise origin/workflow/ref/SHA rejection, required platform checks,
provenance absence, original attestation run/event matching, byte-preserving
staging, digest tampering, publication gates and short license paths with source
mappings. Actual GitHub attestation production requires the new trusted CI build;
central signing and draft publication require separately authorized release tests.
