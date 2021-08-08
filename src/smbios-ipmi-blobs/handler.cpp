#include "handler.hpp"

#include "mdrv2.hpp"
#include "smbios_mdrv2.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <ipmid/api.hpp>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace blobs
{

namespace internal
{

constexpr const char* mdrV2Service = "xyz.openbmc_project.Smbios.MDR_V2";
constexpr const char* mdrV2Interface = "xyz.openbmc_project.Smbios.MDR_V2";

bool syncSmbiosData()
{
    bool status = false;
    sdbusplus::bus::bus bus =
        sdbusplus::bus::bus(ipmid_get_sd_bus_connection());
    sdbusplus::message::message method =
        bus.new_method_call(mdrV2Service, phosphor::smbios::mdrV2Path,
                            mdrV2Interface, "AgentSynchronizeData");

    try
    {
        sdbusplus::message::message reply = bus.call(method);
        reply.read(status);
    }
    catch (sdbusplus::exception_t& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Error Sync data with service",
            phosphor::logging::entry("ERROR=%s", e.what()),
            phosphor::logging::entry("SERVICE=%s", mdrV2Service),
            phosphor::logging::entry("PATH=%s", phosphor::smbios::mdrV2Path));
        return false;
    }

    if (!status)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Sync data with service failure");
        return false;
    }

    return true;
}

} // namespace internal

bool SmbiosBlobHandler::canHandleBlob(const std::string& path)
{
    return path == blobId;
}

std::vector<std::string> SmbiosBlobHandler::getBlobIds()
{
    return std::vector<std::string>(1, blobId);
}

bool SmbiosBlobHandler::deleteBlob(const std::string&)
{
    return false;
}

bool SmbiosBlobHandler::stat(const std::string& path, struct BlobMeta* meta)
{
    if (!blobPtr || blobPtr->blobId != path)
    {
        return false;
    }

    meta->size = blobPtr->buffer.size();
    meta->blobState = blobPtr->state;
    return true;
}

bool SmbiosBlobHandler::open(uint16_t session, uint16_t flags,
                             const std::string& path)
{
    if (flags & blobs::OpenFlags::read)
    {
        /* Disable the read operation. */
        return false;
    }

    /* The handler only allows one session. If an open blob exists, return
     * false directly.
     */
    if (blobPtr)
    {
        return false;
    }
    blobPtr = std::make_unique<SmbiosBlob>(session, path, flags);
    return true;
}

std::vector<uint8_t> SmbiosBlobHandler::read(uint16_t, uint32_t, uint32_t)
{
    /* SMBIOS blob handler does not support read. */
    return std::vector<uint8_t>();
}

bool SmbiosBlobHandler::write(uint16_t session, uint32_t offset,
                              const std::vector<uint8_t>& data)
{
    if (!blobPtr || blobPtr->sessionId != session)
    {
        return false;
    }

    if (!(blobPtr->state & blobs::StateFlags::open_write))
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "No open blob to write");
        return false;
    }

    /* Is the offset beyond the array? */
    if (offset >= maxBufferSize)
    {
        return false;
    }

    /* Determine whether all their bytes will fit. */
    uint32_t remain = maxBufferSize - offset;
    if (data.size() > remain)
    {
        return false;
    }

    /* Resize the buffer if what we're writing will go over the size */
    uint32_t newBufferSize = data.size() + offset;
    if (newBufferSize > blobPtr->buffer.size())
    {
        blobPtr->buffer.resize(newBufferSize);
    }

    std::memcpy(blobPtr->buffer.data() + offset, data.data(), data.size());
    return true;
}

bool SmbiosBlobHandler::writeMeta(uint16_t, uint32_t,
                                  const std::vector<uint8_t>&)
{
    return false;
}

bool SmbiosBlobHandler::commit(uint16_t session,
                               const std::vector<uint8_t>& data)
{
    if (!data.empty())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Unexpected data provided to commit call");
        return false;
    }

    if (!blobPtr || blobPtr->sessionId != session)
    {
        return false;
    }

    /* If a blob is committing or commited, return true directly. But if last
     * commit fails, may try to commit again.
     */
    if (blobPtr->state &
        (blobs::StateFlags::committing | blobs::StateFlags::committed))
    {
        return true;
    }

    /* Clear the commit_error bit. */
    blobPtr->state &= ~blobs::StateFlags::commit_error;

    MDRSMBIOSHeader mdrHdr;
    mdrHdr.mdrType = mdrTypeII;
    mdrHdr.timestamp = std::time(nullptr);
    mdrHdr.dataSize = blobPtr->buffer.size();
    if (access(smbiosPath, F_OK) == -1)
    {
        int flag = mkdir(smbiosPath, S_IRWXU);
        if (flag != 0)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "create folder failed for writting smbios file");
            blobPtr->state |= blobs::StateFlags::commit_error;
            return false;
        }
    }

    std::ofstream smbiosFile(mdrType2File,
                             std::ios_base::binary | std::ios_base::trunc);
    if (!smbiosFile.good())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Write data from flash error - Open SMBIOS table file failure");
        blobPtr->state |= blobs::StateFlags::commit_error;
        return false;
    }

    smbiosFile.exceptions(std::ofstream::badbit | std::ofstream::failbit);
    try
    {
        smbiosFile.write(reinterpret_cast<char*>(&mdrHdr),
                         sizeof(MDRSMBIOSHeader));
        smbiosFile.write(reinterpret_cast<char*>(blobPtr->buffer.data()),
                         mdrHdr.dataSize);
        blobPtr->state |= blobs::StateFlags::committing;
    }
    catch (std::ofstream::failure& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Write data from flash error - write data error",
            phosphor::logging::entry("ERROR=%s", e.what()));
        blobPtr->state |= blobs::StateFlags::commit_error;
        return false;
    }

    if (!internal::syncSmbiosData())
    {
        blobPtr->state &= ~blobs::StateFlags::committing;
        blobPtr->state |= blobs::StateFlags::commit_error;
        return false;
    }

    // Unset committing state and set committed state
    blobPtr->state &= ~blobs::StateFlags::committing;
    blobPtr->state |= blobs::StateFlags::committed;

    return true;
}

bool SmbiosBlobHandler::close(uint16_t session)
{
    if (!blobPtr || blobPtr->sessionId != session)
    {
        return false;
    }

    blobPtr = nullptr;
    return true;
}

bool SmbiosBlobHandler::stat(uint16_t session, struct BlobMeta* meta)
{
    if (!blobPtr || blobPtr->sessionId != session)
    {
        return false;
    }

    meta->size = blobPtr->buffer.size();
    meta->blobState = blobPtr->state;
    return true;
}

bool SmbiosBlobHandler::expire(uint16_t session)
{
    return close(session);
}

} // namespace blobs
