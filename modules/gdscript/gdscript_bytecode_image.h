/**************************************************************************/
/*  gdscript_bytecode_image.h                                             */
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

#include "gdscript_bytecode_format.h"

#include "core/io/marshalls.h"
#include "core/math/math_funcs_binary.h"
#include "core/templates/hashfuncs.h"
#include "core/variant/variant.h"

class GDScriptBytecodeView;

// Relocatable metadata tables. This validates the image envelope, not VM instructions.
class GDScriptBytecodeImage {
	friend class GDScriptBytecodeView;
	Vector<uint8_t> buffer;
	Vector<Variant> atoms;
	uint32_t node_count = 0;
	uint32_t edge_offset = 0;
	uint32_t hash_offset = 0;
	uint32_t root_index = 0;

	_FORCE_INLINE_ uint32_t word(uint32_t p_offset) const { return decode_uint32(buffer.ptr() + p_offset); }
	_FORCE_INLINE_ uint32_t node_word(uint32_t p_node, uint32_t p_field) const { return word(HEADER_SIZE + p_node * NODE_SIZE + p_field * 4); }
	_FORCE_INLINE_ uint32_t edge(uint32_t p_index) const { return word(edge_offset + p_index * 4); }
	_FORCE_INLINE_ uint32_t node_hash(uint32_t p_node) const { return word(hash_offset + node_word(p_node, 1) * 4); }

public:
	GDScriptBytecodeImage() = default;
	GDScriptBytecodeImage(const GDScriptBytecodeImage &) = delete;
	GDScriptBytecodeImage &operator=(const GDScriptBytecodeImage &) = delete;

	static constexpr uint32_t MAGIC = GDScriptBytecodeFormat::IMAGE_MAGIC;
	static constexpr uint32_t VERSION = GDScriptBytecodeFormat::IMAGE_VERSION;
	static constexpr uint32_t HEADER_SIZE = GDScriptBytecodeFormat::IMAGE_HEADER_SIZE;
	static constexpr uint32_t NODE_SIZE = GDScriptBytecodeFormat::IMAGE_NODE_SIZE;
	static constexpr uint32_t MAX_BYTES = GDScriptBytecodeFormat::MAX_IMAGE_BYTES;
	// Part of the v3 layout: pair ordinals grouped by hash bucket, then a directory
	// of bucket starts. At most two pairs per bucket on average, excluding skew.
	static constexpr uint32_t DICTIONARY_INDEX_THRESHOLD = 8;
	static constexpr uint32_t dictionary_bucket_count(uint32_t p_count) { return Math::next_power_of_2(p_count) / 2; }
	static _FORCE_INLINE_ uint32_t dictionary_bucket(uint32_t p_hash, uint32_t p_buckets) { return hash_fmix32(p_hash) & (p_buckets - 1); }
	static uint32_t key_hash(const Variant &p_key);
	Error open(const Vector<uint8_t> &p_bytes);
	GDScriptBytecodeView root() const;
};
