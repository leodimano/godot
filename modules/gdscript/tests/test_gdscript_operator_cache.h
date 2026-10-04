/**************************************************************************/
/*  test_gdscript_operator_cache.h                                        */
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

#include "../gdscript.h"
#include "../gdscript_bytecode_instructions.h"
#include "../gdscript_function.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace GDScriptTests {

class TestGDScriptFunctionAccessor {
public:
	static bool has_valid_instruction_layout(const GDScriptFunction *p_function) {
		return GDScriptBytecodeInstructions::validate_layout(p_function->code, p_function->_instruction_args_size, p_function->default_arguments);
	}

	static Vector<int> copy_instructions(const GDScriptFunction *p_function) {
		// A deep copy is required: a COW copy would miss writes through a raw pointer.
		Vector<int> result;
		for (int word : p_function->code) {
			result.push_back(word);
		}
		return result;
	}

	static int cache_count(const GDScriptFunction *p_function) {
		return p_function->operator_caches.size();
	}

	static uint32_t first_signature(const GDScriptFunction *p_function) {
		return p_function->operator_caches[0].signature.get();
	}
};

TEST_CASE("[GDScript][Bytecode] Operator caches preserve immutable instructions") {
	GDScriptLanguage::get_singleton()->init();
	const String path = TestUtils::get_temp_path("gdscript_operator_cache.gd");
	{
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(R"(extends RefCounted
static func add(left, right):
	return left + right
static func negate(value):
	return -value
static func equal(left, right):
	return left == right
)");
	}
	Ref<GDScript> script = ResourceLoader::load(path);
	REQUIRE(script.is_valid());
	REQUIRE(script->is_valid());
	HashMap<StringName, Vector<int>> snapshots;
	for (const StringName &name : { SNAME("add"), SNAME("negate"), SNAME("equal") }) {
		GDScriptFunction *const *function = script->get_member_functions().getptr(name);
		REQUIRE(function != nullptr);
		REQUIRE(TestGDScriptFunctionAccessor::cache_count(*function) == 1);
		CHECK(TestGDScriptFunctionAccessor::has_valid_instruction_layout(*function));
		CHECK(TestGDScriptFunctionAccessor::first_signature(*function) == 0);
		snapshots[name] = TestGDScriptFunctionAccessor::copy_instructions(*function);
	}
	for (int run = 0; run < 3; run++) {
		CHECK(script->call(SNAME("add"), 2, 3) == Variant(5));
		CHECK(script->call(SNAME("add"), "a", "b") == Variant("ab"));
		CHECK(script->call(SNAME("add"), 0.5, 1.0) == Variant(1.5));
		CHECK(script->call(SNAME("negate"), 2) == Variant(-2));
		CHECK(script->call(SNAME("negate"), 0.5) == Variant(-0.5));
		CHECK(script->call(SNAME("equal"), Variant(), Variant()) == Variant(true));
		CHECK(script->call(SNAME("equal"), 2, 3) == Variant(false));
	}
	for (const KeyValue<StringName, Vector<int>> &entry : snapshots) {
		const GDScriptFunction *function = script->get_member_functions()[entry.key];
		CHECK(TestGDScriptFunctionAccessor::copy_instructions(function) == entry.value);
		CHECK(TestGDScriptFunctionAccessor::first_signature(function) != 0);
	}
	const GDScriptFunction *equal = script->get_member_functions()[SNAME("equal")];
	CHECK(TestGDScriptFunctionAccessor::first_signature(equal) == 1); // NIL/NIL is cached, not mistaken for uninitialized.
	CHECK(DirAccess::remove_absolute(path) == OK);
}

} // namespace GDScriptTests
