# Installation & Development Workflow

1. `uv sync` で依存を用意（`pyproject.toml` + `uv.lock`）。キャッシュをローカル `.uv-cache/` に置く構成なので、他人との差分を残さず再現できます。
2. `.venv` 内にある `uv`/`python` に対して `uv run python -m pip install --upgrade pip` などでバージョンを上げたいなら、同じ仮想環境を使ってください。
3. Aquaは `aqua.yaml` でコマンドを定義しています。`aqua run lint` や `aqua run format` などを使い、開発ルールに沿ったフォーマット・検証を行いましょう。
4. `pre-commit` は `.pre-commit-config.yaml` 配下で設定済みなので `.venv/bin/pre-commit run --all-files` で手元でのチェックと、`pre-commit run --hook-stage pre-push` をプッシュ前に実行してください。
5. `README.md` にある Issue #1~ 子 Issue を A/B として参照し、`gh` を使って Issue番号入りのブランチを作成してください（例: `feature/#1-minimal-upscaler`）。

以上の手順で初期セットアップと開発準備が整います。
