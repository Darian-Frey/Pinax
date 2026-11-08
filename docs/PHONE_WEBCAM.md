# An Android phone as the webcam

**Recorded:** 2026-10-26, made general 2026-11-08
**Related:** D-014, F-025, Phase 5, AV-012

Phase 5 scans a book's barcode with a camera (D-014). It does not need a
webcam bought for the purpose: an Android phone in its built-in USB
**Webcam** mode works. Linux sees it as a standard UVC camera, so D-014's
approach (Qt Multimedia → `QVideoSink` → ZXing-C++) applies unchanged.

The figures below were measured with one phone on the development machine.
Another phone will differ in its formats and rates, but not in how it is set
up or seen.

---

## The phone

- **Android 14 or later**, with the USB Webcam mode the maker has left in.
  It is part of Android itself (`com.android.DeviceAsWebcam`), so there is no
  app to install. Not every maker ships it.
- **The rear (main) camera**, which has autofocus. A front camera is often
  fixed-focus and will not read a barcode at 10–20 cm.

## On the Linux side

| | |
|---|---|
| Capture node | One `/dev/videoN`: the phone's first UVC interface (index 0) |
| Metadata node | The next `/dev/videoN` (index 1). Not a video source: opening it as one fails with `VIDIOC_G_INPUT: Inappropriate ioctl` |
| Stable path | `/dev/v4l/by-id/usb-<maker>_<model>_<serial>-video-index0` |
| Description | `<model>: Android Webcam`, which is what `QCameraDevice::description()` shows |
| Access | Granted to the logged-in user by udev; membership of the `video` group is not needed |
| Other cameras | A laptop's built-in camera is likely to be the system default. **Pinax must let the user pick the camera and remember the choice.** |

The `/dev/videoN` numbers depend on the order things were plugged in and
are not stable. Pick the camera by its description or ID, never by node
number.

The by-id path carries the phone's serial number. Keep such paths out of the
repository, as with anything else that identifies the owner's devices.

## Formats

Measured on one phone:

| Format | Resolutions | Measured rate |
|---|---|---|
| MJPEG | 640x360, 640x480, 1280x720, 1920x1080 | ~29 fps at 1280x720 and 1920x1080 |
| YUYV 4:2:2 | 640x360, 640x480 | ~25 fps at 640x480 |

**For Pinax: MJPEG at 1280x720.** It matches SPEC.md §6 ("scale to at most
1280 px on the long edge") without scaling, and leaves 1080p for barcodes
that prove too small. YUYV tops out at 640x480, probably too coarse for an
EAN-13 at 20 cm.

## Build dependencies

| Package | Needed for |
|---|---|
| `qt6-multimedia-dev` | Camera capture (`QCamera`, `QMediaCaptureSession`, `QVideoSink`) |
| `libzxing-dev` | Barcode decoding (ZXing-C++) |
| `gstreamer1.0-plugins-good` | Qt 6.4's camera backend: `v4l2src` and `jpegdec` |

```sh
sudo apt install qt6-multimedia-dev libzxing-dev gstreamer1.0-plugins-good
```

The target distribution ships Qt **6.4**, whose Multimedia uses GStreamer on
Linux (FFmpeg became the default only in 6.5). Keep to the 6.4
`QMediaCaptureSession` / `QCamera` / `QVideoSink` API.

## Using it

1. Plug the phone in with a **data** cable; a charging cable may carry no
   data.
2. Unlock the phone, tap the **USB** notification and choose **Webcam**.
   With USB debugging on, `adb shell svc usb setFunctions uvc` does the same,
   and ADB keeps working alongside.
3. In the phone's webcam preview, switch to the **rear** camera.
4. Hold the book 10–20 cm from the phone, as in the Phase 5 acceptance.

Webcam mode lasts only while the cable is connected, and has to be chosen
again after each replug.

## Not yet verified

- Reading a real ISBN barcode through the phone. Only a test frame has been
  captured; the Phase 5 acceptance (a paperback read within two seconds at
  10–20 cm) is still to be run.
- Whether autofocus can be controlled over UVC. The assumption is that the
  phone's continuous autofocus does the work and Pinax needs no focus
  control.
- Low light: the test frame, taken in a dim room, was dark but clear.
- Whether webcam mode keeps running when the phone's screen turns off.
