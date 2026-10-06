# Android build tooling

The Android build uses the checked-in Gradle wrapper and the Android Gradle
Plugin version in `app/config.gradle`. R8 and Kotlin come from AGP's supported
dependency set; do not override them independently to preview versions.
Java source and bytecode compatibility remain at the configured Java 17 level.

AGP 9 uses built-in Kotlin and the public `androidComponents` variant API.
Do not reapply `org.jetbrains.kotlin.android` or disable the new DSL to restore
legacy variant callbacks. Kotlin's JVM target follows Android's Java target.

Build native libraries with the version-matched official Godot release
toolchain first, then package both templates from this directory:

```sh
./gradlew --no-daemon generateGodotTemplates
```

This reuses existing native libraries unless `generateNativeLibs` is explicitly
requested. The Gradle modules keep AGP's default intermediate APK/AAR names.
The existing copy tasks assign Godot's public template names in `bin/` and the
exported game's requested filename. Keep those names stable for the editor and
release packaging scripts.

When updating the wrapper, regenerate its scripts and JAR with the wrapper task
and verify the distribution and wrapper checksums against Gradle's releases.
Validate both template generation and an exported project: they are separate
Gradle roots with different resource and signing settings. Include an unsigned
Release APK, a signed Debug APK, and an app bundle in packaging checks.
Do not publish or install a production-package build as part of those checks.

Exercise the resolved template settings with the focused Gradle init script:

```sh
./gradlew --no-daemon -p /path/to/generated/android/build -I /path/to/godot/platform/android/java/tests/export_configuration.gradle verifyGodotExportConfiguration -Penable_minification=true -Pexport_path=file:/tmp/godot-export
```

Repeat with minification disabled and with a plain native export path. The
configuration check does not qualify an APK; retain package and device evidence
for changes to the optimizer or its keep rules.

Migration references:

- https://developer.android.com/build/releases/agp-9-0-0-release-notes
- https://developer.android.com/build/migrate-to-built-in-kotlin
- https://docs.gradle.org/current/userguide/gradle_wrapper.html
