#include "capture/barcode_decoder.h"
#include "capture/barcode_scanner.h"

#include <BitMatrix.h>
#include <MultiFormatWriter.h>

#include <QPainter>
#include <QSignalSpy>
#include <QTest>
#include <QTransform>

#include <chrono>

using namespace pinax::capture;
using namespace std::chrono_literals;

namespace {

const std::string phlebas = "9780316005388";
const std::string sticker = "5012345678900"; // a shop's EAN-13: valid, not a book's

// An EAN-13 as a camera might see it: black bars on white.
QImage barcode(const std::string& digits, int width = 380, int height = 140)
{
    const auto bits = ZXing::MultiFormatWriter(ZXing::BarcodeFormat::EAN13).setMargin(12).encode(digits, width, height);
    const auto matrix = ZXing::ToMatrix<uint8_t>(bits);
    QImage image(matrix.width(), matrix.height(), QImage::Format_Grayscale8);
    for (int y = 0; y < matrix.height(); ++y)
        std::copy_n(matrix.data() + y * matrix.width(), matrix.width(), image.scanLine(y));
    return image;
}

// A frame with a barcode somewhere on a larger, light background, as on a
// book's back cover.
QImage onCover(const QImage& code, QSize size = {1280, 720}, QPoint at = {400, 300})
{
    QImage frame(size, QImage::Format_RGB32);
    frame.fill(QColor(235, 230, 220));
    QPainter painter(&frame);
    painter.drawImage(at, code);
    return frame;
}

} // namespace

class TestCapture : public QObject {
    Q_OBJECT

private slots:
    void decodesAnIsbnOnACover();
    void decodesABookHeldSideways();
    void readsOnlyEan13();
    void aLargeFrameIsScaledAndStillRead();
    void theScannerReadsAfterThreeAgreeingFrames();
    void aStickerAloneIsNeverRead();
};

void TestCapture::decodesAnIsbnOnACover()
{
    const auto found = decodeEan13(onCover(barcode(phlebas)));
    QCOMPARE(found, std::vector<std::string>({phlebas}));
    QVERIFY(decodeEan13(QImage()).empty());
    QVERIFY(decodeEan13(onCover(QImage(10, 10, QImage::Format_Grayscale8))).empty());
}

void TestCapture::decodesABookHeldSideways()
{
    const QImage turned = barcode(phlebas).transformed(QTransform().rotate(90));
    QCOMPARE(decodeEan13(onCover(turned, {720, 1280}, {300, 400})), std::vector<std::string>({phlebas}));
}

void TestCapture::readsOnlyEan13()
{
    // A Code 128 label, as a shop or library might stick on, is not read.
    const auto bits = ZXing::MultiFormatWriter(ZXing::BarcodeFormat::Code128).setMargin(12).encode("SHELF-0042", 380, 140);
    const auto matrix = ZXing::ToMatrix<uint8_t>(bits);
    QImage label(matrix.width(), matrix.height(), QImage::Format_Grayscale8);
    for (int y = 0; y < matrix.height(); ++y)
        std::copy_n(matrix.data() + y * matrix.width(), matrix.width(), label.scanLine(y));
    QVERIFY(decodeEan13(onCover(label)).empty());
}

void TestCapture::aLargeFrameIsScaledAndStillRead()
{
    // 1080p, as a phone's webcam offers: scaled to 1280 first (SPEC.md §6).
    const auto found = decodeEan13(onCover(barcode(phlebas, 520, 190), {1920, 1080}, {700, 450}));
    QCOMPARE(found, std::vector<std::string>({phlebas}));
}

void TestCapture::theScannerReadsAfterThreeAgreeingFrames()
{
    BarcodeScanner scanner;
    QSignalSpy read(&scanner, &BarcodeScanner::isbnRead);
    const QImage frame = onCover(barcode(phlebas));
    auto t = pinax::domain::BarcodeReads::Clock::time_point {};

    scanner.takeFrame(frame, t);
    scanner.takeFrame(frame, t += 100ms);
    QCOMPARE(read.count(), 0);
    scanner.takeFrame(frame, t += 100ms);
    QCOMPARE(read.count(), 1);
    QCOMPARE(read.front().front().toString(), QString::fromStdString(phlebas));

    // Left in front of the camera: read once.
    for (int i = 0; i < 30; ++i)
        scanner.takeFrame(frame, t += 100ms);
    QCOMPARE(read.count(), 1);
    QVERIFY(!scanner.running()); // no camera was opened for any of this
}

void TestCapture::aStickerAloneIsNeverRead()
{
    BarcodeScanner scanner;
    QSignalSpy read(&scanner, &BarcodeScanner::isbnRead);
    const QImage frame = onCover(barcode(sticker));
    QCOMPARE(decodeEan13(frame), std::vector<std::string>({sticker})); // decoded, but
    auto t = pinax::domain::BarcodeReads::Clock::time_point {};
    for (int i = 0; i < 10; ++i)
        scanner.takeFrame(frame, t += 100ms);
    QCOMPARE(read.count(), 0); // not a book's (AV-012)
}

QTEST_MAIN(TestCapture)
#include "test_capture.moc"
