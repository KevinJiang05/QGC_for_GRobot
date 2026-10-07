# QGC_KevinJiang Windows release runbook

The Windows release is produced by one audited PowerShell entrypoint. The
script owns the mechanics; the operator or Codex only supervises the generated
logs and validation report.

## Standard release

Run from the repository root:

```powershell
.\tools\release\build-windows-release.ps1 -Version 2.0.0
```

Use a new semantic version for every normal release. Rebuilding an existing
version is rejected unless `-Force` is supplied explicitly.

Compilation defaults to 8 jobs. Use `-Jobs 12` or `-Jobs 16` for measured
comparisons, or `-Jobs 4` / `-Jobs 1` to reduce concurrency if memory pressure
or a resource-generation race appears. The existing Ninja link pool remains
limited to 2 jobs. Parallel Release stability and speed must be measured on
the completed source tree; simulated workflow tests do not establish them.

For the existing Chinese MSVC/CMake 4.1 environment, the entrypoint also repairs
the known misdecoded `/showIncludes` prefix in generated compiler metadata and
regenerates Ninja rules before building. This preserves header dependencies for
subsequent incremental builds without changing the installed toolchain.

While source changes are ongoing, use the incremental Debug build instead:

```powershell
.\tools\debug\build-windows-debug.ps1
```

This command builds the existing Debug configuration without deployment,
version changes, installer creation, or dependency installation.

## Preflight only

```powershell
.\tools\release\build-windows-release.ps1 -Version 2.0.0 -PreflightOnly
```

Preflight verifies the source inputs, Visual Studio environment, CMake, and
the portable NSIS executable, together with the configured Qt, GStreamer and
project Python paths, the upgrade contract and the PowerShell signature module,
without changing version sources or building. It writes an audit report but
does not create a staging directory. A successful preflight
does not verify the generated installer, installation or an actual upgrade.

For the v5.1.5 source tree, the script uses these existing dependencies:

- Qt: `D:\Develop\envs\Qt\6.11.1\msvc2022_64`.
- GStreamer: `D:\Develop\envs\GStreamer\1.28.4\msvc_x86_64`.
- Python: `.venv\Scripts\python.exe` in the project.
- Visual Studio: `D:\Develop\Toolchains\VS2022BuildTools`.
- CPM cache: `D:\Develop\envs\qgc-cpm-cache`.

Portable NSIS may be reused from the existing `build-release/nsis-portable/`
directory. The script checks the configured locations; it does not install or
move these dependencies. The upgraded Release build uses
`build-v5.1.5-release`, separate from the old build and the current Debug build.

## Fixed pipeline

The script performs these steps in order and stops on the first failure:

1. Validate source files and build tools, including portable NSIS.
2. Synchronize the version in CMake overrides and Windows resources. The Help
   page reads the same runtime version through `QGroundControl.qgcVersion`.
3. Configure the Release build.
4. Build Release incrementally, using 8 compiler jobs by default.
5. Stage all dependencies into a new run-specific directory, deferring NSIS.
6. Verify the staged application version, required Qt and GStreamer files
   (including `gstgl-1.0-0.dll`) and the three AI entrypoint/core scripts, then
   run the staged application with `--simple-boot-test`.
7. Generate the NSIS installer from that verified staging directory, without
   repeating deployment. Record hashes of the installer, application and NSIS
   source for verification retries.
8. Verify installer/application versions, signature status, upgrade identity,
   user-data preservation contract and SHA-256.

The normal `cmake --install` command still deploys and creates an installer.
The release entrypoint uses the generated install script with
`QGC_SKIP_WINDOWS_INSTALLER=ON` for staging, then installs only the
`windows-installer` component after the boot check succeeds.

## Retry final verification

If step 8 fails after installer creation, retry only verification:

```powershell
.\tools\release\build-windows-release.ps1 -Version 2.0.0 -VerifyOnly `
    -AuditDirectory .\dist\QGC_KevinJiang_v2.0.0\release-audit\<original-run>
```

This creates a separate audit linked to the original release. It requires
steps 1–7 to have succeeded under this updated workflow, checks that the
installer, application and NSIS source hashes match, and repeats runtime,
boot, version and artifact checks. It does not synchronize versions, compile,
deploy or compress. Retain the original audit and staging directory. Older
six-step audits cannot be resumed through this mode.

Do not routinely delete the active Release build tree: retain object files and
the shared CPM cache for incremental compilation. Reclaim old run-specific
staging directories after their evidence is no longer needed; verification
retries require their original staging directory. No cleanup is automatic.

## Test the workflow without packaging

```powershell
.\tools\release\test-windows-release.ps1
```

The tests use isolated fixtures under `.tmp/codex`, replace compilation,
deployment, boot and NSIS commands with test doubles, and check CMake component
dispatch with a small fixture. They do not build QGC, run its application,
install dependencies or produce a usable installer.

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

The architecture-specific installer generated by CMake is copied to the
distribution filename above; the script reads its exact path from the generated
install script rather than assuming an upstream filename.

Run-specific staging files are kept under `build-v5.1.5-release/release-runs/` so a
failed run does not contaminate an earlier package.

## Supervision contract

Do not distribute an installer unless the final report says `succeeded` and
shows identical installer/application versions. Confirm that all eight release
steps succeeded (or steps 1–7 in the source audit plus a successful linked
verification retry), no required runtime file is missing, the upgrade identity remains
`QGC_KevinJiang`, and the reported SHA-256 matches the file being distributed.

The current release workflow does not apply an Authenticode signature. A
`NotSigned` result is reported explicitly and is not treated as a pipeline
failure until a signing workflow and certificate are intentionally added.
