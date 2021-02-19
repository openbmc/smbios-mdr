#include "handler_unittest.hpp"

#include <blobs-ipmid/blobs.hpp>
#include <sdbusplus/exception.hpp>

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

using namespace std::literals;

namespace blobs
{

auto static const test_str = "Hello, world!"s;
std::vector<uint8_t> static const test_buf(test_str.begin(), test_str.end());

class SmbiosBlobHandlerCommitTest : public SmbiosBlobHandlerTest
{};

TEST_F(SmbiosBlobHandlerCommitTest, InvalidSessionCommitIsRejected)
{
    EXPECT_FALSE(handler.commit(session, std::vector<uint8_t>()));
}

TEST_F(SmbiosBlobHandlerCommitTest, UnexpectedDataParam)
{
    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));

    EXPECT_FALSE(handler.commit(session, std::vector<uint8_t>({1, 2, 3})));
}

// Tests the full commit process with example data
TEST_F(SmbiosBlobHandlerCommitTest, HappyPath)
{

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_TRUE(handler.write(session, 0, test_buf));
    EXPECT_THROW(handler.commit(session, std::vector<uint8_t>()),
                 sdbusplus::exception_t);

    blobs::BlobMeta meta;
    EXPECT_TRUE(handler.stat(session, &meta));

    EXPECT_EQ(blobs::StateFlags::committed | blobs::StateFlags::open_write,
              meta.blobState);
}

} // namespace blobs
