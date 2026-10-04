/**************************************************************************/
/*  test_export_android.cpp                                               */
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

TEST_FORCE_LINK(test_export_android)

#ifdef TOOLS_ENABLED

#include "platform/android/export/export_plugin.h"

class TestEditorExportPlatformAndroid {
public:
	static bool should_compress(const String &p_path, const Vector<uint8_t> &p_data) {
		return EditorExportPlatformAndroid::_should_compress_asset(p_path, p_data);
	}
};

TEST_CASE("[EditorExport][Android] VM bundles keep their own storage compression") {
	const Vector<uint8_t> data = String("GDBZ").to_utf8_buffer();
	CHECK_FALSE(TestEditorExportPlatformAndroid::should_compress("assets/.godot/compiled/gdscript.gdbc", data));
	CHECK_FALSE(TestEditorExportPlatformAndroid::should_compress("assets/CODE.GDBC", data));
	CHECK(TestEditorExportPlatformAndroid::should_compress("assets/other.bin", data));
	CHECK_FALSE(TestEditorExportPlatformAndroid::should_compress("assets/image.png", data));
	CHECK_FALSE(TestEditorExportPlatformAndroid::should_compress("assets/resource.res", String("RSCC").to_utf8_buffer()));
}

#endif // TOOLS_ENABLED
