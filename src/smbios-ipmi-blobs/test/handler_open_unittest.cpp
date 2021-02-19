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

    EXPECT_FALSE(handler.open(session, blobs::OpenFlags::read, legacyBlobId));
}

TEST_F(SmbiosBlobHandlerOpenTest, OpenEverythingSucceeds)
{
    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
}

TEST_F(SmbiosBlobHandlerOpenTest, OpenOverSessionLimitFails)
{
    uint16_t sessId = 0;

    for (int i = 0; i < handler.maxSessions(); i++)
    {
        EXPECT_TRUE(
            handler.open(sessId++, blobs::OpenFlags::write, legacyBlobId));
    }

    EXPECT_FALSE(handler.open(sessId++, blobs::OpenFlags::write, legacyBlobId));
}

TEST_F(SmbiosBlobHandlerOpenTest, CannotOpenSameSessionTwice)
{
    EXPECT_TRUE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
    EXPECT_FALSE(handler.open(session, blobs::OpenFlags::write, legacyBlobId));
}

} // namespace blobs
