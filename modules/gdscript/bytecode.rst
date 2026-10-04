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

* Instructions contain VM operations, not CPU machine code. Loading requires
  matching VM source identity, schema, pointer width, real-number precision
  and build profile. A 64-bit host artifact is not accepted by a 32-bit target.
  Native classes and method signatures are validated against the target.
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

The current executable validation covers Windows x86_64 standard Debug and
Release templates, plus an optional compiler-free Debug template. Android ARM64
standard Release has also executed a complete application's startup and first
race; a standard Debug fixture has exercised the remote debugger on a physical
device. iOS execution and Android compiler-free templates remain unvalidated on
this port; previous reference-build results do not establish that coverage.

The C++ tests under ``modules/gdscript/tests`` cover storage, metadata,
instruction layout, writer/reader behavior, shared identities, compatibility
rejection and concurrent logical-path loads. The process tests additionally
exercise the isolated worker and ordinary executable exports::

    python modules/gdscript/tests/bytecode/test_export_compiler.py <editor>
    python modules/gdscript/tests/bytecode/test_export_runtime.py <editor> --debug-template <template> --release-template <template> --log-directory <directory>
    python modules/gdscript/tests/bytecode/test_export_debugger.py <editor> --debug-template <windows-template> --log-directory <new-directory>

The executable-export fixture covers a scene script, autoload, static
initializer, typed constant, closure, inner class, RPC and scripted resource.
It checks the PCK directory for absent source/token files, executes both storage
modes, and rejects an excluded script dependency. It runs in a temporary project
copy, leaving the developer's projects unchanged.

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

For Debug profiling, enable Project Settings > Debug > GDScript >
Compiled Load Profile. Each load emits one ``GDSCRIPT_BYTECODE_LOAD`` JSON record
with phase timings, counts and cumulative source-pipeline entries. It is disabled
by default and has no effect in Release builds. These engine fixtures are
correctness checks, not application startup or memory benchmarks.
