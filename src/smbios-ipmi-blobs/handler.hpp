#pragma once

#include <blobs-ipmid/blobs.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace blobs
{

struct SmbiosBlob
{
    SmbiosBlob(uint16_t id, const std::string& path, uint16_t flags,
               uint32_t maxBufferSize) :
        sessionId(id),
        blobId(path), state(0)
    {
        if (flags & blobs::OpenFlags::read)
        {
            state |= blobs::StateFlags::open_read;
        }
        if (flags & blobs::OpenFlags::write)
        {
            state |= blobs::StateFlags::open_write;
        }

        /* Pre-allocate the buffer.capacity() with maxBufferSize */
        buffer.reserve(maxBufferSize);
    }
    ~SmbiosBlob()
    {
        /* We want to deliberately wipe the buffer to avoid leaking any
         * sensitve infomation.
         */
        buffer.assign(buffer.capacity(), 0);
    }

    /* The blob handler session id. */
    uint16_t sessionId;

    /* The identifier for the blob */
    std::string blobId;

    /* The current state. */
    uint16_t state;

    /* The staging buffer. */
    std::vector<uint8_t> buffer;
};

class SmbiosBlobHandler : public GenericBlobInterface
{
  public:
    SmbiosBlobHandler() = default;
    ~SmbiosBlobHandler() = default;
    SmbiosBlobHandler(const SmbiosBlobHandler&) = delete;
    SmbiosBlobHandler& operator=(const SmbiosBlobHandler&) = delete;
    SmbiosBlobHandler(SmbiosBlobHandler&&) = default;
    SmbiosBlobHandler& operator=(SmbiosBlobHandler&&) = default;

    bool canHandleBlob(const std::string& path) override;
    std::vector<std::string> getBlobIds() override;
    bool deleteBlob(const std::string& path) override;
    bool stat(const std::string& path, struct BlobMeta* meta) override;
    bool open(uint16_t session, uint16_t flags,
              const std::string& path) override;
    std::vector<uint8_t> read(uint16_t session, uint32_t offset,
                              uint32_t requestedSize) override;
    bool write(uint16_t session, uint32_t offset,
               const std::vector<uint8_t>& data) override;
    bool writeMeta(uint16_t session, uint32_t offset,
                   const std::vector<uint8_t>& data) override;
    bool commit(uint16_t session, const std::vector<uint8_t>& data) override;
    bool close(uint16_t session) override;
    bool stat(uint16_t session, struct BlobMeta* meta) override;
    bool expire(uint16_t session) override;
    SmbiosBlob* getBlob(uint16_t id);
    uint16_t maxSessions() const
    {
        return sessionLimit;
    }
    uint32_t maxBufferSize() const
    {
        return smbiosTableStorageSize;
    }

  private:
    static constexpr char blobId[] = "/smbios";
    static constexpr uint16_t sessionLimit = 1;
    static constexpr uint32_t smbiosTableStorageSize = 64 * 1024;
    std::unordered_map<uint16_t, SmbiosBlob> sessions;
    std::unordered_map<std::string, std::unordered_set<uint16_t>> pathSessions;
};

} // namespace blobs
