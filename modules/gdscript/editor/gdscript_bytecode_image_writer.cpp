/**************************************************************************/
/*  gdscript_bytecode_image_writer.cpp                                    */
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

#include "gdscript_bytecode_image_writer.h"

#include "../gdscript_bytecode_image.h"

#include "core/io/marshalls.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

namespace {

struct DictionaryIndexLess {
	uint32_t bucket_count = 0;

	bool operator()(uint64_t p_left, uint64_t p_right) const {
		const uint32_t left_bucket = GDScriptBytecodeImage::dictionary_bucket(uint32_t(p_left >> 32), bucket_count);
		const uint32_t right_bucket = GDScriptBytecodeImage::dictionary_bucket(uint32_t(p_right >> 32), bucket_count);
		return left_bucket != right_bucket ? left_bucket < right_bucket : p_left < p_right;
	}
};

} // namespace

uint32_t GDScriptBytecodeImageWriter::append(const Variant &p_value, int p_depth) {
	if (error != OK || p_depth > Variant::MAX_RECURSION_DEPTH) {
		error = ERR_INVALID_DATA;
		return 0;
	}
	const Variant::Type type = p_value.get_type();
	const uint32_t *existing = nullptr;
	if (type == Variant::STRING) {
		existing = strings.getptr(p_value);
	} else if (type == Variant::STRING_NAME) {
		existing = names.getptr(p_value);
	} else if (type == Variant::INT) {
		existing = integers.getptr(p_value);
	}
	if (existing) {
		return *existing;
	}
	if (type == Variant::OBJECT || type == Variant::CALLABLE || type == Variant::SIGNAL || type == Variant::RID) {
		error = ERR_INVALID_DATA;
		return 0;
	}
	uint32_t offset = atoms.size();
	uint32_t count = 0;
	uint32_t kind = 0;
	if (type == Variant::ARRAY || type == Variant::DICTIONARY) {
		kind = type;
		Vector<uint32_t> children;
		if (type == Variant::ARRAY) {
			const Array values = p_value;
			count = values.size();
			for (const Variant &value : values) {
				children.push_back(append(value, p_depth + 1));
				if (error != OK) {
					return 0;
				}
			}
		} else {
			const Dictionary values = p_value;
			count = values.size();
			for (const Variant &key : values.keys()) {
				if (key.get_type() == Variant::ARRAY || key.get_type() == Variant::DICTIONARY) {
					error = ERR_INVALID_DATA;
					return 0;
				}
				children.push_back(append(key, p_depth + 1));
				children.push_back(append(values[key], p_depth + 1));
				if (error != OK) {
					return 0;
				}
			}
		}
		if (error != OK) {
			return 0;
		}
		offset = edges.size();
		edges.append_array(children);
		if (type == Variant::DICTIONARY && count > GDScriptBytecodeImage::DICTIONARY_INDEX_THRESHOLD) {
			const uint32_t bucket_count = GDScriptBytecodeImage::dictionary_bucket_count(count);
			Vector<uint64_t> order;
			order.resize(count);
			for (uint32_t ordinal = 0; ordinal < count; ordinal++) {
				const Variant &key = atoms[nodes[children[ordinal * 2] * 3 + 1]];
				order.write[ordinal] = (uint64_t(GDScriptBytecodeImage::key_hash(key)) << 32) | ordinal;
			}
			order.sort_custom<DictionaryIndexLess>(DictionaryIndexLess{ bucket_count });
			for (uint64_t entry : order) {
				edges.push_back(uint32_t(entry));
			}
			uint32_t item = 0;
			for (uint32_t bucket = 0; bucket < bucket_count; bucket++) {
				edges.push_back(item);
				while (item < count && GDScriptBytecodeImage::dictionary_bucket(uint32_t(order[item] >> 32), bucket_count) == bucket) {
					item++;
				}
			}
			edges.push_back(count);
		}
	} else {
		atoms.push_back(p_value);
	}
	const uint32_t index = nodes.size() / 3;
	nodes.push_back(kind);
	nodes.push_back(offset);
	nodes.push_back(count);
	if (type == Variant::STRING) {
		strings.insert(p_value, index);
	} else if (type == Variant::STRING_NAME) {
		names.insert(p_value, index);
	} else if (type == Variant::INT) {
		integers.insert(p_value, index);
	}
	return index;
}

Error GDScriptBytecodeImageWriter::encode(const Variant &p_value, Vector<uint8_t> &r_bytes) {
	nodes.clear();
	edges.clear();
	atoms.clear();
	strings.clear();
	names.clear();
	integers.clear();
	error = OK;
	r_bytes.clear();
	const uint32_t root = append(p_value, 0);
	if (error != OK) {
		return error;
	}
	uint64_t size = GDScriptBytecodeImage::HEADER_SIZE + (uint64_t(nodes.size()) + uint64_t(edges.size()) + uint64_t(atoms.size())) * 4;
	Vector<int> lengths;
	for (const Variant &atom : atoms) {
		int length = 0;
		error = encode_variant(atom, nullptr, length, false);
		if (error != OK) {
			return error;
		}
		lengths.push_back(length);
		size += 4 + length;
	}
	if (size > GDScriptBytecodeImage::MAX_BYTES) {
		return ERR_OUT_OF_MEMORY;
	}
	if (r_bytes.resize(size) != OK) {
		return ERR_OUT_OF_MEMORY;
	}
	uint8_t *cursor = r_bytes.ptrw();
	const uint32_t header[] = { GDScriptBytecodeImage::MAGIC, GDScriptBytecodeImage::VERSION, uint32_t(nodes.size() / 3), uint32_t(atoms.size()), uint32_t(edges.size()), root };
	for (uint32_t value : header) {
		cursor += encode_uint32(value, cursor);
	}
	for (uint32_t value : nodes) {
		cursor += encode_uint32(value, cursor);
	}
	for (uint32_t value : edges) {
		cursor += encode_uint32(value, cursor);
	}
	for (const Variant &atom : atoms) {
		cursor += encode_uint32(GDScriptBytecodeImage::key_hash(atom), cursor);
	}
	for (int index = 0; index < atoms.size(); index++) {
		int length = lengths[index];
		cursor += encode_uint32(length, cursor);
		error = encode_variant(atoms[index], cursor, length, false);
		if (error != OK || length != lengths[index]) {
			r_bytes.clear();
			return ERR_INVALID_DATA;
		}
		cursor += length;
	}
	return OK;
}
