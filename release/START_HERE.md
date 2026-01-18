# Totton Raspberry Pi GPU DSP (arm64) 起動手順

## 1. 依存パッケージの導入
Raspberry Pi OS (64-bit) などで以下をインストールしてください。

```bash
sudo apt-get update
sudo apt-get install -y libvulkan1 libasound2 libzmq5
```

## 2. 展開と準備
```bash
tar -xzf totton-rasp-gpu-dsp-linux-arm64.tar.gz
cd totton-rasp-gpu-dsp
cp config.example.json config.json
```

## 3. 起動例
### ALSA ストリーミング
```bash
./bin/alsa_streamer --in hw:0 --out hw:0 \
  --filter data/coefficients/filter_44k_2x_80000_min_phase.json
```

### ZeroMQ コントロールサーバ
```bash
./bin/zmq_control_server --endpoint ipc:///tmp/totton_zmq.sock
```

## 4. 補足
- フィルタの仕様は `data/coefficients/README.md` と `docs/filter_format.md` を参照してください。
- 必要に応じて `config.json` の `eqEnabled` / `eqProfile` を更新してください。
