/**************************************************************************/
/*  test_resource_manifest.cpp                                            */
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

TEST_FORCE_LINK(test_resource_manifest)

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_format_binary.h"
#include "core/io/resource_saver.h"
#include "tests/test_utils.h"

namespace TestResourceManifest {

TEST_CASE("[ResourceLoader] Binary manifests inspect resource types without loading dependencies") {
	const String dependency_path = TestUtils::get_temp_path("manifest_dependency.res");
	const String parent_path = TestUtils::get_temp_path("manifest_parent.res");
	uint32_t flags = 0;
	SUBCASE("Uncompressed binary resource") {}
	SUBCASE("Compressed binary resource") {
		flags = ResourceSaver::FLAG_COMPRESS;
	}
	{
		Ref<Resource> dependency;
		Ref<Resource> parent;
		Ref<Resource> internal;
		dependency.instantiate();
		parent.instantiate();
		internal.instantiate();
		REQUIRE(ResourceSaver::save(dependency, dependency_path) == OK);
		dependency->set_path(dependency_path);
		parent->set_meta("external", dependency);
		parent->set_meta("internal", internal);
		REQUIRE(ResourceSaver::save(parent, parent_path, flags) == OK);
	}
	CHECK_FALSE(ResourceCache::has(dependency_path));
	// A missing dependency must not affect inspection: it must not be loaded.
	REQUIRE(DirAccess::remove_absolute(dependency_path) == OK);
	{
		ResourceLoaderBinary reader;
		Dictionary manifest;
		Ref<FileAccess> file = FileAccess::open(parent_path, FileAccess::READ);
		REQUIRE(file.is_valid());
		REQUIRE(reader.get_resource_manifest(file, manifest) == OK);
		CHECK(String(manifest["root_type"]) == "Resource");
		const Array internals = manifest["internal_resources"];
		const Array externals = manifest["external_resources"];
		CHECK(internals.size() == 2);
		REQUIRE(externals.size() == 1);
		if (externals.size() != 1) {
			return;
		}
		CHECK(String(Dictionary(externals[0])["type"]) == "Resource");
		CHECK(String(Dictionary(externals[0])["path"]) == dependency_path);
		for (const Dictionary &resource : internals) {
			CHECK(String(resource["type"]) == "Resource");
		}
		CHECK_FALSE(ResourceCache::has(dependency_path));
		CHECK_FALSE(ResourceCache::has(parent_path));
	}
	CHECK(DirAccess::remove_absolute(parent_path) == OK);
}

TEST_CASE("[ResourceLoader] Binary manifest rejects a missing stream and clears stale output") {
	ResourceLoaderBinary reader;
	Dictionary manifest;
	manifest["stale"] = true;
	CHECK(reader.get_resource_manifest(Ref<FileAccess>(), manifest) == ERR_INVALID_PARAMETER);
	CHECK(manifest.is_empty());
}

} // namespace TestResourceManifest
