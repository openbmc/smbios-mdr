#include "handler_unittest.hpp"

#include <blobs-ipmid/blobs.hpp>

#include <cstdint>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::IsEmpty;

namespace blobs
{

class SmbiosBlobHandlerReadWriteTest : public SmbiosBlobHandlerTest
{};

TEST_F(SmbiosBlobHandlerReadWriteTest, InvalidSessionWriteIsRejected)
{
    // Verify the handler checks for a valid session.

    std::vector<uint8_t> data = {0x1, 0x2};
    EXPECT_FALSE(handler.write(session, 0, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, NoWriteFlagRejected)
{
    // Verify the handler checks the write flag;

    EXPECT_TRUE(handler.open(session, 0, expectedBlobId));

    std::vector<uint8_t> data = {0x1, 0x2};
    EXPECT_FALSE(handler.write(session, 0, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingTooMuchByOneByteFails)
{
    int bytes = handlerMaxBufferSize + 1;
    std::vector<uint8_t> data(bytes, 0x11);

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, expectedBlobId));
    EXPECT_FALSE(handler.write(session, 0, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingTooMuchByOffsetOfOne)
{
    std::vector<uint8_t> data(handlerMaxBufferSize, 0x11);

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, expectedBlobId));
    EXPECT_FALSE(handler.write(session, 1, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingOneByteBeyondEndFromOffsetFails)
{
    std::vector<uint8_t> data = {0x01, 0x02};

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, expectedBlobId));
    EXPECT_FALSE(handler.write(session, handlerMaxBufferSize - 1, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingOneByteAtOffsetBeyondEndFails)
{
    std::vector<uint8_t> data = {0x01};

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, expectedBlobId));
    EXPECT_FALSE(handler.write(session, handlerMaxBufferSize, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingFullBufferAtOffsetZeroSucceeds)
{
    std::vector<uint8_t> data(handlerMaxBufferSize, 0x01);

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, expectedBlobId));
    EXPECT_TRUE(handler.write(session, 0, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingOneByteToTheLastOffsetSucceeds)
{
    std::vector<uint8_t> data = {0x01};

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, expectedBlobId));
    EXPECT_TRUE(handler.write(session, handlerMaxBufferSize - 1, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, ReadAlwaysReturnsEmpty)
{
    const uint32_t testOffset = 0;
    const std::vector<uint8_t> testData = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, expectedBlobId));
    EXPECT_TRUE(handler.write(session, testOffset, testData));

    EXPECT_THAT(handler.read(session, testOffset, testData.size()), IsEmpty());

    for (size_t i = 0; i < testData.size(); ++i)
    {
        EXPECT_THAT(handler.read(session, i, 1), IsEmpty());
    }
}

} // namespace blobs
