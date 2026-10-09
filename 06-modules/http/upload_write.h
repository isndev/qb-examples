#pragma once

#include <ostream>
#include <string_view>

// Keep the stream check in one small function so failed writes and flushes can
// be exercised with a deterministic failing streambuf, without a full disk.
inline bool
write_upload_contents(std::ostream &out, std::string_view contents) {
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!out) {
        return false;
    }
    out.flush();
    return out.good();
}
