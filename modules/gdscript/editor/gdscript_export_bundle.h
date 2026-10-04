/**************************************************************************/
/*  gdscript_export_bundle.h                                              */
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

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

// Owns one export's actual script inputs, after preset and plugin filtering.
// It does not inspect directory names or maintain an application-specific list.
class GDScriptExportBundle {
	HashSet<String> sources;
	HashMap<String, String> source_hashes;
	HashSet<String> runtime_paths;
	HashMap<String, String> resource_dependencies;
	Dictionary manifest;
	Vector<uint8_t> bytecode;
	String failure;
	bool finished = false;

public:
	static constexpr const char *BUNDLE_PATH = "res://.godot/compiled/gdscript.gdbc";
	static constexpr const char *MANIFEST_PATH = "res://.godot/compiled/gdscript.manifest.json";
	Error collect_file(const String &p_path, const Vector<uint8_t> &p_data, bool &r_skip);
	Error finish(HashSet<String> &r_selected_paths, Vector<String> &r_remaps, bool p_debug, int p_compression);
	const Vector<uint8_t> &get_bytecode() const { return bytecode; }
	const Dictionary &get_manifest() const { return manifest; }
	const String &get_failure() const { return failure; }
};
