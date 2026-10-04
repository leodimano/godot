Precompiled GDScript
===================

This fork adds an opt-in VM-code export mode. It does not change the default
GDScript source and binary-token modes, and does not move game logic to C++.
Normal editing, script analysis and running a source project in the editor
continue to use the source compiler.

Building and exporting
----------------------

Build an editor and export templates from the same source revision. For example,
the standard Windows builds are::

    scons platform=windows target=editor arch=x86_64
    scons platform=windows target=template_debug arch=x86_64
    scons platform=windows target=template_release arch=x86_64

All ordinary engine modules and renderers remain enabled by default. Install
the matching templates or select their files under the export preset's
``Custom Template`` options. Stock upstream templates cannot read this format.

In Project > Export, select the preset and set Script > Export Mode to
``Precompiled GDScript (matching custom templates required)``. The corresponding
``export_presets.cfg`` value is ``script_export_mode=3``. The preset option
``script/compiled_compression`` selects None (0) or Zstandard (1, the default).
Both Debug and Release use the normal export commands and packaging pipeline.

Compilation runs in an isolated native MainLoop, not a SceneTree. Game autoloads,
editor plugins and project static initializers are not executed by that worker.
The worker checks that its source inputs match those selected by the export
pipeline. Compilation or dependency errors fail the export; there is no fallback
to source or token files.

Debug exports preserve script paths, line numbers and local-variable metadata.
Release exports omit debug-only code such as assertions and breakpoints.
Use a template with the corresponding Debug or Release profile. Source reload
is not supported for an already loaded compiled graph; restart to use a new
generation. Remote breakpoint statements, stepping, stack paths/lines and
local/member inspection have been exercised on Windows x86_64 and Android ARM64
Debug exports. Other targets and debugger operations require their own coverage.

Optional compiler-free templates
--------------------------------

``gdscript_compiler=no`` omits the source/token compiler from runtime templates.
It is not required to load precompiled bundles and is not the default. The
editor rejects this option because it needs the compiler for development.
The runtime VM, compiled-code loader and Debug inspection metadata remain.
Source compilation, dynamic source evaluation and source hot reload are
unavailable in this configuration.

Compatibility and current boundaries
-------------------------------------

* Instructions contain VM operations, not CPU machine code. Schema version 2
  stores fixed-width instructions and indices, marshalled values, and symbolic
  native bindings, not host pointers or native memory layouts. Runtime pointers,
  stack storage and operator caches are created by the target. Loading requires
  matching VM source identity, schema, real-number precision and build profile,
  but not matching host and target pointer widths. Native classes and method
  signatures are validated against the target. This feature is unreleased; no
  backward-compatibility or migration contract is provided for development
  artifacts. Re-export them with a matching editor and templates.
* The bundle includes the selected runtime script graph. Scene and resource
  references use ordinary logical script paths remapped to that bundle.
  Scripts loaded dynamically must also be included by the export preset.
* External script files associated with scenes and resources are supported.
  Embedded scripts, opaque nested packs, unconverted text resources, and
  plugin-transformed script inputs are rejected with an export error.
* Runtime graphs cannot depend on editor-only native APIs. Editor-only roots
  can be omitted only when no exported resource or runtime script needs them.
* Constants preserve container sharing and types; mutable static state is
  initialized freshly at runtime. Cache publication and resource dependency
  completion apply to the whole graph, avoiding mixed generations.
* File digests detect corruption, not authenticity. The reader performs
  structural validation but is not a sandbox or a complete verifier for
  hostile bytecode. Only load trusted compiler output. OTA transport,
  signatures, deployment, rollback and live graph replacement are not
  implemented by this feature.

Focused validation
------------------

Release qualification must use the version-matched official
`build containers <https://github.com/godotengine/build-containers/tree/4.7>`_
and `release build scripts <https://github.com/godotengine/godot-build-scripts/tree/4.7>`_,
including their toolchains, dependencies, production options and packaging.
Record the exact tooling commits and container identities for each qualification
run. A supported local build or upstream CI configuration is not a substitute.
Official-container release qualification is pending.

The release-tooling references inspected for this baseline are
``build-containers`` commit ``3f3cc50c3f91c2be66e37aece9d9a57284e39db7`` and
``godot-build-scripts`` commit ``63625f75c0f869dea4a01811522e10fefcd0f322``.
They are reference inputs, not evidence that a release build has executed.

Development validation of schema-v2 exports has covered Windows x86_64 and
Linux x86_64 Debug/Release templates, and single-threaded Web wasm32 Debug/Release
templates in Chromium. Each executed the same fixture in both uncompressed and
Zstandard modes, exported by a Windows x86_64 editor. Web execution checked a
64-bit compiler host producing code for a 32-bit runtime; Linux execution also
checked independence from the host OS/toolchain.

Earlier Windows x86_32 MSVC Debug/Release checks passed with a native string
formatter workaround. That unrelated workaround has been removed to preserve
the upstream formatter. Those historical results do not qualify the current
source: Windows x86_32 must be validated using the official MinGW/GCC setup.

On Android ARM64, a standard Debug template has passed the source-free remote
debugger fixture on a physical device, including stepping and local/member
inspection with zero source-pipeline entries. A standard Release template has
passed the shared runtime fixture with Zstandard compression on the same device.
These are focused development fixtures, not release qualification or a new
full-application validation. The local Android and Web toolchain versions also
differed from the version-matched official build containers.

Platform and architecture coverage is distinct from format portability. Other
architectures, threaded/GDExtension Web variants, compiler-free templates with
this schema, and macOS/iOS still require their own execution coverage. A build
failure in a platform's dependencies does not establish a bytecode failure or a
passing runtime test. Previous reference-build results do not establish coverage
of newly exported bundles.

The C++ tests under ``modules/gdscript/tests`` cover storage, metadata,
instruction layout, writer/reader behavior, shared identities, compatibility
rejection and concurrent logical-path loads. The process tests additionally
exercise the isolated worker and ordinary executable exports::

    python modules/gdscript/tests/bytecode/test_export_compiler.py <editor>
    python modules/gdscript/tests/bytecode/test_export_runtime.py <editor> --debug-template <template> --release-template <template> --log-directory <directory>
    python modules/gdscript/tests/bytecode/test_export_debugger.py <editor> --debug-template <windows-template> --log-directory <new-directory>

The executable-export fixture covers a scene script, autoload, static
initializer, typed constant, closure, inner class, RPC and scripted resource.
It also exercises 64-bit integer values, typed dictionaries, native properties,
runtime math constructors and operator caches. It checks the PCK directory for
absent source/token files, verifies the runtime's architecture, executes both
storage modes, and rejects an excluded script dependency. It runs in a temporary
project copy, leaving the developer's projects unchanged.

For another Windows architecture, pass ``--architecture x86_32`` or
``--architecture arm64`` with matching templates. The host must be able to run
that executable; cross-compiling alone does not validate execution.

For Linux, pass ``--platform linux`` and the target architecture. A Windows
editor can also export to Linux and execute the fixture through WSL: copy the
Linux templates to a Windows-accessible directory, then run::

    python modules/gdscript/tests/bytecode/test_export_runtime.py <windows-editor> --platform linux --architecture x86_64 --wsl-distribution Ubuntu --debug-template <linux-debug-template> --release-template <linux-release-template> --log-directory <new-directory>

The Web harness requires Playwright for Python and a Chromium installation. It
exports through the normal Web preset, serves the temporary output on loopback,
and runs the same fixture in a headless browser. It preserves export and browser
logs, verifies that the PCK contains no source/token scripts, and checks for zero
source-pipeline entries in Debug. With single-threaded ``wasm32`` templates::

    python modules/gdscript/tests/bytecode/test_export_web.py <editor> --debug-template <web-debug.zip> --release-template <web-release.zip> --browser <chromium-executable> --log-directory <new-directory>

Omit ``--browser`` to use Playwright's installed Chromium. For threaded templates,
add ``--threads``; the test server supplies the required isolation headers. The
harness does not use the developer's browser profile or modify the game project.
Selecting a variant is not evidence of support: record only combinations that
have actually built and executed successfully.

The debugger test exports the ``debug_runtime`` fixture, checks that the package
contains no source/token scripts, and starts a loopback debugger peer. It checks
a breakpoint statement, two step-over commands, logical script paths and line
numbers, local/member values, continuation and source-free runtime completion.
The supplied log directory must be new; the exported fixture and all diagnostic
logs are retained there. Only the test's own child processes are terminated.

For Android, copy ``debug_runtime`` into a scratch project and export it through
a normal Android Debug preset with precompiled scripts and a matching ARM64
template. Use a separate application ID and set ``command_line/extra_args`` to
``--remote-debug tcp://127.0.0.1:6036``. Enable the Internet permission. After
installing this test-only APK, forward the device's port with
``adb -s <serial> reverse tcp:6036 tcp:6036`` and start the same host::

    <editor> --headless --path <scratch-project> --script <absolute-path>/debugger_host.gd -- 6036 <result.json>

Launch the fixture after ``COMPILED_DEBUG_LISTENING`` appears. Require a passing
host result, ``COMPILED_DEBUG_RESULT`` with ``compiled=true`` and ``value=43``,
and a successful ``GDSCRIPT_BYTECODE_LOAD`` with ``source_pipeline_entries=0``
in that application's device log. Scope diagnostics to its process; a different
application may log while being suspended. Remove only the test's own port
forwarding and test package afterward. This checks the wire protocol, not the
editor debugger UI or source hot reload.

For a focused Android Release check, copy ``export_runtime`` into a scratch
project, add a project icon, and use a separate application ID with a matching
Release template. Set ``command_line/extra_args`` to ``--xr-mode off -- --verify``
for this non-XR fixture. The explicit XR mode also avoids an existing Android
startup lookup of ``xr/shaders/enabled`` before its default is registered. Require
a source-free APK and ``COMPILED_EXPORT_RESULT`` with ``compiled=true``, the
expected architecture and an empty ``failures`` array. Retain device diagnostics
and remove only the test package afterward.

For Debug profiling, enable Project Settings > Debug > GDScript >
Compiled Load Profile. Each load emits one ``GDSCRIPT_BYTECODE_LOAD`` JSON record
with phase timings, counts and cumulative source-pipeline entries. It is disabled
by default and has no effect in Release builds. These engine fixtures are
correctness checks, not application startup or memory benchmarks.
