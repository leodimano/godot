# Wild changelog

Wild is an unreleased fork of Godot **4.7.2-stable**
(`ed1daf0bf001b61586d9930840f2f1394092c079`). The planned first release label is
`Godot 4.7.2 — Wild v0.1.0`; the entries below are development milestones, not
published releases. Dates follow the recorded commit dates.

This is the fork's change timeline. Keep the upstream [changelog](CHANGELOG.md)
separate. Each milestone records what changed, why, the evidence and its limits.
Application measurements are configuration/device-specific, not general engine
benchmarks. Missing measurements remain unmeasured.

## 2026-10-05 — Opt-in Android R8 export optimization

**Commit:** `dcf86027bb` (backport of upstream `f2fbde174af`).

**Why:** Allow ordinary Release Gradle exports to shrink, optimize and obfuscate
Java/Kotlin DEX code without changing game scripts or native binaries.

**Change:** Exposed `gradle_build/minification` in the editor and passed it to
the Android template. Preserved the upstream opt-in default, JNI/plugin keep
rules, unminified Debug behavior and disabled Android resource shrinking.

**Results:** The full Windows editor was rebuilt with the official release
toolchain. Focused editor-option and Gradle configuration checks passed.
Regenerated template AARs were byte-identical to their baseline. In the Bike
Race S23 campaign, the signed test APK decreased from **100,080,467** to
**93,477,494 bytes** (6.6%); DEX decreased from **7,748,524** to **1,146,444 bytes**
(85.2%). Native libraries, game textures and compiled scripts were identical.
Four Android plugins initialized; Home, touch acceleration and foreground
rendering after background/resume were observed.

**Limits:** Test-signed development-package APK, not a store artifact. Existing
suspend-save `Busy` and development service diagnostics remained. No new startup
or memory measurement, purchase qualification, full-game regression result or
Google Play optimization score was claimed. Raw measurements and the tested
APK hash are recorded in Bike Race's `docs/performance/android_snapshots.json`,
snapshot `aligned-reduced-release-r8-20261005`.

## 2026-10-04 — Exact GDExtension API for custom runtimes

**Commit:** `7b827235dd`.

**Why:** Bindings generated from a full editor can reference native classes
removed from a product-specific template. The runtime must report its own API;
consumer projects should not filter a full-editor API by hand.

**Change:** Added `--dump-extension-api-to` to editors and Debug templates using
the existing API JSON generator. Release templates exclude the generator and
reject the command. API generation exits before application initialization.

**Results:** Focused tests cover registered classes, native method hashes and
output errors. The consumer integration retains eager native-binding checks.
See [runtime API generation](core/extension/runtime_api.rst) for the supported
workflow and matching Debug/Release requirements.

## 2026-10-04 — Portable bytecode, debugging and official build validation

**Key commits:** `72991045d2`, `005afaa1f7`, `fd6df2aebd`, `bcede7f4ae`,
`951cd17140`, `1ec5091171`, `06268e6b26`.

**Why:** A Windows compiler host must be able to produce VM code for different
OSes and pointer widths, while retaining device debugging and upstream build
practices.

**Change:** Replaced pointer-width-dependent metadata with fixed-width fields
and symbolic native bindings. Hardened compatibility checks and added runtime,
export and remote-debugger fixtures. Registered the export compiler through an
editor-owned interface to support official static-library link ordering.

**Results:** Development fixtures covered Windows/Linux x86_64 Debug/Release,
single-threaded Web wasm32 Debug/Release, and Android ARM64 debugging plus a
Release runtime check. The separate official MinGW Windows x86_32 campaign
passed Debug/Release in None/Zstandard modes, isolated compilation and rejected
missing dependencies. Debug checks recorded zero source-pipeline entries.

**Limits:** Development-toolchain coverage is not official release qualification.
macOS/iOS, remaining architectures and other target variants require their own
execution checks. A temporary MSVC x86 string-format workaround (`a5e79e2e86`)
was removed (`d8eb4da926`); the official MinGW result uses the upstream formatter.
See [bytecode coverage](modules/gdscript/bytecode.rst) and the reproducible
[Windows validation record](modules/gdscript/tests/bytecode/windows_mingw.rst).

## 2026-10-04 — Runtime/compiler separation and Android packaging

**Key commits:** `e9f1d53cfd`, `6211bfdff2`, `ecda445ec0`.

**Why:** Runtime templates should be able to consume compiled VM code without
requiring the source compiler, and Android should not add redundant ZIP
decompression around an already compressed script bundle.

**Change:** Added optional `gdscript_compiler=no` for templates, retained the
compiler in editors, stored `.gdbc` entries without APK ZIP compression, and
resolved Android native-library detection relative to the project.

**Limits:** Compiler-free templates are optional, not the standard build.
They cannot compile source, evaluate new source or hot-reload source scripts.
These commits alone do not establish an application startup-time result.

## 2026-10-03 — Export-time GDScript compilation and runtime loading

**Key commits:** `dffb878cfa`, `544754335f`, `41f262422f`, `cdf699479b`,
`87698e86c7`, `5ce650bffd`, `3d7d225af9`, `8e09e9ef4d`.

**Why:** Avoid repeating source parsing, analysis and VM-code generation during
application startup while keeping the game in GDScript and its architecture
unchanged.

**Change:** Added bounded storage, validated metadata/instruction views,
deterministic serialization, immutable instructions and a shared source
identity. Added an editor-only writer, an isolated native compilation worker,
runtime graph loading and ordinary export-preset integration. None/Zstandard
compression and Debug metadata are part of the export contract. Companion
changes address graph publication and shutdown lifetimes.

**Results and limits:** The feature exports source-free VM bundles through the
normal editor pipeline; detailed focused tests and later target coverage are
documented in [Precompiled GDScript](modules/gdscript/bytecode.rst). Matching
custom templates are required. This is VM code, not CPU machine code or a
promise of zero-cost loading. OTA delivery, signatures, rollback and live graph
replacement are not implemented; only trusted compiler output is supported.
