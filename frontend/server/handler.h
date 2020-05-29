//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_FRONTEND_SERVER_HANDLER_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_FRONTEND_SERVER_HANDLER_H_

#include "zetasql/base/logging.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/impl/codegen/sync_stream.h"
#include "common/config.h"
#include "frontend/server/request_context.h"
#include "absl/status/status.h"

namespace google {
namespace spanner {
namespace emulator {
namespace frontend {

// ServerStream intercepts writes to a grpc::ServerWriter.
//
// Instead of passing a grpc::ServerWriter to server streaming handlers, we pass
// an instance of this class. This allows us to do custom processing on the
// responses generated by the handler (e.g. we can add logging, delays).
template <typename T>
class ServerStream {
 public:
  explicit ServerStream(grpc::ServerWriterInterface<T>* writer)
      : writer_(writer) {}

  void Send(const T& msg) {
    if (config::should_log_requests()) {
      LOG(INFO) << "Sending streaming response:\n" << msg.DebugString();
    }
    writer_->Write(msg);
  }

 private:
  grpc::ServerWriterInterface<T>* writer_;
};

// Base class for gRPC handlers.
class GRPCHandlerBase {
 public:
  GRPCHandlerBase(const std::string& service_name,
                  const std::string& method_name)
      : service_name_(service_name), method_name_(method_name) {}
  virtual ~GRPCHandlerBase() {}

  const std::string& service_name() { return service_name_; }
  const std::string& method_name() { return method_name_; }

 private:
  const std::string service_name_;
  const std::string method_name_;
};

// UnaryGRPCHandler handles unary gRPC methods.
template <typename RequestT, typename ResponseT>
class UnaryGRPCHandler final : public GRPCHandlerBase {
 public:
  // Signature of the user-defined handler function.
  using HandlerFn =
      std::function<absl::Status(RequestContext*, const RequestT*, ResponseT*)>;

  // Constructs a wrapper around the user-defined handler function.
  UnaryGRPCHandler(const std::string& service_name,
                   const std::string& method_name, HandlerFn fn)
      : GRPCHandlerBase(service_name, method_name), fn_(fn) {}

  // Invokes the user-defined handler function wrapped by this class.
  absl::Status Run(RequestContext* ctx, const RequestT* request,
                   ResponseT* response) {
    if (config::should_log_requests()) {
      LOG(INFO) << "Request[" << service_name() << "." << method_name() << "]\n"
                << request->DebugString();
    }
    absl::Status status = fn_(ctx, request, response);
    if (config::should_log_requests()) {
      LOG(INFO) << "Response[" << service_name() << "." << method_name()
                << "]\n"
                << response->DebugString() << "\n"
                << (status.ok() ? "OK" : "Error: " + status.ToString());
    }
    return status;
  }

 private:
  HandlerFn fn_;
};

// ServerStreamingGRPCHandler handles server streaming gRPC methods.
template <typename RequestT, typename ResponseT>
class ServerStreamingGRPCHandler final : public GRPCHandlerBase {
 public:
  // Signature of the user-defined handler function.
  using HandlerFn = std::function<absl::Status(RequestContext*, const RequestT*,
                                               ServerStream<ResponseT>*)>;

  // Constructs a wrapper around the user-defined handler function.
  ServerStreamingGRPCHandler(const std::string& service_name,
                             const std::string& method_name, HandlerFn fn)
      : GRPCHandlerBase(service_name, method_name), fn_(fn) {}

  // Invokes the user-defined handler function wrapped by this class.
  absl::Status Run(RequestContext* ctx, const RequestT* request,
                   grpc::ServerWriterInterface<ResponseT>* writer) {
    if (config::should_log_requests()) {
      LOG(INFO) << "Request[" << service_name() << "." << method_name() << "]\n"
                << request->DebugString();
    }
    ServerStream<ResponseT> stream(writer);
    absl::Status status = fn_(ctx, request, &stream);
    if (config::should_log_requests()) {
      LOG(INFO) << "Response[" << service_name() << "." << method_name()
                << "]\n"
                << (status.ok() ? "OK" : "Error: " + status.ToString());
    }

    return status;
  }

 private:
  HandlerFn fn_;
};

// HandlerRegisterer enables handler registration at static initialization time.
class HandlerRegisterer {
 public:
  // Constructor to which registration is delegated by other constructors.
  explicit HandlerRegisterer(std::unique_ptr<GRPCHandlerBase> handler);

  // Constructor for unary handler registration.
  template <typename RequestT, typename ResponseT>
  HandlerRegisterer(const std::string& service_name,
                    const std::string& method_name,
                    absl::Status (*fn)(RequestContext*, const RequestT*,
                                       ResponseT*))
      : HandlerRegisterer(
            absl::make_unique<UnaryGRPCHandler<RequestT, ResponseT>>(
                service_name, method_name, fn)) {}

  // Constructor for server streaming handler registration.
  template <typename RequestT, typename ResponseT>
  HandlerRegisterer(const std::string& service_name,
                    const std::string& method_name,
                    absl::Status (*fn)(RequestContext*, const RequestT*,
                                       ServerStream<ResponseT>*))
      : HandlerRegisterer(
            absl::make_unique<ServerStreamingGRPCHandler<RequestT, ResponseT>>(
                service_name, method_name, fn)) {}
};

// Macro for registering grpc handlers (unary or server streaming).
#define REGISTER_GRPC_HANDLER(Service, Method)                         \
  static HandlerRegisterer Service##_##Method##_##Registerer(#Service, \
                                                             #Method, Method);

// Returns a handler for a method within a service by name.
//
// The handler must have been registered previously via
//     REGISTER_GRPC_HANDLER(<service_name>, <method_name>)
//
// Returns nullptr if no such handler could be found.
GRPCHandlerBase* GetHandler(const std::string& service_name,
                            const std::string& method_name);

}  // namespace frontend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_FRONTEND_SERVER_HANDLER_H_
