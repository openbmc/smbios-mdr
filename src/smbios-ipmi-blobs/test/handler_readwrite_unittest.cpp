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

    EXPECT_TRUE(handler.open(session, 0, legacyBlobId));

    std::vector<uint8_t> data = {0x1, 0x2};
    EXPECT_FALSE(handler.write(session, 0, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingTooMuchByOneByteFails)
{
    int bytes = handler.maxBufferSize() + 1;
    std::vector<uint8_t> data(0x11);
    data.resize(bytes);
    ASSERT_EQ(bytes, data.size());

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_FALSE(handler.write(session, 0, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingTooMuchByOffsetOfOne)
{
    int bytes = handler.maxBufferSize();
    std::vector<uint8_t> data(0x11);
    data.resize(bytes);
    ASSERT_EQ(bytes, data.size());

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_FALSE(handler.write(session, 1, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingOneByteBeyondEndFromOffsetFails)
{
    std::vector<uint8_t> data = {0x01, 0x02};
    ASSERT_EQ(2, data.size());

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_FALSE(handler.write(session, (handler.maxBufferSize() - 1), data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingOneByteAtOffsetBeyondEndFails)
{
    std::vector<uint8_t> data = {0x01};
    ASSERT_EQ(1, data.size());

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_FALSE(handler.write(session, handler.maxBufferSize(), data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingFullBufferAtOffsetZeroSucceeds)
{
    int bytes = handler.maxBufferSize();
    std::vector<uint8_t> data = {0x01};
    data.resize(bytes);
    ASSERT_EQ(bytes, data.size());

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_TRUE(handler.write(session, 0, data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, WritingOneByteToTheLastOffsetSucceeds)
{
    std::vector<uint8_t> data = {0x01};
    ASSERT_EQ(1, data.size());

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_TRUE(handler.write(session, (handler.maxBufferSize() - 1), data));
}

TEST_F(SmbiosBlobHandlerReadWriteTest, ReadAlwaysReturnsEmpty)
{
    const uint32_t testOffset = 0;
    const std::vector<uint8_t> testData = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_TRUE(handler.write(session, testOffset, testData));

    EXPECT_THAT(handler.read(session, testOffset, testData.size()), IsEmpty());

    for (size_t i = 0; i < testData.size(); ++i)
    {
        EXPECT_THAT(handler.read(session, i, 1), IsEmpty());
    }
}

} // namespace blobs
