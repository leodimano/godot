# Fork development rules

## Upstream build parity

- Godot `4.7.2-stable` is the comparison baseline. Follow upstream C++ style,
  contribution guidelines, and the version-matched official release tooling.
- Use `godotengine/build-containers` and `godotengine/godot-build-scripts` for
  release toolchains, dependency versions, compiler options, and packaging.
  Pin the source and tooling commits; record container digests and resolved
  toolchain versions with validation evidence.
- A supported local compiler or an upstream CI job is not necessarily the
  official release setup. For Windows x86/x64, follow the release scripts'
  MinGW/GCC container build, not the automatically detected MSVC installation.
- Do not add unrelated engine workarounds to accommodate a different local
  toolchain. Check the same behavior with the official release setup first.
  Ask before deviating from its tools or build practices.
- Do not disable engine modules, renderers, or optimizations to get a build or
  test to pass. Keep build outputs separate by toolchain and architecture.
- Preserve earlier local test evidence, but do not present it as official-build
  qualification. Mark untested release configurations as pending.

## Working practices

- Run tests selected by the code change, not the entire game suite each time.
  Keep full logs on disk and report concise summaries or relevant failures.
- Do not open new terminal windows or tabs. Use internal/hidden processes and
  `--no-daemon` for Gradle.
- Make small cohesive commits with explanatory bodies. Do not add release tags,
  version bumps, or compatibility migrations for unreleased development work.
