/**************************************************************************/
/*  test_extension_api_dump.cpp                                           */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_extension_api_dump)

#ifdef DEBUG_ENABLED

#include "core/config/engine.h"
#include "core/extension/extension_api_dump.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "tests/test_utils.h"

namespace TestExtensionAPIDump {

struct APIDumpTestFixture {
	List<Engine::Singleton> uninitialized_singletons;

	APIDumpTestFixture() {
		// Unit test setup registers Input and InputMap before creating them.
		// Supply their native type names without constructing platform input.
		List<Engine::Singleton> singletons;
		Engine::get_singleton()->get_singletons(&singletons);
		for (const Engine::Singleton &singleton : singletons) {
			if (!singleton.ptr && singleton.class_name == StringName()) {
				REQUIRE(ClassDB::class_exists(singleton.name));
				uninitialized_singletons.push_back(singleton);
				Engine::get_singleton()->remove_singleton(singleton.name);
				Engine::get_singleton()->add_singleton(Engine::Singleton(singleton.name, nullptr, singleton.name));
			}
		}
	}

	~APIDumpTestFixture() {
		for (const Engine::Singleton &singleton : uninitialized_singletons) {
			Engine::get_singleton()->remove_singleton(singleton.name);
			Engine::get_singleton()->add_singleton(singleton);
		}
	}
};

TEST_CASE_FIXTURE(APIDumpTestFixture, "[GDExtensionAPI] Dump reflects the registered native API") {
	const Dictionary api = GDExtensionAPIDump::generate_extension_api();
	const Array classes = api["classes"];
	HashSet<StringName> dumped_classes;
	for (int i = 0; i < classes.size(); i++) {
		const Dictionary entry = classes[i];
		const StringName name = entry["name"];
		CHECK(ClassDB::class_exists(name));
		CHECK(ClassDB::is_class_exposed(name));
		CHECK_FALSE(dumped_classes.has(name));
		dumped_classes.insert(name);
		if (entry.has("inherits")) {
			CHECK(StringName(entry["inherits"]) == ClassDB::get_parent_class(name));
		}
		const Array methods = entry.get("methods", Array());
		for (int j = 0; j < methods.size(); j++) {
			const Dictionary method = methods[j];
			if (bool(method["is_virtual"])) {
				continue;
			}
			MethodBind *bind = ClassDB::get_method(name, method["name"]);
			REQUIRE(bind != nullptr);
			CHECK(bind->get_hash() == uint32_t(method["hash"]));
		}
	}
	LocalVector<StringName> registered_classes;
	ClassDB::get_class_list(registered_classes);
	for (const StringName &name : registered_classes) {
		CHECK(dumped_classes.has(name) == ClassDB::is_class_exposed(name));
	}
	CHECK(dumped_classes.has("Object"));
}

TEST_CASE_FIXTURE(APIDumpTestFixture, "[GDExtensionAPI] JSON output reports file errors") {
	const String path = TestUtils::get_temp_path("extension_api_dump.json");
	CHECK(GDExtensionAPIDump::generate_extension_json_file(path) == OK);
	const Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(path));
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	CHECK(Dictionary(parsed).has("classes"));
	CHECK(DirAccess::remove_absolute(path) == OK);
	ERR_PRINT_OFF
	CHECK(GDExtensionAPIDump::generate_extension_json_file(TestUtils::get_temp_path("")) != OK);
	ERR_PRINT_ON
}

} // namespace TestExtensionAPIDump

#endif // DEBUG_ENABLED
