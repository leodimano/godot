/**************************************************************************/
/*  gdscript_bytecode_view.h                                              */
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

#include "gdscript_bytecode_image.h"

#include "core/variant/array.h"

// A borrowed view into validated tables. Reopening or destroying the owning
// image invalidates its views. Containers are traversed without materialization.
class GDScriptBytecodeView {
	friend class GDScriptBytecodeImage;
	const GDScriptBytecodeImage *image = nullptr;
	uint32_t index = 0;
	bool key_view = false;
	void dictionary_range(uint32_t p_hash, int &r_start, int &r_end) const;

	GDScriptBytecodeView(const GDScriptBytecodeImage *p_image, uint32_t p_index, bool p_keys = false) :
			image(p_image), index(p_index), key_view(p_keys) {}

public:
	GDScriptBytecodeView() = default;
	_FORCE_INLINE_ Variant::Type get_type() const {
		if (!image) {
			return Variant::NIL;
		}
		const uint32_t kind = image->node_word(index, 0);
		return key_view ? Variant::ARRAY : kind == 0 ? scalar().get_type()
													 : Variant::Type(kind);
	}
	_FORCE_INLINE_ int size() const { return image && (key_view || image->node_word(index, 0) != 0) ? image->node_word(index, 2) : 0; }
	bool is_empty() const { return size() == 0; }
	bool has(const Variant &p_key) const;
	bool has(const char *p_key) const;
	GDScriptBytecodeView operator[](int p_index) const;
	GDScriptBytecodeView operator[](const Variant &p_key) const;
	GDScriptBytecodeView operator[](const GDScriptBytecodeView &p_key) const { return (*this)[p_key.scalar()]; }
	GDScriptBytecodeView operator[](const char *p_key) const;
	Variant get(const Variant &p_key, const Variant &p_default) const;
	Variant get(const char *p_key, const Variant &p_default) const;
	GDScriptBytecodeView keys() const { return get_type() == Variant::DICTIONARY ? GDScriptBytecodeView(image, index, true) : GDScriptBytecodeView(); }
	_FORCE_INLINE_ const Variant &scalar() const {
		static const Variant nil;
		return image && !key_view && image->node_word(index, 0) == 0 ? image->atoms[image->node_word(index, 1)] : nil;
	}

	// Scalar conversions do not allocate a container graph.
	operator Variant() const { return scalar(); }
	operator bool() const { return scalar(); }
	operator int32_t() const { return scalar(); }
	operator int64_t() const { return scalar(); }
	operator uint32_t() const { return scalar(); }
	operator String() const { return scalar(); }
	operator StringName() const { return scalar(); }
	operator Vector2i() const { return scalar(); }
	operator Vector3i() const { return scalar(); }
	operator Vector<int32_t>() const { return scalar(); }
	operator Vector<String>() const { return scalar(); }
	bool operator!=(const Variant &p_other) const { return scalar() != p_other; }
	bool operator!=(const GDScriptBytecodeView &p_other) const { return scalar() != p_other.scalar(); }

	struct Iterator {
		const GDScriptBytecodeView *view = nullptr;
		int position = 0;
		GDScriptBytecodeView operator*() const { return (*view)[position]; }
		Iterator &operator++() {
			position++;
			return *this;
		}
		bool operator!=(const Iterator &p_other) const { return position != p_other.position; }
	};
	Iterator begin() const { return { this, 0 }; }
	Iterator end() const { return { this, size() }; }
};
