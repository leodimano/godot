/**************************************************************************/
/*  gdscript_export_compiler.cpp                                          */
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

#include "gdscript_export_compiler.h"

#include "../gdscript_compilation_context.h"
#include "gdscript_bytecode_writer.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

void GDScriptExportCompiler::initialize() {
	OS::get_singleton()->set_exit_code(1);
	const List<String> &arguments = OS::get_singleton()->get_cmdline_user_args();
	const List<String>::Element *request_argument = arguments.find("--gdscript-export-request");
	ERR_FAIL_COND_MSG(!GDScriptCompilationContext::is_compile_only() || !request_argument || !request_argument->next(), "Missing isolated GDScript export request.");
	const String request_path = request_argument->next()->get();
	const Variant decoded = JSON::parse_string(FileAccess::get_file_as_string(request_path));
	ERR_FAIL_COND_MSG(decoded.get_type() != Variant::DICTIONARY, "Invalid isolated GDScript export request.");
	const Dictionary request = decoded;
	ERR_FAIL_COND_MSG(request.get("sources", Variant()).get_type() != Variant::DICTIONARY ||
					request.get("bundle", Variant()).get_type() != Variant::STRING ||
					request.get("response", Variant()).get_type() != Variant::STRING,
			"Incomplete isolated GDScript export request.");
	ERR_FAIL_COND_MSG(request.get("debug", Variant()).get_type() != Variant::BOOL || bool(request["debug"]) != GDScriptCompilationContext::is_debug_compilation(), "Isolated GDScript compiler target does not match its request.");
	const Dictionary sources = request["sources"];
	Array paths = sources.keys();
	paths.sort();
	TypedArray<GDScript> scripts;
	Dictionary result;
	Array excluded;
	Error error = OK;
	for (const String &path : paths) {
		if (!path.begins_with("res://") || sources[path].get_type() != Variant::STRING) {
			result["failure"] = "Invalid compiler source input: " + path;
			error = ERR_INVALID_DATA;
			break;
		}
		Ref<GDScript> script = ResourceLoader::load(path);
		if (script.is_null() || !script->is_valid()) {
			result["failure"] = "Failed to compile exported GDScript: " + path;
			error = ERR_COMPILATION_FAILED;
			break;
		}
		if (script->get_source_code().sha256_text() != String(sources[path])) {
			result["failure"] = "Compiler source differs from export input: " + path;
			error = ERR_INVALID_DATA;
			break;
		}
		const ClassDB::APIType api = ClassDB::get_api_type(script->get_instance_base_type());
		if (api == ClassDB::API_EDITOR || api == ClassDB::API_EDITOR_EXTENSION) {
			excluded.push_back(path);
		} else {
			scripts.push_back(script);
		}
	}
	if (error == OK && !scripts.is_empty()) {
		result = GDScriptBytecodeWriter::save_runtime_scripts(scripts, request["bundle"]);
		error = Error(int(result["error"]));
		if (error != OK) {
			result["failure"] = "The exported script graph cannot be serialized for runtime use.";
		}
	}
	result["error"] = error;
	result["excluded_editor_roots"] = excluded;
	if (!result.has("script_paths")) {
		result["script_paths"] = Array();
	}
	result["source_pipeline_entries"] = GDScriptCompilationContext::get_source_pipeline_entries();
	result["compiler_mode"] = "isolated_native_main_loop";
	result["debug"] = GDScriptCompilationContext::is_debug_compilation();
	Ref<FileAccess> response = FileAccess::open(request["response"], FileAccess::WRITE);
	ERR_FAIL_COND_MSG(response.is_null(), "Cannot write isolated GDScript export response.");
	response->store_string(JSON::stringify(result, "\t", true));
	if (response->get_error() == OK) {
		OS::get_singleton()->set_exit_code(error == OK ? 0 : 1);
	}
}
