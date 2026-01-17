#include "zmq/command_server.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <zmq.hpp>

namespace totton::zmq_server {

namespace {

std::string EscapeJson(const std::string &value) {
  std::ostringstream out;
  for (char c : value) {
    switch (c) {
    case '\\':
      out << "\\\\";
      break;
    case '"':
      out << "\\\"";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      out << c;
      break;
    }
  }
  return out.str();
}

} // namespace

class ZmqCommandServer::Impl {
public:
  zmq::context_t context{1};
  zmq::socket_t repSocket{context, zmq::socket_type::rep};
  std::optional<zmq::socket_t> pubSocket;
};

ZmqCommandServer::ZmqCommandServer(std::string endpoint,
                                   std::string pubEndpoint)
    : endpoint_(std::move(endpoint)),
      pubEndpoint_(std::move(pubEndpoint)),
      impl_(std::make_unique<Impl>()) {}

ZmqCommandServer::~ZmqCommandServer() { Stop(); }

void ZmqCommandServer::Register(const std::string &command, Handler handler) {
  handlers_[command] = std::move(handler);
}

bool ZmqCommandServer::Start() {
  if (running_.exchange(true)) {
    return false;
  }
  if (!InitializeSockets()) {
    running_.store(false);
    return false;
  }

  serverThread_ = std::thread([this]() {
    while (running_.load()) {
      zmq::message_t request;
      auto recv = impl_->repSocket.recv(request, zmq::recv_flags::none);
      if (!recv.has_value()) {
        continue;
      }
      std::string raw(static_cast<char *>(request.data()), request.size());
      auto response = Dispatch(BuildRequest(raw));
      impl_->repSocket.send(zmq::buffer(response), zmq::send_flags::none);
    }
    CleanupSockets();
  });
  return true;
}

void ZmqCommandServer::Stop() {
  running_.store(false);
  if (serverThread_.joinable()) {
    serverThread_.join();
  }
}

std::string ZmqCommandServer::BuildOk(const std::string &dataJson) {
  if (dataJson.empty()) {
    return "{\"status\":\"ok\"}";
  }
  return "{\"status\":\"ok\",\"data\":" + dataJson + "}";
}

std::string ZmqCommandServer::BuildError(const std::string &code,
                                         const std::string &message) {
  return "{\"status\":\"error\",\"error_code\":\"" +
         EscapeJson(code) + "\",\"message\":\"" +
         EscapeJson(message) + "\"}";
}

std::optional<std::string> ZmqCommandServer::Publish(const std::string &message) {
  if (!impl_->pubSocket) {
    return std::nullopt;
  }
  try {
    impl_->pubSocket->send(zmq::buffer(message), zmq::send_flags::dontwait);
  } catch (const zmq::error_t &e) {
    return std::string(e.what());
  }
  return std::nullopt;
}

ZmqRequest ZmqCommandServer::BuildRequest(const std::string &raw) const {
  ZmqRequest req;
  req.raw = raw;
  ExtractJsonString(raw, "cmd", &req.cmd);
  return req;
}

std::string ZmqCommandServer::Dispatch(const ZmqRequest &request) {
  if (request.cmd.empty()) {
    return BuildError("INVALID_JSON", "cmd is required");
  }
  auto it = handlers_.find(request.cmd);
  if (it == handlers_.end()) {
    return BuildError("UNKNOWN_CMD", "unknown command");
  }
  ZmqResponse response = it->second(request);
  if (!response.ok) {
    return response.payload;
  }
  return response.payload;
}

bool ZmqCommandServer::InitializeSockets() {
  try {
    impl_->repSocket.set(zmq::sockopt::linger, 0);
    impl_->repSocket.set(zmq::sockopt::rcvtimeo, 100);
    impl_->repSocket.bind(endpoint_);

    if (!pubEndpoint_.empty()) {
      impl_->pubSocket.emplace(impl_->context, zmq::socket_type::pub);
      impl_->pubSocket->set(zmq::sockopt::linger, 0);
      impl_->pubSocket->bind(pubEndpoint_);
    }
    return true;
  } catch (const zmq::error_t &e) {
    std::cerr << "ZMQ bind failed: " << e.what() << "\n";
    return false;
  }
}

void ZmqCommandServer::CleanupSockets() {
  try {
    impl_->repSocket.close();
    if (impl_->pubSocket) {
      impl_->pubSocket->close();
    }
  } catch (const zmq::error_t &) {
  }

  if (endpoint_.rfind("ipc://", 0) == 0) {
    std::filesystem::path path(endpoint_.substr(6));
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
}

std::string ExtractJsonString(const std::string &json, const std::string &key,
                              std::string *out) {
  const std::string pattern = "\"" + key + "\"";
  std::size_t pos = json.find(pattern);
  if (pos == std::string::npos) {
    return {};
  }
  pos = json.find(':', pos + pattern.size());
  if (pos == std::string::npos) {
    return {};
  }
  pos = json.find('"', pos);
  if (pos == std::string::npos) {
    return {};
  }
  const std::size_t end = json.find('"', pos + 1);
  if (end == std::string::npos) {
    return {};
  }
  if (out) {
    *out = json.substr(pos + 1, end - pos - 1);
  }
  return json.substr(pos + 1, end - pos - 1);
}

} // namespace totton::zmq_server
