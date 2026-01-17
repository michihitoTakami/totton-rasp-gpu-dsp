#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace totton::zmq_server {

struct ZmqRequest {
  std::string raw;
  std::string cmd;
  std::string parseError;
  bool isJson = false;
};

struct ZmqResponse {
  std::string payload;
  bool ok = true;
};

class ZmqCommandServer {
public:
  using Handler = std::function<ZmqResponse(const ZmqRequest &)>;

  ZmqCommandServer(std::string endpoint, std::string pubEndpoint);
  ~ZmqCommandServer();

  void Register(const std::string &command, Handler handler);
  bool Start();
  void Stop();

  static std::string BuildOk(const std::string &dataJson);
  static std::string BuildError(const std::string &code,
                                const std::string &message);

  std::optional<std::string> Publish(const std::string &message);

private:
  ZmqRequest BuildRequest(const std::string &raw) const;
  std::string Dispatch(const ZmqRequest &request);
  bool InitializeSockets();
  void CleanupSockets();
  void CleanupIpcPath(const std::string &endpoint) const;

  std::string endpoint_;
  std::string pubEndpoint_;
  std::unordered_map<std::string, Handler> handlers_;
  std::atomic<bool> running_{false};
  std::thread serverThread_;
  std::mutex pubMutex_;

  class Impl;
  std::unique_ptr<Impl> impl_;
};

std::string ExtractJsonString(const std::string &json, const std::string &key,
                              std::string *out);

} // namespace totton::zmq_server
