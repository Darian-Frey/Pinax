#pragma once

#include <QImage>

#include <string>
#include <vector>

namespace pinax::capture {

// The EAN-13 barcodes ZXing-C++ finds in one frame (D-014, SPEC.md §6), as
// their thirteen digits; a price add-on is ignored. EAN-13 only, so a QR
// code or a shop's Code 128 is never read. The frame is turned to grey and
// scaled to at most 1280 pixels on its long edge first. What counts as an
// ISBN is domain::BarcodeReads' to decide, not this.
std::vector<std::string> decodeEan13(const QImage& frame);

} // namespace pinax::capture
