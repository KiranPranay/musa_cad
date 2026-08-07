// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string_view>
#include <vector>

#include "musacad/core/image_decoder.hpp"

namespace musacad::ui {

/// Qt-backed implementation of the core IImageDecoder seam -- the same shape as
/// QtFontEngine implementing IFontEngine. Core declares the interface and speaks only
/// its own types; this side owns the only knowledge of QImage. No image library is
/// vendored: Qt is already a dependency and decodes PNG/JPEG/BMP/GIF out of the box.
///
/// Threading: QImage decoding reads immutable file/byte data and is safe off the GUI
/// thread, so the geometry thread may call this directly (like glyph outline extraction).
class QtImageDecoder final : public core::IImageDecoder {
public:
    [[nodiscard]] core::DecodedImage decode_file(std::string_view path) const override;
    [[nodiscard]] core::DecodedImage decode_bytes(
        const std::vector<std::uint8_t>& bytes) const override;
};

} // namespace musacad::ui
