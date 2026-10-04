/**************************************************************************/
/*  gdscript_bytecode_data.h                                              */
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

#include "gdscript_bytecode_view.h"

#include <type_traits>

namespace GDScriptBytecodeData {

class ResolveScope {
	int &depth;

public:
	explicit ResolveScope(int &p_depth) : depth(p_depth) { ++depth; }
	ResolveScope(const ResolveScope &) = delete;
	ResolveScope &operator=(const ResolveScope &) = delete;
	~ResolveScope() { --depth; }
	bool allowed() const { return depth <= Variant::MAX_RECURSION_DEPTH; }
};

template <typename Sequence>
bool get_container_order(const Sequence &p_data, Vector<int> &r_order) {
	using Record = std::conditional_t<std::is_same_v<Sequence, Array>, Dictionary, GDScriptBytecodeView>;
	// Constant expressions form a DAG. Walk it iteratively: even a malformed
	// artifact must not cause recursive expansion or allocate reference cycles.
	r_order.clear();
	Vector<int> order;
	Vector<int> pending;
	Vector<Vector<int>> parents;
	if (pending.resize(p_data.size()) != OK || parents.resize(p_data.size()) != OK) {
		return false;
	}
	for (int index = 0; index < p_data.size(); index++) {
		if (p_data[index].get_type() != Variant::DICTIONARY) {
			return false;
		}
		const Record data = p_data[index];
		const Variant type = data.get("type", -1);
		if (type.get_type() != Variant::INT || (int(type) != Variant::ARRAY && int(type) != Variant::DICTIONARY) || data["readonly"].get_type() != Variant::BOOL || data["element"].get_type() != Variant::DICTIONARY || data["values"].get_type() != Variant::ARRAY) {
			return false;
		}
		const Sequence values = data["values"];
		if (int(type) == Variant::DICTIONARY && (values.size() % 2 || data["key"].get_type() != Variant::DICTIONARY)) {
			return false;
		}
		pending.write[index] = 0;
		for (int value_index = 0; value_index < values.size(); value_index++) {
			const typename std::conditional_t<std::is_same_v<Sequence, Array>, Variant, GDScriptBytecodeView> value = values[value_index];
			if (value.get_type() != Variant::DICTIONARY) {
				return false;
			}
			const Record reference = value;
			if (reference.get("kind", "") != "container") {
				continue;
			}
			const Variant child = reference.get("id", -1);
			if (child.get_type() != Variant::INT || int64_t(child) < 0 || int64_t(child) >= p_data.size()) {
				return false;
			}
			parents.write[int(child)].push_back(index);
			pending.write[index]++;
		}
		if (pending[index] == 0) {
			order.push_back(index);
		}
	}
	for (int next = 0; next < order.size(); next++) {
		for (int parent : parents[order[next]]) {
			if (--pending.write[parent] == 0) {
				order.push_back(parent);
			}
		}
	}
	if (order.size() != p_data.size()) {
		return false;
	}
	r_order = order;
	return true;
}

} // namespace GDScriptBytecodeData
