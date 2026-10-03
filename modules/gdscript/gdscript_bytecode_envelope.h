/**************************************************************************/
/*  gdscript_bytecode_envelope.h                                          */
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

#include "gdscript_bytecode_format.h"

#include "core/io/file_access.h"

// Storage framing only: the caller must validate the decoded image and VM data.
// The digest detects corruption. It does not authenticate downloaded code.
class GDScriptBytecodeEnvelope {
public:
	enum CompressionMode {
		COMPRESSION_NONE,
		COMPRESSION_ZSTD,
	};

	static constexpr uint32_t MAGIC = 0x5a424447; // GDBZ, little endian.
	static constexpr uint32_t VERSION = 1;
	static constexpr uint32_t HEADER_SIZE = 52;
	// ZSTD_compressBound(n) is n + n / 256 for n >= 128 KiB.
	static constexpr uint32_t MAX_STORED_BYTES = HEADER_SIZE + GDScriptBytecodeFormat::MAX_IMAGE_BYTES + GDScriptBytecodeFormat::MAX_IMAGE_BYTES / 256;

	// Input and output may alias. Failed operations clear the output.
#ifdef TOOLS_ENABLED
	static Error encode(const Vector<uint8_t> &p_image, CompressionMode p_compression, Vector<uint8_t> &r_stored);
#endif
	static Error decode(const Vector<uint8_t> &p_stored, Vector<uint8_t> &r_image);
	// Read a complete file positioned at its beginning.
	static Error read(const Ref<FileAccess> &p_file, Vector<uint8_t> &r_image);
};
