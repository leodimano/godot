/**************************************************************************/
/*  gdscript_bytecode_writer.h                                            */
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

#include "../gdscript.h"

#include "core/variant/typed_array.h"

// Serializes compiler-owned structures. No process addresses or mutable runtime
// state are persisted. Native pointers are represented by symbolic descriptors.
class GDScriptBytecodeWriter {
	struct WriteContext {
		bool supported = true;
		bool runtime_only = false;
		String current_script;
		Dictionary native_classes;
		Dictionary native_methods;
		Dictionary rejected_native_classes;
		Array native_bindings;
		HashMap<MethodBind *, int> native_binding_indices;
		Vector<GDScript *> scripts;
		HashMap<GDScript *, int> indices;
		Vector<Variant> containers;
		Vector<String> container_origins;
		HashMap<const void *, int> container_indices;
		HashSet<String> resource_paths;
		int resource_depth = 0;
	};

	static bool plain_value(const Variant &p_value);
	static int add_script(GDScript *p_script, WriteContext &r_context);
	static void require_native_class(const StringName &p_name, WriteContext &r_context);
	static void require_property_class(const PropertyInfo &p_property, WriteContext &r_context);
	static void discover_resource_scripts(const String &p_path, WriteContext &r_context);
	static Dictionary save_value(const Variant &p_value, WriteContext &r_context);
	static Dictionary save_container_type(const ContainerType &p_type, WriteContext &r_context);
	static Dictionary save_container(const Variant &p_value, WriteContext &r_context);
	static Dictionary save_method_info(const MethodInfo &p_info, WriteContext &r_context);
	static Dictionary save_type(const GDScriptDataType &p_type, GDScript *p_owner, WriteContext &r_context);
	static Dictionary save_function(GDScriptFunction *p_function, WriteContext &r_context);
	static Dictionary save_script(GDScript *p_script, WriteContext &r_context);
	static Error save_scripts_internal(const TypedArray<GDScript> &p_scripts, const String &p_path, WriteContext &r_context);
	static Error save_metadata(const Variant &p_data, const String &p_path);

public:
	static Error save_scripts(const TypedArray<GDScript> &p_scripts, const String &p_path);
	static Dictionary save_runtime_scripts(const TypedArray<GDScript> &p_scripts, const String &p_path);
};

#endif // TOOLS_ENABLED
