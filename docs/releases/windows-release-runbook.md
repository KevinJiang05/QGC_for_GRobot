# QGC_KevinJiang Windows release runbook

The Windows release is produced by one audited PowerShell entrypoint. The
script owns the mechanics; the operator or Codex only supervises the generated
logs and validation report.

## Standard release

Run from the repository root:

```powershell
.\tools\release\build-windows-release.ps1 -Version 1.3.2
```

Use a new semantic version for every normal release. Rebuilding an existing
version is rejected unless `-Force` is supplied explicitly.

## Preflight only

```powershell
.\tools\release\build-windows-release.ps1 -Version 1.3.2 -PreflightOnly
```

Preflight verifies the source inputs, Visual Studio environment, CMake, and
the portable NSIS executable without changing version sources or building.

## Fixed pipeline

The script performs these steps in order and stops on the first failure:

1. Validate source files and build tools, including portable NSIS.
2. Synchronize the version in CMake overrides, Windows resources, and the Help page.
3. Configure the Release build.
4. Build Release with one compiler job.
5. Stage dependencies into a new run-specific directory and build the NSIS installer.
6. Verify installer/application versions, required Qt and GStreamer files,
   the stable upgrade identity, user-data preservation behavior, and SHA-256.

## Logs and artifacts

Each run writes an audit directory under:

```text
dist/QGC_KevinJiang_v<version>/release-audit/<timestamp>/
```

It contains:

- `release-state.json`: machine-readable status, updated after every step.
- `release-report.md`: concise final supervision report.
- `steps/NN-<step>.log`: a complete transcript for every individual step.

The distributable installer is written to:

```text
dist/QGC_KevinJiang_v<version>/QGC_v<version>_KevinJiang-installer.exe
```

Run-specific staging files are kept under `build-release/release-runs/` so a
failed run does not contaminate an earlier package.

## Supervision contract

Do not distribute an installer unless the final report says `succeeded` and
shows identical installer/application versions. Confirm that all six steps
succeeded, no required runtime file is missing, the upgrade identity remains
`QGC_KevinJiang`, and the reported SHA-256 matches the file being distributed.

The current release workflow does not apply an Authenticode signature. A
`NotSigned` result is reported explicitly and is not treated as a pipeline
failure until a signing workflow and certificate are intentionally added.

