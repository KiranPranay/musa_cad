// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/qt_image_decoder.hpp"

#include <QByteArray>
#include <QImage>
#include <QString>

namespace musacad::ui {

namespace {

/// QImage -> our own RGBA8 type. Converting to one canonical format here is what keeps
/// every Qt image concept on this side of the seam.
core::DecodedImage from_qimage(const QImage& src) {
    core::DecodedImage out;
    if (src.isNull()) {
        return out; // invalid: the caller treats this as "could not decode"
    }
    const QImage rgba = src.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull() || rgba.width() <= 0 || rgba.height() <= 0) {
        return out;
    }
    out.width = static_cast<std::uint32_t>(rgba.width());
    out.height = static_cast<std::uint32_t>(rgba.height());
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4u);
    // Copy row by row: QImage rows are padded to a 4-byte stride, so a single memcpy of
    // the whole buffer would embed the padding.
    for (std::uint32_t y = 0; y < out.height; ++y) {
        const uchar* line = rgba.constScanLine(static_cast<int>(y));
        std::copy(line, line + static_cast<std::size_t>(out.width) * 4u,
                  out.rgba.begin() + static_cast<std::ptrdiff_t>(y) * out.width * 4);
    }
    return out;
}

} // namespace

core::DecodedImage QtImageDecoder::decode_file(std::string_view path) const {
    QImage img;
    if (!img.load(QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size())))) {
        return {}; // missing / unreadable / unsupported -- never throws
    }
    return from_qimage(img);
}

core::DecodedImage QtImageDecoder::decode_bytes(const std::vector<std::uint8_t>& bytes) const {
    QImage img;
    if (bytes.empty() ||
        !img.loadFromData(reinterpret_cast<const uchar*>(bytes.data()),
                          static_cast<int>(bytes.size()))) {
        return {};
    }
    return from_qimage(img);
}

} // namespace musacad::ui
