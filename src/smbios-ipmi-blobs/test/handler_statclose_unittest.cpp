#include "handler_unittest.hpp"

#include <blobs-ipmid/blobs.hpp>

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace blobs
{

class SmbiosBlobHandlerStatCloseTest : public SmbiosBlobHandlerTest
{
  protected:
    blobs::BlobMeta meta;

    // Initialize expected_meta_ with empty members
    blobs::BlobMeta expected_meta = {};
};

TEST_F(SmbiosBlobHandlerStatCloseTest, InvalidSessionStatIsRejected)
{
    EXPECT_FALSE(handler.stat(session, &meta));
}

TEST_F(SmbiosBlobHandlerStatCloseTest, SessionStatAlwaysInitialReadAndWrite)
{
    // Verify the session stat returns the information for a session.

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_TRUE(handler.stat(session, &meta));

    expected_meta.blobState = blobs::StateFlags::open_write;
    EXPECT_EQ(meta, expected_meta);
}

TEST_F(SmbiosBlobHandlerStatCloseTest, AfterWriteMetadataLengthMatches)
{
    // Verify that after writes, the length returned matches.

    std::vector<uint8_t> data = {0x01};
    EXPECT_EQ(1, data.size());

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));

    EXPECT_TRUE(handler.write(session, (handler.maxBufferSize() - 1), data));

    EXPECT_TRUE(handler.stat(session, &meta));

    // We wrote one byte to the last index, making the length the buffer size.
    expected_meta.size = handler.maxBufferSize();
    expected_meta.blobState = blobs::StateFlags::open_write;
    EXPECT_EQ(meta, expected_meta);
}

TEST_F(SmbiosBlobHandlerStatCloseTest, CloseWithInvalidSessionFails)
{
    // Verify you cannot close an invalid session.

    EXPECT_FALSE(handler.close(session));
}

TEST_F(SmbiosBlobHandlerStatCloseTest, CloseWithValidSessionSuccess)
{
    // Verify you can close a valid session.

    EXPECT_TRUE(handler.open(session, 0, legacyBlobId));

    EXPECT_TRUE(handler.close(session));
}
} // namespace blobs
