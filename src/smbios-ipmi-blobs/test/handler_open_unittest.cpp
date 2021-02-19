#include "handler_unittest.hpp"

#include <blobs-ipmid/blobs.hpp>

#include <cstdint>

namespace blobs
{

class SmbiosBlobHandlerOpenTest : public SmbiosBlobHandlerTest
{};

TEST_F(SmbiosBlobHandlerOpenTest, OpenWithBadFlagsFails)
{
    // SMBIOS blob handler disables read flag

    EXPECT_FALSE(handler.open(session, blobs::OpenFlags::read, expectedBlobId));
}

TEST_F(SmbiosBlobHandlerOpenTest, OpenEverythingSucceeds)
{
    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, expectedBlobId));
}

TEST_F(SmbiosBlobHandlerOpenTest, CannotOpenSameSessionTwice)
{
    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, expectedBlobId));
    EXPECT_FALSE(
        handler.open(session, blobs::OpenFlags::write, expectedBlobId));
}

} // namespace blobs
