/**************************************************************************/
/*  gdscript_bytecode_view.cpp                                            */
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

#include "gdscript_bytecode_view.h"

#include "core/variant/variant_internal.h"

void GDScriptBytecodeView::dictionary_range(uint32_t p_hash, int &r_start, int &r_end) const {
	const uint32_t offset = image->node_word(index, 1);
	const uint32_t count = size();
	const uint32_t buckets = GDScriptBytecodeImage::dictionary_bucket_count(count);
	const uint32_t bucket = GDScriptBytecodeImage::dictionary_bucket(p_hash, buckets);
	const uint32_t directory = offset + count * 3;
	r_start = image->edge(directory + bucket);
	r_end = image->edge(directory + bucket + 1);
}

GDScriptBytecodeView GDScriptBytecodeView::operator[](int p_index) const {
	if (p_index < 0 || p_index >= size() || get_type() != Variant::ARRAY) {
		return {};
	}
	const uint32_t offset = image->node_word(index, 1);
	return GDScriptBytecodeView(image, image->edge(offset + p_index * (key_view ? 2 : 1)));
}

GDScriptBytecodeView GDScriptBytecodeView::operator[](const Variant &p_key) const {
	if (get_type() != Variant::DICTIONARY) {
		return {};
	}
	const uint32_t offset = image->node_word(index, 1);
	const int count = size();
	const bool indexed = count > GDScriptBytecodeImage::DICTIONARY_INDEX_THRESHOLD && (p_key.get_type() == Variant::STRING || p_key.get_type() == Variant::STRING_NAME);
	const uint32_t hash = indexed ? GDScriptBytecodeImage::key_hash(p_key) : 0;
	int start = 0;
	int end = count;
	if (indexed) {
		dictionary_range(hash, start, end);
	}
	for (int candidate = start; candidate < end; candidate++) {
		const uint32_t item = indexed ? image->edge(offset + count * 2 + candidate) : candidate;
		const GDScriptBytecodeView key(image, image->edge(offset + item * 2));
		if (indexed && image->node_hash(key.index) != hash) {
			continue;
		}
		if (StringLikeVariantComparator::compare(key.scalar(), p_key)) {
			return GDScriptBytecodeView(image, image->edge(offset + item * 2 + 1));
		}
	}
	return {};
}

GDScriptBytecodeView GDScriptBytecodeView::operator[](const char *p_key) const {
	if (!p_key || get_type() != Variant::DICTIONARY) {
		return {};
	}
	const uint32_t offset = image->node_word(index, 1);
	const int count = size();
	const bool indexed = count > GDScriptBytecodeImage::DICTIONARY_INDEX_THRESHOLD;
	const uint32_t hash = indexed ? String::hash(p_key) : 0;
	int start = 0;
	int end = count;
	if (indexed) {
		dictionary_range(hash, start, end);
	}
	for (int candidate = start; candidate < end; candidate++) {
		const uint32_t item = indexed ? image->edge(offset + count * 2 + candidate) : candidate;
		const GDScriptBytecodeView key(image, image->edge(offset + item * 2));
		if (indexed && image->node_hash(key.index) != hash) {
			continue;
		}
		const Variant &atom = key.scalar();
		// Same Latin-1 key semantics as Variant(const char *), without allocating
		// a String or copying/refcounting every candidate Variant during lookup.
		const bool matches = (atom.get_type() == Variant::STRING && *VariantInternal::get_string(&atom) == p_key) ||
				(atom.get_type() == Variant::STRING_NAME && *VariantInternal::get_string_name(&atom) == p_key);
		if (matches) {
			return GDScriptBytecodeView(image, image->edge(offset + item * 2 + 1));
		}
	}
	return {};
}

bool GDScriptBytecodeView::has(const Variant &p_key) const {
	return (*this)[p_key].image != nullptr;
}

bool GDScriptBytecodeView::has(const char *p_key) const {
	return (*this)[p_key].image != nullptr;
}

Variant GDScriptBytecodeView::get(const Variant &p_key, const Variant &p_default) const {
	const GDScriptBytecodeView value = (*this)[p_key];
	return value.image ? value.scalar() : p_default;
}

Variant GDScriptBytecodeView::get(const char *p_key, const Variant &p_default) const {
	const GDScriptBytecodeView value = (*this)[p_key];
	return value.image ? value.scalar() : p_default;
}
