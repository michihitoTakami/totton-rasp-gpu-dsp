# Totton Raspberry Pi GPU DSP

Pi 単体で動く最小構成の GPU アップサンプラ（Vulkan + EQ + FastAPI + Jinja2 UI）を `totton audio project` から切り出し、Raspberry Pi ユーザー向けにシンプルで再利用しやすい形にしたリポジトリです。Vulkan (VkFFT) による FIR 畳み込みアップサンプリング、OPRA 系 EQ、ZeroMQ 制御、基本的な Web UI/API/管理用スクリプトを統合します。

This repository is the minimalist GPU upsampler migration from `totton audio project`, centered on Vulkan + EQ + FastAPI + Jinja2 UI operability on Raspberry Pi hardware. It combines Vulkan (VkFFT) convolution upsampling, OPRA-style EQ, ZeroMQ control, and the basic Web UI/API scaffolding for easy reuse.

## 特徴 / Features
- Vulkan + VkFFT による FIR 畳み込みアップサンプリング
- OPRA 形式に基づいた EQ 定義と FastAPI/Jinja2 ベースの設定 UI/API
- ZeroMQ での RELOAD/STATUS など最小の制御コマンド
- Web UI で EQ 適用・基本設定変更 → サーバ側に即時反映するワークフロー
- フィルタを同梱し、Pi 上でコンパイル不要で起動できるリリースを想定
- Issue [#1](https://github.com/michihitoTakami/totton-rasp-gpu-dsp/issues/1) に記載された EPIC の範囲・完了条件に準拠

- Vulkan + VkFFT based FIR convolution upsampling
- OPRA-style EQ definitions with FastAPI/Jinja2 UI/API
- Minimal ZeroMQ control surface covering RELOAD/STATUS
- Web UI workflow that applies EQ and config changes live on the server
- Bundled filters and configurations to run on Pi without rebuilding
- Aligns with the EPIC scope and completion criteria listed in Issue [#1](https://github.com/michihitoTakami/totton-rasp-gpu-dsp/issues/1)

## セットアップの概要 / Setup Overview
1. `uv sync` で Python/C++/Vulkan 関連依存を整理（`uv.lock` を再利用）
2. `uv run python scripts/filters/generate_minimum_phase.py` などでフィルタを生成（必要に応じて）
3. `.pre-commit-config.yaml` を使って `pre-commit run --all-files` を実行
4. `aqua.yaml` を使い Aqua CLI で lint/format を統一

1. Use `uv sync` to gather Python/C++/Vulkan dependencies (reusing `uv.lock`).
2. Generate filters via `uv run python scripts/filters/generate_minimum_phase.py` if required.
3. Run `pre-commit run --all-files` with the provided `.pre-commit-config.yaml`.
4. Leverage `aqua.yaml` to keep lint/format tooling consistent through Aqua CLI.

## 期待する成果 / Expected Outcomes
- Pi (arm64) リリースにバイナリ＋フィルタ＋設定例をバンドル
- UI と ZeroMQ API 双方から EQ 適用/設定変更/RELOAD などが可能
- `docs/` や `scripts/` に実装骨格を置き、開発者が手を入れやすくする

- Bundle binaries, filters, and configuration examples into the Pi (arm64) release
- Enable EQ application, config tweaks, and RELOAD via both the UI and ZeroMQ API
- Seed `docs/` and `scripts/` with scaffolding to help contributors understand the system

## ディレクトリ構成案 / Directory Layout
```
src/       : Vulkan/C++/ZeroMQ/ALSA などの実装
include/   : 共通ヘッダ
scripts/   : フィルタ生成・デプロイスクリプト
data/      : FIR フィルタや EQ プロファイル
docs/      : インストール/仕様/チュートリアル
web/       : FastAPI + Jinja2 テンプレート (btn_primary 等の再利用必須)
build/     : ビルドアウトプット
```
```
src/       : Implementations for Vulkan/C++/ZeroMQ/ALSA
include/   : Shared headers
scripts/   : Filter generation and deployment scripts
data/      : FIR filters and EQ profiles
docs/      : Installation, specs, and tutorials
web/       : FastAPI + Jinja2 templates (reusing macros like btn_primary)
build/     : Build artifacts
```

## 参照と今後の流れ / References & Next Steps
- [Issue #1](https://github.com/michihitoTakami/totton-rasp-gpu-dsp/issues/1) にある EPIC の目的・完了条件を達成すること
- まずは `Vulkan upsampler コア (#2)`、`EQ (#5)`、`FastAPI UI/API (#6)` などの子 Issue を埋めていく

- Deliver on the goals/completion criteria defined in [Issue #1](https://github.com/michihitoTakami/totton-rasp-gpu-dsp/issues/1).
- Start filling the child issues such as `Vulkan upsampler core (#2)`, `EQ (#5)`, `FastAPI UI/API (#6)`, etc.

## コントリビューション / Contribution
- GitHub CLI `gh` で Issue/PR を操作
- ブランチ名には Issue 番号を含める（例: `feature/#1-minimal-upscaler`）
- `pre-commit run --hook-stage pre-push` を常に通す

- Use GitHub CLI `gh` for Issue/PR workflows
- Include the Issue number in branch names (e.g., `feature/#1-minimal-upscaler`)
- Always run `pre-commit run --hook-stage pre-push`
