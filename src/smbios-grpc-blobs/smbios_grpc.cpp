#include "smbios_grpc.hpp"

#include "SmbiosTransfer.grpc.pb.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <phosphor-logging/lg2.hpp>

#include <thread>

namespace blobs
{

// Arbitrary constant to avoid overlap with other services
const int basePort = 10166;

// Occupy three consecutive port numbers
const int numPorts = 3;

class SmbiosTransferServiceImpl final :
    public phosphor::smbios::SmbiosTransferService::Service
{
  public:
    grpc::Status SmbiosTransfer(
        grpc::ServerContext* context,
        const phosphor::smbios::SmbiosTransferRequest* request,
        phosphor::smbios::SmbiosTransferResponse* response) override
    {
        lg2::info("SMBIOS gRPC server: Received transfer");

        std::string entryPoint;
        std::string structureTable;

        entryPoint = request->smbios_entry_point();
        structureTable = request->smbios_structure_table();

        lg2::info("Received on this instance number: {INSTANCE}", "INSTANCE",
                  instanceNumber_);
        lg2::info("Received SMBIOS entry point: {SIZE} bytes", "SIZE",
                  entryPoint.size());
        lg2::info("Received SMBIOS structure table: {SIZE} bytes", "SIZE",
                  structureTable.size());

        return grpc::Status::OK;
    }

    void setInstance(const int& instanceNumber)
    {
        instanceNumber_ = instanceNumber;
    }

    int getInstance()
    {
        return instanceNumber_;
    }

  private:
    int instanceNumber_;
};

class SmbiosGrpcInstance
{
  public:
    void start(const int& instanceNumber)
    {
        int port = basePort + instanceNumber;

        std::string address = "[::]:";
        address += std::to_string(port);

        service_.setInstance(instanceNumber);

        credentials_ = grpc::InsecureServerCredentials();

        builder_.AddListeningPort(address, credentials_);
        builder_.RegisterService(&service_);

        lg2::info("Claimed gRPC address: {ADDRESS}", "ADDRESS", address);
        server_ = builder_.BuildAndStart();

        lg2::info("Starting thread: {INSTANCE}", "INSTANCE", instanceNumber);
        thread_ =
            std::make_unique<std::thread>([this]() { this->threadRun(); });

        lg2::info("Started thread: {INSTANCE}", "INSTANCE", instanceNumber);
    }

    void threadRun()
    {
        int instance = service_.getInstance();

        lg2::info("Starting gRPC server: {INSTANCE}", "INSTANCE", instance);
        server_->Wait();

        lg2::info("Finishing gRPC server: {INSTANCE}", "INSTANCE", instance);
    }

    void stop()
    {
        int instance = service_.getInstance();

        lg2::info("Stopping gRPC server: {INSTANCE}", "INSTANCE", instance);
        server_->Shutdown();

        lg2::info("Stopping thread: {INSTANCE}", "INSTANCE", instance);
        thread_->join();

        lg2::info("All finished: {INSTANCE}", "INSTANCE", instance);
    }

  private:
    SmbiosTransferServiceImpl service_;

    grpc::ServerBuilder builder_;

    std::shared_ptr<grpc::ServerCredentials> credentials_;

    std::unique_ptr<grpc::Server> server_;

    std::unique_ptr<std::thread> thread_;
};

class SmbiosGrpcDetails
{
  public:
    void start()
    {
        for (int i = 0; i < instances_.size(); ++i)
        {
            instances_[i].start(i);
        }
    }

    void stop()
    {
        for (int i = 0; i < instances_.size(); ++i)
        {
            instances_[i].stop();
        }
    }

  private:
    std::array<SmbiosGrpcInstance, numPorts> instances_;
};

SmbiosGrpcServer::SmbiosGrpcServer(
    std::shared_ptr<boost::asio::io_context> io) :
    io_(io),
    details_(std::make_shared<SmbiosGrpcDetails>())
{}

void SmbiosGrpcServer::start()
{
    lg2::info("SMBIOS gRPC server: Start");

    // Universal gRPC server boilerplate
    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    details_->start();
}

void SmbiosGrpcServer::stop()
{
    lg2::info("SMBIOS gRPC server: Stop");

    details_->stop();
}

} // namespace blobs
