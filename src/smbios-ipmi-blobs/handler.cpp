#include "handler.hpp"

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

constexpr const char* mdrv2Service = "xyz.openbmc_project.Smbios.MDR_V2";
constexpr const char* mdrv2Path = "/xyz/openbmc_project/Smbios/MDR_V2";
constexpr const char* mdrv2Interface = "xyz.openbmc_project.Smbios.MDR_V2";

void syncSmbiosData()
{
    bool status = false;
    // std::shared_ptr<sdbusplus::asio::connection> bus = getSdBus();
    sdbusplus::bus::bus bus = sdbusplus::bus::bus(ipmid_get_sd_bus_connection());
    // std::string service = ipmi::getService(*bus, mdrv2Interface, mdrv2Path);
    sdbusplus::message::message method = bus.new_method_call(
        mdrv2Service, mdrv2Path, mdrv2Interface, "AgentSynchronizeData");

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
            phosphor::logging::entry("SERVICE=%s", mdrv2Service),
            phosphor::logging::entry("PATH=%s", mdrv2Path));
    }

    if (!status)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Sync data with service failure");
        return;
    }
}

} // namespace internal

SmbiosBlob* SmbiosBlobHandler::getBlob(uint16_t id)
{
    auto search = sessions.find(id);
    if (search == sessions.end())
    {
        return nullptr;
    }

    /* Not thread-safe, however, the blob handler deliberately assumes serial
     * execution. */
    return &search->second;
}

bool SmbiosBlobHandler::canHandleBlob(const std::string& path)
{
    return path == blobId;
}

std::vector<std::string> SmbiosBlobHandler::getBlobIds()
{
    std::vector<std::string> ret;
    ret.push_back(blobId);
    return ret;
}

bool SmbiosBlobHandler::deleteBlob(const std::string& path)
{
    return false;
}

bool SmbiosBlobHandler::stat(const std::string& path, struct BlobMeta* meta)
{
    return false;
}

bool SmbiosBlobHandler::open(uint16_t session, uint16_t flags,
                             const std::string& path)
{
    if (flags & blobs::OpenFlags::read)
    {
        /* Disable the read operation. */
        return false;
    }

    auto findSess = sessions.find(session);
    if (findSess != sessions.end())
    {
        /* This session is already active. */
        return false;
    }

    auto pathSession = pathSessions.find(path);
    if (pathSession != pathSessions.end() &&
        pathSession->second.size() >= maxSessions())
    {
        return false;
    }

    pathSessions[path].emplace(session);
    sessions.emplace(session,
                     SmbiosBlob(session, path, flags, maxBufferSize()));
    return true;
}

std::vector<uint8_t> SmbiosBlobHandler::read(uint16_t session, uint32_t offset,
                                             uint32_t requestedSize)
{
    /* SMBIOS blob handler does not support read. */
    return std::vector<uint8_t>();
}

bool SmbiosBlobHandler::write(uint16_t session, uint32_t offset,
                              const std::vector<uint8_t>& data)
{
    auto blobIt = getBlob(session);
    if (!blobIt)
    {
        return false;
    }

    if (!(blobIt->state & blobs::StateFlags::open_write))
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "No open blob to write");
        return false;
    }

    /* Is the offset beyond the array? */
    if (offset >= maxBufferSize())
    {
        return false;
    }

    /* Determine whether all their bytes will fit. */
    uint32_t remain = maxBufferSize() - offset;
    if (data.size() > remain)
    {
        return false;
    }

    /* Resize the buffer if what we're writing will go over the size */
    uint32_t newBufferSize = data.size() + offset;
    if (newBufferSize > blobIt->buffer.size())
    {
        blobIt->buffer.resize(newBufferSize);
    }

    std::memcpy(blobIt->buffer.data() + offset, data.data(), data.size());
    return true;
}

bool SmbiosBlobHandler::writeMeta(uint16_t session, uint32_t offset,
                                  const std::vector<uint8_t>& data)
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

    auto blobIt = getBlob(session);
    if (!blobIt)
    {
        return false;
    }

    // If commit is called multiple times, return the same result as last time
    if (blobIt->state &
        (blobs::StateFlags::committing | blobs::StateFlags::committed))
    {
        return true;
    }
    else if (blobIt->state & blobs::StateFlags::commit_error)
    {
        return false;
    }

    MDRSMBIOSHeader mdrHdr;
    mdrHdr.mdrType = mdrTypeII;
    mdrHdr.timestamp = std::time(nullptr);
    mdrHdr.dataSize = blobIt->buffer.size();
    if (access(smbiosPath, 0) == -1)
    {
        int flag = mkdir(smbiosPath, S_IRWXU);
        if (flag != 0)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "create folder failed for writting smbios file");
            blobIt->state |= blobs::StateFlags::commit_error;
            return false;
        }
    }

    std::ofstream smbiosFile(mdrType2File,
                             std::ios_base::binary | std::ios_base::trunc);
    if (!smbiosFile.good())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Write data from flash error - Open SMBIOS table file failure");
        blobIt->state |= blobs::StateFlags::commit_error;
        return false;
    }

    try
    {
        smbiosFile.write(reinterpret_cast<char*>(&mdrHdr),
                         sizeof(MDRSMBIOSHeader));
        smbiosFile.write(reinterpret_cast<char*>(blobIt->buffer.data()),
                         mdrHdr.dataSize);
        blobIt->state |= blobs::StateFlags::committing;
    }
    catch (std::ofstream::failure& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Write data from flash error - write data error",
            phosphor::logging::entry("ERROR=%s", e.what()));
        blobIt->state |= blobs::StateFlags::commit_error;
        return false;
    }

    // Unset committing state and set commited state
    blobIt->state &= ~blobs::StateFlags::committing;
    blobIt->state |= blobs::StateFlags::committed;

    internal::syncSmbiosData();
    return true;
}

bool SmbiosBlobHandler::close(uint16_t session)
{
    auto session_it = sessions.find(session);
    if (session_it == sessions.end())
    {
        return false;
    }

    auto path_it = pathSessions.find(session_it->second.blobId);
    path_it->second.erase(session);
    if (path_it->second.empty())
    {
        pathSessions.erase(path_it);
    }

    sessions.erase(session_it);
    return true;
}

bool SmbiosBlobHandler::stat(uint16_t session, struct BlobMeta* meta)
{
    auto blobIt = getBlob(session);
    if (!blobIt)
    {
        return false;
    }

    meta->size = blobIt->buffer.size();
    meta->blobState = blobIt->state;
    return true;
}

bool SmbiosBlobHandler::expire(uint16_t session)
{
    return close(session);
}

} // namespace blobs
