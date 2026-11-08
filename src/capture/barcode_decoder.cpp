#include "capture/barcode_decoder.h"

#include <ReadBarcode.h>

namespace pinax::capture {

namespace {

constexpr int longestEdge = 1280; // SPEC.md §6

} // namespace

std::vector<std::string> decodeEan13(const QImage& frame)
{
    if (frame.isNull())
        return {};
    QImage grey = frame;
    if (std::max(grey.width(), grey.height()) > longestEdge)
        grey = grey.scaled(longestEdge, longestEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    grey = grey.convertToFormat(QImage::Format_Grayscale8);

    const ZXing::ImageView view(grey.constBits(), grey.width(), grey.height(), ZXing::ImageFormat::Lum,
        static_cast<int>(grey.bytesPerLine()));
    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::EAN13);
    options.setEanAddOnSymbol(ZXing::EanAddOnSymbol::Ignore);
    options.setTryHarder(true);
    options.setTryRotate(true); // a book held sideways

    std::vector<std::string> found;
    for (const auto& result : ZXing::ReadBarcodes(view, options)) {
        if (result.isValid())
            found.push_back(result.text());
    }
    return found;
}

} // namespace pinax::capture
