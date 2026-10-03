/**************************************************************************/
/*  gdscript_bytecode_image.cpp                                           */
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

#include "gdscript_bytecode_image.h"

#include "gdscript_bytecode_view.h"

#include "core/io/marshalls.h"
#include "core/variant/variant_internal.h"

uint32_t GDScriptBytecodeImage::key_hash(const Variant &p_key) {
	if (p_key.get_type() == Variant::STRING) {
		return VariantInternal::get_string(&p_key)->hash();
	}
	if (p_key.get_type() == Variant::STRING_NAME) {
		return VariantInternal::get_string_name(&p_key)->hash();
	}
	return UINT32_MAX; // Non-string keys retain the general linear lookup path.
}

Error GDScriptBytecodeImage::open(const Vector<uint8_t> &p_bytes) {
	buffer.clear();
	atoms.clear();
	node_count = 0;
	if (p_bytes.size() < HEADER_SIZE || p_bytes.size() > MAX_BYTES) {
		return ERR_INVALID_DATA;
	}
	buffer = p_bytes;
	if (word(0) != MAGIC || word(4) != VERSION) {
		return ERR_INVALID_DATA;
	}
	const uint32_t nodes = word(8);
	const uint32_t atom_count = word(12);
	const uint32_t edge_count = word(16);
	const uint32_t root = word(20);
	const uint64_t edges_begin = HEADER_SIZE + uint64_t(nodes) * NODE_SIZE;
	const uint64_t hashes_begin = edges_begin + uint64_t(edge_count) * 4;
	uint64_t cursor = hashes_begin + uint64_t(atom_count) * 4;
	if (!nodes || root >= nodes || cursor > uint64_t(buffer.size()) || atom_count > (buffer.size() - cursor) / 8) {
		return ERR_INVALID_DATA;
	}
	edge_offset = uint32_t(edges_begin);
	hash_offset = uint32_t(hashes_begin);
	if (atoms.resize(atom_count) != OK) {
		return ERR_OUT_OF_MEMORY;
	}
	for (uint32_t atom = 0; atom < atom_count; atom++) {
		if (cursor + 4 > uint64_t(buffer.size())) {
			return ERR_INVALID_DATA;
		}
		const uint32_t length = word(cursor);
		cursor += 4;
		if (length < 4 || cursor + length > uint64_t(buffer.size())) {
			return ERR_INVALID_DATA;
		}
		const uint32_t type = word(cursor) & 0xff;
		if (type >= Variant::VARIANT_MAX || type == Variant::ARRAY || type == Variant::DICTIONARY || type == Variant::OBJECT || type == Variant::CALLABLE || type == Variant::SIGNAL || type == Variant::RID) {
			return ERR_INVALID_DATA;
		}
		int consumed = 0;
		if (decode_variant(atoms.write[atom], buffer.ptr() + cursor, length, &consumed, false) != OK || consumed != int(length)) {
			return ERR_INVALID_DATA;
		}
		if (word(hash_offset + atom * 4) != key_hash(atoms[atom])) {
			return ERR_INVALID_DATA;
		}
		cursor += length;
	}
	if (cursor != uint64_t(buffer.size())) {
		return ERR_INVALID_DATA;
	}
	for (uint32_t node = 0; node < nodes; node++) {
		const uint32_t kind = node_word(node, 0);
		const uint32_t offset = node_word(node, 1);
		const uint32_t count = node_word(node, 2);
		if (kind == 0) {
			if (offset >= atom_count || count != 0) {
				return ERR_INVALID_DATA;
			}
			continue;
		}
		if (kind != Variant::ARRAY && kind != Variant::DICTIONARY) {
			return ERR_INVALID_DATA;
		}
		const uint64_t entries = uint64_t(count) * (kind == Variant::DICTIONARY ? 2 : 1);
		const bool indexed = kind == Variant::DICTIONARY && count > DICTIONARY_INDEX_THRESHOLD;
		// Bound count before rounding the bucket capacity in 32-bit arithmetic.
		if (uint64_t(offset) + entries > edge_count) {
			return ERR_INVALID_DATA;
		}
		const uint32_t bucket_count = indexed ? dictionary_bucket_count(count) : 0;
		if (indexed && uint64_t(offset) + entries + count + bucket_count + 1 > edge_count) {
			return ERR_INVALID_DATA;
		}
		for (uint64_t item = 0; item < entries; item++) {
			const uint32_t child = edge(offset + item);
			// Postorder records cannot contain cycles or self-references.
			if (child >= node || (kind == Variant::DICTIONARY && item % 2 == 0 && node_word(child, 0) != 0)) {
				return ERR_INVALID_DATA;
			}
		}
		if (indexed) {
			const uint32_t directory = offset + entries + count;
			uint32_t item = 0;
			for (uint32_t bucket = 0; bucket < bucket_count; bucket++) {
				if (edge(directory + bucket) != item) {
					return ERR_INVALID_DATA;
				}
				const uint32_t start = item;
				uint64_t previous = 0;
				while (item < count) {
					const uint32_t ordinal = edge(offset + entries + item);
					if (ordinal >= count) {
						return ERR_INVALID_DATA;
					}
					const uint32_t key = edge(offset + ordinal * 2);
					const uint32_t hash = node_hash(key);
					if (dictionary_bucket(hash, bucket_count) != bucket) {
						break;
					}
					const uint64_t order = (uint64_t(hash) << 32) | ordinal;
					// Exact membership plus strict (hash, ordinal) order proves a
					// permutation, without rebuilding an index or visited set.
					if (item > start && order <= previous) {
						return ERR_INVALID_DATA;
					}
					previous = order;
					item++;
				}
			}
			if (item != count || edge(directory + bucket_count) != count) {
				return ERR_INVALID_DATA;
			}
		}
	}
	node_count = nodes;
	root_index = root;
	return OK;
}

GDScriptBytecodeView GDScriptBytecodeImage::root() const {
	return node_count ? GDScriptBytecodeView(this, root_index) : GDScriptBytecodeView();
}
