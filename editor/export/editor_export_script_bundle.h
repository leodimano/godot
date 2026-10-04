/**************************************************************************/
/*  editor_export_script_bundle.h                                         */
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

#include "core/object/ref_counted.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

// The language module supplies the implementation. The export pipeline must
// not link directly against a module's concrete bundle implementation.
class EditorExportScriptBundle : public RefCounted {
	GDCLASS(EditorExportScriptBundle, RefCounted);

public:
	typedef Ref<EditorExportScriptBundle> (*CreateFunc)();

private:
	static CreateFunc create_func;

protected:
	Dictionary manifest;
	Vector<uint8_t> bytecode;
	String failure;

public:
	static void set_create_func(CreateFunc p_create_func);
	static Ref<EditorExportScriptBundle> create();

	virtual Error collect_file(const String &p_path, const Vector<uint8_t> &p_data, bool &r_skip) = 0;
	virtual Error finish(HashSet<String> &r_selected_paths, Vector<String> &r_remaps, bool p_debug, int p_compression) = 0;
	virtual String get_bundle_path() const = 0;
	virtual String get_manifest_path() const = 0;

	const Vector<uint8_t> &get_bytecode() const { return bytecode; }
	const Dictionary &get_manifest() const { return manifest; }
	const String &get_failure() const { return failure; }
};
