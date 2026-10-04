/**************************************************************************/
/*  test_gdscript_bytecode_data.h                                         */
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

#include "../gdscript_bytecode_data.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

static Dictionary make_container_record(const Array &p_values = Array()) {
	Dictionary record;
	record["type"] = Variant::ARRAY;
	record["readonly"] = true;
	record["element"] = Dictionary();
	record["values"] = p_values;
	return record;
}

static Dictionary make_container_reference(int64_t p_index) {
	Dictionary record;
	record["kind"] = "container";
	record["id"] = p_index;
	return record;
}

TEST_CASE("[GDScript][Bytecode] Constant containers are ordered without recursive expansion") {
	Array records;
	records.push_back(make_container_record(Array{ make_container_reference(1), make_container_reference(1) }));
	records.push_back(make_container_record(Array{ make_container_reference(2) }));
	records.push_back(make_container_record());
	Vector<int> order;
	order.push_back(99);
	CHECK(GDScriptBytecodeData::get_container_order(records, order));
	CHECK(order == Vector<int>({ 2, 1, 0 }));
	CHECK(GDScriptBytecodeData::get_container_order(Array(), order));
	CHECK(order.is_empty());
}

TEST_CASE("[GDScript][Bytecode] Constant container cycles and invalid links are rejected") {
	Array records;
	records.push_back(make_container_record(Array{ make_container_reference(1) }));
	records.push_back(make_container_record(Array{ make_container_reference(0) }));
	Vector<int> order;
	CHECK_FALSE(GDScriptBytecodeData::get_container_order(records, order));
	CHECK(order.is_empty());
	for (int64_t invalid : { int64_t(-1), int64_t(2), INT64_MAX }) {
		records[1] = make_container_record(Array{ make_container_reference(invalid) });
		CHECK_FALSE(GDScriptBytecodeData::get_container_order(records, order));
		CHECK(order.is_empty());
	}
	records[1] = 0;
	CHECK_FALSE(GDScriptBytecodeData::get_container_order(records, order));
	Dictionary dictionary = make_container_record(Array{ make_container_reference(0) });
	dictionary["type"] = Variant::DICTIONARY;
	dictionary["key"] = Dictionary();
	records[1] = dictionary;
	CHECK_FALSE(GDScriptBytecodeData::get_container_order(records, order));
}

TEST_CASE("[GDScript][Bytecode] Resolve depth is bounded and restored on scope exit") {
	int depth = Variant::MAX_RECURSION_DEPTH - 1;
	{
		GDScriptBytecodeData::ResolveScope outer(depth);
		CHECK(outer.allowed());
		{
			GDScriptBytecodeData::ResolveScope inner(depth);
			CHECK_FALSE(inner.allowed());
		}
		CHECK(depth == Variant::MAX_RECURSION_DEPTH);
	}
	CHECK(depth == Variant::MAX_RECURSION_DEPTH - 1);
}

} // namespace GDScriptTests
