/**************************************************************************/
/*  test_gdscript_bytecode_image.h                                        */
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

#include "../gdscript_bytecode_image.h"
#include "../gdscript_bytecode_view.h"

#include "tests/test_macros.h"

namespace TestGDScriptBytecodeImage {

static void append_word(Vector<uint8_t> &r_bytes, uint32_t p_word) {
	const int offset = r_bytes.size();
	r_bytes.resize(offset + 4);
	encode_uint32(p_word, r_bytes.ptrw() + offset);
}

// An independent disk-format fixture, not output from the writer under test.
// Root: [{"answer": 42, "empty": null}, 42]. Container edges are postordered.
static Vector<uint8_t> make_image() {
	Vector<uint8_t> bytes;
	const uint32_t words[] = {
		GDScriptBytecodeImage::MAGIC,
		GDScriptBytecodeImage::VERSION,
		6,
		4,
		6,
		5,
		0,
		0,
		0,
		0,
		1,
		0,
		0,
		2,
		0,
		0,
		3,
		0,
		Variant::DICTIONARY,
		0,
		2,
		Variant::ARRAY,
		4,
		2,
		0,
		1,
		2,
		3,
		4,
		1,
	};
	for (uint32_t word : words) {
		append_word(bytes, word);
	}
	const Variant atoms[] = { String("answer"), 42, String("empty"), Variant() };
	for (const Variant &atom : atoms) {
		append_word(bytes, GDScriptBytecodeImage::key_hash(atom));
	}
	for (const Variant &atom : atoms) {
		int length = 0;
		REQUIRE(encode_variant(atom, nullptr, length, false) == OK);
		append_word(bytes, length);
		const int offset = bytes.size();
		bytes.resize(offset + length);
		REQUIRE(encode_variant(atom, bytes.ptrw() + offset, length, false) == OK);
	}
	return bytes;
}

TEST_CASE("[GDScript][Bytecode] Traverse validated metadata without container hydration") {
	GDScriptBytecodeImage image;
	Vector<uint8_t> bytes = make_image();
	REQUIRE(image.open(bytes) == OK);
	bytes.clear(); // The image retains its immutable buffer.
	const GDScriptBytecodeView root = image.root();
	CHECK(root.get_type() == Variant::ARRAY);
	CHECK(root.size() == 2);
	CHECK(root[-1].get_type() == Variant::NIL);
	CHECK(root[2].get_type() == Variant::NIL);
	CHECK(root.keys().get_type() == Variant::NIL);
	CHECK(root[1].scalar() == Variant(42));

	const GDScriptBytecodeView dictionary = root[0];
	CHECK(dictionary.get_type() == Variant::DICTIONARY);
	CHECK(dictionary.size() == 2);
	CHECK(dictionary["answer"].scalar() == Variant(42));
	CHECK(dictionary[Variant(StringName("answer"))].scalar() == Variant(42));
	CHECK(dictionary.has("empty"));
	CHECK(dictionary.get("empty", true).get_type() == Variant::NIL);
	CHECK_FALSE(dictionary.has("missing"));
	CHECK(dictionary.get("missing", true) == Variant(true));
	CHECK_FALSE(dictionary.has(static_cast<const char *>(nullptr)));
	CHECK(dictionary[0].get_type() == Variant::NIL);
	const GDScriptBytecodeView keys = dictionary.keys();
	REQUIRE(keys.size() == 2);
	CHECK(keys[0].scalar() == Variant("answer"));
	CHECK(keys[1].scalar() == Variant("empty"));
	int count = 0;
	for (const GDScriptBytecodeView &key : keys) {
		CHECK(dictionary.has(key.scalar()));
		count++;
	}
	CHECK(count == 2);
}

TEST_CASE("[GDScript][Bytecode] Reject malformed metadata and invalidate the root") {
	const Vector<uint8_t> valid = make_image();
	GDScriptBytecodeImage image;
	for (const int offset : { 0, 4, 8, 12, 16, 20 }) {
		REQUIRE(image.open(valid) == OK);
		Vector<uint8_t> damaged = valid;
		encode_uint32(UINT32_MAX, damaged.ptrw() + offset);
		CHECK(image.open(damaged) == ERR_INVALID_DATA);
		CHECK(image.root().get_type() == Variant::NIL);
	}
	for (int length = 0; length < valid.size(); length++) {
		Vector<uint8_t> truncated = valid;
		truncated.resize(length);
		CHECK(image.open(truncated) == ERR_INVALID_DATA);
		CHECK(image.root().get_type() == Variant::NIL);
	}
	Vector<uint8_t> trailing = valid;
	trailing.push_back(0);
	CHECK(image.open(trailing) == ERR_INVALID_DATA);
	CHECK(image.root().get_type() == Variant::NIL);

	const uint32_t edges_begin = GDScriptBytecodeImage::HEADER_SIZE + 6 * GDScriptBytecodeImage::NODE_SIZE;
	Vector<uint8_t> cyclic = valid;
	encode_uint32(5, cyclic.ptrw() + edges_begin + 4 * 4);
	CHECK(image.open(cyclic) == ERR_INVALID_DATA);
	CHECK(image.root().get_type() == Variant::NIL);
	Vector<uint8_t> container_key = valid;
	encode_uint32(4, container_key.ptrw() + edges_begin);
	CHECK(image.open(container_key) == ERR_INVALID_DATA);

	const uint32_t hashes_begin = edges_begin + 6 * 4;
	Vector<uint8_t> bad_hash = valid;
	bad_hash.write[hashes_begin] ^= 1;
	CHECK(image.open(bad_hash) == ERR_INVALID_DATA);
	const uint32_t first_atom = hashes_begin + 4 * 4 + 4;
	for (const Variant::Type type : { Variant::OBJECT, Variant::RID, Variant::CALLABLE, Variant::SIGNAL, Variant::ARRAY, Variant::DICTIONARY }) {
		Vector<uint8_t> forbidden = valid;
		encode_uint32(type, forbidden.ptrw() + first_atom);
		CHECK(image.open(forbidden) == ERR_INVALID_DATA);
		CHECK(image.root().get_type() == Variant::NIL);
	}
	REQUIRE(image.open(valid) == OK); // A failed open does not poison the reader.
	CHECK(image.root()[1].scalar() == Variant(42));
}

} // namespace TestGDScriptBytecodeImage
