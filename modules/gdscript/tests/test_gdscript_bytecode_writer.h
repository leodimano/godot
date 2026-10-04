/**************************************************************************/
/*  test_gdscript_bytecode_writer.h                                       */
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

#pragma once

#ifdef TOOLS_ENABLED

#include "../editor/gdscript_bytecode_writer.h"
#include "../gdscript_bytecode_data.h"
#include "../gdscript_bytecode_format.h"
#include "../gdscript_bytecode_image.h"
#include "../gdscript_bytecode_instructions.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace GDScriptTests {

TEST_CASE("[GDScript][Bytecode] Graph writer preserves symbolic code and shared metadata") {
	GDScriptLanguage::get_singleton()->init();
	const String source_path = TestUtils::get_temp_path("gdscript_graph_writer.gd");
	const String output_path = TestUtils::get_temp_path("gdscript_graph_writer.gdbc");
	{
		Ref<FileAccess> file = FileAccess::open(source_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(R"(extends Node
const VALUES: Array[int] = [1, 2, 3]
const LOOKUP = {"a": VALUES, "b": VALUES}
class Child extends RefCounted:
	var score: int = 4
signal finished(score: int)
@rpc("any_peer", "call_local", "reliable", 2)
func announce(value: int):
	finished.emit(value)
static func make_adder(offset: int) -> Callable:
	return func(value: int) -> int: return value + offset
static func tick() -> int:
	return Engine.get_process_frames()
)");
	}
	Ref<GDScript> script = ResourceLoader::load(source_path);
	REQUIRE(script.is_valid());
	REQUIRE(script->is_valid());
	TypedArray<GDScript> scripts;
	scripts.push_back(script);
	const Dictionary report = GDScriptBytecodeWriter::save_runtime_scripts(scripts, output_path);
	REQUIRE(int(report["error"]) == OK);
	CHECK(Array(report["script_paths"]).size() == 1);
	CHECK(Dictionary(report["rejected_native_classes"]).is_empty());
	const Vector<uint8_t> first = FileAccess::get_file_as_bytes(output_path);
	REQUIRE(GDScriptBytecodeWriter::save_scripts(scripts, output_path) == OK);
	CHECK(FileAccess::get_file_as_bytes(output_path) == first);
	GDScriptBytecodeImage image;
	REQUIRE(image.open(first) == OK);
	const GDScriptBytecodeView data = image.root();
	CHECK(String(data["abi"]) == GDScriptBytecodeFormat::SCHEMA_ID);
	CHECK(String(data["vm_source_sha256"]).length() == 64);
	CHECK(data["scripts"].size() == 2);
	CHECK(data["native_bindings"].size() > 0);
	const GDScriptBytecodeView root = data["scripts"][0];
	CHECK(String(root["path"]) == source_path);
	CHECK(root["subclasses"].has("Child"));
	CHECK(root["signals"].has("finished"));
	const GDScriptBytecodeView adder = root["functions"]["make_adder"];
	CHECK(adder["lambdas"].size() == 1);
	CHECK(int(adder["lambdas"][0]["capture_count"]) == 1);
	CHECK(GDScriptBytecodeInstructions::validate_layout(adder["code"], adder["instruction_args_size"], adder["default_arguments"]));
	CHECK(String(root["rpc_config"]["kind"]) == "container");
	CHECK(String(root["functions"]["announce"]["rpc_config"]["kind"]) == "container");
	Vector<int> order;
	CHECK(GDScriptBytecodeData::get_container_order(data["containers"], order));
	CHECK(order.size() == data["containers"].size());
	const int shared = root["constants"]["VALUES"]["id"];
	const int lookup = root["constants"]["LOOKUP"]["id"];
	CHECK(int(data["containers"][lookup]["values"][1]["id"]) == shared);
	CHECK(int(data["containers"][lookup]["values"][3]["id"]) == shared);
	CHECK(DirAccess::remove_absolute(output_path) == OK);
	CHECK(DirAccess::remove_absolute(source_path) == OK);
}

TEST_CASE("[GDScript][Bytecode] Graph writer rejects uncompiled inputs and editor-only dependencies") {
	const String output_path = TestUtils::get_temp_path("gdscript_rejected_writer.gdbc");
	TypedArray<GDScript> scripts;
	CHECK(GDScriptBytecodeWriter::save_scripts(scripts, output_path) == ERR_INVALID_PARAMETER);
	Ref<GDScript> invalid;
	invalid.instantiate();
	scripts.push_back(invalid);
	CHECK(GDScriptBytecodeWriter::save_scripts(scripts, output_path) == ERR_INVALID_PARAMETER);
	scripts.clear();
	const String source_path = TestUtils::get_temp_path("gdscript_editor_dependency.gd");
	{
		Ref<FileAccess> file = FileAccess::open(source_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("@tool\nextends Node\nfunc editor_only(plugin: EditorPlugin) -> EditorPlugin:\n\treturn plugin\n");
	}
	Ref<GDScript> script = ResourceLoader::load(source_path);
	REQUIRE(script.is_valid());
	REQUIRE(script->is_valid());
	scripts.push_back(script);
	const Dictionary report = GDScriptBytecodeWriter::save_runtime_scripts(scripts, output_path);
	CHECK(int(report["error"]) == ERR_UNAVAILABLE);
	CHECK(Dictionary(report["rejected_native_classes"]).has(SNAME("EditorPlugin")));
	CHECK_FALSE(FileAccess::exists(output_path));
	CHECK(DirAccess::remove_absolute(source_path) == OK);
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
