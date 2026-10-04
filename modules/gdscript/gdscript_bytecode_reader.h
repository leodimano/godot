/**************************************************************************/
/*  gdscript_bytecode_reader.h                                            */
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

#include "gdscript.h"
#include "gdscript_bytecode_view.h"

#include "core/io/resource_loader.h"
#include "core/os/thread.h"

class GDScriptBytecodeLoadProfile;

// Internal loader for trusted compiler output. The artifact digest detects
// corruption and mixed generations; it is not an authenticity guarantee.
class GDScriptBytecodeReader {
	struct ResourceBinding {
		// Both references are borrowed until the owning transaction completes.
		Variant *destination = nullptr;
		GDScriptBytecodeView descriptor;
	};
	struct LoadOwner {
		Ref<ResourceLoader::LoadToken> token;
		Thread::ID thread = 0;
	};
	struct ReadContext {
		bool valid = true;
		GDScriptBytecodeLoadProfile *profile = nullptr;
		Vector<Ref<GDScript>> scripts;
		Vector<MethodBind *> native_bindings;
		Vector<bool> reused;
		Vector<Variant> containers;
		GDScriptBytecodeView container_data;
		GDScriptBytecodeView initializers;
		Vector<int> outer_classes;
		Vector<uint8_t> container_states;
		Vector<uint8_t> binding_states;
		Vector<Vector<int>> container_dependencies;
		Vector<Vector<ResourceBinding>> resource_bindings;
		int current_script = -1;
		int next_initializer = 0;
		bool resolving_resources = false;
		int resource_load_depth = 0;
		int resolve_depth = 0;
		String bundle;
		ReadContext *previous = nullptr;
	};
	static thread_local ReadContext *initializing_context;
	static Mutex resource_mutex;
	static HashMap<String, LoadOwner> resource_owners;

	static bool plain_value(const Variant &p_value);
	static Ref<GDScript> load_graph(const String &p_path, const String &p_original_path, bool p_cache_resources);
	static Variant load_value(const GDScriptBytecodeView &p_value, ReadContext &r_context);
	static void load_value_into(const GDScriptBytecodeView &p_value, Variant &r_value, ReadContext &r_context);
	static bool resolve_script_values(int p_index, ReadContext &r_context);
	static bool initialize_until(int p_position, ReadContext &r_context);
	static bool prepare_resource_script(int p_index, ReadContext &r_context);
	static bool fill_container(int p_index, ReadContext &r_context);
	static ContainerType load_container_type(const GDScriptBytecodeView &p_data, ReadContext &r_context);
	static bool create_containers(const GDScriptBytecodeView &p_data, ReadContext &r_context);
	static bool fill_containers(const GDScriptBytecodeView &p_data, const Vector<int> &p_order, ReadContext &r_context);
	static PropertyInfo load_property_info(const GDScriptBytecodeView &p_data);
	static MethodInfo load_method_info(const GDScriptBytecodeView &p_data, ReadContext &r_context);
	static GDScriptDataType load_type(const GDScriptBytecodeView &p_data, GDScript *p_owner, ReadContext &r_context);
	static bool load_function_debug(GDScriptFunction *p_function, const GDScriptBytecodeView &p_data, ReadContext &r_context);
	static GDScriptFunction *load_function(GDScript *p_script, const GDScriptBytecodeView &p_data, ReadContext &r_context);
	static bool load_script(GDScript *p_script, const GDScriptBytecodeView &p_data, ReadContext &r_context);

public:
	// An isolated graph does not replace any cached generation.
	static Ref<GDScript> load(const String &p_path);
	static Ref<GDScript> load_resource(const String &p_path, const String &p_original_path, bool p_cache_resources);
};
