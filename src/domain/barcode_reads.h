#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pinax::domain {

// The ISBN in one decoded EAN-13, if it is one (SPEC.md §6): thirteen digits
// with a 978 or 979 prefix and a correct check digit. A price add-on beside
// it — two or five digits, with or without a space — is dropped. Anything
// else, such as a shop's own EAN-13 price sticker, is not an ISBN (AV-012).
std::optional<std::string> isbnFromBarcode(std::string_view decoded);

// Decides, frame by frame, when the camera has read an ISBN (F-025, SPEC.md
// §6, AV-012). A read is accepted when the same ISBN is the only one in three
// consecutive frames; a frame without it, or with a second ISBN, starts the
// count again. Once accepted, the same ISBN is ignored until two seconds
// pass without it being read, so a barcode left in front of the camera is
// read once, and again only after it has been taken away and brought back.
//
// No Qt and no camera: frames are what a decoder found in each, and time is
// given, so the rules are tested without either.
class BarcodeReads {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr int framesToAgree = 3;
    static constexpr std::chrono::milliseconds quietAfterRead {2000};

    // What the decoder found in one frame, seen at `at`. Returns the ISBN
    // when this frame completes a read.
    std::optional<std::string> frame(const std::vector<std::string>& decoded, Clock::time_point at);

    // Forgets everything: a new scan starts afresh.
    void reset();

private:
    std::string candidate_;
    int agreeing_ = 0;
    std::string lastRead_;
    std::optional<Clock::time_point> lastReadAt_;
};

} // namespace pinax::domain
