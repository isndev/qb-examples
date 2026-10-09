#include "../upload_write.h"

#include <gtest/gtest.h>

#include <sstream>
#include <streambuf>

namespace {

class ShortWriteBuffer : public std::streambuf {
protected:
    std::streamsize
    xsputn(const char *, std::streamsize count) override {
        return count / 2;
    }
};

class FailedFlushBuffer : public std::stringbuf {
protected:
    int
    sync() override {
        return -1;
    }
};

TEST(UploadWrite, CompleteWriteSucceeds) {
    std::ostringstream out;
    EXPECT_TRUE(write_upload_contents(out, "first content"));
    EXPECT_EQ(out.str(), "first content");
}

TEST(UploadWrite, ShortWriteFails) {
    ShortWriteBuffer buffer;
    std::ostream     out(&buffer);
    EXPECT_FALSE(write_upload_contents(out, "first content"));
}

TEST(UploadWrite, FailedFlushFails) {
    FailedFlushBuffer buffer;
    std::ostream      out(&buffer);
    EXPECT_FALSE(write_upload_contents(out, "first content"));
}

} // namespace
