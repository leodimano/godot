/**************************************************************************/
/*  test_gdscript_bytecode_image_writer.h                                 */
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
#include "../gdscript_bytecode_image.h"
#include "../gdscript_bytecode_view.h"

#include "core/object/ref_counted.h"
#include "tests/test_macros.h"

namespace TestGDScriptBytecodeImageWriter {

TEST_CASE("[GDScript][Bytecode] Metadata writer round trips scalar types") {
	GDScriptBytecodeImageWriter writer;
	Vector<int32_t> packed;
	packed.push_back(-12);
	packed.push_back(345);
	const Variant values[] = {
		Variant(),
		true,
		int64_t(INT64_MAX),
		1.25,
		String("text"),
		StringName("symbol"),
		Vector2(1, 2),
		packed,
	};
	for (const Variant &value : values) {
		Vector<uint8_t> bytes;
		REQUIRE(writer.encode(value, bytes) == OK);
		GDScriptBytecodeImage image;
		REQUIRE(image.open(bytes) == OK);
		CHECK(image.root().get_type() == value.get_type());
		CHECK(image.root().scalar() == value);
	}
}

TEST_CASE("[GDScript][Bytecode] Metadata writer prepares indexed dictionaries") {
	Dictionary values;
	for (int index = 0; index < 64; index++) {
		const String key = "key_" + String::num_int64(index);
		values[index % 2 ? Variant(StringName(key)) : Variant(key)] = index;
	}
	values[42] = "integer key";
	const String latin1_key = String::chr(0xe9);
	values[latin1_key] = "latin1 value";
	Array nested;
	nested.push_back(123);
	nested.push_back(values);
	Dictionary root;
	root["nested"] = nested;
	root["empty_array"] = Array();
	root["empty_dictionary"] = Dictionary();

	GDScriptBytecodeImageWriter writer;
	Vector<uint8_t> bytes;
	REQUIRE(writer.encode(root, bytes) == OK);
	Vector<uint8_t> repeated;
	REQUIRE(writer.encode(root, repeated) == OK);
	CHECK(repeated == bytes);
	GDScriptBytecodeImage image;
	REQUIRE(image.open(bytes) == OK);
	CHECK(image.root()["empty_array"].get_type() == Variant::ARRAY);
	CHECK(image.root()["empty_array"].is_empty());
	CHECK(image.root()["empty_dictionary"].get_type() == Variant::DICTIONARY);
	CHECK(image.root()["empty_dictionary"].is_empty());
	const GDScriptBytecodeView dictionary = image.root()["nested"][1];
	REQUIRE(dictionary.size() == values.size());
	for (int index = 0; index < 64; index++) {
		const String key = "key_" + String::num_int64(index);
		const CharString ascii = key.ascii();
		CHECK(dictionary[ascii.get_data()].scalar() == Variant(index));
		CHECK(dictionary[Variant(key)].scalar() == Variant(index));
		CHECK(dictionary[Variant(StringName(key))].scalar() == Variant(index));
	}
	CHECK(dictionary[Variant(42)].scalar() == Variant("integer key"));
	const char latin1[] = { char(0xe9), 0 };
	CHECK(dictionary[latin1].scalar() == Variant("latin1 value"));
	CHECK_FALSE(dictionary.has("absent"));
}

TEST_CASE("[GDScript][Bytecode] Metadata writer indexes are validated before lookup") {
	Dictionary values;
	for (int index = 0; index < 32; index++) {
		values["key_" + String::num_int64(index)] = index;
	}
	GDScriptBytecodeImageWriter writer;
	Vector<uint8_t> bytes;
	REQUIRE(writer.encode(values, bytes) == OK);
	const uint32_t root = decode_uint32(bytes.ptr() + 20);
	const uint32_t root_offset = GDScriptBytecodeImage::HEADER_SIZE + root * GDScriptBytecodeImage::NODE_SIZE;
	const uint32_t offset = decode_uint32(bytes.ptr() + root_offset + 4);
	const uint32_t count = decode_uint32(bytes.ptr() + root_offset + 8);
	const uint32_t edges = GDScriptBytecodeImage::HEADER_SIZE + decode_uint32(bytes.ptr() + 8) * GDScriptBytecodeImage::NODE_SIZE;
	const uint32_t ordinals = edges + (offset + count * 2) * 4;
	const uint32_t directory = ordinals + count * 4;
	GDScriptBytecodeImage image;
	REQUIRE(image.open(bytes) == OK);
	Vector<uint8_t> invalid_ordinal = bytes;
	encode_uint32(count, invalid_ordinal.ptrw() + ordinals);
	CHECK(image.open(invalid_ordinal) == ERR_INVALID_DATA);
	CHECK(image.root().get_type() == Variant::NIL);
	Vector<uint8_t> duplicate = bytes;
	encode_uint32(decode_uint32(bytes.ptr() + ordinals), duplicate.ptrw() + ordinals + 4);
	CHECK(image.open(duplicate) == ERR_INVALID_DATA);
	Vector<uint8_t> invalid_directory = bytes;
	encode_uint32(1, invalid_directory.ptrw() + directory);
	CHECK(image.open(invalid_directory) == ERR_INVALID_DATA);
}

TEST_CASE("[GDScript][Bytecode] Metadata writer rejects unsupported and recursive values") {
	GDScriptBytecodeImageWriter writer;
	Ref<RefCounted> object;
	object.instantiate();
	const Variant unsupported[] = { object, RID(), Callable(), Signal() };
	Vector<uint8_t> bytes;
	for (const Variant &value : unsupported) {
		REQUIRE(writer.encode(123, bytes) == OK);
		CHECK(writer.encode(value, bytes) == ERR_INVALID_DATA);
		CHECK(bytes.is_empty());
	}
	Array cyclic;
	cyclic.push_back(cyclic);
	const Error cycle_error = writer.encode(cyclic, bytes);
	cyclic.clear(); // Break the reference cycle even when the assertion fails.
	CHECK(cycle_error == ERR_INVALID_DATA);
	CHECK(bytes.is_empty());
	Dictionary unsupported_key;
	unsupported_key[Array()] = 1;
	CHECK(writer.encode(unsupported_key, bytes) == ERR_INVALID_DATA);
	CHECK(bytes.is_empty());
	REQUIRE(writer.encode(456, bytes) == OK);
	GDScriptBytecodeImage image;
	REQUIRE(image.open(bytes) == OK);
	CHECK(image.root().scalar() == Variant(456));
}

} // namespace TestGDScriptBytecodeImageWriter

#endif // TOOLS_ENABLED
