/**************************************************************************/
/*  test_gdscript_compilation_context.h                                   */
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

#include "../gdscript.h"
#include "../gdscript_compilation_context.h"

#include "core/config/engine.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace GDScriptTests {

TEST_CASE("[GDScript][Bytecode] Isolated compilation suppresses initialization and respects target policy") {
	GDScriptLanguage::get_singleton()->init();
	Object *engine = Engine::get_singleton()->get_singleton_object(SNAME("Engine"));
	REQUIRE(engine != nullptr);
	const StringName initialized = SNAME("gdscript_compile_test_initialized");
	const StringName asserted = SNAME("gdscript_compile_test_asserted");
	const bool compile_only = OS::get_singleton()->get_cmdline_user_args().find("--gdscript-compile-only") != nullptr;
	const bool debug = !compile_only || OS::get_singleton()->get_cmdline_user_args().find("--gdscript-target-release") == nullptr;
	CHECK(GDScriptCompilationContext::is_compile_only() == compile_only);
	CHECK(GDScriptCompilationContext::is_debug_compilation() == debug);
	CHECK(GDScriptLanguage::get_singleton()->should_track_call_stack() == debug);
	if (compile_only && debug) {
		CHECK(GDScriptLanguage::get_singleton()->should_track_locals());
	}
	const String path = TestUtils::get_temp_path("gdscript_isolated_compiler.gd");
	{
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(R"(@tool
extends RefCounted
static func _static_init():
	Engine.set_meta("gdscript_compile_test_initialized", true)
static func mark():
	Engine.set_meta("gdscript_compile_test_asserted", true)
	return true
static func check_assertion():
	assert(mark(), "test")
	return 7
)");
	}
	const uint64_t before = GDScriptCompilationContext::get_source_pipeline_entries();
	Ref<GDScript> script = ResourceLoader::load(path);
	REQUIRE(script.is_valid());
	REQUIRE(script->is_valid());
	CHECK(GDScriptCompilationContext::get_source_pipeline_entries() > before);
	CHECK(engine->has_meta(initialized) == !compile_only);
	CHECK(script->call(SNAME("check_assertion")) == Variant(7));
	CHECK(engine->has_meta(asserted) == debug);
	if (engine->has_meta(initialized)) {
		engine->remove_meta(initialized);
	}
	if (engine->has_meta(asserted)) {
		engine->remove_meta(asserted);
	}
	CHECK(DirAccess::remove_absolute(path) == OK);
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
