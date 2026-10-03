/**************************************************************************/
/*  test_gdscript_bytecode_envelope.h                                     */
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

#include "../gdscript_bytecode_envelope.h"

#include "core/io/file_access_memory.h"
#include "core/io/marshalls.h"
#include "tests/test_macros.h"

namespace TestGDScriptBytecodeEnvelope {

// Framing tests intentionally do not depend on the metadata or VM decoder.
static Vector<uint8_t> make_image(int p_size = 4096) {
	Vector<uint8_t> image;
	image.resize(p_size);
	image.fill(0);
	encode_uint32(GDScriptBytecodeFormat::IMAGE_MAGIC, image.ptrw());
	encode_uint32(GDScriptBytecodeFormat::IMAGE_VERSION, image.ptrw() + 4);
	return image;
}

TEST_CASE("[GDScript][Bytecode] Storage round trip and aliased buffers") {
	const Vector<uint8_t> image = make_image();
	for (const GDScriptBytecodeEnvelope::CompressionMode mode : { GDScriptBytecodeEnvelope::COMPRESSION_NONE, GDScriptBytecodeEnvelope::COMPRESSION_ZSTD }) {
		Vector<uint8_t> stored;
		REQUIRE(GDScriptBytecodeEnvelope::encode(image, mode, stored) == OK);
		Vector<uint8_t> decoded;
		CHECK(GDScriptBytecodeEnvelope::decode(stored, decoded) == OK);
		CHECK(decoded == image);
		if (mode == GDScriptBytecodeEnvelope::COMPRESSION_ZSTD) {
			CHECK(stored.size() < image.size());
		} else {
			CHECK(stored == image);
		}

		Vector<uint8_t> aliased = image;
		REQUIRE(GDScriptBytecodeEnvelope::encode(aliased, mode, aliased) == OK);
		CHECK(aliased == stored);
		REQUIRE(GDScriptBytecodeEnvelope::decode(aliased, aliased) == OK);
		CHECK(aliased == image);
	}
}

TEST_CASE("[GDScript][Bytecode] Incompressible and minimum-size images") {
	for (const int size : { int(GDScriptBytecodeFormat::IMAGE_HEADER_SIZE), 65536 }) {
		Vector<uint8_t> image = make_image(size);
		uint32_t random = 0x12345678;
		for (int i = GDScriptBytecodeFormat::IMAGE_HEADER_SIZE; i < size; i++) {
			random ^= random << 13;
			random ^= random >> 17;
			random ^= random << 5;
			image.write[i] = uint8_t(random);
		}
		Vector<uint8_t> stored;
		REQUIRE(GDScriptBytecodeEnvelope::encode(image, GDScriptBytecodeEnvelope::COMPRESSION_ZSTD, stored) == OK);
		Vector<uint8_t> decoded;
		CHECK(GDScriptBytecodeEnvelope::decode(stored, decoded) == OK);
		CHECK(decoded == image);
	}
}

TEST_CASE("[GDScript][Bytecode] Reject invalid headers, truncation and corruption") {
	Vector<uint8_t> stored;
	REQUIRE(GDScriptBytecodeEnvelope::encode(make_image(), GDScriptBytecodeEnvelope::COMPRESSION_ZSTD, stored) == OK);
	Vector<uint8_t> decoded;
	for (const int offset : { 0, 4, 8, 12, 16, 20, int(GDScriptBytecodeEnvelope::HEADER_SIZE) }) {
		Vector<uint8_t> damaged = stored;
		damaged.write[offset] ^= 0xff;
		decoded = make_image();
		CHECK(GDScriptBytecodeEnvelope::decode(damaged, decoded) == ERR_INVALID_DATA);
		CHECK(decoded.is_empty());
	}
	for (int length = 0; length < stored.size(); length++) {
		Vector<uint8_t> truncated = stored;
		truncated.resize(length);
		CHECK(GDScriptBytecodeEnvelope::decode(truncated, decoded) == ERR_INVALID_DATA);
		CHECK(decoded.is_empty());
	}
	Vector<uint8_t> oversized = stored;
	encode_uint32(GDScriptBytecodeFormat::MAX_IMAGE_BYTES + 1, oversized.ptrw() + 12);
	CHECK(GDScriptBytecodeEnvelope::decode(oversized, decoded) == ERR_INVALID_DATA);
	CHECK(decoded.is_empty());

	Vector<uint8_t> trailing = stored;
	trailing.push_back(0);
	// Even if the outer length is corrected, trailing frame data must be rejected.
	encode_uint32(trailing.size() - GDScriptBytecodeEnvelope::HEADER_SIZE, trailing.ptrw() + 16);
	CHECK(GDScriptBytecodeEnvelope::decode(trailing, decoded) == ERR_INVALID_DATA);
	CHECK(decoded.is_empty());

	Vector<uint8_t> concatenated = stored;
	for (int i = GDScriptBytecodeEnvelope::HEADER_SIZE; i < stored.size(); i++) {
		concatenated.push_back(stored[i]);
	}
	encode_uint32(concatenated.size() - GDScriptBytecodeEnvelope::HEADER_SIZE, concatenated.ptrw() + 16);
	CHECK(GDScriptBytecodeEnvelope::decode(concatenated, decoded) == ERR_INVALID_DATA);
	CHECK(decoded.is_empty());
}

TEST_CASE("[GDScript][Bytecode] Invalid encode arguments clear output") {
	Vector<uint8_t> output = make_image();
	CHECK(GDScriptBytecodeEnvelope::encode(make_image(), GDScriptBytecodeEnvelope::CompressionMode(99), output) == ERR_INVALID_PARAMETER);
	CHECK(output.is_empty());
	CHECK(GDScriptBytecodeEnvelope::encode(Vector<uint8_t>(), GDScriptBytecodeEnvelope::COMPRESSION_NONE, output) == ERR_INVALID_DATA);
	CHECK(output.is_empty());
	Vector<uint8_t> invalid = make_image();
	invalid.write[0] = 0;
	CHECK(GDScriptBytecodeEnvelope::encode(invalid, GDScriptBytecodeEnvelope::COMPRESSION_ZSTD, invalid) == ERR_INVALID_DATA);
	CHECK(invalid.is_empty());
}

TEST_CASE("[GDScript][Bytecode] Bounded file reads") {
	const Vector<uint8_t> image = make_image();
	Vector<uint8_t> stored;
	REQUIRE(GDScriptBytecodeEnvelope::encode(image, GDScriptBytecodeEnvelope::COMPRESSION_ZSTD, stored) == OK);
	Ref<FileAccessMemory> file;
	file.instantiate();
	file->open_custom(stored.ptr(), stored.size());
	Vector<uint8_t> decoded;
	CHECK(GDScriptBytecodeEnvelope::read(file, decoded) == OK);
	CHECK(decoded == image);
	file->seek(1);
	CHECK(GDScriptBytecodeEnvelope::read(file, decoded) == ERR_FILE_CANT_READ);
	CHECK(decoded.is_empty());
	CHECK(GDScriptBytecodeEnvelope::read(Ref<FileAccess>(), decoded) == ERR_INVALID_PARAMETER);
	CHECK(decoded.is_empty());
}

} // namespace TestGDScriptBytecodeEnvelope
