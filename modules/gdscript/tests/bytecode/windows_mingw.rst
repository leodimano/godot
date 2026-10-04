Windows MinGW validation
========================

This records the focused Windows x86_32 validation using the upstream release
configuration. It is not a published Godot release or qualification of other
platforms, renderers, editor workflows, or .NET variants.

Build inputs
------------

* Comparison baseline: ``4.7.2-stable``.
* Engine source: ``951cd17140efe011ff1643b61d941c49193c5a6e``.
  The native string formatter matches upstream; no MSVC workaround is included.
* Runtime fixture: ``1ec5091171f2f41f1c7503c443722d8452d7964b``.
  Its formatting assertions do not change the engine binary or VM fingerprint.
* ``godotengine/build-containers``:
  ``3f3cc50c3f91c2be66e37aece9d9a57284e39db7`` (4.7 branch).
* ``godotengine/godot-build-scripts``:
  ``63625f75c0f869dea4a01811522e10fefcd0f322`` (4.7 branch).

The source archive was generated with the unchanged
``misc/scripts/make_tarball.sh`` using ``-v 4.7.2-stable`` and the source commit
above. Its SHA-256 is::

    be75bd008145a3ac52230e532a2f12de3340ca332e8250cebeb102b884cff154

The unmodified ``Dockerfile.base`` and ``Dockerfile.windows`` from the pinned
container repository were built with Podman in Ubuntu WSL. No dependency or
compiler substitutions were made. The resolved Fedora 43 base digest is::

    sha256:a60334b24af6818e9ccf199d188b5ddef1c3d7f549470b560d9b744c78c2c58b

The locally built Windows image's ID and manifest digest are respectively::

    c2755bdf44e1db1ac814945ce995cd4aaa36f8e966ff6f369686394d0dc4898b
    sha256:15b76ae6ce78a9c81f03c3857b29232d4f3142902c04cc2212efc9602e0d92e2

These identify the local validation image, not an upstream-published image.
Resolved tools were GCC 15.2.1 20250808 (Fedora MinGW 15.2.1-2.fc43), MinGW
CRT/headers 13.0.0-2.fc43, binutils 2.45.1-5.fc43, and SCons 4.10.1.
The image also contains upstream's llvm-mingw 20251118, which was not selected
for these x86/x64 builds.

Dependency archives match the pinned release script: AccessKit 0.22.3,
WinRT headers 72, ANGLE chromium/7578 GCC-13 Release, and Mesa/NIR 25.3.1-3
GCC Release. Archive SHA-256 values are::

    accesskit-c-0.22.3.zip
    41b30b1382d03b225b6f7e8721269a32557a68065804f3797bdffc34a3273498
    winrt-headers.zip
    9310868d0a82d11b7ae1943c4f35c202c6c9a692ee2deec4bde241fbe2639961
    godot-angle-static-x86_64-gcc-13-release.zip
    8b70c38be4120ba73d8f926b1b62929d9c59aebbb389197913ba69939e4815a1
    godot-angle-static-x86_32-gcc-13-release.zip
    09d84c569c28056fe3f7fa96769720245a80ec85deb19a0db55b907ccd30aa53
    godot-nir-static-x86_64-gcc-release.zip
    dc98a962ac90613a4fccf29082d34b12fcb97bf3819517125bc3d1c86ed31afc
    godot-nir-static-x86_32-gcc-release.zip
    bce42c2ddfd1e093c52edf59b6708a00daa8f70634e538027c0b675cb6c33b97

Build configuration
-------------------

The runner reused the ``SCONS`` and ``OPTIONS`` definitions from the pinned
``build-windows/build.sh`` verbatim::

    export SCONS="scons -j${NUM_CORES} verbose=yes warnings=no progress=no redirect_build_objects=no"
    export OPTIONS="production=yes use_mingw=yes angle_libs=/root/angle mesa_libs=/root/mesa d3d12=yes accesskit_sdk_path=/root/accesskit/accesskit-c winrt_path=/root/winrt"

``NUM_CORES=8`` fits this host's WSL memory budget. ``BUILD_NAME=custom_build``
and ``GODOT_VERSION_STATUS=stable`` identify the fork build. Only the targets
needed for this check were selected from the upstream matrix::

    $SCONS platform=windows arch=x86_32 $OPTIONS target=template_debug
    $SCONS platform=windows arch=x86_32 $OPTIONS target=template_release
    $SCONS platform=windows arch=x86_64 $OPTIONS target=editor

Dependencies were mounted at the paths expected by that script. Architectures
used separate build directories and each target's complete ``bin`` output was
preserved separately. Production LTO and the default modules and renderers were
retained. No compiler-free, size-reduced, or test-enabled engine was used.

The top-level multi-platform release orchestration was not run. Signing,
release archive publication, and the remaining target matrix are out of scope.
Application exports use the normal Godot Windows preset and matching custom
template paths, not a hand-built PCK or executable.

Runtime validation
------------------

The matching ``godot.windows.editor.x86_64.exe`` exported the shared
``export_runtime`` project through ``test_export_runtime.py`` using
``--architecture x86_32`` and the two templates below. All four combinations
passed: Debug/Release, each with None/Zstandard compression. Each executed
source-free bytecode on Windows, including sequential, positional, mixed and
dynamic-width formatting checks. Debug recorded zero source-pipeline entries;
Release emitted no load profiling. The missing selected dependency was rejected.

``test_export_compiler.py`` also passed its Debug and Release isolated-worker
cases and rejected the changed-source hash. No native C++ unit-test binary was
built in this configuration; earlier MSVC unit results are separate evidence.

Executable SHA-256 values are::

    godot.windows.editor.x86_64.exe
    61919b0bdb48891dd8556ff2fa962ffe1cc4e6a5abe8d0b50143c928c3233ce6
    godot.windows.template_debug.x86_32.exe
    13f232312633adfac6ec2e17b8f9e43d92a9d6f8869f34816069cf364076fb17
    godot.windows.template_release.x86_32.exe
    fdbba41cbe604f71fc36fc514580ca6884f6a35b62d9fe15f7ba3f0696fca9e1

The first editor build at ``83a7578a1089861918dbeced4f6ed86c45663df7`` exposed
an editor-to-module static-library dependency: ``GDScriptExportBundle`` methods
were referenced after the module archive had been processed. The engine source
above fixes that boundary with an editor-owned interface and a module-registered
factory. No upstream linker flags, module selections or optimization settings
were changed to resolve the failure. Both templates were rebuilt to match that
source's VM fingerprint before the runtime checks.

Full build, dependency, provenance and test logs are retained locally under
``.cache/build/official-windows*``. These are correctness checks, not application
startup, package-size or memory benchmarks. No game or device tests were rerun.
