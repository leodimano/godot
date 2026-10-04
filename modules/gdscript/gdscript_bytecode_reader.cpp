/**************************************************************************/
/*  gdscript_bytecode_reader.cpp                                          */
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

#include "gdscript_bytecode_reader.h"

#include "gdscript_bytecode_compatibility.gen.h"
#include "gdscript_bytecode_data.h"
#include "gdscript_bytecode_envelope.h"
#include "gdscript_bytecode_format.h"
#include "gdscript_bytecode_instructions.h"
#include "gdscript_bytecode_load_profile.h"
#include "gdscript_cache.h"
#include "gdscript_compilation_context.h"

#include "core/config/engine.h"
#include "core/crypto/crypto_core.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/object/method_bind.h"
#include "core/variant/container_type_validate.h"

thread_local GDScriptBytecodeReader::ReadContext *GDScriptBytecodeReader::initializing_context = nullptr;
Mutex GDScriptBytecodeReader::resource_mutex;
HashMap<String, GDScriptBytecodeReader::LoadOwner> GDScriptBytecodeReader::resource_owners;

template <typename T>
static bool load_named_accessors(const GDScriptBytecodeView &p_records, Vector<T> &r_accessors, T (*p_lookup)(Variant::Type, const StringName &)) {
	if (p_records.get_type() != Variant::ARRAY) {
		return false;
	}
	const GDScriptBytecodeView records = p_records;
	for (const GDScriptBytecodeView &record : records) {
		if (record.get_type() != Variant::ARRAY) {
			return false;
		}
		const GDScriptBytecodeView descriptor = record;
		if (descriptor.size() != 2 || descriptor[0].get_type() != Variant::INT || int64_t(descriptor[0]) < 0 || int64_t(descriptor[0]) >= Variant::VARIANT_MAX || (descriptor[1].get_type() != Variant::STRING_NAME && descriptor[1].get_type() != Variant::STRING)) {
			return false;
		}
		T accessor = p_lookup(Variant::Type(int(descriptor[0])), descriptor[1]);
		if (!accessor) {
			return false;
		}
		r_accessors.push_back(accessor);
	}
	return true;
}

template <typename T>
static bool load_indexed_accessors(const GDScriptBytecodeView &p_records, Vector<T> &r_accessors, T (*p_lookup)(Variant::Type)) {
	if (p_records.get_type() != Variant::ARRAY) {
		return false;
	}
	const GDScriptBytecodeView records = p_records;
	for (const GDScriptBytecodeView &record : records) {
		if (record.get_type() != Variant::INT || int64_t(record) < 0 || int64_t(record) >= Variant::VARIANT_MAX) {
			return false;
		}
		T accessor = p_lookup(Variant::Type(int(record)));
		if (!accessor) {
			return false;
		}
		r_accessors.push_back(accessor);
	}
	return true;
}

bool GDScriptBytecodeReader::plain_value(const Variant &p_value) {
	if (p_value.get_type() == Variant::OBJECT || p_value.get_type() == Variant::CALLABLE || p_value.get_type() == Variant::SIGNAL || p_value.get_type() == Variant::RID || p_value.get_type() == Variant::ARRAY || p_value.get_type() == Variant::DICTIONARY) {
		return false;
	}
	return true;
}

Variant GDScriptBytecodeReader::load_value(const GDScriptBytecodeView &p_value, ReadContext &r_context) {
	const String kind = p_value.get("kind", "");
	if (kind == "value" && p_value.has("value") && p_value["value"].get_type() != Variant::ARRAY && p_value["value"].get_type() != Variant::DICTIONARY && plain_value(p_value["value"])) {
		return p_value["value"];
	}
	if (kind == "container") {
		const Variant index = p_value.get("id", -1);
		if (index.get_type() == Variant::INT && int64_t(index) >= 0 && int64_t(index) < r_context.containers.size()) {
			if (r_context.resolving_resources) {
				if (!fill_container(int(index), r_context)) {
					r_context.valid = false;
					return Variant();
				}
			} else if (r_context.current_script >= 0) {
				r_context.container_dependencies.write[r_context.current_script].push_back(int(index));
			}
			return r_context.containers[int(index)];
		}
	}
	if (kind == "resource" && r_context.resolving_resources) {
		const Variant path_value = p_value["path"];
		const Variant class_value = p_value["class"];
		if (path_value.get_type() == Variant::STRING && class_value.get_type() == Variant::STRING_NAME) {
			const String path = path_value;
			const StringName expected_class = class_value;
			if (path.begins_with("res://") && path.simplify_path() == path && ClassDB::is_parent_class(expected_class, SNAME("Resource"))) {
				GDScriptBytecodeData::ResolveScope scope(r_context.resource_load_depth);
				if (!scope.allowed()) {
					r_context.valid = false;
					return Variant();
				}
				Error error = OK;
				GDScriptBytecodeHydrationScope hydration_scope(r_context.profile, GDScriptBytecodeLoadProfile::RESOURCE_LOAD);
				Ref<Resource> resource = ResourceLoader::load(path, expected_class, ResourceFormatLoader::CACHE_MODE_REUSE, &error);
				if (error == ERR_BUSY) {
					// Match source preload semantics when an outer scene/resource load
					// is waiting for this script. Its loader fills this same reference.
					resource = ResourceLoader::ensure_resource_ref_override_for_outer_load(path, expected_class);
					error = resource.is_valid() ? OK : error;
				}
				if (error == OK && resource.is_valid() && resource->get_class_name() == expected_class) {
					return resource;
				}
			}
		}
	}
	if (kind == "null_object") {
		return Variant(static_cast<Object *>(nullptr));
	}
	if (kind == "empty_callable") {
		return Callable();
	}
	if (kind == "empty_rid") {
		return RID();
	}
	if (kind == "script") {
		const int index = p_value.get("id", -1);
		if (index >= 0 && index < r_context.scripts.size()) {
			return r_context.scripts[index];
		}
	} else if (kind == "native_class") {
		const StringName name = p_value["name"];
		if (ClassDB::class_exists(name)) {
			return Ref<GDScriptNativeClass>(memnew(GDScriptNativeClass(name)));
		}
	} else if (kind == "singleton") {
		const StringName name = p_value["name"];
		if (Engine::get_singleton()->has_singleton(name)) {
			return Engine::get_singleton()->get_singleton_object(name);
		}
	}
	r_context.valid = false;
	return Variant();
}

void GDScriptBytecodeReader::load_value_into(const GDScriptBytecodeView &p_value, Variant &r_value, ReadContext &r_context) {
	if (p_value.get("kind", "") == "resource" && !r_context.resolving_resources) {
		r_value = Variant(static_cast<Object *>(nullptr));
		r_context.resource_bindings.write[r_context.current_script].push_back({ &r_value, p_value });
	} else {
		r_value = load_value(p_value, r_context);
	}
}

bool GDScriptBytecodeReader::resolve_script_values(int p_index, ReadContext &r_context) {
	if (r_context.binding_states[p_index] == 2) {
		return true;
	}
	if (r_context.binding_states[p_index] == 1) {
		print_verbose(vformat("Bytecode resource constants reentered before completion: %s", r_context.scripts[p_index]->get_path()));
		return false; // A resource constructor must not observe unfinished constants.
	}
	GDScriptBytecodeData::ResolveScope scope(r_context.resolve_depth);
	if (!scope.allowed()) {
		return false;
	}
	GDScriptBytecodeHydrationScope hydration_scope(r_context.profile, GDScriptBytecodeLoadProfile::SCRIPT_BINDINGS);
	r_context.binding_states.write[p_index] = 1;
	for (const ResourceBinding &binding : r_context.resource_bindings[p_index]) {
		*binding.destination = load_value(binding.descriptor, r_context);
		if (!r_context.valid) {
			return false;
		}
	}
	for (int container : r_context.container_dependencies[p_index]) {
		if (!fill_container(container, r_context)) {
			return false;
		}
	}
	r_context.binding_states.write[p_index] = 2;
	return true;
}

bool GDScriptBytecodeReader::initialize_until(int p_position, ReadContext &r_context) {
	while (r_context.next_initializer <= p_position) {
		const int index = r_context.initializers[r_context.next_initializer++];
		if (r_context.reused[index]) {
			continue;
		}
		for (int member = 0; member < r_context.scripts.size(); member++) {
			if (r_context.outer_classes[member] == index && !resolve_script_values(member, r_context)) {
				return false;
			}
		}
		GDScriptCompilationContext::record_initialization_order(r_context.scripts[index].ptr());
		GDScriptBytecodeHydrationScope hydration_scope(r_context.profile, GDScriptBytecodeLoadProfile::STATIC_INITIALIZERS);
		if (r_context.scripts[index]->_static_init() != OK || !r_context.valid) {
			return false;
		}
	}
	return true;
}

bool GDScriptBytecodeReader::prepare_resource_script(int p_index, ReadContext &r_context) {
	if (!resolve_script_values(p_index, r_context)) {
		return false;
	}
	int last_initializer = -1;
	for (Ref<GDScript> current = r_context.scripts[p_index]; current.is_valid(); current = current->base) {
		int index = r_context.scripts.find(current);
		if (index < 0 || !resolve_script_values(index, r_context)) {
			return false;
		}
		index = r_context.outer_classes[index];
		for (int position = 0; position < r_context.initializers.size(); position++) {
			if (int(r_context.initializers[position]) == index) {
				last_initializer = MAX(last_initializer, position);
			}
		}
	}
	return initialize_until(last_initializer, r_context);
}

ContainerType GDScriptBytecodeReader::load_container_type(const GDScriptBytecodeView &p_data, ReadContext &r_context) {
	ContainerType type;
	const Variant builtin = p_data.get("builtin", -1);
	const Variant name = p_data["class"];
	const Variant script = p_data.get("script", -2);
	if (builtin.get_type() != Variant::INT || int64_t(builtin) < Variant::NIL || int64_t(builtin) >= Variant::VARIANT_MAX || (name.get_type() != Variant::STRING_NAME && name.get_type() != Variant::STRING) || script.get_type() != Variant::INT || int64_t(script) < -1 || int64_t(script) >= r_context.scripts.size()) {
		r_context.valid = false;
		return type;
	}
	type.builtin_type = Variant::Type(int(builtin));
	type.class_name = name;
	if ((type.class_name != StringName() && (type.builtin_type != Variant::OBJECT || !ClassDB::class_exists(type.class_name))) || (int(script) >= 0 && type.class_name == StringName())) {
		r_context.valid = false;
		return type;
	}
	if (int(script) >= 0) {
		type.script = r_context.scripts[int(script)];
	}
	return type;
}

bool GDScriptBytecodeReader::create_containers(const GDScriptBytecodeView &p_data, ReadContext &r_context) {
	for (const GDScriptBytecodeView &value : p_data) {
		const GDScriptBytecodeView data = value;
		const ContainerType element = load_container_type(data["element"], r_context);
		if (!r_context.valid) {
			return false;
		}
		// Assign types before publishing references: set_typed requires sole ownership.
		if (int(data["type"]) == Variant::ARRAY) {
			Array array;
			array.set_typed(element);
			r_context.containers.push_back(array);
		} else {
			const ContainerType key = load_container_type(data["key"], r_context);
			if (!r_context.valid) {
				return false;
			}
			Dictionary dictionary;
			dictionary.set_typed(key.builtin_type, key.class_name, key.script, element.builtin_type, element.class_name, element.script);
			r_context.containers.push_back(dictionary);
		}
	}
	return true;
}

bool GDScriptBytecodeReader::fill_containers(const GDScriptBytecodeView &p_data, const Vector<int> &p_order, ReadContext &r_context) {
	for (int index : p_order) {
		if (!fill_container(index, r_context)) {
			return false;
		}
	}
	return true;
}

bool GDScriptBytecodeReader::fill_container(int p_index, ReadContext &r_context) {
	if (r_context.container_states[p_index] == 2) {
		return true;
	}
	if (r_context.container_states[p_index] == 1) {
		print_verbose(vformat("Bytecode resource container reentered before completion: %d", p_index));
		return false;
	}
	GDScriptBytecodeData::ResolveScope scope(r_context.resolve_depth);
	if (!scope.allowed()) {
		return false;
	}
	GDScriptBytecodeHydrationScope hydration_scope(r_context.profile, GDScriptBytecodeLoadProfile::CONTAINER_VALUES);
	r_context.container_states.write[p_index] = 1;
	{
		const int index = p_index;
		const GDScriptBytecodeView data = r_context.container_data[index];
		const GDScriptBytecodeView values = data["values"];
		const ContainerType element = load_container_type(data["element"], r_context);
		ContainerTypeValidate element_validator;
		element_validator.type = element.builtin_type;
		element_validator.class_name = element.class_name;
		element_validator.script = element.script;
		if (int(data["type"]) == Variant::ARRAY) {
			Array array = r_context.containers[index];
			for (const GDScriptBytecodeView &encoded : values) {
				const Variant value = load_value(encoded, r_context);
				if (!r_context.valid || !element_validator.test_validate(value)) {
					return false;
				}
				array.push_back(value);
			}
			if (bool(data["readonly"])) {
				array.make_read_only();
			}
		} else {
			Dictionary dictionary = r_context.containers[index];
			const ContainerType key = load_container_type(data["key"], r_context);
			ContainerTypeValidate key_validator;
			key_validator.type = key.builtin_type;
			key_validator.class_name = key.class_name;
			key_validator.script = key.script;
			for (int entry = 0; entry < values.size(); entry += 2) {
				const Variant key_value = load_value(values[entry], r_context);
				const Variant value = load_value(values[entry + 1], r_context);
				if (!r_context.valid || !key_validator.test_validate(key_value) || !element_validator.test_validate(value) || dictionary.has(key_value)) {
					return false;
				}
				// Children are complete before hashing a container used as a key.
				dictionary[key_value] = value;
			}
			if (bool(data["readonly"])) {
				dictionary.make_read_only();
			}
		}
	}
	r_context.container_states.write[p_index] = 2;
	return true;
}

PropertyInfo GDScriptBytecodeReader::load_property_info(const GDScriptBytecodeView &p_data) {
	PropertyInfo result;
	result.type = Variant::Type(int(p_data.get("type", int(result.type))));
	result.name = p_data.get("name", result.name);
	result.class_name = p_data.get("class_name", result.class_name);
	result.hint = PropertyHint(int(p_data.get("hint", int(result.hint))));
	result.hint_string = p_data.get("hint_string", result.hint_string);
	result.usage = p_data.get("usage", result.usage);
	return result;
}

MethodInfo GDScriptBytecodeReader::load_method_info(const GDScriptBytecodeView &p_data, ReadContext &r_context) {
	MethodInfo result;
	result.name = p_data.get("name", result.name);
	result.flags = p_data.get("flags", result.flags);
	result.return_val = load_property_info(p_data["return"]);
	for (const GDScriptBytecodeView &argument : p_data["args"]) {
		result.arguments.push_back(load_property_info(argument));
	}
	const GDScriptBytecodeView defaults = p_data["default_args"];
	result.default_arguments.resize(defaults.size());
	for (int index = 0; index < defaults.size(); index++) {
		load_value_into(defaults[index], result.default_arguments.write[index], r_context);
	}
	return result;
}

GDScriptDataType GDScriptBytecodeReader::load_type(const GDScriptBytecodeView &p_data, GDScript *p_owner, ReadContext &r_context) {
	GDScriptDataType value;
	value.kind = GDScriptDataType::Kind(int(p_data["kind"]));
	value.builtin_type = Variant::Type(int(p_data["builtin"]));
	value.native_type = p_data["native"];
	if (value.kind == GDScriptDataType::GDSCRIPT) {
		const int index = p_data.get("script", -1);
		if (index < 0 || index >= r_context.scripts.size()) {
			r_context.valid = false;
			return value;
		}
		value.script_type = r_context.scripts[index].ptr();
		if (bool(p_data["strong"])) {
			value.script_type_ref = r_context.scripts[index];
		}
	}
	const GDScriptBytecodeView elements = p_data["elements"];
	for (int i = 0; i < elements.size(); i++) {
		value.container_element_types.push_back(load_type(elements[i], p_owner, r_context));
	}
	return value;
}

bool GDScriptBytecodeReader::load_function_debug(GDScriptFunction *p_function, const GDScriptBytecodeView &p_data, ReadContext &r_context) {
	if (p_data["debug_constants"].get_type() != Variant::DICTIONARY || p_data["stack_debug"].get_type() != Variant::ARRAY) {
		return false;
	}
	const GDScriptBytecodeView constants = p_data["debug_constants"];
	for (const Variant &name : constants.keys()) {
		if (name.get_type() != Variant::STRING_NAME || StringName(name) == StringName() || constants[name].get_type() != Variant::DICTIONARY) {
			return false;
		}
		p_function->constant_map.insert(name, Variant());
		load_value_into(constants[name], p_function->constant_map[name], r_context);
	}
	if (!r_context.valid) {
		return false;
	}
	const GDScriptBytecodeView entries = p_data["stack_debug"];
	// Events are append-only during compilation and read sequentially by the
	// debugger. Restore one contiguous allocation instead of one node per event.
	p_function->stack_debug.resize(entries.size());
	GDScriptFunction::StackDebug *stack_debug = p_function->stack_debug.ptrw();
	HashMap<StringName, Vector<int>> scopes;
	for (int index = 0; index < entries.size(); index++) {
		const GDScriptBytecodeView value = entries[index];
		if (value.get_type() != Variant::ARRAY) {
			return false;
		}
		const GDScriptBytecodeView entry = value;
		if (entry.size() != 4 || entry[0].get_type() != Variant::INT || entry[1].get_type() != Variant::INT || entry[2].get_type() != Variant::BOOL || entry[3].get_type() != Variant::STRING_NAME) {
			return false;
		}
		const int64_t line = entry[0];
		const int64_t position = entry[1];
		const StringName identifier = entry[3];
		if (line < 0 || line > INT32_MAX || position < 0 || position >= p_function->_stack_size || identifier == StringName()) {
			return false;
		}
		if (bool(entry[2])) {
			scopes[identifier].push_back(int(position));
		} else {
			Vector<int> *positions = scopes.getptr(identifier);
			if (!positions || positions->is_empty() || positions->get(positions->size() - 1) != position) {
				return false;
			}
			positions->resize(positions->size() - 1);
		}
		stack_debug[index] = { int(line), int(position), bool(entry[2]), identifier };
	}
#ifdef DEBUG_ENABLED
	if (p_data["profile_signature"].get_type() != Variant::STRING_NAME) {
		return false;
	}
	const char *labels[] = { "operator_names", "setter_names", "getter_names", "builtin_methods_names", "constructors_names", "utilities_names" };
	for (const char *label : labels) {
		if (p_data[label].get_type() != Variant::PACKED_STRING_ARRAY) {
			return false;
		}
	}
	p_function->profile.signature = p_data["profile_signature"];
	p_function->operator_names = p_data["operator_names"];
	p_function->setter_names = p_data["setter_names"];
	p_function->getter_names = p_data["getter_names"];
	p_function->builtin_methods_names = p_data["builtin_methods_names"];
	p_function->constructors_names = p_data["constructors_names"];
	p_function->utilities_names = p_data["utilities_names"];
#endif
	return true;
}

GDScriptFunction *GDScriptBytecodeReader::load_function(GDScript *p_script, const GDScriptBytecodeView &p_data, ReadContext &r_context) {
	if (p_data.is_empty()) {
		return nullptr;
	}
	const GDScriptBytecodeView instruction_args_size = p_data["instruction_args_size"];
	if (p_data["code"].get_type() != Variant::PACKED_INT32_ARRAY || p_data["default_arguments"].get_type() != Variant::PACKED_INT32_ARRAY ||
			instruction_args_size.get_type() != Variant::INT || int64_t(instruction_args_size) < 0 || int64_t(instruction_args_size) > INT32_MAX) {
		r_context.valid = false;
		return nullptr;
	}
	const Vector<int> code = p_data["code"];
	const Vector<int> default_arguments = p_data["default_arguments"];
	if (!GDScriptBytecodeInstructions::validate_layout(code, int(instruction_args_size), default_arguments)) {
		r_context.valid = false;
		return nullptr;
	}
	// On failure return the partial function with an invalid context. Its owner must
	// retain it until script rollback clears the member map: named lambdas can have
	// the same name as an already loaded method, and their destructor erases that name.
	GDScriptBytecodeLinkScope link_scope(r_context.profile, GDScriptBytecodeLoadProfile::FUNCTION_METADATA);
	GDScriptFunction *function = memnew(GDScriptFunction);
	function->_script = p_script;
	function->name = p_data["name"];
	function->source = p_data["source"];
	function->_static = p_data["static"];
	function->_initial_line = p_data["line"];
	function->_argument_count = p_data["argument_count"];
	function->_vararg_index = p_data["vararg_index"];
	function->_stack_size = p_data["stack_size"];
	function->_instruction_args_size = int(instruction_args_size);
	function->return_type = load_type(p_data["return_type"], p_script, r_context);
	const GDScriptBytecodeView arguments = p_data["argument_types"];
	for (int i = 0; i < arguments.size(); i++) {
		function->argument_types.push_back(load_type(arguments[i], p_script, r_context));
	}
	function->method_info = load_method_info(p_data["method_info"], r_context);
	load_value_into(p_data["rpc_config"], function->rpc_config, r_context);
	function->code = code;
	const GDScriptBytecodeView cache_count = p_data["operator_cache_count"];
	if (cache_count.get_type() != Variant::INT || int64_t(cache_count) < 0 || int64_t(cache_count) > function->code.size() / 6) {
		r_context.valid = false;
		return function;
	}
	function->operator_caches.resize(int64_t(cache_count));
	function->default_arguments = default_arguments;
	link_scope.phase(GDScriptBytecodeLoadProfile::FUNCTION_CONSTANTS);
	const GDScriptBytecodeView constants = p_data["constants"];
	function->constants.resize(constants.size());
	for (int i = 0; i < constants.size(); i++) {
		load_value_into(constants[i], function->constants.write[i], r_context);
	}
	link_scope.phase(GDScriptBytecodeLoadProfile::FUNCTION_DEBUG);
	if (!load_function_debug(function, p_data, r_context)) {
		r_context.valid = false;
		return function;
	}
	link_scope.phase(GDScriptBytecodeLoadProfile::FUNCTION_METADATA);
	const GDScriptBytecodeView names = p_data["global_names"];
	for (int i = 0; i < names.size(); i++) {
		function->global_names.push_back(names[i]);
	}
	const GDScriptBytecodeView temporary_slots = p_data["temporary_slots"];
	for (int i = 0; i < temporary_slots.size(); i++) {
		const Vector2i slot = temporary_slots[i];
		function->temporary_slots.push_back(Pair(slot.x, Variant::Type(slot.y)));
	}
	link_scope.phase(GDScriptBytecodeLoadProfile::NATIVE_BINDINGS);
	const GDScriptBytecodeView operators = p_data["operators"];
	for (int i = 0; i < operators.size(); i++) {
		const Vector3i descriptor = operators[i];
		Variant::ValidatedOperatorEvaluator evaluator = Variant::get_validated_operator_evaluator(Variant::Operator(descriptor.x), Variant::Type(descriptor.y), Variant::Type(descriptor.z));
		if (!evaluator) {
			r_context.valid = false;
			return function;
		}
		function->operator_funcs.push_back(evaluator);
	}
	const GDScriptBytecodeView constructors = p_data["constructors"];
	for (int i = 0; i < constructors.size(); i++) {
		const Vector2i descriptor = constructors[i];
		Variant::ValidatedConstructor constructor = Variant::get_validated_constructor(Variant::Type(descriptor.x), descriptor.y);
		if (!constructor) {
			r_context.valid = false;
			return function;
		}
		function->constructors.push_back(constructor);
	}
	const GDScriptBytecodeView methods = p_data["methods"];
	if (methods.get_type() != Variant::ARRAY) {
		r_context.valid = false;
		return function;
	}
	for (int i = 0; i < methods.size(); i++) {
		const GDScriptBytecodeView index = methods[i];
		if (index.get_type() != Variant::INT || int64_t(index) < 0 || int64_t(index) >= r_context.native_bindings.size()) {
			r_context.valid = false;
			return function;
		}
		function->methods.push_back(r_context.native_bindings[int(index)]);
	}
	const GDScriptBytecodeView builtin_methods = p_data["builtin_methods"];
	for (int i = 0; i < builtin_methods.size(); i++) {
		const GDScriptBytecodeView descriptor = builtin_methods[i];
		Variant::ValidatedBuiltInMethod method = Variant::get_validated_builtin_method(Variant::Type(int(descriptor[0])), descriptor[1]);
		if (!method) {
			r_context.valid = false;
			return function;
		}
		function->builtin_methods.push_back(method);
	}
	const GDScriptBytecodeView utilities = p_data["utilities"];
	for (int i = 0; i < utilities.size(); i++) {
		Variant::ValidatedUtilityFunction utility = Variant::get_validated_utility_function(utilities[i]);
		if (!utility) {
			r_context.valid = false;
			return function;
		}
		function->utilities.push_back(utility);
	}
	const GDScriptBytecodeView gds_utilities = p_data["gds_utilities"];
	for (int i = 0; i < gds_utilities.size(); i++) {
		const StringName name = gds_utilities[i];
		if (!GDScriptUtilityFunctions::function_exists(name)) {
			r_context.valid = false;
			return function;
		}
		function->gds_utilities.push_back(GDScriptUtilityFunctions::get_function(name));
#ifdef DEBUG_ENABLED
		function->gds_utilities_names.push_back(name);
#endif
	}
	const GDScriptBytecodeView keyed_getters = p_data["keyed_getters"];
	for (int i = 0; i < keyed_getters.size(); i++) {
		const Variant::Type type = Variant::Type(int(keyed_getters[i]));
		if (type < 0 || type >= Variant::VARIANT_MAX || !Variant::is_keyed(type)) {
			r_context.valid = false;
			return function;
		}
		function->keyed_getters.push_back(Variant::get_member_validated_keyed_getter(type));
	}
	const GDScriptBytecodeView keyed_setters = p_data["keyed_setters"];
	for (int i = 0; i < keyed_setters.size(); i++) {
		const Variant::Type type = Variant::Type(int(keyed_setters[i]));
		if (type < 0 || type >= Variant::VARIANT_MAX || !Variant::is_keyed(type)) {
			r_context.valid = false;
			return function;
		}
		function->keyed_setters.push_back(Variant::get_member_validated_keyed_setter(type));
	}
	if (!load_named_accessors(p_data["getters"], function->getters, Variant::get_member_validated_getter) ||
			!load_named_accessors(p_data["setters"], function->setters, Variant::get_member_validated_setter) ||
			!load_indexed_accessors(p_data["indexed_getters"], function->indexed_getters, Variant::get_member_validated_indexed_getter) ||
			!load_indexed_accessors(p_data["indexed_setters"], function->indexed_setters, Variant::get_member_validated_indexed_setter)) {
		r_context.valid = false;
		return function;
	}
	link_scope.phase(GDScriptBytecodeLoadProfile::FUNCTION_FINALIZE);
	const GDScriptBytecodeView lambdas = p_data["lambdas"];
	for (int i = 0; i < lambdas.size(); i++) {
		const GDScriptBytecodeView descriptor = lambdas[i];
		GDScriptFunction *lambda = load_function(p_script, descriptor, r_context);
		if (!lambda) {
			r_context.valid = false;
			return function;
		}
		function->lambdas.push_back(lambda);
		if (!r_context.valid) {
			return function;
		}
		GDScript::LambdaInfo info;
		info.capture_count = descriptor["capture_count"];
		info.use_self = descriptor["use_self"];
		if (info.capture_count < 0 || info.capture_count > lambda->_argument_count) {
			r_context.valid = false;
			return function;
		}
		p_script->lambda_info.insert(lambda, info);
	}
	function->_code_size = function->code.size();
	function->_code_ptr = function->code.ptr();
	function->_default_arg_count = MAX(0, function->default_arguments.size() - 1);
	function->_default_arg_ptr = function->default_arguments.ptr();
	function->_constant_count = function->constants.size();
	function->_constants_ptr = function->constants.ptrw();
	function->_global_names_count = function->global_names.size();
	function->_global_names_ptr = function->global_names.ptr();
	function->_operator_funcs_count = function->operator_funcs.size();
	function->_operator_funcs_ptr = function->operator_funcs.ptr();
	function->_constructors_count = function->constructors.size();
	function->_constructors_ptr = function->constructors.ptr();
	function->_methods_count = function->methods.size();
	function->_methods_ptr = function->methods.ptrw();
	function->_builtin_methods_count = function->builtin_methods.size();
	function->_builtin_methods_ptr = function->builtin_methods.ptr();
	function->_utilities_count = function->utilities.size();
	function->_utilities_ptr = function->utilities.ptr();
	function->_gds_utilities_count = function->gds_utilities.size();
	function->_gds_utilities_ptr = function->gds_utilities.ptr();
	function->_keyed_getters_count = function->keyed_getters.size();
	function->_keyed_getters_ptr = function->keyed_getters.ptr();
	function->_keyed_setters_count = function->keyed_setters.size();
	function->_keyed_setters_ptr = function->keyed_setters.ptr();
	function->_getters_count = function->getters.size();
	function->_getters_ptr = function->getters.ptr();
	function->_setters_count = function->setters.size();
	function->_setters_ptr = function->setters.ptr();
	function->_indexed_getters_count = function->indexed_getters.size();
	function->_indexed_getters_ptr = function->indexed_getters.ptr();
	function->_indexed_setters_count = function->indexed_setters.size();
	function->_indexed_setters_ptr = function->indexed_setters.ptr();
	function->_lambdas_count = function->lambdas.size();
	function->_lambdas_ptr = function->lambdas.ptrw();
#ifdef DEBUG_ENABLED
	function->func_cname = String(function->name).utf8();
	function->_func_cname = function->func_cname.get_data();
#endif
	return function;
}

bool GDScriptBytecodeReader::load_script(GDScript *p_script, const GDScriptBytecodeView &p_data, ReadContext &r_context) {
	GDScriptBytecodeLinkScope link_scope(r_context.profile, GDScriptBytecodeLoadProfile::CLASS_METADATA);
	const StringName native_name = p_data["native"];
	if (native_name != StringName()) {
		if (!ClassDB::class_exists(native_name)) {
			return false;
		}
		p_script->native = Ref<GDScriptNativeClass>(memnew(GDScriptNativeClass(native_name)));
	}
	const int base_index = p_data["base"];
	if (base_index >= 0) {
		if (base_index >= r_context.scripts.size() || r_context.scripts[base_index].ptr() == p_script) {
			return false;
		}
		p_script->base = r_context.scripts[base_index];
	}
	p_script->global_name = p_data["global_name"];
	p_script->local_name = p_data["local_name"];
	p_script->fully_qualified_name = p_data["qualified_name"];
	p_script->path = p_data["path"];
	p_script->path_valid = true;
	p_script->_is_abstract = p_data["abstract"];
	p_script->tool = p_data["tool"];
	const int owner_index = p_data["owner"];
	p_script->_owner = owner_index >= 0 ? r_context.scripts[owner_index].ptr() : nullptr;
	const GDScriptBytecodeView subclasses = p_data["subclasses"];
	const GDScriptBytecodeView subclass_names = subclasses.keys();
	for (const Variant &name : subclass_names) {
		p_script->subclasses.insert(name, r_context.scripts[int(subclasses[name])]);
	}
	const Variant rpc_config = load_value(p_data["rpc_config"], r_context);
	if (!r_context.valid || rpc_config.get_type() != Variant::DICTIONARY) {
		return false;
	}
	p_script->rpc_config = rpc_config;
	p_script->retain_static_data = p_data["keep_static"];
	const GDScriptBytecodeView static_members = p_data["static_members"];
	p_script->static_variables.resize(static_members.size());
	HashSet<int> static_indices;
	for (int i = 0; i < static_members.size(); i++) {
		const GDScriptBytecodeView value = static_members[i];
		GDScript::MemberInfo info;
		info.index = value["index"];
		const StringName name = value["name"];
		if (info.index < 0 || info.index >= static_members.size() || static_indices.has(info.index) || p_script->static_variables_indices.has(name)) {
			return false;
		}
		static_indices.insert(info.index);
		info.setter = value["setter"];
		info.getter = value["getter"];
		info.data_type = load_type(value["type"], p_script, r_context);
		info.property_info = load_property_info(value["property"]);
		p_script->static_variables_indices.insert(name, info);
	}
	const GDScriptBytecodeView members = p_data["members"];
	for (int i = 0; i < members.size(); i++) {
		const GDScriptBytecodeView value = members[i];
		GDScript::MemberInfo info;
		info.index = value["index"];
		info.setter = value["setter"];
		info.getter = value["getter"];
		info.data_type = load_type(value["type"], p_script, r_context);
		info.property_info = load_property_info(value["property"]);
		p_script->member_indices.insert(value["name"], info);
		if (bool(value["own"])) {
			p_script->members.insert(value["name"]);
		}
	}
	const GDScriptBytecodeView constants = p_data["constants"];
	const GDScriptBytecodeView constant_names = constants.keys();
	for (int i = 0; i < constant_names.size(); i++) {
		p_script->constants.insert(constant_names[i], Variant());
		load_value_into(constants[constant_names[i]], p_script->constants[constant_names[i]], r_context);
	}
	const GDScriptBytecodeView signals = p_data["signals"];
	const GDScriptBytecodeView signal_names = signals.keys();
	for (int i = 0; i < signal_names.size(); i++) {
		p_script->_signals.insert(signal_names[i], load_method_info(signals[signal_names[i]], r_context));
	}
	const GDScriptBytecodeView functions = p_data["functions"];
	const GDScriptBytecodeView names = functions.keys();
	for (int i = 0; i < names.size(); i++) {
		GDScriptFunction *function = load_function(p_script, functions[names[i]], r_context);
		if (!function) {
			return false;
		}
		p_script->member_functions.insert(names[i], function);
		if (!r_context.valid) {
			return false;
		}
	}
	const StringName initializer = p_data["initializer"];
	if (initializer != StringName()) {
		if (!p_script->member_functions.has(initializer)) {
			return false;
		}
		p_script->initializer = p_script->member_functions[initializer];
	}
	p_script->implicit_initializer = load_function(p_script, p_data["implicit_initializer"], r_context);
	p_script->implicit_ready = load_function(p_script, p_data["implicit_ready"], r_context);
	p_script->static_initializer = load_function(p_script, p_data["static_initializer"], r_context);
	if (!p_script->implicit_initializer) {
		return false;
	}
	return r_context.valid;
}

Ref<GDScript> GDScriptBytecodeReader::load(const String &p_path) {
	return load_graph(p_path, String(), false);
}

Ref<GDScript> GDScriptBytecodeReader::load_resource(const String &p_path, const String &p_original_path, bool p_cache_resources) {
	// Normal loading selects a logical script path through an export remap.
	// Direct bundle inspection remains available through the explicit load API.
	if (p_original_path.is_empty() || p_original_path == p_path) {
		return Ref<GDScript>();
	}
	// A static initializer can load a peer in the graph being linked on this
	// thread. Do not publish the entire graph globally before initialization.
	for (ReadContext *context = initializing_context; context; context = context->previous) {
		if (context->bundle == p_path) {
			for (int index = 0; index < context->scripts.size(); index++) {
				const Ref<GDScript> &script = context->scripts[index];
				if (!script->_owner && script->path == p_original_path) {
					if (context->resource_load_depth > 0 && !prepare_resource_script(index, *context)) {
						print_verbose(vformat("Bytecode resource script preparation failed: %s", p_original_path));
						context->valid = false;
						return Ref<GDScript>();
					}
					return script;
				}
			}
		}
	}
	const Ref<ResourceLoader::LoadToken> token = ResourceLoader::_get_current_load_token();
	const Thread::ID thread = Thread::get_caller_id();
	while (true) {
		Ref<ResourceLoader::LoadToken> wait_for;
		bool owns_load = false;
		bool was_affine = false;
		{
			MutexLock lock(resource_mutex);
			// Preserve serialized graph publication without holding an opaque
			// mutex across ResourceLoader calls. Nested transactions stay on
			// this thread; peers wait through native load-task cycle detection.
			for (const KeyValue<String, LoadOwner> &owner : resource_owners) {
				if (owner.value.thread != thread) {
					wait_for = owner.value.token;
					if (wait_for.is_null()) {
						return Ref<GDScript>(); // Explicit inspection has no native load task to await.
					}
					break;
				}
			}
			if (wait_for.is_null()) {
				if (resource_owners.has(p_path)) {
					return Ref<GDScript>(); // Reentry before script shells are ready.
				}
				was_affine = ResourceLoader::_set_load_task_owner_affine(token, true);
				resource_owners.insert(p_path, { token, thread });
				owns_load = true;
			}
		}
		if (owns_load) {
			Ref<GDScript> result = load_graph(p_path, p_original_path, p_cache_resources);
			{
				MutexLock lock(resource_mutex);
				resource_owners.erase(p_path);
				ResourceLoader::_set_load_task_owner_affine(token, was_affine);
			}
			return result;
		}
		Error error = OK;
		if (ResourceLoader::_load_complete(*wait_for.ptr(), &error).is_null() || error != OK) {
			return Ref<GDScript>();
		}
	}
}

Ref<GDScript> GDScriptBytecodeReader::load_graph(const String &p_path, const String &p_original_path, bool p_cache_resources) {
	// Declared first so cleanup of temporary decoded metadata is timed too.
	GDScriptBytecodeLoadProfile profile(p_path);
	if (GDScriptCompilationContext::is_compile_only()) {
		return Ref<GDScript>();
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		return Ref<GDScript>();
	}
	// Bound/decompress the storage envelope before the unchanged image/ABI checks.
	const uint64_t file_length = file->get_length();
	Vector<uint8_t> bytes;
	if (GDScriptBytecodeEnvelope::read(file, bytes) != OK) {
		return Ref<GDScript>();
	}
	profile.phase(GDScriptBytecodeLoadProfile::DECODE);
	GDScriptBytecodeImage image;
	if (image.open(bytes) != OK) {
		return Ref<GDScript>();
	}
	const GDScriptBytecodeView data = image.root();
	if (data.get_type() != Variant::DICTIONARY) {
		return Ref<GDScript>();
	}
	String digest;
	profile.phase(GDScriptBytecodeLoadProfile::HASH);
	if (p_cache_resources) {
		unsigned char hash[32];
		if (CryptoCore::sha256(bytes.ptr(), bytes.size(), hash) != OK) {
			return Ref<GDScript>();
		}
		digest = String::hex_encode_buffer(hash, 32);
	}
	profile.phase(GDScriptBytecodeLoadProfile::VALIDATE);
#ifdef DEBUG_ENABLED
	const bool debug = true;
#else
	const bool debug = false;
#endif
	if (data["abi"].get_type() != Variant::STRING || data["vm_source_sha256"].get_type() != Variant::STRING ||
			data["pointer_size"].get_type() != Variant::INT || data["real_size"].get_type() != Variant::INT || data["debug"].get_type() != Variant::BOOL) {
		return Ref<GDScript>();
	}
	const bool compatible = String(data["abi"]) == GDScriptBytecodeFormat::SCHEMA_ID &&
			String(data["vm_source_sha256"]) == GDSCRIPT_BYTECODE_VM_SOURCE_SHA256 &&
			int64_t(data["pointer_size"]) == sizeof(void *) && int64_t(data["real_size"]) == sizeof(real_t) && bool(data["debug"]) == debug;
	ERR_FAIL_COND_V_MSG(!compatible, Ref<GDScript>(), vformat("Cannot load precompiled GDScript '%s': incompatible bytecode. Use an editor and export template built from matching sources, with the same build profile and precision.", p_path));
	// Resolve and verify each native signature once, before scripts or resources exist.
	const GDScriptBytecodeView bindings = data["native_bindings"];
	if (bindings.get_type() != Variant::ARRAY) {
		return Ref<GDScript>();
	}
	Vector<MethodBind *> native_bindings;
	native_bindings.resize(bindings.size());
	for (int i = 0; i < bindings.size(); i++) {
		const GDScriptBytecodeView descriptor = bindings[i];
		if (descriptor.get_type() != Variant::ARRAY || descriptor.size() != 4 || descriptor[0].get_type() != Variant::STRING_NAME || descriptor[1].get_type() != Variant::STRING_NAME || descriptor[2].get_type() != Variant::INT || int64_t(descriptor[2]) < 0 || int64_t(descriptor[2]) > UINT32_MAX || descriptor[3].get_type() != Variant::BOOL) {
			return Ref<GDScript>();
		}
		MethodBind *method = ClassDB::get_method(descriptor[0], descriptor[1]);
		if (!method || method->get_hash() != uint32_t(descriptor[2]) || method->is_static() != bool(descriptor[3])) {
			ERR_PRINT(vformat("The template does not provide the required native method or signature: %s::%s.", String(descriptor[0]), String(descriptor[1])));
			return Ref<GDScript>();
		}
		native_bindings.write[i] = method;
	}
	// Metadata is executable code from a trusted compiler. These structural and
	// compatibility checks do not sandbox or authenticate malicious artifacts.
	const GDScriptBytecodeView scripts = data["scripts"];
	profile.inventory(file_length, scripts.size());
	if (data["containers"].get_type() != Variant::ARRAY) {
		return Ref<GDScript>();
	}
	const GDScriptBytecodeView containers = data["containers"];
	Vector<int> container_order;
	if (!GDScriptBytecodeData::get_container_order(containers, container_order)) {
		return Ref<GDScript>();
	}
	int root = data.get("root", -1);
	if (scripts.is_empty() || root < 0 || root >= scripts.size()) {
		return Ref<GDScript>();
	}
	const GDScriptBytecodeView initializers = data["initializers"];
	HashSet<int> initializer_ids;
	for (const GDScriptBytecodeView &value : initializers) {
		if (value.get_type() != Variant::INT) {
			return Ref<GDScript>();
		}
		const int index = value;
		if (index < 0 || index >= scripts.size() || initializer_ids.has(index)) {
			return Ref<GDScript>();
		}
		initializer_ids.insert(index);
	}
	// Reject invalid base links before allocating objects or establishing strong references.
	for (int index = 0; index < scripts.size(); index++) {
		int current = index;
		int depth = 0;
		while (current != -1) {
			if (current < 0 || current >= scripts.size() || ++depth > scripts.size() || scripts[current].get_type() != Variant::DICTIONARY) {
				return Ref<GDScript>();
			}
			const GDScriptBytecodeView script_data = scripts[current];
			current = script_data.get("base", -2);
		}
	}
	Vector<int> owners;
	Vector<int> outer_classes;
	owners.resize(scripts.size());
	outer_classes.resize(scripts.size());
	for (int index = 0; index < scripts.size(); index++) {
		const GDScriptBytecodeView script_data = scripts[index];
		const Variant owner = script_data.get("owner", -2);
		if (owner.get_type() != Variant::INT || int64_t(owner) < -1 || int64_t(owner) >= scripts.size()) {
			return Ref<GDScript>();
		}
		owners.write[index] = owner;
	}
	HashSet<int> owned_classes;
	HashSet<int> required_initializers;
	for (int index = 0; index < scripts.size(); index++) {
		int outer = index;
		int depth = 0;
		while (owners[outer] != -1) {
			if (++depth >= scripts.size()) {
				return Ref<GDScript>();
			}
			outer = owners[outer];
		}
		outer_classes.write[index] = outer;
		const GDScriptBytecodeView script_data = scripts[index];
		const GDScriptBytecodeView initializer = script_data["static_initializer"];
		if (!initializer.is_empty()) {
			required_initializers.insert(outer);
		}
		const GDScriptBytecodeView subclasses = script_data["subclasses"];
		const GDScriptBytecodeView constants = script_data["constants"];
		for (const Variant &name : subclasses.keys()) {
			const Variant child_id = subclasses[name];
			if ((name.get_type() != Variant::STRING && name.get_type() != Variant::STRING_NAME) || child_id.get_type() != Variant::INT || int64_t(child_id) < 0 || int64_t(child_id) >= scripts.size()) {
				return Ref<GDScript>();
			}
			const int child = child_id;
			const GDScriptBytecodeView child_data = scripts[child];
			const GDScriptBytecodeView constant = constants[name];
			if (owners[child] != index || owned_classes.has(child) || child_data["local_name"] != name || child_data["path"] != script_data["path"] || constant.get("kind", "") != "script" || int(constant.get("id", -1)) != child) {
				return Ref<GDScript>();
			}
			owned_classes.insert(child);
		}
	}
	if (required_initializers.size() != initializer_ids.size()) {
		return Ref<GDScript>();
	}
	for (int index = 0; index < scripts.size(); index++) {
		if (required_initializers.has(index) != initializer_ids.has(index) || (owners[index] >= 0) != owned_classes.has(index)) {
			return Ref<GDScript>();
		}
	}
	HashSet<String> paths;
	int requested_root = -1;
	for (int index = 0; index < scripts.size(); index++) {
		const GDScriptBytecodeView script_data = scripts[index];
		const String path = script_data["path"];
		if (!p_original_path.is_empty() && owners[index] == -1) {
			if (!path.begins_with("res://") || paths.has(path)) {
				return Ref<GDScript>();
			}
			paths.insert(path);
			if (path == p_original_path) {
				requested_root = index;
			}
		}
	}
	if (!p_original_path.is_empty() && p_original_path != p_path) {
		if (requested_root < 0) {
			return Ref<GDScript>();
		}
		root = requested_root;
	}
	profile.phase(GDScriptBytecodeLoadProfile::ALLOCATE);
	ReadContext context;
	context.native_bindings = native_bindings;
	context.profile = &profile;
	context.bundle = p_path;
	context.container_data = containers;
	context.initializers = initializers;
	context.outer_classes = outer_classes;
	context.container_states.resize(containers.size());
	context.container_states.fill(0);
	context.binding_states.resize(scripts.size());
	context.binding_states.fill(0);
	context.resource_bindings.resize(scripts.size());
	context.container_dependencies.resize(scripts.size());
	for (int index = 0; index < scripts.size(); index++) {
		Ref<GDScript> script;
		const GDScriptBytecodeView script_data = scripts[index];
		if (p_cache_resources && owners[index] == -1) {
			const Ref<Resource> existing = ResourceCache::get_ref(script_data["path"]);
			if (existing.is_valid()) {
				script = existing;
				if (script.is_null() || !script->valid || script->compiled_bundle != p_path || script->compiled_bundle_digest != digest) {
					return Ref<GDScript>();
				}
			}
		}
		context.reused.push_back(script.is_valid());
		if (script.is_null()) {
			script.instantiate();
		}
		context.scripts.push_back(script);
	}
	// Nested classes share a resource path with their owner. Reuse them through
	// the cached owner's tree, never by looking that shared path up independently.
	for (int index = 0; index < scripts.size(); index++) {
		const int outer = outer_classes[index];
		if (owners[index] < 0 || !context.reused[outer]) {
			continue;
		}
		Vector<StringName> names;
		for (int current = index; owners[current] != -1; current = owners[current]) {
			const GDScriptBytecodeView script_data = scripts[current];
			names.push_back(script_data["local_name"]);
		}
		Ref<GDScript> script = context.scripts[outer];
		for (int current = names.size() - 1; current >= 0; current--) {
			const Ref<GDScript> *child = script->subclasses.getptr(names[current]);
			if (!child || child->is_null()) {
				return Ref<GDScript>();
			}
			script = *child;
		}
		if (!script->valid || script->compiled_bundle != p_path || script->compiled_bundle_digest != digest) {
			return Ref<GDScript>();
		}
		context.scripts.write[index] = script;
		context.reused.write[index] = true;
	}
	// A bundle is published atomically and the script cache retains every outer
	// class. Reuse the entire graph, including constant identities, or fail closed.
	int reused_count = 0;
	for (bool reused : context.reused) {
		reused_count += reused ? 1 : 0;
	}
	if (reused_count) {
		if (reused_count == context.scripts.size()) {
			profile.complete(true);
		}
		return reused_count == context.scripts.size() ? context.scripts[root] : Ref<GDScript>();
	}
	context.valid = create_containers(containers, context);
	profile.phase(GDScriptBytecodeLoadProfile::LINK);
	for (int index = 0; index < scripts.size() && context.valid; index++) {
		context.current_script = index;
		if (!context.reused[index] && !load_script(context.scripts[index].ptr(), scripts[index], context)) {
			context.valid = false;
			break;
		}
	}
	profile.phase(GDScriptBytecodeLoadProfile::INITIALIZE);
	if (context.valid) {
		for (int index = 0; index < context.scripts.size(); index++) {
			if (!context.reused[index]) {
				context.scripts[index]->valid = true;
				context.scripts[index]->_static_default_init();
			}
		}
		context.previous = initializing_context;
		initializing_context = &context;
		context.resolving_resources = true;
		profile.phase(GDScriptBytecodeLoadProfile::CONTAINERS);
		context.valid = fill_containers(containers, container_order, context);
		for (int index = 0; index < scripts.size() && context.valid; index++) {
			context.valid = resolve_script_values(index, context);
		}
		profile.phase(GDScriptBytecodeLoadProfile::INITIALIZE);
		if (context.valid) {
			context.valid = initialize_until(initializers.size() - 1, context);
		}
		initializing_context = context.previous;
	}
	if (!context.valid) {
		// Rejection must not register partially hydrated classes as live orphans.
		for (int index = 0; index < context.scripts.size(); index++) {
			if (!context.reused[index]) {
				context.scripts[index]->_owner = nullptr;
				context.scripts[index]->subclasses.clear();
			}
		}
		for (int index = 0; index < context.scripts.size(); index++) {
			if (context.reused[index]) {
				continue;
			}
			const Ref<GDScript> &script = context.scripts[index];
			script->valid = false;
			script->clear();
			script->constants.clear();
			script->base.unref();
		}
		return Ref<GDScript>();
	}
	// Match source retention for scripts without @static_unload, but only after
	// validation and initialization. Isolated inspection loads do not replace
	// another live generation's process-wide static cache entry.
	profile.phase(GDScriptBytecodeLoadProfile::PUBLISH);
	if (p_cache_resources) {
		for (int index = 0; index < context.scripts.size(); index++) {
			if (context.reused[index]) {
				continue;
			}
			const Ref<GDScript> &script = context.scripts[index];
			script->compiled_bundle = p_path;
			script->compiled_bundle_digest = digest;
			if (script->_owner) {
				continue;
			}
			// Publish the whole validated graph before another bundle load can begin.
			script->set_path(script->path);
			// Match source loading's strong outer-class lifetime. Inner classes only
			// hold a non-owning owner pointer, and must not outlive a cached owner.
			GDScriptCache::add_compiled_script(script);
			if (script->retain_static_data) {
				GDScriptCache::add_static_script(script);
			}
		}
	}
	profile.complete();
	return context.scripts[root];
}
