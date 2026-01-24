#include "io/audio_ring_buffer.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <random>
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

bool ExpectSizeEq(size_t actual, size_t expected, const char *message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << " (got " << actual << ", expected "
              << expected << ")\n";
    return false;
  }
  return true;
}

bool TestInitSetsCapacity() {
  AudioRingBuffer buffer;
  buffer.init(1024);
  return ExpectSizeEq(buffer.capacity(), 1024, "Init sets capacity");
}

bool TestInitStartsEmpty() {
  AudioRingBuffer buffer;
  buffer.init(1024);
  return ExpectSizeEq(buffer.availableToRead(), 0, "Init starts empty") &&
         ExpectSizeEq(buffer.availableToWrite(), 1024, "Init starts writable");
}

bool TestWriteUpdatesAvailable() {
  AudioRingBuffer buffer;
  buffer.init(1024);
  std::vector<float> data(100, 1.0f);
  if (!Expect(buffer.write(data.data(), 100), "Write succeeds")) {
    return false;
  }
  return ExpectSizeEq(buffer.availableToRead(), 100, "Write updates read size") &&
         ExpectSizeEq(buffer.availableToWrite(), 924, "Write updates write size");
}

bool TestWriteFailsWhenFull() {
  AudioRingBuffer buffer;
  buffer.init(100);
  std::vector<float> data(100, 1.0f);
  if (!Expect(buffer.write(data.data(), 100), "Write fills buffer")) {
    return false;
  }
  return Expect(!buffer.write(data.data(), 1), "Write fails when full");
}

bool TestWriteFailsWhenOverCapacity() {
  AudioRingBuffer buffer;
  buffer.init(100);
  std::vector<float> data(101, 1.0f);
  return Expect(!buffer.write(data.data(), 101),
                "Write fails when over capacity");
}

bool TestReadUpdatesAvailable() {
  AudioRingBuffer buffer;
  buffer.init(1024);
  std::vector<float> writeData(100, 1.0f);
  std::vector<float> readData(100);
  if (!Expect(buffer.write(writeData.data(), 100), "Write before read")) {
    return false;
  }
  if (!Expect(buffer.read(readData.data(), 50), "Read succeeds")) {
    return false;
  }
  return ExpectSizeEq(buffer.availableToRead(), 50, "Read updates read size") &&
         ExpectSizeEq(buffer.availableToWrite(), 974, "Read updates write size");
}

bool TestReadFailsWhenEmpty() {
  AudioRingBuffer buffer;
  buffer.init(1024);
  std::vector<float> data(100);
  return Expect(!buffer.read(data.data(), 1), "Read fails when empty");
}

bool TestReadFailsWhenUnderAvailable() {
  AudioRingBuffer buffer;
  buffer.init(1024);
  std::vector<float> writeData(50, 1.0f);
  std::vector<float> readData(100);
  if (!Expect(buffer.write(writeData.data(), 50), "Write before read")) {
    return false;
  }
  return Expect(!buffer.read(readData.data(), 100),
                "Read fails when under available");
}

bool TestReadWriteDataIntegrity() {
  AudioRingBuffer buffer;
  buffer.init(1024);
  std::vector<float> writeData = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  std::vector<float> readData(5);
  if (!Expect(buffer.write(writeData.data(), writeData.size()),
              "Write integrity data")) {
    return false;
  }
  if (!Expect(buffer.read(readData.data(), readData.size()),
              "Read integrity data")) {
    return false;
  }
  for (size_t i = 0; i < writeData.size(); ++i) {
    if (!Expect(readData[i] == writeData[i], "Data integrity mismatch")) {
      return false;
    }
  }
  return true;
}

bool TestWrapAroundWriteThenRead() {
  AudioRingBuffer buffer;
  buffer.init(10);
  std::vector<float> data(8, 1.0f);
  std::vector<float> readData(8);
  if (!Expect(buffer.write(data.data(), data.size()), "Initial write")) {
    return false;
  }
  if (!Expect(buffer.read(readData.data(), readData.size()), "Initial read")) {
    return false;
  }
  std::vector<float> wrapData = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  if (!Expect(buffer.write(wrapData.data(), wrapData.size()),
              "Wrap write succeeds")) {
    return false;
  }
  std::vector<float> wrapReadData(wrapData.size());
  if (!Expect(buffer.read(wrapReadData.data(), wrapReadData.size()),
              "Wrap read succeeds")) {
    return false;
  }
  for (size_t i = 0; i < wrapData.size(); ++i) {
    if (!Expect(wrapReadData[i] == wrapData[i], "Wrap data mismatch")) {
      return false;
    }
  }
  return true;
}

bool TestClearResetsBuffer() {
  AudioRingBuffer buffer;
  buffer.init(1024);
  std::vector<float> data(100, 1.0f);
  if (!Expect(buffer.write(data.data(), data.size()), "Write before clear")) {
    return false;
  }
  buffer.clear();
  return ExpectSizeEq(buffer.availableToRead(), 0, "Clear resets read size") &&
         ExpectSizeEq(buffer.availableToWrite(), 1024,
                      "Clear resets write size");
}

bool TestMultipleWriteReadCycles() {
  AudioRingBuffer buffer;
  buffer.init(256);
  for (int cycle = 0; cycle < 100; ++cycle) {
    std::vector<float> writeData(64);
    for (size_t i = 0; i < writeData.size(); ++i) {
      writeData[i] = static_cast<float>(static_cast<size_t>(cycle * 64) + i);
    }
    if (!Expect(buffer.write(writeData.data(), writeData.size()),
                "Cycle write succeeds")) {
      return false;
    }
    std::vector<float> readData(64);
    if (!Expect(buffer.read(readData.data(), readData.size()),
                "Cycle read succeeds")) {
      return false;
    }
    for (size_t i = 0; i < readData.size(); ++i) {
      if (!Expect(readData[i] == writeData[i], "Cycle data mismatch")) {
        return false;
      }
    }
  }
  return true;
}

bool TestUninitializedBufferWriteReturnsFalse() {
  AudioRingBuffer buffer;
  std::vector<float> data(10, 1.0f);
  return Expect(!buffer.write(data.data(), data.size()),
                "Uninitialized write fails");
}

bool TestUninitializedBufferReadReturnsFalse() {
  AudioRingBuffer buffer;
  std::vector<float> data(10);
  return Expect(!buffer.read(data.data(), data.size()),
                "Uninitialized read fails");
}

bool TestConcurrentAccessSpscPattern() {
  AudioRingBuffer buffer;
  buffer.init(4096);
  const size_t blockSize = 64;
  const size_t totalSamples = blockSize * 150;
  std::atomic<bool> producerDone{false};
  std::atomic<bool> mismatch{false};
  std::atomic<size_t> samplesWritten{0};
  std::atomic<size_t> samplesRead{0};

  std::thread producer([&]() {
    std::vector<float> data(blockSize);
    size_t written = 0;
    while (written < totalSamples) {
      for (size_t i = 0; i < blockSize; ++i) {
        data[i] = static_cast<float>(written + i);
      }
      if (buffer.write(data.data(), blockSize)) {
        written += blockSize;
        samplesWritten.store(written, std::memory_order_relaxed);
      } else {
        std::this_thread::yield();
      }
    }
    producerDone.store(true, std::memory_order_release);
  });

  std::thread consumer([&]() {
    std::vector<float> data(blockSize);
    size_t read = 0;
    while (read < totalSamples) {
      if (buffer.read(data.data(), blockSize)) {
        for (size_t i = 0; i < blockSize; ++i) {
          if (data[i] != static_cast<float>(read + i)) {
            mismatch.store(true, std::memory_order_relaxed);
            return;
          }
        }
        read += blockSize;
        samplesRead.store(read, std::memory_order_relaxed);
      } else {
        if (producerDone.load(std::memory_order_acquire) &&
            buffer.availableToRead() == 0) {
          break;
        }
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  if (!Expect(!mismatch.load(), "Concurrent access data mismatch")) {
    return false;
  }
  return ExpectSizeEq(samplesWritten.load(), totalSamples,
                      "Concurrent write total") &&
         ExpectSizeEq(samplesRead.load(), totalSamples,
                      "Concurrent read total");
}

bool TestConcurrentAccessStressSequence() {
  AudioRingBuffer buffer;
  buffer.init(4096);
  const size_t totalSamples = 1 << 18;
  const size_t maxChunk = 128;
  std::atomic<bool> producerDone{false};
  std::atomic<bool> mismatch{false};
  std::atomic<size_t> samplesWritten{0};
  std::atomic<size_t> samplesRead{0};

  std::thread producer([&]() {
    std::minstd_rand rng(12345);
    std::uniform_int_distribution<size_t> dist(1, maxChunk);
    std::vector<float> data(maxChunk);
    size_t written = 0;
    while (written < totalSamples && !mismatch.load(std::memory_order_relaxed)) {
      size_t chunk = std::min(dist(rng), totalSamples - written);
      if (buffer.availableToWrite() < chunk) {
        std::this_thread::yield();
        continue;
      }
      for (size_t i = 0; i < chunk; ++i) {
        data[i] = static_cast<float>(written + i);
      }
      if (buffer.write(data.data(), chunk)) {
        written += chunk;
        samplesWritten.store(written, std::memory_order_relaxed);
      } else {
        std::this_thread::yield();
      }
    }
    producerDone.store(true, std::memory_order_release);
  });

  std::thread consumer([&]() {
    std::minstd_rand rng(67890);
    std::uniform_int_distribution<size_t> dist(1, maxChunk);
    std::vector<float> data(maxChunk);
    size_t read = 0;
    while (read < totalSamples && !mismatch.load(std::memory_order_relaxed)) {
      size_t chunk = std::min(dist(rng), totalSamples - read);
      if (buffer.availableToRead() < chunk) {
        if (producerDone.load(std::memory_order_acquire) &&
            buffer.availableToRead() == 0) {
          break;
        }
        std::this_thread::yield();
        continue;
      }
      if (buffer.read(data.data(), chunk)) {
        for (size_t i = 0; i < chunk; ++i) {
          if (data[i] != static_cast<float>(read + i)) {
            mismatch.store(true, std::memory_order_relaxed);
            break;
          }
        }
        read += chunk;
        samplesRead.store(read, std::memory_order_relaxed);
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  if (!Expect(!mismatch.load(), "Concurrent access data mismatch")) {
    return false;
  }
  return ExpectSizeEq(samplesWritten.load(), totalSamples,
                      "Concurrent stress write total") &&
         ExpectSizeEq(samplesRead.load(), totalSamples,
                      "Concurrent stress read total");
}

} // namespace

int main() {
  struct TestCase {
    const char *name;
    bool (*fn)();
  };

  const TestCase tests[] = {
      {"InitSetsCapacity", TestInitSetsCapacity},
      {"InitStartsEmpty", TestInitStartsEmpty},
      {"WriteUpdatesAvailable", TestWriteUpdatesAvailable},
      {"WriteFailsWhenFull", TestWriteFailsWhenFull},
      {"WriteFailsWhenOverCapacity", TestWriteFailsWhenOverCapacity},
      {"ReadUpdatesAvailable", TestReadUpdatesAvailable},
      {"ReadFailsWhenEmpty", TestReadFailsWhenEmpty},
      {"ReadFailsWhenUnderAvailable", TestReadFailsWhenUnderAvailable},
      {"ReadWriteDataIntegrity", TestReadWriteDataIntegrity},
      {"WrapAroundWriteThenRead", TestWrapAroundWriteThenRead},
      {"ClearResetsBuffer", TestClearResetsBuffer},
      {"MultipleWriteReadCycles", TestMultipleWriteReadCycles},
      {"UninitializedWriteReturnsFalse", TestUninitializedBufferWriteReturnsFalse},
      {"UninitializedReadReturnsFalse", TestUninitializedBufferReadReturnsFalse},
      {"ConcurrentAccessSpscPattern", TestConcurrentAccessSpscPattern},
      {"ConcurrentAccessStressSequence", TestConcurrentAccessStressSequence},
  };

  int failures = 0;
  for (const auto &test : tests) {
    std::cout << "Running " << test.name << "...\n";
    if (!test.fn()) {
      ++failures;
    }
  }

  if (failures == 0) {
    std::cout << "OK: AudioRingBuffer tests passed\n";
    return 0;
  }
  std::cerr << "FAIL: " << failures << " AudioRingBuffer tests failed\n";
  return 1;
}
