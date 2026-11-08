#pragma once

#include "domain/barcode_reads.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QMediaCaptureSession>
#include <QObject>
#include <QString>
#include <QVideoSink>

#include <memory>
#include <vector>

class QCamera;
class QVideoFrame;

namespace pinax::capture {

// A camera a scan may use: its stable id, and what to call it — "Integrated
// Camera", or "<model>: Android Webcam" for a phone (docs/PHONE_WEBCAM.md).
struct CameraChoice {
    QByteArray id;
    QString description;
    bool isDefault = false;
};

// Reads ISBNs from a camera (F-025, D-014, D-033): frames from Qt
// Multimedia, EAN-13 decoded by ZXing-C++, and accepted under SPEC.md §6 by
// domain::BarcodeReads. Emits each frame for a preview and each ISBN read,
// and nothing else: it neither looks a book up nor writes one (AV-012).
//
// Decoding runs on the UI thread, at most every `decodeInterval`, so a
// preview at the camera's rate is not held up; three agreeing decodes then
// take about a third of a second.
class BarcodeScanner : public QObject {
    Q_OBJECT

public:
    static constexpr int decodeIntervalMs = 100;

    explicit BarcodeScanner(QObject* parent = nullptr);
    ~BarcodeScanner() override;

    // The cameras present now, the system's default first.
    static std::vector<CameraChoice> cameras();

    // Starts the camera with this id, or the default with none. Returns
    // false, with problem() saying why, if there is no such camera.
    bool start(const QByteArray& cameraId = {});
    void stop();
    bool running() const { return camera_ != nullptr; }
    QByteArray cameraId() const { return cameraId_; }

    // One frame, as the camera would deliver it: decoded, and the read
    // emitted if this frame completes one. Public so tests can feed frames
    // without a camera; `at` stands for the time it was taken.
    void takeFrame(const QImage& image, domain::BarcodeReads::Clock::time_point at);

signals:
    // A frame for the preview.
    void frameReady(const QImage& frame);
    // An ISBN-13, read under SPEC.md §6.
    void isbnRead(const QString& isbn13);
    // The camera failed, or went away.
    void problem(const QString& message);

private:
    void frameArrived(const QVideoFrame& frame);

    QMediaCaptureSession session_;
    QVideoSink sink_;
    std::unique_ptr<QCamera> camera_;
    QByteArray cameraId_;
    domain::BarcodeReads reads_;
    QElapsedTimer sinceDecode_;
};

} // namespace pinax::capture
