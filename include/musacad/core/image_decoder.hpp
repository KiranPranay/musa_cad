// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// The platform seam for raster decoding, mirroring IFontEngine/IGeometryKernel exactly:
// core declares the narrow interface and speaks only its OWN types, the Qt layer above
// implements it, and the store carries an injected pointer. Core therefore stays Qt-free
// and no image library is vendored -- the same reasoning that keeps a DWG converter
// behind a process boundary and glyph outlines behind IFontEngine.

namespace musacad::core {

/// A decoded raster image in our own type: 8-bit RGBA, row-major, top row first,
/// `width * height * 4` bytes. No external image type ever crosses this boundary.
struct DecodedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 &&
               rgba.size() == static_cast<std::size_t>(width) * height * 4u;
    }
};

/// Decodes raster images for the store. Implemented above core (QtImageDecoder in the
/// UI layer) and injected via GeometryStore::set_image_decoder, so the geometry thread,
/// the plot path and the headless CLI all reach the same decoder.
///
/// Threading: implementations must be safe to call from the geometry thread. Decoding
/// happens on a cache miss, not per frame.
class IImageDecoder {
public:
    virtual ~IImageDecoder() = default;

    /// Decode an image file. Returns an invalid DecodedImage on any failure (missing
    /// file, unreadable, unsupported format) -- never throws, never partially fills.
    [[nodiscard]] virtual DecodedImage decode_file(std::string_view path) const = 0;

    /// Decode from an in-memory encoded blob (a PNG/JPEG byte stream), used for the
    /// base64-embedded form so an embedded image needs no temporary file.
    [[nodiscard]] virtual DecodedImage decode_bytes(const std::vector<std::uint8_t>& bytes) const = 0;
};

} // namespace musacad::core
