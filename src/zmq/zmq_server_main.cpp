#include "zmq/command_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> gRunning{true};

void SignalHandler(int) { gRunning.store(false); }

std::string GetEnvOrDefault(const char *name, const std::string &fallback) {
  const char *val = std::getenv(name);
  if (!val || std::string(val).empty()) {
    return fallback;
  }
  return val;
}

std::string ExtractPhaseParam(const std::string &raw) {
  std::string phase;
  totton::zmq_server::ExtractJsonString(raw, "phase", &phase);
  if (phase.empty()) {
    totton::zmq_server::ExtractJsonString(raw, "phase_type", &phase);
  }
  return phase;
}

void PrintUsage(const char *argv0) {
  std::cout << "Usage: " << argv0
            << " [--endpoint <endpoint>] [--pub-endpoint <endpoint>]\n";
}

} // namespace

int main(int argc, char **argv) {
  std::string endpoint =
      GetEnvOrDefault("TOTTON_ZMQ_ENDPOINT", "ipc:///tmp/totton_zmq.sock");
  std::string pubEndpoint = GetEnvOrDefault("TOTTON_ZMQ_PUB_ENDPOINT", "");

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto requireValue = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    }
    if (arg == "--endpoint") {
      const char *val = requireValue("--endpoint");
      if (!val) {
        return 1;
      }
      endpoint = val;
      continue;
    }
    if (arg == "--pub-endpoint") {
      const char *val = requireValue("--pub-endpoint");
      if (!val) {
        return 1;
      }
      pubEndpoint = val;
      continue;
    }

    std::cerr << "Unknown argument: " << arg << "\n";
    PrintUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  totton::zmq_server::ZmqCommandServer server(endpoint, pubEndpoint);
  std::atomic<int> reloadCount{0};
  std::atomic<int> softResetCount{0};
  std::string phaseType = "min";
  auto startTime = std::chrono::steady_clock::now();

  server.Register("PING", [&](const totton::zmq_server::ZmqRequest &) {
    return totton::zmq_server::ZmqResponse{
        totton::zmq_server::ZmqCommandServer::BuildOk("{\"pong\":true}")};
  });

  server.Register("STATS", [&](const totton::zmq_server::ZmqRequest &) {
    auto now = std::chrono::steady_clock::now();
    auto uptimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime)
            .count();
    std::string data =
        "{\"uptime_ms\":" + std::to_string(uptimeMs) + ",\"phase_type\":\"" +
        phaseType + "\",\"reloads\":" + std::to_string(reloadCount.load()) +
        ",\"soft_resets\":" + std::to_string(softResetCount.load()) + "}";
    return totton::zmq_server::ZmqResponse{
        totton::zmq_server::ZmqCommandServer::BuildOk(data)};
  });

  server.Register("RELOAD", [&](const totton::zmq_server::ZmqRequest &) {
    reloadCount.fetch_add(1);
    return totton::zmq_server::ZmqResponse{
        totton::zmq_server::ZmqCommandServer::BuildOk("{\"reloaded\":true}")};
  });

  server.Register("SOFT_RESET", [&](const totton::zmq_server::ZmqRequest &) {
    softResetCount.fetch_add(1);
    return totton::zmq_server::ZmqResponse{
        totton::zmq_server::ZmqCommandServer::BuildOk("{\"reset\":true}")};
  });

  server.Register("PHASE_TYPE_GET",
                  [&](const totton::zmq_server::ZmqRequest &) {
                    std::string data = "{\"phase_type\":\"" + phaseType + "\"}";
                    return totton::zmq_server::ZmqResponse{
                        totton::zmq_server::ZmqCommandServer::BuildOk(data)};
                  });

  server.Register("PHASE_TYPE_SET",
                  [&](const totton::zmq_server::ZmqRequest &request) {
                    std::string phase = ExtractPhaseParam(request.raw);
                    if (phase != "min" && phase != "linear") {
                      return totton::zmq_server::ZmqResponse{
                          totton::zmq_server::ZmqCommandServer::BuildError(
                              "INVALID_PARAMS", "phase must be min or linear"),
                          false};
                    }
                    phaseType = phase;
                    std::string data = "{\"phase_type\":\"" + phaseType + "\"}";
                    return totton::zmq_server::ZmqResponse{
                        totton::zmq_server::ZmqCommandServer::BuildOk(data)};
                  });

  server.Register("SHUTDOWN", [&](const totton::zmq_server::ZmqRequest &) {
    gRunning.store(false);
    return totton::zmq_server::ZmqResponse{
        totton::zmq_server::ZmqCommandServer::BuildOk("{\"shutdown\":true}")};
  });

  std::cout << "ZMQ server listening on " << endpoint << "\n";
  if (!pubEndpoint.empty()) {
    std::cout << "ZMQ pub endpoint " << pubEndpoint << "\n";
  }

  if (!server.Start()) {
    return 1;
  }

  while (gRunning.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  server.Stop();
  return 0;
}
