#include "capture/barcode_scanner.h"

#include "capture/barcode_decoder.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QMediaDevices>
#include <QVideoFrame>

#include <algorithm>
#include <cmath>

namespace pinax::capture {

namespace {

// The format to ask for: 1280 pixels on the long edge if offered, which is
// what SPEC.md §6 decodes at (a phone's MJPEG 1280x720,
// docs/PHONE_WEBCAM.md); else the nearest. The faster rate breaks a tie.
QCameraFormat bestFormat(const QCameraDevice& device)
{
    QCameraFormat best;
    int bestDistance = 0;
    for (const QCameraFormat& format : device.videoFormats()) {
        const QSize size = format.resolution();
        const int distance = std::abs(std::max(size.width(), size.height()) - 1280);
        if (best.isNull() || distance < bestDistance
            || (distance == bestDistance && format.maxFrameRate() > best.maxFrameRate())) {
            best = format;
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace

BarcodeScanner::BarcodeScanner(QObject* parent)
    : QObject(parent)
{
    session_.setVideoSink(&sink_);
    connect(&sink_, &QVideoSink::videoFrameChanged, this, &BarcodeScanner::frameArrived);
}

BarcodeScanner::~BarcodeScanner() = default;

std::vector<CameraChoice> BarcodeScanner::cameras()
{
    std::vector<CameraChoice> found;
    for (const QCameraDevice& device : QMediaDevices::videoInputs())
        found.push_back({device.id(), device.description(), device.isDefault()});
    std::stable_partition(found.begin(), found.end(), [](const CameraChoice& c) { return c.isDefault; });
    return found;
}

bool BarcodeScanner::start(const QByteArray& cameraId)
{
    stop();
    const auto devices = QMediaDevices::videoInputs();
    QCameraDevice device;
    for (const QCameraDevice& candidate : devices) {
        if (cameraId.isEmpty() ? candidate.isDefault() : candidate.id() == cameraId)
            device = candidate;
    }
    if (device.isNull() && cameraId.isEmpty() && !devices.isEmpty())
        device = devices.front();
    if (device.isNull()) {
        emit problem(cameraId.isEmpty() ? tr("No camera is connected.")
                                        : tr("That camera is no longer connected."));
        return false;
    }

    camera_ = std::make_unique<QCamera>(device);
    if (const QCameraFormat format = bestFormat(device); !format.isNull())
        camera_->setCameraFormat(format);
    connect(camera_.get(), &QCamera::errorOccurred, this, [this](QCamera::Error, const QString& message) {
        emit problem(message.isEmpty() ? tr("The camera stopped working.") : message);
    });
    cameraId_ = device.id();
    reads_.reset();
    sinceDecode_.invalidate();
    session_.setCamera(camera_.get());
    camera_->start();
    return true;
}

void BarcodeScanner::stop()
{
    if (!camera_)
        return;
    camera_->stop();
    session_.setCamera(nullptr);
    camera_.reset();
}

void BarcodeScanner::frameArrived(const QVideoFrame& frame)
{
    if (!camera_ || !frame.isValid())
        return;
    const QImage image = frame.toImage();
    if (image.isNull())
        return;
    emit frameReady(image);
    if (sinceDecode_.isValid() && sinceDecode_.elapsed() < decodeIntervalMs)
        return;
    sinceDecode_.start();
    takeFrame(image, domain::BarcodeReads::Clock::now());
}

void BarcodeScanner::takeFrame(const QImage& image, domain::BarcodeReads::Clock::time_point at)
{
    if (const auto isbn = reads_.frame(decodeEan13(image), at))
        emit isbnRead(QString::fromStdString(*isbn));
}

} // namespace pinax::capture
