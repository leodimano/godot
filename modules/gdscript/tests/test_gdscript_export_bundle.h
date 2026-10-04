/**************************************************************************/
/*  test_gdscript_export_bundle.h                                         */
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

#include "../editor/gdscript_export_bundle.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

TEST_CASE("[GDScript][Export] Compiled collection rejects opaque script inputs") {
	for (const String &path : { "res://old.gdc", "res://old.gdbc", "res://old.gd.remap", "res://scene.tscn", "res://data.tres", "res://nested.pck" }) {
		GDScriptExportBundle bundle;
		bool skip = true;
		CHECK(bundle.collect_file(path, Vector<uint8_t>(), skip) != OK);
		CHECK_FALSE(skip);
		CHECK_FALSE(bundle.get_failure().is_empty());
	}
}

TEST_CASE("[GDScript][Export] Empty script selection preserves ordinary assets") {
	GDScriptExportBundle bundle;
	bool skip = true;
	CHECK(bundle.collect_file("res://image.png", Vector<uint8_t>(), skip) == OK);
	CHECK_FALSE(skip);
	HashSet<String> paths;
	paths.insert("res://image.png");
	Vector<String> remaps;
	CHECK(bundle.finish(paths, remaps, true, 1) == OK);
	CHECK(paths.has("res://image.png"));
	CHECK(remaps.is_empty());
	CHECK(bundle.get_bytecode().is_empty());
	CHECK(String(bundle.get_manifest()["compression"]) == "zstd");
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
