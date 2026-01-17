#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    return false;
  }
  return true;
}

bool WaitForExit(pid_t pid, int *status, std::chrono::milliseconds timeout) {
  auto start = std::chrono::steady_clock::now();
  while (true) {
    pid_t result = waitpid(pid, status, WNOHANG);
    if (result == pid) {
      return true;
    }
    if (result == -1) {
      return false;
    }
    if (std::chrono::steady_clock::now() - start > timeout) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

std::filesystem::path GetExecutableDir() {
  char path[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (count <= 0) {
    return std::filesystem::current_path();
  }
  path[count] = '\0';
  return std::filesystem::path(path).parent_path();
}

} // namespace

int main() {
  std::filesystem::path execDir = GetExecutableDir();
  std::filesystem::path streamerPath = execDir / "alsa_streamer";
  if (!std::filesystem::exists(streamerPath)) {
    std::cerr << "FAIL: alsa_streamer not found at " << streamerPath << "\n";
    return 1;
  }

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    std::cerr << "FAIL: pipe: " << std::strerror(errno) << "\n";
    return 1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "FAIL: fork: " << std::strerror(errno) << "\n";
    return 1;
  }

  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    std::vector<const char *> args = {
        streamerPath.c_str(),
        "--in",
        "null",
        "--out",
        "null",
        "--rate",
        "44100",
        "--period",
        "128",
        "--buffer",
        "512",
        "--channels",
        "2",
        "--format",
        "s32",
        nullptr,
    };
    execv(args[0], const_cast<char *const *>(args.data()));
    std::cerr << "FAIL: execv: " << std::strerror(errno) << "\n";
    _exit(127);
  }

  close(pipefd[1]);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  kill(pid, SIGINT);

  int status = 0;
  if (!WaitForExit(pid, &status, std::chrono::seconds(3))) {
    kill(pid, SIGKILL);
    std::cerr << "FAIL: timeout waiting for alsa_streamer\n";
    return 1;
  }

  std::string output;
  char buffer[4096];
  ssize_t n = 0;
  while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
    output.append(buffer, static_cast<size_t>(n));
  }
  close(pipefd[0]);

  if (!Expect(WIFEXITED(status), "alsa_streamer exit")) {
    return 1;
  }
  if (!Expect(WEXITSTATUS(status) == 0, "alsa_streamer exit code")) {
    std::cerr << output << "\n";
    return 1;
  }

  if (!Expect(output.find("ALSA streaming started") != std::string::npos,
              "start log")) {
    std::cerr << output << "\n";
    return 1;
  }
  if (!Expect(output.find("ALSA streaming stopped") != std::string::npos,
              "stop log")) {
    std::cerr << output << "\n";
    return 1;
  }

  std::cout << "OK\n";
  return 0;
}
