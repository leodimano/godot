/**************************************************************************/
/*  gdscript_bytecode_envelope.cpp                                        */
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

#include "gdscript_bytecode_envelope.h"

#include "core/crypto/crypto_core.h"
#include "core/io/marshalls.h"

#include <zstd.h>

namespace {

class ZstdDecompressionContext {
	ZSTD_DCtx *context = ZSTD_createDCtx();

public:
	ZstdDecompressionContext() = default;
	ZstdDecompressionContext(const ZstdDecompressionContext &) = delete;
	ZstdDecompressionContext &operator=(const ZstdDecompressionContext &) = delete;
	~ZstdDecompressionContext() { ZSTD_freeDCtx(context); }
	ZSTD_DCtx *get() const { return context; }
};

} // namespace

#ifdef TOOLS_ENABLED
Error GDScriptBytecodeEnvelope::encode(const Vector<uint8_t> &p_image, CompressionMode p_compression, Vector<uint8_t> &r_stored) {
	// Retain the copy-on-write buffer before clearing a possibly aliased output.
	const Vector<uint8_t> image = p_image;
	r_stored.clear();
	if (p_compression != COMPRESSION_NONE && p_compression != COMPRESSION_ZSTD) {
		return ERR_INVALID_PARAMETER;
	}
	if (image.size() < GDScriptBytecodeFormat::IMAGE_HEADER_SIZE || image.size() > GDScriptBytecodeFormat::MAX_IMAGE_BYTES || decode_uint32(image.ptr()) != GDScriptBytecodeFormat::IMAGE_MAGIC) {
		return ERR_INVALID_DATA;
	}
	if (p_compression == COMPRESSION_NONE) {
		r_stored = image;
		return OK;
	}

	Vector<uint8_t> compressed;
	const size_t capacity = ZSTD_compressBound(image.size());
	if (capacity > MAX_STORED_BYTES - HEADER_SIZE) {
		return ERR_INVALID_DATA;
	}
	if (compressed.resize(HEADER_SIZE + capacity) != OK) {
		return ERR_OUT_OF_MEMORY;
	}
	const size_t payload_size = ZSTD_compress(compressed.ptrw() + HEADER_SIZE, capacity, image.ptr(), image.size(), 10);
	if (ZSTD_isError(payload_size)) {
		return ERR_INVALID_DATA;
	}
	compressed.resize(HEADER_SIZE + payload_size);
	uint8_t *header = compressed.ptrw();
	encode_uint32(MAGIC, header);
	encode_uint32(VERSION, header + 4);
	encode_uint32(COMPRESSION_ZSTD, header + 8);
	encode_uint32(image.size(), header + 12);
	encode_uint32(payload_size, header + 16);
	const Error error = CryptoCore::sha256(image.ptr(), image.size(), header + 20);
	if (error != OK) {
		return error;
	}
	r_stored = compressed;
	return OK;
}
#endif // TOOLS_ENABLED

Error GDScriptBytecodeEnvelope::decode(const Vector<uint8_t> &p_stored, Vector<uint8_t> &r_image) {
	const Vector<uint8_t> stored = p_stored;
	r_image.clear();
	if (stored.size() < GDScriptBytecodeFormat::IMAGE_HEADER_SIZE || stored.size() > MAX_STORED_BYTES) {
		return ERR_INVALID_DATA;
	}
	const uint8_t *header = stored.ptr();
	if (decode_uint32(header) == GDScriptBytecodeFormat::IMAGE_MAGIC) {
		if (stored.size() > GDScriptBytecodeFormat::MAX_IMAGE_BYTES) {
			return ERR_INVALID_DATA;
		}
		r_image = stored;
		return OK;
	}
	if (stored.size() < HEADER_SIZE || decode_uint32(header) != MAGIC || decode_uint32(header + 4) != VERSION || decode_uint32(header + 8) != COMPRESSION_ZSTD) {
		return ERR_INVALID_DATA;
	}
	const uint32_t image_size = decode_uint32(header + 12);
	const uint32_t payload_size = decode_uint32(header + 16);
	if (image_size < GDScriptBytecodeFormat::IMAGE_HEADER_SIZE || image_size > GDScriptBytecodeFormat::MAX_IMAGE_BYTES || payload_size != stored.size() - HEADER_SIZE) {
		return ERR_INVALID_DATA;
	}
	const uint8_t *payload = header + HEADER_SIZE;
	// Require one sized, dictionary-free frame, with no trailing bytes.
	if (ZSTD_getFrameContentSize(payload, payload_size) != image_size || ZSTD_getDictID_fromFrame(payload, payload_size) != 0 || ZSTD_findFrameCompressedSize(payload, payload_size) != payload_size) {
		return ERR_INVALID_DATA;
	}
	ZstdDecompressionContext context;
	if (!context.get()) {
		return ERR_OUT_OF_MEMORY;
	}
	if (ZSTD_isError(ZSTD_DCtx_setParameter(context.get(), ZSTD_d_windowLogMax, 27))) {
		return ERR_INVALID_DATA;
	}
	Vector<uint8_t> image;
	if (image.resize(image_size) != OK) {
		return ERR_OUT_OF_MEMORY;
	}
	const size_t decoded = ZSTD_decompressDCtx(context.get(), image.ptrw(), image_size, payload, payload_size);
	if (ZSTD_isError(decoded) || decoded != image_size || decode_uint32(image.ptr()) != GDScriptBytecodeFormat::IMAGE_MAGIC) {
		return ERR_INVALID_DATA;
	}
	uint8_t digest[32];
	const Error error = CryptoCore::sha256(image.ptr(), image.size(), digest);
	if (error != OK) {
		return error;
	}
	if (memcmp(digest, header + 20, sizeof(digest)) != 0) {
		return ERR_INVALID_DATA;
	}
	r_image = image;
	return OK;
}

Error GDScriptBytecodeEnvelope::read(const Ref<FileAccess> &p_file, Vector<uint8_t> &r_image) {
	r_image.clear();
	if (p_file.is_null()) {
		return ERR_INVALID_PARAMETER;
	}
	if (p_file->get_position() != 0) {
		return ERR_FILE_CANT_READ;
	}
	const uint64_t length = p_file->get_length();
	if (length < GDScriptBytecodeFormat::IMAGE_HEADER_SIZE || length > MAX_STORED_BYTES) {
		return ERR_INVALID_DATA;
	}
	const Vector<uint8_t> stored = p_file->get_buffer(length);
	if (stored.size() != int64_t(length)) {
		return ERR_FILE_CANT_READ;
	}
	return decode(stored, r_image);
}
