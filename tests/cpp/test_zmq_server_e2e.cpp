#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <zmq.hpp>

namespace {

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    return false;
  }
  return true;
}

std::filesystem::path GetExecutableDir() {
  char path[4096];
  ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (count <= 0) {
    return std::filesystem::current_path();
  }
  path[count] = '\0';
  return std::filesystem::path(path).parent_path();
}

std::string SendCommand(zmq::socket_t &socket, const std::string &payload) {
  socket.send(zmq::buffer(payload), zmq::send_flags::none);
  zmq::message_t reply;
  if (!socket.recv(reply, zmq::recv_flags::none)) {
    return {};
  }
  return std::string(static_cast<char *>(reply.data()), reply.size());
}

bool WaitForExit(pid_t pid, int *status, int timeoutMs) {
  int waited = 0;
  while (waited < timeoutMs) {
    pid_t result = waitpid(pid, status, WNOHANG);
    if (result == pid) {
      return true;
    }
    if (result < 0) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    waited += 20;
  }
  return false;
}

} // namespace

int main() {
  std::filesystem::path execDir = GetExecutableDir();
  std::filesystem::path serverPath = execDir / "zmq_control_server";
  if (!std::filesystem::exists(serverPath)) {
    std::cerr << "FAIL: zmq_control_server not found at " << serverPath << "\n";
    return 1;
  }

  std::string endpoint = "ipc:///tmp/totton_zmq_test.sock";
  std::filesystem::remove("/tmp/totton_zmq_test.sock");

  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "FAIL: fork\n";
    return 1;
  }
  if (pid == 0) {
    std::vector<const char *> args = {serverPath.c_str(), "--endpoint",
                                      endpoint.c_str(), nullptr};
    execv(args[0], const_cast<char *const *>(args.data()));
    _exit(127);
  }

  zmq::context_t ctx(1);
  zmq::socket_t req(ctx, zmq::socket_type::req);
  req.set(zmq::sockopt::rcvtimeo, 500);
  req.set(zmq::sockopt::sndtimeo, 500);
  req.connect(endpoint);

  bool ready = false;
  for (int i = 0; i < 20; ++i) {
    std::string reply = SendCommand(req, "{\"cmd\":\"PING\"}");
    if (!reply.empty()) {
      ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (!Expect(ready, "server ready")) {
    kill(pid, SIGKILL);
    return 1;
  }

  std::string pong = SendCommand(req, "{\"cmd\":\"PING\"}");
  if (!Expect(pong.find("\"status\":\"ok\"") != std::string::npos, "PING ok")) {
    kill(pid, SIGKILL);
    return 1;
  }

  std::string stats = SendCommand(req, "{\"cmd\":\"STATS\"}");
  if (!Expect(stats.find("\"phase_type\"") != std::string::npos,
              "STATS has phase_type")) {
    kill(pid, SIGKILL);
    return 1;
  }

  std::string setPhase = SendCommand(
      req, "{\"cmd\":\"PHASE_TYPE_SET\",\"params\":{\"phase\":\"linear\"}}");
  if (!Expect(setPhase.find("\"status\":\"ok\"") != std::string::npos,
              "PHASE_TYPE_SET ok")) {
    kill(pid, SIGKILL);
    return 1;
  }

  std::string getPhase = SendCommand(req, "{\"cmd\":\"PHASE_TYPE_GET\"}");
  if (!Expect(getPhase.find("linear") != std::string::npos,
              "PHASE_TYPE_GET linear")) {
    kill(pid, SIGKILL);
    return 1;
  }

  std::string unknown = SendCommand(req, "{\"cmd\":\"NOPE\"}");
  if (!Expect(unknown.find("UNKNOWN_CMD") != std::string::npos,
              "unknown cmd")) {
    kill(pid, SIGKILL);
    return 1;
  }

  std::string shutdown = SendCommand(req, "{\"cmd\":\"SHUTDOWN\"}");
  if (!Expect(shutdown.find("\"status\":\"ok\"") != std::string::npos,
              "shutdown ok")) {
    kill(pid, SIGKILL);
    return 1;
  }

  int status = 0;
  if (!Expect(WaitForExit(pid, &status, 3000), "server exit")) {
    kill(pid, SIGKILL);
    return 1;
  }
  if (!Expect(WIFEXITED(status), "server exit status")) {
    return 1;
  }

  std::cout << "OK\n";
  return 0;
}
