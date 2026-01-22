# Totton Raspberry Pi GPU DSP

## English

This repository hosts the minimal GPU upsampler that migrates Vulkan+EQ+FastAPI/Jinja2 UI from `totton audio project` into a Raspberry Pi-friendly package. It pairs Vulkan (VkFFT) FIR convolution upsampling with OPRA-style EQ, ZeroMQ control, a lightweight Web UI/API, and scripts so the solution is easy to re-use on Pi hardware.

### Features
- Vulkan + VkFFT based FIR convolution upsampling
- OPRA-style EQ definitions with FastAPI/Jinja2 UI/API
- Minimal ZeroMQ control surface for RELOAD/STATUS
- Web UI workflow that applies EQ and configuration changes live on the server
- Bundled filters and configuration to run on Pi without rebuilding
- Aligns with the EPIC scope and completion criteria described in Issue [#1](https://github.com/michihitoTakami/totton-rasp-gpu-dsp/issues/1)

### Setup overview
1. Run `uv sync` to gather Python/C++/Vulkan dependencies (reusing `uv.lock`).
2. Generate filters via `uv run python -m scripts.filters.generate_minimum_phase --generate-all --taps 80000 --kaiser-beta 25 --stopband-attenuation 140` if required.
3. Configure the build with `cmake -B build -DENABLE_VULKAN=ON` (and `-DUSE_VKFFT=ON` if needed).
4. Run `pre-commit run --all-files` using the provided `.pre-commit-config.yaml`.
5. Use `aqua.yaml` to keep lint/format tooling consistent via Aqua CLI.

### Docker deployment (Issue #9, #23)
Pre-built images are available on GitHub Container Registry (GHCR). No build step required on Raspberry Pi.

**Quick start:**
```bash
git clone https://github.com/michihitoTakami/totton-rasp-gpu-dsp.git
cd totton-rasp-gpu-dsp
docker compose up -d
```

The `docker-compose.yml` automatically pulls the latest image from GHCR.

**Manual image pull:**
```bash
docker pull ghcr.io/michihitotakami/totton-rasp-gpu-dsp:latest
```

**Management:**
- Start: `docker compose up -d`
- Stop: `docker compose down`
- Web UI: `http://<pi-host>:8080`
- ALSA settings UI: `http://<pi-host>:8080/settings`
- Persistent config/EQ data: stored in Docker volume at `/var/lib/totton-dsp`
- Logs: `docker compose logs -f totton-dsp` or `docker logs -f totton-dsp`

**Build locally (optional):**
If you want to build the image yourself instead of using pre-built images:
```bash
docker compose build
```

Environment overrides (use `.env` or export):
- `TOTTON_ALSA_IN` / `TOTTON_ALSA_OUT` (default: `hw:0,0`)
- `TOTTON_ALSA_CHANNELS` / `TOTTON_ALSA_FORMAT` (default: `2` / `S32_LE`)
- `TOTTON_FILTER_DIR` / `TOTTON_FILTER_RATIO` / `TOTTON_FILTER_PHASE`
- `TOTTON_WEB_PORT` (default: `8080`)

Auto-start on boot (example systemd unit running docker compose):
```ini
[Unit]
Description=Totton DSP Docker Compose
After=docker.service
Requires=docker.service

[Service]
Type=oneshot
WorkingDirectory=/opt/totton-rasp-gpu-dsp
ExecStart=/usr/bin/docker compose up -d
ExecStop=/usr/bin/docker compose down
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```
Enable with:
`sudo systemctl enable docker` and `sudo systemctl enable --now totton-dsp.service`

### Expected outcomes
- Bundle binaries, filters, and configuration examples into the Pi (arm64) release
- Enable EQ application, config tweaks, and RELOAD via both the UI and the ZeroMQ API
- Seed `docs/` and `scripts/` with scaffolding so contributors can understand the system quickly

### ALSA streaming (Issue #3)
- Build: `cmake -B build -DENABLE_ALSA=ON` then `cmake --build build -j$(nproc)`
- Run (minimal): `./build/alsa_streamer --in hw:0 --out hw:0`
- Run with filter: `./build/alsa_streamer --in hw:0 --out hw:0 --filter data/coefficients/filter_44k_2x_80000_min_phase.json`
- Auto-select filter set: `./build/alsa_streamer --in hw:0 --out hw:0 --filter-dir data/coefficients --ratio 2 --phase min`
- XRUN handling: logs the XRUN and calls `snd_pcm_recover` to continue streaming; if recovery fails the app exits

### ZeroMQ control server (Issue #4)
- Build: `cmake -B build -DENABLE_ZMQ=ON` then `cmake --build build -j$(nproc)`
- Run: `./build/zmq_control_server --endpoint ipc:///tmp/totton_zmq.sock`
- Env override: `TOTTON_ZMQ_ENDPOINT` / `TOTTON_ZMQ_PUB_ENDPOINT`
- Commands: `PING`, `STATS`, `RELOAD`, `SOFT_RESET`, `PHASE_TYPE_GET`, `PHASE_TYPE_SET`
- ALSA device list: `LIST_ALSA_DEVICES`

### Directory layout
```
src/       : Implementations for Vulkan/C++/ZeroMQ/ALSA
include/   : Shared headers
scripts/   : Filter generation and deployment scripts
data/      : FIR filters and EQ profiles
docs/      : Installation, specs, and tutorials
web/       : FastAPI + Jinja2 templates (reusing macros such as btn_primary)
build/     : Build artifacts
```

### Bundled filters (Issue #7)
- `data/coefficients/` ships 44k/48k families with ratios 2x/4x/8x/16x (minimum-phase, 80k taps).
- Regenerate: `uv run python -m scripts.filters.generate_minimum_phase --generate-all --taps 80000 --kaiser-beta 25 --stopband-attenuation 140`
- Target: Kaiser β=25, stopband attenuation 140 dB (temporary for 80k taps)
- License/notes: generated coefficients follow this repository's license; no third-party datasets are embedded.

### References & next steps
- Deliver on the goals/completion criteria defined in [Issue #1](https://github.com/michihitoTakami/totton-rasp-gpu-dsp/issues/1).
- Start filling the child issues such as `Vulkan upsampler core (#2)`, `EQ (#5)`, `FastAPI UI/API (#6)`, etc.
- Filter sidecar format is documented in `docs/filter_format.md`.

### Contribution
- Use GitHub CLI `gh` for Issue/PR workflows
- Include the Issue number in branch names (e.g., `feature/#1-minimal-upscaler`)
- Always run `pre-commit run --hook-stage pre-push`

## 日本語

このリポジトリは `totton audio project` から Vulkan+EQ+FastAPI/Jinja2 UI を切り出し、Raspberry Pi 向けに再構成した最小 GPU アップサンプラです。Vulkan (VkFFT) による FIR 畳み込みアップサンプリング、OPRA 系 EQ、ZeroMQ 制御、軽量な Web UI/API、運用スクリプトを組み合わせて、Pi 上で手早く再利用できる構成を目指しています。

### 特徴
- Vulkan + VkFFT による FIR 畳み込みアップサンプリング
- OPRA 形式に基づいた EQ 定義と FastAPI/Jinja2 ベースの UI/API
- RELOAD/STATUS を扱う ZeroMQ の最小制御
- EQ と設定変更を即時に反映する Web UI ワークフロー
- Pi 上で再ビルド不要なフィルタと設定の同梱
- Issue [#1](https://github.com/michihitoTakami/totton-rasp-gpu-dsp/issues/1) に記された EPIC の完了条件に準拠

### セットアップの概要
1. `uv sync` で Python/C++/Vulkan 関連依存を整理（`uv.lock` を再利用）
2. 必要に応じて `uv run python -m scripts.filters.generate_minimum_phase --generate-all --taps 80000 --kaiser-beta 25 --stopband-attenuation 140` でフィルタを生成
3. `cmake -B build -DENABLE_VULKAN=ON`（必要に応じて `-DUSE_VKFFT=ON`）でビルド設定を作成
4. `.pre-commit-config.yaml` で `pre-commit run --all-files` を実行
5. `aqua.yaml` を使って Aqua CLI で lint/format を統一

### Dockerデプロイ (Issue #9, #23)
ビルド済みのイメージが GitHub Container Registry (GHCR) で公開されています。Raspberry Pi でのビルドは不要です。

**クイックスタート:**
```bash
git clone https://github.com/michihitoTakami/totton-rasp-gpu-dsp.git
cd totton-rasp-gpu-dsp
docker compose up -d
```

`docker-compose.yml` が自動的に GHCR から最新イメージを取得します。

**手動でイメージを取得:**
```bash
docker pull ghcr.io/michihitotakami/totton-rasp-gpu-dsp:latest
```

**管理コマンド:**
- 起動: `docker compose up -d`
- 停止: `docker compose down`
- Web UI: `http://<pi-host>:8080`
- 設定/EQ 永続化: Docker Volume により `/var/lib/totton-dsp` に保存
- ログ: `docker compose logs -f totton-dsp` または `docker logs -f totton-dsp`

**ローカルビルド（オプション）:**
ビルド済みイメージを使わず、自分でビルドする場合:
```bash
docker compose build
```

環境変数の上書き（`.env` か export で設定）:
- `TOTTON_ALSA_IN` / `TOTTON_ALSA_OUT`（既定: `hw:0,0`）
- `TOTTON_ALSA_CHANNELS` / `TOTTON_ALSA_FORMAT`（既定: `2` / `S32_LE`）
- `TOTTON_FILTER_DIR` / `TOTTON_FILTER_RATIO` / `TOTTON_FILTER_PHASE`
- `TOTTON_WEB_PORT`（既定: `8080`）

自動起動（docker compose を起動する systemd 例）:
```ini
[Unit]
Description=Totton DSP Docker Compose
After=docker.service
Requires=docker.service

[Service]
Type=oneshot
WorkingDirectory=/opt/totton-rasp-gpu-dsp
ExecStart=/usr/bin/docker compose up -d
ExecStop=/usr/bin/docker compose down
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```
有効化:
`sudo systemctl enable docker` と `sudo systemctl enable --now totton-dsp.service`

### 期待する成果
- Pi (arm64) 向けリリースにバイナリ・フィルタ・設定例を添付
- UI と ZeroMQ API 双方から EQ 適用/設定変更/RELOAD が実行可能
- `docs/` や `scripts/` に実装の骨格を置き、開発者が理解しやすくする

### ALSA ストリーミング (Issue #3)
- ビルド: `cmake -B build -DENABLE_ALSA=ON` → `cmake --build build -j$(nproc)`
- 起動（最小）: `./build/alsa_streamer --in hw:0 --out hw:0`
- フィルタ指定: `./build/alsa_streamer --in hw:0 --out hw:0 --filter data/coefficients/filter_44k_2x_80000_min_phase.json`
- フィルタ自動選択: `./build/alsa_streamer --in hw:0 --out hw:0 --filter-dir data/coefficients --ratio 2 --phase min`
- XRUN 対応: XRUN をログに出し、`snd_pcm_recover` で継続。復帰不能なら終了

### ZeroMQ 制御サーバ (Issue #4)
- ビルド: `cmake -B build -DENABLE_ZMQ=ON` → `cmake --build build -j$(nproc)`
- 起動: `./build/zmq_control_server --endpoint ipc:///tmp/totton_zmq.sock`
- 環境変数: `TOTTON_ZMQ_ENDPOINT` / `TOTTON_ZMQ_PUB_ENDPOINT`
- コマンド: `PING`, `STATS`, `RELOAD`, `SOFT_RESET`, `PHASE_TYPE_GET`, `PHASE_TYPE_SET`

### ディレクトリ構成案
```
src/       : Vulkan/C++/ZeroMQ/ALSA などの実装
include/   : 共通ヘッダ
scripts/   : フィルタ生成・デプロイスクリプト
data/      : FIR フィルタや EQ プロファイル
docs/      : インストール/仕様/チュートリアル
web/       : FastAPI + Jinja2 テンプレート（btn_primary 等の再利用必須）
build/     : ビルドアウトプット
```

### 同梱フィルタ (Issue #7)
- `data/coefficients/` に 44k/48k の各ファミリ × 2/4/8/16x（最小位相、80kタップ）を同梱
- 再生成: `uv run python -m scripts.filters.generate_minimum_phase --generate-all --taps 80000 --kaiser-beta 25 --stopband-attenuation 140`
- 目標: Kaiser β=25, 阻止帯域減衰 140 dB（80kタップ暫定）
- ライセンス/注意: 係数は本リポジトリのライセンスに従い、外部データセットは含まれません

### 参照と今後の流れ
- [Issue #1](https://github.com/michihitoTakami/totton-rasp-gpu-dsp/issues/1) に書かれた EPIC の目的と完了条件を満たす
- `Vulkan upsampler コア (#2)`、`EQ (#5)`、`FastAPI UI/API (#6)` などの子 Issue を順次埋めていく
- フィルタのサイドカー形式は `docs/filter_format.md` を参照

### コントリビューション
- GitHub CLI `gh` で Issue/PR 操作
- ブランチ名には Issue 番号を含める（例: `feature/#1-minimal-upscaler`）
- `pre-commit run --hook-stage pre-push` を常に通す
