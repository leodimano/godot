/**************************************************************************/
/*  gdscript_bytecode_writer.cpp                                          */
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

#include "gdscript_bytecode_writer.h"

#include "../gdscript_bytecode_compatibility.gen.h"
#include "../gdscript_bytecode_data.h"
#include "../gdscript_bytecode_format.h"
#include "../gdscript_compilation_context.h"
#include "gdscript_bytecode_image_writer.h"

#include "core/config/engine.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/class_db.h"
#include "core/object/method_bind.h"
#include "core/templates/rb_map.h"
#include "core/variant/container_type_validate.h"

template <typename T>
static Array save_named_accessors(const Vector<T> &p_accessors, T (*p_lookup)(Variant::Type, const StringName &), bool &r_supported) {
	Array records;
	for (T accessor : p_accessors) {
		bool found = false;
		for (int type = 0; accessor && type < Variant::VARIANT_MAX && !found; type++) {
			List<StringName> members;
			Variant::get_member_list(Variant::Type(type), &members);
			for (const StringName &member : members) {
				if (p_lookup(Variant::Type(type), member) == accessor) {
					Array descriptor;
					descriptor.push_back(type);
					descriptor.push_back(member);
					records.push_back(descriptor);
					found = true;
					break;
				}
			}
		}
		r_supported = r_supported && found;
	}
	return records;
}

template <typename T>
static Array save_indexed_accessors(const Vector<T> &p_accessors, T (*p_lookup)(Variant::Type), bool &r_supported) {
	Array records;
	for (T accessor : p_accessors) {
		bool found = false;
		for (int type = 0; accessor && type < Variant::VARIANT_MAX && !found; type++) {
			if (p_lookup(Variant::Type(type)) == accessor) {
				records.push_back(type);
				found = true;
			}
		}
		r_supported = r_supported && found;
	}
	return records;
}

bool GDScriptBytecodeWriter::plain_value(const Variant &p_value) {
	if (p_value.get_type() == Variant::OBJECT || p_value.get_type() == Variant::CALLABLE || p_value.get_type() == Variant::SIGNAL || p_value.get_type() == Variant::RID || p_value.get_type() == Variant::ARRAY || p_value.get_type() == Variant::DICTIONARY) {
		return false;
	}
	return true;
}

int GDScriptBytecodeWriter::add_script(GDScript *p_script, WriteContext &r_context) {
	if (!p_script || !p_script->valid) {
		r_context.supported = false;
		return -1;
	}
	if (r_context.indices.has(p_script)) {
		return r_context.indices[p_script];
	}
	const int index = r_context.scripts.size();
	r_context.indices.insert(p_script, index);
	r_context.scripts.push_back(p_script);
	return index;
}

void GDScriptBytecodeWriter::require_native_class(const StringName &p_name, WriteContext &r_context) {
	if (!r_context.runtime_only || p_name == StringName() || r_context.native_classes.has(p_name)) {
		return;
	}
	const ClassDB::APIType api = ClassDB::class_exists(p_name) ? ClassDB::get_api_type(p_name) : ClassDB::API_NONE;
	r_context.native_classes[p_name] = int(api);
	if (api != ClassDB::API_CORE && api != ClassDB::API_EXTENSION) {
		// Report the first owning script, but finish collecting the full closure.
		r_context.rejected_native_classes[p_name] = r_context.current_script;
	}
}

void GDScriptBytecodeWriter::require_property_class(const PropertyInfo &p_property, WriteContext &r_context) {
	// Reflection may name a script class or enum rather than a native class.
	if (p_property.type == Variant::OBJECT && ClassDB::class_exists(p_property.class_name)) {
		require_native_class(p_property.class_name, r_context);
	}
}

void GDScriptBytecodeWriter::discover_resource_scripts(const String &p_path, WriteContext &r_context) {
	if (r_context.resource_paths.has(p_path)) {
		return;
	}
	GDScriptBytecodeData::ResolveScope scope(r_context.resource_depth);
	if (!scope.allowed()) {
		r_context.supported = false;
		return;
	}
	r_context.resource_paths.insert(p_path);
	List<String> dependencies;
	ResourceLoader::get_dependencies(p_path.get_slice("::", 0), &dependencies);
	for (const String &dependency : dependencies) {
		String path = dependency.get_slice("::", 0);
		if (path.begins_with("uid://")) {
			const ResourceUID::ID uid = ResourceUID::get_singleton()->text_to_id(path);
			path = ResourceUID::get_singleton()->has_id(uid) ? ResourceUID::get_singleton()->get_id_path(uid) : dependency.get_slice("::", 2);
		}
		if (path.is_empty()) {
			r_context.supported = false;
			return;
		}
		if (path.is_relative_path()) {
			path = p_path.get_slice("::", 0).get_base_dir().path_join(path);
		}
		path = path.simplify_path();
		if (!path.begins_with("res://")) {
			r_context.supported = false;
			return;
		}
		if (path.get_extension() == "gd") {
			Ref<GDScript> script = ResourceLoader::load(path);
			add_script(script.ptr(), r_context);
		} else {
			discover_resource_scripts(path, r_context);
		}
	}
}

Dictionary GDScriptBytecodeWriter::save_value(const Variant &p_value, WriteContext &r_context) {
	Dictionary data;
	if (p_value.get_type() == Variant::ARRAY || p_value.get_type() == Variant::DICTIONARY) {
		const void *identity = p_value.get_type() == Variant::ARRAY ? Array(p_value).id() : Dictionary(p_value).id();
		if (!r_context.container_indices.has(identity)) {
			r_context.container_indices.insert(identity, r_context.containers.size());
			r_context.containers.push_back(p_value);
			r_context.container_origins.push_back(r_context.current_script);
		}
		data["kind"] = "container";
		data["id"] = r_context.container_indices[identity];
		return data;
	}
	if (plain_value(p_value)) {
		data["kind"] = "value";
		data["value"] = p_value;
		return data;
	}
	if (p_value.get_type() == Variant::CALLABLE && Callable(p_value).is_null()) {
		data["kind"] = "empty_callable";
		return data;
	}
	if (p_value.get_type() == Variant::RID && !RID(p_value).is_valid()) {
		data["kind"] = "empty_rid";
		return data;
	}
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value;
		if (!object) {
			data["kind"] = "null_object";
			return data;
		}
		if (GDScript *script = Object::cast_to<GDScript>(object)) {
			data["kind"] = "script";
			data["id"] = add_script(script, r_context);
			return data;
		}
		if (GDScriptNativeClass *native = Object::cast_to<GDScriptNativeClass>(object)) {
			require_native_class(native->get_name(), r_context);
			data["kind"] = "native_class";
			data["name"] = native->get_name();
			return data;
		}
		if (Resource *resource = Object::cast_to<Resource>(object)) {
			const String path = resource->get_path();
			if (path.begins_with("res://") && path.simplify_path() == path) {
				require_native_class(resource->get_class_name(), r_context);
				discover_resource_scripts(path, r_context);
				data["kind"] = "resource";
				data["path"] = path;
				data["class"] = resource->get_class_name();
				return data;
			}
		}
		List<Engine::Singleton> singletons;
		Engine::get_singleton()->get_singletons(&singletons);
		for (const Engine::Singleton &singleton : singletons) {
			if (singleton.ptr == object && !singleton.user_created) {
				require_native_class(object->get_class_name(), r_context);
				data["kind"] = "singleton";
				data["name"] = singleton.name;
				return data;
			}
		}
	}
	ERR_PRINT(vformat("Cannot precompile a constant of type '%s' in '%s'.", Variant::get_type_name(p_value.get_type()), r_context.current_script));
	r_context.supported = false;
	return data;
}

Dictionary GDScriptBytecodeWriter::save_container_type(const ContainerType &p_type, WriteContext &r_context) {
	require_native_class(p_type.class_name, r_context);
	Dictionary data;
	data["builtin"] = p_type.builtin_type;
	data["class"] = p_type.class_name;
	data["script"] = p_type.script.is_valid() ? add_script(Object::cast_to<GDScript>(p_type.script.ptr()), r_context) : -1;
	return data;
}

Dictionary GDScriptBytecodeWriter::save_container(const Variant &p_value, WriteContext &r_context) {
	Dictionary data;
	Array values;
	data["type"] = p_value.get_type();
	if (p_value.get_type() == Variant::ARRAY) {
		const Array array = p_value;
		data["readonly"] = array.is_read_only();
		data["element"] = save_container_type(array.get_element_type(), r_context);
		for (const Variant &value : array) {
			values.push_back(save_value(value, r_context));
		}
	} else {
		const Dictionary dictionary = p_value;
		data["readonly"] = dictionary.is_read_only();
		data["key"] = save_container_type(dictionary.get_key_type(), r_context);
		data["element"] = save_container_type(dictionary.get_value_type(), r_context);
		for (const Variant &key : dictionary.keys()) {
			values.push_back(save_value(key, r_context));
			values.push_back(save_value(dictionary[key], r_context));
		}
	}
	data["values"] = values;
	return data;
}

Dictionary GDScriptBytecodeWriter::save_method_info(const MethodInfo &p_info, WriteContext &r_context) {
	require_property_class(p_info.return_val, r_context);
	for (const PropertyInfo &argument : p_info.arguments) {
		require_property_class(argument, r_context);
	}
	Dictionary data = p_info;
	Array defaults;
	for (const Variant &value : p_info.default_arguments) {
		defaults.push_back(save_value(value, r_context));
	}
	data["default_args"] = defaults;
	return data;
}

Dictionary GDScriptBytecodeWriter::save_type(const GDScriptDataType &p_type, GDScript *p_owner, WriteContext &r_context) {
	require_native_class(p_type.native_type, r_context);
	Dictionary value;
	if (p_type.kind == GDScriptDataType::SCRIPT) {
		ERR_PRINT("Cannot precompile a type provided by another scripting language.");
		r_context.supported = false;
	}
	value["kind"] = p_type.kind;
	value["builtin"] = p_type.builtin_type;
	value["native"] = p_type.native_type;
	if (p_type.kind == GDScriptDataType::GDSCRIPT) {
		value["script"] = add_script(Object::cast_to<GDScript>(p_type.script_type), r_context);
		value["strong"] = p_type.script_type_ref.is_valid();
	}
	Array elements;
	for (const GDScriptDataType &element : p_type.container_element_types) {
		elements.push_back(save_type(element, p_owner, r_context));
	}
	value["elements"] = elements;
	return value;
}

Dictionary GDScriptBytecodeWriter::save_function(GDScriptFunction *p_function, WriteContext &r_context) {
	bool &r_supported = r_context.supported;
	Dictionary data;
	if (!p_function) {
		return data;
	}
	data["name"] = p_function->name;
	data["source"] = p_function->source;
	data["static"] = p_function->_static;
	data["line"] = p_function->_initial_line;
	data["argument_count"] = p_function->_argument_count;
	data["vararg_index"] = p_function->_vararg_index;
	data["stack_size"] = p_function->_stack_size;
	data["instruction_args_size"] = p_function->_instruction_args_size;
	data["return_type"] = save_type(p_function->return_type, p_function->_script, r_context);
	Array arguments;
	for (const GDScriptDataType &argument : p_function->argument_types) {
		arguments.push_back(save_type(argument, p_function->_script, r_context));
	}
	data["argument_types"] = arguments;
	data["method_info"] = save_method_info(p_function->method_info, r_context);
	data["rpc_config"] = save_value(p_function->rpc_config, r_context);
	data["code"] = p_function->code;
	data["operator_cache_count"] = int64_t(p_function->operator_caches.size());
	data["default_arguments"] = p_function->default_arguments;
	Array constants;
	for (const Variant &constant : p_function->constants) {
		constants.push_back(save_value(constant, r_context));
	}
	data["constants"] = constants;
	Array stack_debug;
	for (const GDScriptFunction::StackDebug &entry : p_function->stack_debug) {
		stack_debug.push_back(Array{ entry.line, entry.pos, entry.added, entry.identifier });
	}
	data["stack_debug"] = stack_debug;
	Dictionary debug_constants;
	if (GDScriptCompilationContext::is_debug_compilation() || GDScriptLanguage::get_singleton()->should_track_locals()) {
		for (const KeyValue<StringName, Variant> &entry : p_function->constant_map) {
			debug_constants[entry.key] = save_value(entry.value, r_context);
		}
	}
	data["debug_constants"] = debug_constants;
#ifdef DEBUG_ENABLED
	if (GDScriptCompilationContext::is_debug_compilation()) {
		// Preserve identities and diagnostic labels, never live profiling counters.
		data["profile_signature"] = p_function->profile.signature;
		data["operator_names"] = p_function->operator_names;
		data["setter_names"] = p_function->setter_names;
		data["getter_names"] = p_function->getter_names;
		data["builtin_methods_names"] = p_function->builtin_methods_names;
		data["constructors_names"] = p_function->constructors_names;
		data["utilities_names"] = p_function->utilities_names;
	}
#endif
	Array names;
	for (const StringName &name : p_function->global_names) {
		names.push_back(name);
	}
	data["global_names"] = names;
	Array temporary_slots;
	for (const Pair<int, Variant::Type> &slot : p_function->temporary_slots) {
		temporary_slots.push_back(Vector2i(slot.first, slot.second));
	}
	data["temporary_slots"] = temporary_slots;
	Array operators;
	for (Variant::ValidatedOperatorEvaluator evaluator : p_function->operator_funcs) {
		bool found = false;
		for (int op = 0; op < Variant::OP_MAX && !found; op++) {
			for (int left = 0; left < Variant::VARIANT_MAX && !found; left++) {
				for (int right = 0; right < Variant::VARIANT_MAX && !found; right++) {
					if (Variant::get_validated_operator_evaluator(Variant::Operator(op), Variant::Type(left), Variant::Type(right)) == evaluator) {
						operators.push_back(Vector3i(op, left, right));
						found = true;
					}
				}
			}
		}
		if (!found) {
			ERR_PRINT(vformat("Cannot resolve a native operator in function '%s'.", p_function->name));
		}
		r_supported = r_supported && found;
	}
	data["operators"] = operators;
	Array constructors;
	for (Variant::ValidatedConstructor constructor : p_function->constructors) {
		bool found = false;
		for (int type = 0; type < Variant::VARIANT_MAX && !found; type++) {
			for (int index = 0; index < Variant::get_constructor_count(Variant::Type(type)) && !found; index++) {
				if (Variant::get_validated_constructor(Variant::Type(type), index) == constructor) {
					constructors.push_back(Vector2i(type, index));
					found = true;
				}
			}
		}
		r_supported = r_supported && found;
	}
	data["constructors"] = constructors;
	if (constructors.size() != p_function->constructors.size()) {
		ERR_PRINT(vformat("Cannot resolve a native constructor in function '%s'.", p_function->name));
	}
	Array builtin_methods;
	for (Variant::ValidatedBuiltInMethod method : p_function->builtin_methods) {
		bool found = false;
		for (int type = 0; type < Variant::VARIANT_MAX && !found; type++) {
			List<StringName> names;
			Variant::get_builtin_method_list(Variant::Type(type), &names);
			for (const StringName &name : names) {
				if (Variant::get_validated_builtin_method(Variant::Type(type), name) == method) {
					Array descriptor;
					descriptor.push_back(type);
					descriptor.push_back(name);
					builtin_methods.push_back(descriptor);
					found = true;
					break;
				}
			}
		}
		r_supported = r_supported && found;
	}
	data["builtin_methods"] = builtin_methods;
	Array utilities;
	List<StringName> utility_names;
	Variant::get_utility_function_list(&utility_names);
	for (Variant::ValidatedUtilityFunction utility : p_function->utilities) {
		bool found = false;
		for (const StringName &name : utility_names) {
			if (Variant::get_validated_utility_function(name) == utility) {
				utilities.push_back(name);
				found = true;
				break;
			}
		}
		r_supported = r_supported && found;
	}
	data["utilities"] = utilities;
	Array gds_utilities;
	List<StringName> gds_utility_names;
	GDScriptUtilityFunctions::get_function_list(&gds_utility_names);
	for (GDScriptUtilityFunctions::FunctionPtr utility : p_function->gds_utilities) {
		bool found = false;
		for (const StringName &name : gds_utility_names) {
			if (GDScriptUtilityFunctions::get_function(name) == utility) {
				gds_utilities.push_back(name);
				found = true;
				break;
			}
		}
		r_supported = r_supported && found;
	}
	data["gds_utilities"] = gds_utilities;
	Array keyed_getters;
	for (Variant::ValidatedKeyedGetter getter : p_function->keyed_getters) {
		bool found = false;
		for (int type = 0; type < Variant::VARIANT_MAX && !found; type++) {
			const Variant::Type variant_type = Variant::Type(type);
			if (Variant::is_keyed(variant_type) && Variant::get_member_validated_keyed_getter(variant_type) == getter) {
				keyed_getters.push_back(type);
				found = true;
			}
		}
		r_supported = r_supported && found;
	}
	data["keyed_getters"] = keyed_getters;
	Array keyed_setters;
	for (Variant::ValidatedKeyedSetter setter : p_function->keyed_setters) {
		bool found = false;
		for (int type = 0; type < Variant::VARIANT_MAX && !found; type++) {
			const Variant::Type variant_type = Variant::Type(type);
			if (Variant::is_keyed(variant_type) && Variant::get_member_validated_keyed_setter(variant_type) == setter) {
				keyed_setters.push_back(type);
				found = true;
			}
		}
		r_supported = r_supported && found;
	}
	data["keyed_setters"] = keyed_setters;
	data["getters"] = save_named_accessors(p_function->getters, Variant::get_member_validated_getter, r_supported);
	data["setters"] = save_named_accessors(p_function->setters, Variant::get_member_validated_setter, r_supported);
	data["indexed_getters"] = save_indexed_accessors(p_function->indexed_getters, Variant::get_member_validated_indexed_getter, r_supported);
	data["indexed_setters"] = save_indexed_accessors(p_function->indexed_setters, Variant::get_member_validated_indexed_setter, r_supported);
	Array lambdas;
	for (GDScriptFunction *lambda : p_function->lambdas) {
		if (!p_function->_script->lambda_info.has(lambda)) {
			r_supported = false;
			continue;
		}
		Dictionary descriptor = save_function(lambda, r_context);
		const GDScript::LambdaInfo &info = p_function->_script->lambda_info[lambda];
		descriptor["capture_count"] = info.capture_count;
		descriptor["use_self"] = info.use_self;
		lambdas.push_back(descriptor);
	}
	data["lambdas"] = lambdas;
	Array methods;
	for (MethodBind *method : p_function->methods) {
		require_native_class(method->get_instance_class(), r_context);
		if (!r_context.native_binding_indices.has(method)) {
			const int64_t signature = method->get_hash();
			Array descriptor;
			descriptor.push_back(method->get_instance_class());
			descriptor.push_back(method->get_name());
			descriptor.push_back(signature);
			descriptor.push_back(method->is_static());
			r_context.native_binding_indices.insert(method, r_context.native_bindings.size());
			r_context.native_bindings.push_back(descriptor);
			if (r_context.runtime_only) {
				r_context.native_methods[String(method->get_instance_class()) + "::" + String(method->get_name())] = signature;
			}
		}
		methods.push_back(r_context.native_binding_indices[method]);
	}
	data["methods"] = methods;
	return data;
}

Dictionary GDScriptBytecodeWriter::save_script(GDScript *p_script, WriteContext &r_context) {
	require_native_class(p_script->get_instance_base_type(), r_context);
	Dictionary data;
	data["owner"] = p_script->_owner ? add_script(p_script->_owner, r_context) : -1;
	data["abstract"] = p_script->_is_abstract;
	data["tool"] = p_script->tool;
	Dictionary subclasses;
	for (const KeyValue<StringName, Ref<GDScript>> &entry : p_script->subclasses) {
		subclasses[entry.key] = add_script(entry.value.ptr(), r_context);
	}
	data["subclasses"] = subclasses;
	data["native"] = p_script->native.is_valid() ? p_script->native->get_name() : StringName();
	data["global_name"] = p_script->global_name;
	data["local_name"] = p_script->local_name;
	data["qualified_name"] = p_script->fully_qualified_name;
	data["path"] = p_script->path;
	data["rpc_config"] = save_value(p_script->rpc_config, r_context);
	data["base"] = p_script->base.is_valid() ? add_script(p_script->base.ptr(), r_context) : -1;
	Array members;
	for (const KeyValue<StringName, GDScript::MemberInfo> &entry : p_script->member_indices) {
		require_property_class(entry.value.property_info, r_context);
		Dictionary value;
		value["name"] = entry.key;
		value["index"] = entry.value.index;
		value["setter"] = entry.value.setter;
		value["getter"] = entry.value.getter;
		value["type"] = save_type(entry.value.data_type, p_script, r_context);
		value["property"] = Dictionary(entry.value.property_info);
		value["own"] = p_script->members.has(entry.key);
		members.push_back(value);
	}
	data["members"] = members;
	Array static_members;
	for (const KeyValue<StringName, GDScript::MemberInfo> &entry : p_script->static_variables_indices) {
		require_property_class(entry.value.property_info, r_context);
		Dictionary value;
		value["name"] = entry.key;
		value["index"] = entry.value.index;
		value["setter"] = entry.value.setter;
		value["getter"] = entry.value.getter;
		value["type"] = save_type(entry.value.data_type, p_script, r_context);
		value["property"] = Dictionary(entry.value.property_info);
		static_members.push_back(value);
	}
	// Never serialize the writer's mutable static_variables values.
	data["static_members"] = static_members;
	data["keep_static"] = p_script->retain_static_data;
	Dictionary constants;
	for (const KeyValue<StringName, Variant> &entry : p_script->constants) {
		constants[entry.key] = save_value(entry.value, r_context);
	}
	data["constants"] = constants;
	Dictionary signals;
	for (const KeyValue<StringName, MethodInfo> &entry : p_script->_signals) {
		signals[entry.key] = save_method_info(entry.value, r_context);
	}
	data["signals"] = signals;
	Dictionary functions;
	for (const KeyValue<StringName, GDScriptFunction *> &entry : p_script->member_functions) {
		functions[entry.key] = save_function(entry.value, r_context);
	}
	data["functions"] = functions;
	data["initializer"] = p_script->initializer ? p_script->initializer->name : StringName();
	data["implicit_initializer"] = save_function(p_script->implicit_initializer, r_context);
	data["implicit_ready"] = save_function(p_script->implicit_ready, r_context);
	data["static_initializer"] = save_function(p_script->static_initializer, r_context);
	return data;
}

Error GDScriptBytecodeWriter::save_scripts(const TypedArray<GDScript> &p_scripts, const String &p_path) {
	WriteContext context;
	return save_scripts_internal(p_scripts, p_path, context);
}

Dictionary GDScriptBytecodeWriter::save_runtime_scripts(const TypedArray<GDScript> &p_scripts, const String &p_path) {
	Dictionary report;
	Array excluded;
	TypedArray<GDScript> roots;
	for (const Ref<GDScript> script : p_scripts) {
		if (!(script.is_valid() && script->is_valid())) {
			report["error"] = ERR_INVALID_PARAMETER;
			return report;
		}
		const StringName base = script->get_instance_base_type();
		const ClassDB::APIType api = ClassDB::class_exists(base) ? ClassDB::get_api_type(base) : ClassDB::API_NONE;
		if (api == ClassDB::API_EDITOR || api == ClassDB::API_EDITOR_EXTENSION) {
			if (!excluded.has(script->path)) {
				excluded.push_back(script->path);
			}
		} else {
			roots.push_back(script);
		}
	}
	WriteContext context;
	context.runtime_only = true;
	const Error error = save_scripts_internal(roots, p_path, context);
	Array paths;
	for (GDScript *script : context.scripts) {
		if (!script->_owner) {
			paths.push_back(script->path);
		}
	}
	paths.sort();
	excluded.sort();
	report["error"] = error;
	report["excluded_editor_roots"] = excluded;
	report["script_paths"] = paths;
	report["native_classes"] = context.native_classes;
	report["native_methods"] = context.native_methods;
	report["rejected_native_classes"] = context.rejected_native_classes;
	report["debug"] = GDScriptCompilationContext::is_debug_compilation();
	report["vm_source_sha256"] = GDSCRIPT_BYTECODE_VM_SOURCE_SHA256;
	report["real_size"] = int(sizeof(real_t));
	if (error == OK) {
		report["artifact_sha256"] = FileAccess::get_sha256(p_path);
	}
	return report;
}

Error GDScriptBytecodeWriter::save_scripts_internal(const TypedArray<GDScript> &p_scripts, const String &p_path, WriteContext &context) {
	if (p_scripts.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	for (const Ref<GDScript> script : p_scripts) {
		if (!(script.is_valid() && script->is_valid())) {
			return ERR_INVALID_PARAMETER;
		}
		add_script(script.ptr(), context);
	}
	Array scripts;
	Array containers;
	while (context.supported && (scripts.size() < context.scripts.size() || containers.size() < context.containers.size())) {
		if (scripts.size() < context.scripts.size()) {
			GDScript *script = context.scripts[scripts.size()];
			context.current_script = script->path;
			scripts.push_back(save_script(script, context));
			if (!context.supported) {
				ERR_PRINT(vformat("Cannot precompile GDScript '%s' (%s).", script->path, script->fully_qualified_name));
			}
		} else {
			// Copy before discovery can reallocate the context's container vector.
			const Variant value = context.containers[containers.size()];
			context.current_script = context.container_origins[containers.size()];
			containers.push_back(save_container(value, context));
		}
	}
	Vector<int> container_order;
	if (!context.supported || !context.rejected_native_classes.is_empty() || !GDScriptBytecodeData::get_container_order(containers, container_order)) {
		return ERR_UNAVAILABLE;
	}
	if (context.runtime_only) {
		HashSet<String> paths;
		for (GDScript *script : context.scripts) {
			if (!script->_owner) {
				if (script->path.is_empty() || paths.has(script->path)) {
					return ERR_INVALID_DATA;
				}
				paths.insert(script->path);
			}
		}
	}
	RBMap<uint64_t, int> initialization_order;
	for (int index = 0; index < context.scripts.size(); index++) {
		GDScript *script = context.scripts[index];
		if (script->static_initializer) {
			// Source reload initializes the outer class and recursively its owned
			// classes, in declaration order. Do not run an inner initializer twice.
			while (script->_owner) {
				script = script->_owner;
			}
			if (!script->compilation_initialization_order) {
				return ERR_UNAVAILABLE;
			}
			initialization_order.insert(script->compilation_initialization_order, context.indices[script]);
		}
	}
	Array initializers;
	for (const KeyValue<uint64_t, int> &entry : initialization_order) {
		initializers.push_back(entry.value);
	}
	Dictionary data;
	data["abi"] = GDScriptBytecodeFormat::SCHEMA_ID;
	data["vm_source_sha256"] = GDSCRIPT_BYTECODE_VM_SOURCE_SHA256;
	data["real_size"] = int(sizeof(real_t));
	data["debug"] = GDScriptCompilationContext::is_debug_compilation();
	data["native_bindings"] = context.native_bindings;
	data["root"] = 0;
	data["scripts"] = scripts;
	data["containers"] = containers;
	data["initializers"] = initializers;
	return save_metadata(data, p_path);
}

Error GDScriptBytecodeWriter::save_metadata(const Variant &p_data, const String &p_path) {
	GDScriptBytecodeImageWriter writer;
	Vector<uint8_t> bytes;
	Error error = writer.encode(p_data, bytes);
	if (error != OK) {
		return error;
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &error);
	if (file.is_null()) {
		return error;
	}
	file->store_buffer(bytes);
	return file->get_error();
}
