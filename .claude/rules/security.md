# Security Rules

このプロジェクトにおけるセキュリティ原則です。**全てのコード変更はこのルールに従う必要があります。**

## GPU/Vulkan セキュリティ

### 1. Vulkan エラーチェック必須

**全ての Vulkan API 呼び出しの戻り値 (`VkResult`) をチェックすること。**

```cpp
// ❌ Bad: エラーチェックなし
vkAllocateMemory(device, &allocInfo, nullptr, &memory);
vkBindBufferMemory(device, buffer, memory, 0);

// ✅ Good: エラーチェックあり
VkResult result = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
if (result != VK_SUCCESS) {
    fprintf(stderr, "vkAllocateMemory failed: %d\n", result);
    return result;
}

result = vkBindBufferMemory(device, buffer, memory, 0);
if (result != VK_SUCCESS) {
    fprintf(stderr, "vkBindBufferMemory failed: %d\n", result);
    vkFreeMemory(device, memory, nullptr);
    return result;
}
```

**ヘルパーマクロの使用を推奨:**

```cpp
#define VK_CHECK(result) \
    do { \
        VkResult _r = (result); \
        if (_r != VK_SUCCESS) { \
            fprintf(stderr, "Vulkan error at %s:%d: %d\n", __FILE__, __LINE__, _r); \
            return _r; \
        } \
    } while(0)

// 使用例
VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &memory));
VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));
```

### 2. GPU メモリリーク防止

**全ての `vkAllocateMemory` には対応する `vkFreeMemory` が必要。RAII パターンを推奨。**

```cpp
// ❌ Bad: メモリリークの危険性
void process() {
    VkDeviceMemory memory;
    vkAllocateMemory(device, &allocInfo, nullptr, &memory);

    if (error_condition) {
        return;  // リーク！
    }

    // ... 処理 ...
    vkFreeMemory(device, memory, nullptr);
}

// ✅ Good: RAII パターン
class VulkanBuffer {
    VkDevice device_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;

public:
    VulkanBuffer(VkDevice device, const VkBufferCreateInfo& bufferInfo,
                 VkMemoryPropertyFlags properties)
        : device_(device) {
        VK_CHECK(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer_));

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device_, buffer_, &memReq);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

        VK_CHECK(vkAllocateMemory(device_, &allocInfo, nullptr, &memory_));
        VK_CHECK(vkBindBufferMemory(device_, buffer_, memory_, 0));
    }

    ~VulkanBuffer() {
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buffer_, nullptr);
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, memory_, nullptr);
        }
    }

    // Copy禁止、Move可能
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& other) noexcept
        : device_(other.device_), buffer_(other.buffer_), memory_(other.memory_) {
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
    }

    VkBuffer get() const { return buffer_; }
};
```

### 3. Compute Shader バリア同期

**Compute dispatch 間のメモリアクセスには必ずバリアを設定。**

```cpp
// ✅ Good: パイプラインバリアで同期
vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);

VkMemoryBarrier memoryBarrier = {};
memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

vkCmdPipelineBarrier(
    commandBuffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
    0,
    1, &memoryBarrier,
    0, nullptr,
    0, nullptr
);

// 次の dispatch は前の結果を安全に読み取れる
vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
```

### 4. VRAM 使用量監視

**大規模メモリ確保前に利用可能な VRAM を確認。**

```cpp
VkPhysicalDeviceMemoryProperties memProps;
vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

// ヒープサイズを確認
for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
    if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        VkDeviceSize heapSize = memProps.memoryHeaps[i].size;
        fprintf(stderr, "GPU heap %u size: %zu MB\n", i, heapSize / (1024 * 1024));

        if (required_size > heapSize) {
            fprintf(stderr, "Insufficient VRAM: required %zu MB, available %zu MB\n",
                    required_size / (1024 * 1024), heapSize / (1024 * 1024));
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
    }
}
```

### 5. Validation Layers 必須（開発時）

**開発ビルドでは Validation Layers を有効化すること。**

```cpp
#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

VkInstanceCreateInfo createInfo = {};
// ... 他のフィールド ...

if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
}

VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance));
```

**Validation エラーは即座に修正:**

```
VALIDATION ERROR: [ VUID-vkCmdDispatch-None-02697 ] Object 0: ...
→ この警告を無視してはならない。本番環境でのクラッシュにつながる。
```

---

## Audio Processing セキュリティ

### 1. バッファオーバーフロー防止

**音声バッファ操作時は必ずサイズチェック。**

```cpp
// ❌ Bad: サイズチェックなし
void process_audio(float* buffer, int size) {
    for (int i = 0; i < size + 1; i++) {  // オーバーフロー！
        buffer[i] *= 2.0f;
    }
}

// ✅ Good: サイズチェックあり
void process_audio(float* buffer, int size, int capacity) {
    if (size > capacity) {
        throw std::invalid_argument("Buffer overflow: size exceeds capacity");
    }

    for (int i = 0; i < size; i++) {
        buffer[i] *= 2.0f;
    }
}
```

### 2. サンプルレート検証

**入力サンプルレートは許可リストで検証。**

```cpp
// ✅ Good: ホワイトリスト検証
const std::set<int> SUPPORTED_RATES = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};

bool validate_sample_rate(int rate) {
    if (SUPPORTED_RATES.find(rate) == SUPPORTED_RATES.end()) {
        fprintf(stderr, "Unsupported sample rate: %d Hz\n", rate);
        return false;
    }
    return true;
}
```

### 3. 数値安定性

**浮動小数点演算は数値安定性を考慮。**

```cpp
// ❌ Bad: オーバーフロー/アンダーフローの危険性
float result = large_value * large_value;  // inf になる可能性

// ✅ Good: 範囲チェック
float safe_multiply(float a, float b) {
    if (std::abs(a) > 1e20f || std::abs(b) > 1e20f) {
        return std::copysign(std::numeric_limits<float>::max(), a * b);
    }
    return a * b;
}
```

---

## 通信セキュリティ (ALSA/ZeroMQ)

### 1. ALSA エラーハンドリング

**全ての ALSA API 呼び出しはエラーチェック必須。**

```cpp
// ✅ Good: エラーハンドリング
int err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0);
if (err < 0) {
    fprintf(stderr, "Cannot open audio device %s: %s\n",
            device, snd_strerror(err));
    return err;
}

err = snd_pcm_writei(handle, buffer, frames);
if (err == -EPIPE) {
    fprintf(stderr, "ALSA buffer underrun\n");
    snd_pcm_prepare(handle);  // リカバリ
} else if (err < 0) {
    fprintf(stderr, "ALSA write error: %s\n", snd_strerror(err));
    return err;
}
```

### 2. ZeroMQ タイムアウト

**ZeroMQ 通信には必ずタイムアウトを設定。**

```cpp
// ✅ Good: タイムアウト設定
zmq::socket_t socket(context, ZMQ_REQ);
int timeout = 5000;  // 5秒
socket.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
socket.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));

try {
    socket.send(request);
    socket.recv(reply);
} catch (const zmq::error_t& e) {
    if (e.num() == EAGAIN) {
        fprintf(stderr, "ZeroMQ timeout\n");
    } else {
        fprintf(stderr, "ZeroMQ error: %s\n", e.what());
    }
    return -1;
}
```

### 3. メッセージサイズ制限

**受信メッセージサイズは上限チェック。**

```cpp
const size_t MAX_MESSAGE_SIZE = 1024 * 1024;  // 1 MB

zmq::message_t message;
socket.recv(&message);

if (message.size() > MAX_MESSAGE_SIZE) {
    fprintf(stderr, "Message too large: %zu bytes (max %zu)\n",
            message.size(), MAX_MESSAGE_SIZE);
    return -1;
}
```

---

## Python/FastAPI セキュリティ

### 1. 入力検証

**全ての API 入力は Pydantic モデルで検証。**

```python
# ✅ Good: Pydantic 検証
from pydantic import BaseModel, Field, validator

class EQSettings(BaseModel):
    frequency: float = Field(..., ge=20.0, le=20000.0)  # 20Hz - 20kHz
    gain: float = Field(..., ge=-12.0, le=12.0)  # ±12dB
    q_factor: float = Field(..., ge=0.1, le=10.0)  # 0.1 - 10

    @validator('frequency')
    def validate_frequency(cls, v):
        if not (20 <= v <= 20000):
            raise ValueError('Frequency must be between 20 and 20000 Hz')
        return v
```

### 2. ファイルパス検証

**ファイルパスは必ずサニタイズ。パストラバーサル攻撃を防止。**

```python
# ❌ Bad: パストラバーサルの危険性
def load_profile(name: str):
    path = f"data/EQ/{name}.txt"  # name="../../../etc/passwd" の場合に危険
    with open(path) as f:
        return f.read()

# ✅ Good: パス検証
from pathlib import Path

def load_profile(name: str):
    # ファイル名のみを許可（パス区切り文字を含まない）
    if '/' in name or '\\' in name or '..' in name:
        raise ValueError("Invalid profile name")

    base_dir = Path("data/EQ").resolve()
    profile_path = (base_dir / f"{name}.txt").resolve()

    # base_dir 配下であることを確認
    if not str(profile_path).startswith(str(base_dir)):
        raise ValueError("Path traversal detected")

    if not profile_path.exists():
        raise FileNotFoundError(f"Profile not found: {name}")

    return profile_path.read_text()
```

### 3. シークレット管理

**シークレットは環境変数または `.env` ファイルで管理。コミット禁止。**

```python
# ✅ Good: 環境変数から読み込み
import os
from pydantic_settings import BaseSettings

class Settings(BaseSettings):
    api_key: str
    database_url: str

    class Config:
        env_file = ".env"  # .gitignore に追加必須

settings = Settings()
```

**`.gitignore` に追加:**
```
.env
.env.local
*.key
*.pem
credentials.json
```

---

## コード品質セキュリティ

### 1. リソースリーク防止

**RAII 原則を徹底。全てのリソースは自動管理。**

```cpp
// ✅ Good: RAII
class AudioDevice {
    snd_pcm_t* handle_;
public:
    AudioDevice(const char* device) {
        int err = snd_pcm_open(&handle_, device, SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0) {
            throw std::runtime_error(snd_strerror(err));
        }
    }

    ~AudioDevice() {
        if (handle_) {
            snd_pcm_close(handle_);
        }
    }

    // コピー禁止、ムーブ可能
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;
    AudioDevice(AudioDevice&&) noexcept = default;
    AudioDevice& operator=(AudioDevice&&) noexcept = default;
};
```

### 2. スレッドセーフティ

**共有データへのアクセスは必ず mutex でガード。**

```cpp
// ✅ Good: mutex ガード
class AudioBuffer {
    std::vector<float> buffer_;
    mutable std::mutex mutex_;

public:
    void write(const float* data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.assign(data, data + size);
    }

    std::vector<float> read() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_;
    }
};
```

### 3. 整数オーバーフロー防止

**算術演算前にオーバーフローチェック。**

```cpp
// ✅ Good: オーバーフローチェック
bool safe_multiply(size_t a, size_t b, size_t* result) {
    if (a == 0 || b == 0) {
        *result = 0;
        return true;
    }

    if (a > SIZE_MAX / b) {
        return false;  // オーバーフロー
    }

    *result = a * b;
    return true;
}

// 使用例
size_t total_size;
if (!safe_multiply(num_channels, buffer_size, &total_size)) {
    fprintf(stderr, "Integer overflow in buffer size calculation\n");
    return -1;
}
```

---

## Vulkan 特有のセキュリティ問題

### 1. Descriptor Set の適切な管理

**Descriptor Set は使用後に必ず解放。**

```cpp
// ✅ Good: Descriptor Pool を使った管理
class DescriptorManager {
    VkDevice device_;
    VkDescriptorPool pool_;

public:
    DescriptorManager(VkDevice device, uint32_t maxSets) : device_(device) {
        VkDescriptorPoolSize poolSize = {};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = maxSets;

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = maxSets;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        VK_CHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool_));
    }

    ~DescriptorManager() {
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, pool_, nullptr);
        }
    }
};
```

### 2. Command Buffer の同期

**Command Buffer の再利用時は完了を待機。**

```cpp
// ✅ Good: Fence で同期
VkFence fence;
VkFenceCreateInfo fenceInfo = {};
fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

// Submit
VkSubmitInfo submitInfo = {};
// ... submitInfo の設定 ...
VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));

// 完了待機
VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
VK_CHECK(vkResetFences(device, 1, &fence));

// これで command buffer を安全に再利用可能
vkResetCommandBuffer(commandBuffer, 0);
```

### 3. Push Constants のサイズ制限

**Push Constants は最小限に抑える（128バイト以下推奨）。**

```cpp
// ✅ Good: サイズチェック
struct PushConstants {
    uint32_t frameIndex;
    float time;
    // ... 他のフィールド ...
};

static_assert(sizeof(PushConstants) <= 128, "Push constants too large");

VkPushConstantRange pushConstantRange = {};
pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
pushConstantRange.offset = 0;
pushConstantRange.size = sizeof(PushConstants);
```

---

## Pre-commit Hook 統合

このプロジェクトでは `.pre-commit-config.yaml` で以下のセキュリティチェックを自動実行：

### detect-secrets

**シークレットの誤コミットを防止。**

```yaml
- repo: https://github.com/Yelp/detect-secrets
  rev: v1.4.0
  hooks:
    - id: detect-secrets
      args: ['--baseline', '.secrets.baseline']
```

**新しいシークレットが検出された場合:**

```bash
# False positive の場合はベースラインを更新
detect-secrets scan --baseline .secrets.baseline

# それ以外の場合はシークレットを削除してコミット
```

### その他のフック

- **clang-format**: C++/Vulkan コードの自動整形
- **ruff**: Python コードの自動整形・リント
- **trailing-whitespace**: 末尾空白削除
- **end-of-file-fixer**: ファイル末尾改行追加

**フックをスキップしてはならない（`--no-verify` 禁止）。**

---

## セキュリティレビューチェックリスト

コード変更時は以下を確認：

### Vulkan/GPU

- [ ] 全ての Vulkan API 呼び出しに `VkResult` チェックがある
- [ ] `vkAllocateMemory` に対応する `vkFreeMemory` がある（または RAII）
- [ ] Compute dispatch 間に適切なバリアがある
- [ ] VRAM 使用量が制限内である（8GB 以下）
- [ ] Validation Layers が有効化されている（開発ビルド）

### Audio Processing

- [ ] バッファサイズが検証されている
- [ ] サンプルレートがホワイトリストで検証されている
- [ ] 数値演算がオーバーフロー/アンダーフローを起こさない

### 通信 (ALSA/ZeroMQ)

- [ ] 全ての ALSA API 呼び出しにエラーチェックがある
- [ ] ZeroMQ にタイムアウトが設定されている
- [ ] メッセージサイズに上限がある

### Python/FastAPI

- [ ] 入力が Pydantic モデルで検証されている
- [ ] ファイルパスがサニタイズされている
- [ ] シークレットが環境変数で管理されている

### コード品質

- [ ] リソースが RAII で管理されている
- [ ] 共有データが mutex でガードされている
- [ ] 整数オーバーフローチェックがある

---

## セキュリティインシデント対応

### 脆弱性を発見した場合

1. **即座に修正を開始**
2. Issue を作成（ラベル: `security`）
3. 修正 PR を作成（タイトルに `[SECURITY]` を含める）
4. レビュー後、速やかにマージ

### 外部ライブラリの脆弱性

```bash
# Python 依存関係の脆弱性スキャン
uv pip install safety
safety check

# C++ 依存関係の更新
# CMakeLists.txt で FetchContent を使用している場合、バージョンを更新
```

---

## 関連ドキュメント

- **CLAUDE.md**: プロジェクト全体の開発ガイド
- **.claude/rules/git-workflow.md**: Git ワークフローのセキュリティ原則
- **.pre-commit-config.yaml**: 自動セキュリティチェック設定
- **VkFFT Documentation**: https://github.com/DTolm/VkFFT

---

## 更新履歴

- 2026-01-22: `.claude/rules/` に Security Rules を作成（Vulkan 特化）
