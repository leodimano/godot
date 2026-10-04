/**************************************************************************/
/*  gdscript_export_bundle.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "gdscript_export_bundle.h"

#include "../gdscript_bytecode_envelope.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access_memory.h"
#include "core/io/json.h"
#include "core/io/resource_format_binary.h"
#include "core/os/os.h"

Error GDScriptExportBundle::collect_file(const String &p_path, const Vector<uint8_t> &p_data, bool &r_skip) {
	r_skip = false;
	const String path = p_path.simplify_path();
	const String extension = path.get_extension().to_lower();
	if (extension == "gd") {
		if (finished || !path.begins_with("res://") || !FileAccess::exists(path) || FileAccess::get_file_as_bytes(path) != p_data) {
			failure = "Compiled export cannot resolve an unchanged source input: " + path;
			return ERR_INVALID_DATA;
		}
		sources.insert(path);
		source_hashes.insert(path, String::utf8(reinterpret_cast<const char *>(p_data.ptr()), p_data.size()).sha256_text());
		r_skip = true;
		return OK;
	}
	if (extension == "gdc" || extension == "gdbc" || path.ends_with(".gd.remap")) {
		failure = "Compiled export received an unexpected preexisting script artifact: " + path;
		return ERR_INVALID_DATA;
	}
	if (p_data.size() >= 4 && (memcmp(p_data.ptr(), "RSRC", 4) == 0 || memcmp(p_data.ptr(), "RSCC", 4) == 0)) {
		Ref<FileAccessMemory> memory;
		memory.instantiate();
		memory->open_custom(p_data.ptr(), p_data.size());
		ResourceLoaderBinary inspector;
		Dictionary inventory;
		Error error = inspector.get_resource_manifest(memory, inventory);
		if (error != OK) {
			failure = "Cannot inspect exported binary resource: " + path;
			return error;
		}
		const Array internal = inventory["internal_resources"];
		for (const Dictionary &resource : internal) {
			const String type = resource["type"];
			if (type == "Script" || type == "GDScript") {
				failure = "Embedded script conversion is not implemented: " + path;
				return ERR_UNAVAILABLE;
			}
		}
		const Array external = inventory["external_resources"];
		for (const Dictionary &resource : external) {
			const String type = resource["type"];
			String dependency = resource["path"];
			const String dependency_extension = dependency.get_extension().to_lower();
			if (type == "Script" || type == "GDScript" || dependency_extension == "gd" || dependency_extension == "gdc" || dependency_extension == "gdbc") {
				if (!dependency.begins_with("res://")) {
					dependency = path.get_base_dir().path_join(dependency).simplify_path();
				}
				resource_dependencies.insert(dependency, path);
				if (finished && !runtime_paths.has(dependency)) {
					failure = "Exported resource references an absent runtime script: " + path + " -> " + dependency;
					return ERR_FILE_MISSING_DEPENDENCIES;
				}
			}
		}
	} else if (extension == "tscn" || extension == "tres" || extension == "scn" || extension == "res" || extension == "pck" || (p_data.size() >= 4 && memcmp(p_data.ptr(), "GDPC", 4) == 0)) {
		failure = "Compiled export requires inspectable binary resources and no nested packs: " + path;
		return ERR_UNAVAILABLE;
	}
	return OK;
}

Error GDScriptExportBundle::finish(HashSet<String> &r_selected_paths, Vector<String> &r_remaps, bool p_debug, int p_compression) {
	ERR_FAIL_COND_V(finished, ERR_ALREADY_IN_USE);
	ERR_FAIL_COND_V(p_compression != 0 && p_compression != 1, ERR_INVALID_PARAMETER);
	finished = true;
	Vector<String> sorted_sources;
	for (const String &path : sources) {
		sorted_sources.push_back(path);
	}
	sorted_sources.sort();
	Dictionary compiler_sources;
	for (const String &path : sorted_sources) {
		compiler_sources[path] = source_hashes[path];
	}
	if (!sources.is_empty()) {
		const String directory = ProjectSettings::get_singleton()->get_project_data_path().path_join(vformat("exported/gdscript-%d-%d", OS::get_singleton()->get_process_id(), OS::get_singleton()->get_ticks_usec()));
		Error error = DirAccess::make_dir_recursive_absolute(directory);
		if (error != OK) {
			failure = "Cannot create isolated compiler evidence directory: " + directory;
			return error;
		}
		const String request_path = ProjectSettings::get_singleton()->globalize_path(directory.path_join("request.json"));
		const String response_path = ProjectSettings::get_singleton()->globalize_path(directory.path_join("response.json"));
		const String bundle_path = ProjectSettings::get_singleton()->globalize_path(directory.path_join("scripts.gdbc"));
		const String log_path = ProjectSettings::get_singleton()->globalize_path(directory.path_join("compiler.log"));
		Dictionary request;
		request["sources"] = compiler_sources;
		request["response"] = response_path;
		request["bundle"] = bundle_path;
		request["debug"] = p_debug;
		Ref<FileAccess> request_file = FileAccess::open(request_path, FileAccess::WRITE);
		if (request_file.is_null()) {
			failure = "Cannot write isolated compiler request.";
			return ERR_FILE_CANT_WRITE;
		}
		request_file->store_string(JSON::stringify(request, "\t", true));
		if (request_file->get_error() != OK) {
			failure = "Cannot write isolated compiler request.";
			return ERR_FILE_CANT_WRITE;
		}
		request_file.unref();
		List<String> arguments;
		arguments.push_back("--headless");
		arguments.push_back("--path");
		arguments.push_back(ProjectSettings::get_singleton()->get_resource_path());
		arguments.push_back("--main-loop");
		arguments.push_back("GDScriptExportCompiler");
		arguments.push_back("--");
		arguments.push_back("--gdscript-compile-only");
		if (!p_debug) {
			arguments.push_back("--gdscript-target-release");
		}
		arguments.push_back("--gdscript-export-request");
		arguments.push_back(request_path);
		String output;
		int exit_code = -1;
		error = OS::get_singleton()->execute(OS::get_singleton()->get_executable_path(), arguments, &output, &exit_code, true, nullptr, false);
		Ref<FileAccess> log_file = FileAccess::open(log_path, FileAccess::WRITE);
		if (log_file.is_valid()) {
			log_file->store_string(output);
		}
		log_file.unref();
		bool diagnostics = false;
		for (const String &line : output.split("\n")) {
			const String stripped = line.strip_edges();
			diagnostics |= stripped.begins_with("ERROR:") || stripped.begins_with("WARNING:") || stripped.begins_with("SCRIPT ERROR:");
		}
		const Variant decoded = FileAccess::exists(response_path) ? JSON::parse_string(FileAccess::get_file_as_string(response_path)) : Variant();
		if (decoded.get_type() == Variant::DICTIONARY) {
			manifest = decoded;
		}
		if (error != OK || exit_code != 0 || diagnostics || int(manifest.get("error", ERR_INVALID_DATA)) != OK) {
			failure = String(manifest.get("failure", "Isolated GDScript compiler failed.")) + " Compiler log: " + log_path;
			return ERR_COMPILATION_FAILED;
		}
		if (manifest.get("debug", Variant()).get_type() != Variant::BOOL || bool(manifest["debug"]) != p_debug) {
			failure = "Isolated GDScript compiler returned a different build profile.";
			return ERR_INVALID_DATA;
		}
		const Array excluded = manifest["excluded_editor_roots"];
		const Array paths = manifest["script_paths"];
		for (const String &path : paths) {
			if (!sources.has(path) || excluded.has(path)) {
				failure = "Compiled script dependency is absent from the export selection: " + path;
				return ERR_FILE_MISSING_DEPENDENCIES;
			}
			runtime_paths.insert(path);
		}
		if (!paths.is_empty()) {
			bytecode = FileAccess::get_file_as_bytes(bundle_path);
		}
		if (!paths.is_empty() && bytecode.is_empty()) {
			failure = "The compiled script bundle is empty.";
			return ERR_FILE_CANT_READ;
		}
		// Keep the request and compiler log on failure, but do not accumulate
		// successful export artifacts in the project's import cache.
		DirAccess::remove_absolute(request_path);
		DirAccess::remove_absolute(response_path);
		if (FileAccess::exists(bundle_path)) {
			DirAccess::remove_absolute(bundle_path);
		}
		DirAccess::remove_absolute(log_path);
		DirAccess::remove_absolute(directory);
	}
	for (const KeyValue<String, String> &dependency : resource_dependencies) {
		if (!runtime_paths.has(dependency.key)) {
			failure = "Exported resource references an absent runtime script: " + dependency.value + " -> " + dependency.key;
			return ERR_FILE_MISSING_DEPENDENCIES;
		}
	}
	Vector<String> removed_paths;
	for (const String &path : r_selected_paths) {
		if (path.get_extension().to_lower() == "gd" && !runtime_paths.has(path)) {
			removed_paths.push_back(path);
		}
	}
	for (const String &path : removed_paths) {
		r_selected_paths.erase(path);
	}
	for (const String &path : sorted_sources) {
		if (runtime_paths.has(path)) {
			r_remaps.push_back(path);
			r_remaps.push_back(BUNDLE_PATH);
		}
	}
	if (!manifest.has("excluded_editor_roots")) {
		manifest["excluded_editor_roots"] = Array();
		manifest["script_paths"] = Array();
	}
	manifest["format"] = "godot-gdscript-compiled-export-v1";
	manifest["compression"] = p_compression == 1 ? "zstd" : "none";
	manifest["decoded_bytes"] = bytecode.size();
	if (!bytecode.is_empty()) {
		Vector<uint8_t> stored;
		const Error error = GDScriptBytecodeEnvelope::encode(bytecode, GDScriptBytecodeEnvelope::CompressionMode(p_compression), stored);
		if (error != OK) {
			failure = "Cannot encode compiled script storage envelope.";
			return error;
		}
		bytecode = stored;
	}
	manifest["stored_bytes"] = bytecode.size();
	return OK;
}
