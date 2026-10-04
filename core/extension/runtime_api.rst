Generating an API for a custom runtime
====================================

Native bindings must describe the engine that will load the extension. An API
dump from a full editor can contain classes or methods removed from a custom
export template. Generating bindings from that dump does not restore those
features in the runtime.

Editor and debug template builds support ``--dump-extension-api-to <path>``.
The command writes the normal GDExtension API JSON from the build's registered
classes, then exits before starting the project main loop or autoloads. Use an
empty project without GDExtensions so the dump describes the engine itself.
For example, on a desktop host::

    godot.template_debug --headless --path empty_project --dump-extension-api-to extension_api.json

For Android, export an empty project with the matching debug template and put
``--dump-extension-api-to user://extension_api.json`` in its extra command-line
arguments. Run the exported application and retrieve the file from its private
data directory using the platform's development tools. The application exits
after writing the file; it does not display its main scene.

Build the debug template from the same engine revision, module selection,
precision, and other API-affecting settings as the intended release template.
Generate bindings with the resulting JSON and, when required by the binding
generator, ``core/extension/gdextension_interface.json`` from that engine
revision. Both JSON files are build inputs, not runtime game resources. Do not
filter a full API dump by hand or silently fall back to a stock API. Validate
the resulting extension in both debug and release builds.

The command returns a failure status if it cannot generate or write the file.
Release templates reject the option and do not contain the API dump generator.
The existing editor-only ``--dump-extension-api``, documentation dump, and API
compatibility validation commands remain available unchanged.
