#include "handler_unittest.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace blobs
{

class SmbiosBlobHandlerBasicTest : public SmbiosBlobHandlerTest
{};

TEST_F(SmbiosBlobHandlerBasicTest, CanHandleBlobChecksNameInvalid)
{
    // Verify canHandleBlob checks and returns false on an invalid name.

    EXPECT_FALSE(handler.canHandleBlob("asdf"));
    EXPECT_FALSE(handler.canHandleBlob("smbios"));
    EXPECT_FALSE(handler.canHandleBlob("/smbios0"));
    EXPECT_FALSE(handler.canHandleBlob("/smbios/0"));
}

TEST_F(SmbiosBlobHandlerBasicTest, CanHandleBlobChecksNameVaild)
{
    // Verify canHandleBlob checks and returns true on the valid name.

    EXPECT_TRUE(handler.canHandleBlob(expectedBlobId));
}

TEST_F(SmbiosBlobHandlerBasicTest, GetblobIdsAsExpected)
{
    // Verify getBlobIds returns the expected blob list.

    EXPECT_EQ(handler.getBlobIds(), expectedBlobIdList);
}

} // namespace blobs
