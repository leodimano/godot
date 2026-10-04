/**************************************************************************/
/*  test_gdscript_bytecode_reader.h                                       */
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

#include "../editor/gdscript_bytecode_image_writer.h"
#include "../editor/gdscript_bytecode_writer.h"
#include "../gdscript_bytecode_reader.h"
#include "../gdscript_cache.h"
#include "../gdscript_compilation_context.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace GDScriptTests {

class BytecodeReaderFixture {
public:
	String source_path;
	String output_path;
	String resource_path;
	Ref<GDScript> source;

	BytecodeReaderFixture() {
		static uint64_t sequence = 0;
		const String stem = "gdscript_reader_" + uitos(++sequence);
		source_path = TestUtils::get_temp_path(stem + ".gd");
		output_path = TestUtils::get_temp_path(stem + ".gdbc");
	}

	bool compile(const String &p_code) {
		GDScriptLanguage::get_singleton()->init();
		Ref<FileAccess> file = FileAccess::open(source_path, FileAccess::WRITE);
		if (file.is_null()) {
			return false;
		}
		file->store_string(p_code);
		file.unref();
		source = ResourceLoader::load(source_path);
		if (source.is_valid()) {
			resource_path = source->get_script_path();
		}
		return source.is_valid() && source->is_valid();
	}

	Error save() const {
		TypedArray<GDScript> scripts;
		scripts.push_back(source);
		return GDScriptBytecodeWriter::save_scripts(scripts, output_path);
	}

	~BytecodeReaderFixture() {
		if (!resource_path.is_empty()) {
			GDScriptCache::remove_script(resource_path);
		}
		if (FileAccess::exists(source_path + ".remap")) {
			DirAccess::remove_absolute(source_path + ".remap");
		}
		if (FileAccess::exists(source_path)) {
			DirAccess::remove_absolute(source_path);
		}
		if (FileAccess::exists(output_path)) {
			DirAccess::remove_absolute(output_path);
		}
	}
};

TEST_CASE("[GDScript][Bytecode] Reader executes restored functions without source compilation") {
	BytecodeReaderFixture fixture;
	REQUIRE(fixture.compile(R"(@static_unload
extends RefCounted
const ITEMS: Array[int] = [1, 2, 3]
const PAIR = {"left": ITEMS, "right": ITEMS}
static var generation: int = 7
class Child extends RefCounted:
	var score: int = 4
static func compute(offset: int) -> int:
	var operation = func(value: int) -> int: return value + offset
	return operation.call(3)
static func tick() -> int:
	return Engine.get_process_frames()
static func vector_x(value: Vector2) -> float:
	value.x += 2.0
	return value.x
)"));
	fixture.source->set("generation", 99);
	REQUIRE(fixture.save() == OK);
	const uint64_t before = GDScriptCompilationContext::get_source_pipeline_entries();
	Ref<GDScript> restored = GDScriptBytecodeReader::load(fixture.output_path);
	REQUIRE(restored.is_valid());
	if (restored.is_null()) {
		return;
	}
	CHECK(restored->is_valid());
	CHECK(restored != fixture.source);
	CHECK(restored->get("generation") == Variant(7));
	CHECK(fixture.source->get("generation") == Variant(99));
	CHECK(restored->call("compute", 10) == Variant(13));
	CHECK(restored->call("tick") == fixture.source->call("tick"));
	CHECK(restored->call("vector_x", Vector2(3, 4)) == Variant(5.0));
	const Array items = restored->get("ITEMS");
	const Dictionary pair = restored->get("PAIR");
	CHECK(items.is_read_only());
	CHECK(items.get_typed_builtin() == Variant::INT);
	CHECK(items.is_same_instance(pair["left"]));
	CHECK(items.is_same_instance(pair["right"]));
	Ref<GDScript> child = restored->get("Child");
	REQUIRE(child.is_valid());
	Ref<RefCounted> instance = child->call("new");
	REQUIRE(instance.is_valid());
	CHECK(instance->get("score") == Variant(4));
	CHECK(GDScriptCompilationContext::get_source_pipeline_entries() == before);
}

TEST_CASE("[GDScript][Bytecode] Reader preserves script and function RPC dictionaries") {
	BytecodeReaderFixture fixture;
	REQUIRE(fixture.compile("extends Node\n@rpc(\"any_peer\", \"call_local\", \"reliable\", 2)\nfunc announce(value: int):\n\tpass\n"));
	const Variant expected = fixture.source->get_rpc_config();
	REQUIRE(Dictionary(expected).size() == 1);
	REQUIRE(fixture.save() == OK);
	Ref<GDScript> restored = GDScriptBytecodeReader::load(fixture.output_path);
	REQUIRE(restored.is_valid());
	if (restored.is_null()) {
		return;
	}
	CHECK(restored->get_rpc_config() == expected);
	const Variant function_config = restored->get_member_functions()["announce"]->get_rpc_config();
	CHECK(function_config == fixture.source->get_member_functions()["announce"]->get_rpc_config());
	CHECK(Dictionary(function_config)["channel"] == Variant(2));
}

#ifdef THREADS_ENABLED
TEST_CASE("[GDScript][Bytecode] Reader publishes one graph through concurrent export remaps") {
	struct ResourcePathScope {
		String previous = TestProjectSettingsInternalsAccessor::resource_path();
		ResourcePathScope() {
			TestProjectSettingsInternalsAccessor::resource_path() = TestUtils::get_temp_path("").trim_suffix("/");
		}
		~ResourcePathScope() {
			TestProjectSettingsInternalsAccessor::resource_path() = previous;
		}
	} resource_path_scope;
	BytecodeReaderFixture peer;
	BytecodeReaderFixture root;
	REQUIRE(peer.compile("@static_unload\nextends RefCounted\nstatic func value() -> int:\n\treturn 11\n"));
	REQUIRE(root.compile("@static_unload\nextends RefCounted\nconst Peer = preload(\"" + peer.resource_path + "\")\nstatic func value() -> int:\n\treturn Peer.value() + 1\n"));
	REQUIRE(root.resource_path.begins_with("res://"));
	REQUIRE(peer.resource_path.begins_with("res://"));
	REQUIRE(root.save() == OK);
	for (const String &path : { root.source_path, peer.source_path }) {
		Ref<FileAccess> file = FileAccess::open(path + ".remap", FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("[remap]\npath=\"res://" + root.output_path.get_file() + "\"\n");
	}
	// Model separate compiler/runtime processes: do not leave writer-owned
	// resources in the shared ResourceCache. The language runner can already
	// have shut down GDScriptCache, making remove_script() alone a no-op.
	root.source->Resource::set_path(String());
	peer.source->Resource::set_path(String());
	GDScriptCache::remove_script(root.resource_path);
	GDScriptCache::remove_script(peer.resource_path);
	root.source.unref();
	peer.source.unref();
	REQUIRE_FALSE(ResourceCache::has(root.resource_path));
	REQUIRE_FALSE(ResourceCache::has(peer.resource_path));
	if (ResourceCache::has(root.resource_path) || ResourceCache::has(peer.resource_path)) {
		return; // Never let cached source scripts make this test pass vacuously.
	}
	CHECK(DirAccess::remove_absolute(root.source_path) == OK);
	CHECK(DirAccess::remove_absolute(peer.source_path) == OK);
	const uint64_t before = GDScriptCompilationContext::get_source_pipeline_entries();
	REQUIRE(ResourceLoader::load_threaded_request(root.resource_path, "GDScript", true) == OK);
	REQUIRE(ResourceLoader::load_threaded_request(peer.resource_path, "GDScript", true) == OK);
	Ref<GDScript> loaded_root = ResourceLoader::load_threaded_get(root.resource_path);
	Ref<GDScript> loaded_peer = ResourceLoader::load_threaded_get(peer.resource_path);
	REQUIRE(loaded_root.is_valid());
	REQUIRE(loaded_peer.is_valid());
	if (loaded_root.is_null() || loaded_peer.is_null()) {
		return;
	}
	CHECK(loaded_root->call("value") == Variant(12));
	CHECK(Ref<GDScript>(loaded_root->get("Peer")) == loaded_peer);
	CHECK(loaded_root->get_source_code().is_empty());
	CHECK(loaded_peer->get_source_code().is_empty());
	CHECK(ResourceLoader::load(root.resource_path) == loaded_root);
	CHECK(ResourceLoader::load(peer.resource_path) == loaded_peer);
	CHECK(GDScriptCompilationContext::get_source_pipeline_entries() == before);
}
#endif // THREADS_ENABLED

// Mutations need a small owning copy in tests only. The runtime reader never
// materializes the whole metadata document into Array/Dictionary objects.
static Variant copy_bytecode_test_metadata(const GDScriptBytecodeView &p_view) {
	if (p_view.get_type() == Variant::ARRAY) {
		Array result;
		for (const GDScriptBytecodeView &element : p_view) {
			result.push_back(copy_bytecode_test_metadata(element));
		}
		return result;
	}
	if (p_view.get_type() == Variant::DICTIONARY) {
		Dictionary result;
		for (const Variant &key : p_view.keys()) {
			result[key] = copy_bytecode_test_metadata(p_view[key]);
		}
		return result;
	}
	return p_view.scalar();
}

TEST_CASE("[GDScript][Bytecode] Reader rejects incompatible graphs and malformed instructions") {
	BytecodeReaderFixture fixture;
	REQUIRE(fixture.compile("extends RefCounted\nstatic func tick() -> int:\n\treturn Engine.get_process_frames()\n"));
	REQUIRE(fixture.save() == OK);
	GDScriptBytecodeImage image;
	REQUIRE(image.open(FileAccess::get_file_as_bytes(fixture.output_path)) == OK);
	Dictionary data = copy_bytecode_test_metadata(image.root());
	SUBCASE("Legacy pointer-dependent schema") {
		data["abi"] = "godot-gdscript-bytecode-v1";
		data["pointer_size"] = int(sizeof(void *));
	}
	SUBCASE("Real-number precision differs") {
		data["real_size"] = sizeof(real_t) == 4 ? 8 : 4;
	}
	SUBCASE("Source identity differs") {
		data["vm_source_sha256"] = "different-build";
	}
	SUBCASE("Build profile differs") {
		data["debug"] = !bool(data["debug"]);
	}
	SUBCASE("Native method signature differs") {
		Array bindings = data["native_bindings"];
		Array binding = bindings[0];
		binding[2] = int64_t(binding[2]) ^ 1;
	}
	SUBCASE("Inheritance is cyclic") {
		Dictionary root = Array(data["scripts"])[0];
		root["base"] = 0;
	}
	SUBCASE("Instruction layout is invalid") {
		Dictionary root = Array(data["scripts"])[0];
		Dictionary function = Dictionary(root["functions"])["tick"];
		function["code"] = Vector<int32_t>({ INT32_MAX });
	}
	GDScriptBytecodeImageWriter writer;
	Vector<uint8_t> bytes;
	REQUIRE(writer.encode(data, bytes) == OK);
	{
		Ref<FileAccess> file = FileAccess::open(fixture.output_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_buffer(bytes);
	}
	const uint64_t before = GDScriptCompilationContext::get_source_pipeline_entries();
	ERR_PRINT_OFF;
	Ref<GDScript> rejected = GDScriptBytecodeReader::load(fixture.output_path);
	ERR_PRINT_ON;
	CHECK(rejected.is_null());
	CHECK(GDScriptCompilationContext::get_source_pipeline_entries() == before);
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
