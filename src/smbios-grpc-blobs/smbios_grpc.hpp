#pragma once

#include <boost/asio/io_context.hpp>

#include <memory>

namespace blobs
{

// Forward declaration to avoid complicating app with gRPC internals
class SmbiosGrpcDetails;

class SmbiosGrpcServer
{
  public:
    SmbiosGrpcServer() = delete;
    ~SmbiosGrpcServer() = default;
    SmbiosGrpcServer(const SmbiosGrpcServer& copy) = delete;
    SmbiosGrpcServer& operator=(const SmbiosGrpcServer& assign) = delete;
    SmbiosGrpcServer(SmbiosGrpcServer&& move) = default;
    SmbiosGrpcServer& operator=(SmbiosGrpcServer&& moveAssign) = default;

    explicit SmbiosGrpcServer(std::shared_ptr<boost::asio::io_context> io);

    void start();
    void stop();

  private:
    std::shared_ptr<boost::asio::io_context> io_;
    std::shared_ptr<SmbiosGrpcDetails> details_;
};

} // namespace blobs
