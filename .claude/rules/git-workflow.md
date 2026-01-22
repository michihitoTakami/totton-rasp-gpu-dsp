# Git Workflow Rules

このプロジェクトにおける Git ワークフローの必須ルールです。**全ての開発作業はこのルールに従う必要があります。**

## 必須ルール (Mandatory Rules)

### 1. GitHub CLI (`gh`) 必須

GitHub に関する全ての操作（Issue、PR、ラベル等）は**必ず `gh` コマンド**を使用すること。

```bash
# Issue 操作
gh issue list
gh issue view 123
gh issue create --title "..." --body "..."

# PR 操作
gh pr create --title "#123 ..." --body "..."
gh pr list
gh pr view 123

# ラベル・マイルストーン
gh label list
gh milestone list
```

### 2. Issue 番号必須

ブランチ名・PR 名には**必ず Issue 番号**を含めること。

**ブランチ命名規則:**
- `feature/#123-feature-name`
- `fix/#456-bug-description`

**PR 名規則:**
- `#123 機能の説明`
- `Fix #456: バグの説明`

### 3. ⚠️ PR マージ禁止

`gh pr merge` は**ユーザーの明示的な許可なしに実行してはならない**。

- PR の作成までは実行可能
- マージはユーザーがレビュー後に自分で実行
- 自動マージは厳禁

### 4. ⚠️ main ブランチ直接作業禁止

main ブランチで直接コミット・編集しないこと。

- **必ず worktree で feature/fix ブランチを作成**して作業
- `.claude/skills/` 配下のファイルも同様（Skills もコードである）

### 5. ⚠️ `--no-verify` 禁止

`git push --no-verify` や `git commit --no-verify` は**絶対に使用しないこと**。

- pre-commit フックや pre-push フックで失敗した場合は、エラーを修正してからプッシュ
- 自分の変更に起因しないエラーでも修正する（テストの品質維持は全員の責任）

### 6. ⚠️ `.claude/skills/` もworktree必須

`.claude/skills/` 配下のファイル作成・編集時も、**必ず worktree で作業**すること。

- main ブランチでの直接操作は厳禁
- Skills もコードであるため Git Workflow ルールが適用される

### 7. Worktree 作成前の fetch 必須

**Worktree 作成前に必ず `git fetch origin main` を実行**すること。

```bash
# 最新の origin/main を取得
git fetch origin main

# Worktree を作成
git worktree add worktrees/123-feature-name -b feature/#123-feature-name
```

- 最新の `origin/main` を取り込まずに worktree を切ると、後続で大規模コンフリクトが発生しやすい

---

## Git Worktree ワークフロー

**全ての機能開発・バグ修正には Git Worktree を使用すること。**

### 標準ワークフロー

```bash
# 1. 最新の main を取得
git fetch origin main

# 2. 新しい worktree を作成（Issue 番号を含める）
git worktree add worktrees/123-feature-name -b feature/#123-feature-name

# 3. worktree ディレクトリで作業
cd worktrees/123-feature-name

# 4. 変更をコミット
git add .
git commit -m "Add feature XYZ

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"

# 5. プッシュして PR 作成（Issue 番号をタイトルに含める）
git push -u origin feature/#123-feature-name
gh pr create --title "#123 機能の説明" --body "..."

# 6. PR マージ後、worktree をクリーンアップ
cd ../..
git worktree remove worktrees/123-feature-name
```

### バグ修正の場合

```bash
git fetch origin main
git worktree add worktrees/456-bug-fix -b fix/#456-bug-description
cd worktrees/456-bug-fix
# ... 作業 ...
git push -u origin fix/#456-bug-description
gh pr create --title "Fix #456: バグの説明" --body "..."
```

---

## 禁止されているコマンド

以下のコマンドは**絶対に実行してはならない**：

```bash
# ❌ 禁止: フックをスキップ
git commit --no-verify
git push --no-verify

# ❌ 禁止: 強制プッシュ（特に main/master）
git push --force
git push --force-with-lease  # main/master への場合

# ❌ 禁止: main ブランチでの直接作業
git checkout main
# ... 編集 ...
git commit -m "..."  # これは NG！

# ❌ 禁止: 破壊的コマンド（ユーザーの明示的な指示がない限り）
git reset --hard
git clean -f
git branch -D
```

---

## Git Safety Protocol（コミット作成時）

コミットを作成する際は、以下のプロトコルに従うこと：

### 基本原則

- **NEVER** update the git config
- **NEVER** run destructive git commands (push --force, reset --hard, checkout ., restore ., clean -f, branch -D) unless the user explicitly requests these actions
- **NEVER** skip hooks (--no-verify, --no-gpg-sign, etc) unless the user explicitly requests it
- **NEVER** run force push to main/master, warn the user if they request it
- **CRITICAL**: Always create NEW commits rather than amending, unless the user explicitly requests a git amend
- When staging files, prefer adding specific files by name rather than using "git add -A" or "git add ."
- **NEVER** commit changes unless the user explicitly asks you to

### コミット作成手順

ユーザーがコミット作成を明示的に要求した場合のみ、以下の手順を実行：

1. **現状確認（並列実行）:**
   ```bash
   git status  # NEVER use -uall flag
   git diff
   git log --oneline -5
   ```

2. **コミットメッセージ作成:**
   - 変更の性質を要約（新機能、既存機能の改善、バグ修正、リファクタリング等）
   - 簡潔に（1-2文）、"why" に焦点を当てる
   - 最近のコミットメッセージのスタイルに従う

3. **コミット実行（並列実行）:**
   ```bash
   # 関連ファイルをステージング（個別指定推奨）
   git add src/file1.cpp src/file2.cu

   # コミット作成（HEREDOC で整形）
   git commit -m "$(cat <<'EOF'
   Add feature XYZ

   Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
   EOF
   )"

   # 結果確認
   git status
   ```

4. **Pre-commit フック失敗時:**
   - 問題を修正
   - 再度ステージング
   - **新しいコミットを作成**（`--amend` は使用しない）

### 注意事項

- **NEVER** use git commands with the -i flag (interactive mode not supported)
- 変更がない場合は空コミットを作成しない
- シークレットを含むファイル（.env, credentials.json 等）はコミットしない

---

## 例外とフォールバック

### ユーザーが明示的に要求した場合のみ実行可能

以下の操作は**ユーザーが明示的に要求した場合のみ**実行可能：

- `git push --force`（ただし main/master への force push は警告）
- `git commit --amend`
- `git reset --hard`
- `git clean -f`
- `--no-verify` フラグの使用

### 緊急時のフォールバック

**Worktree が使用できない緊急時のみ**、以下の手順で作業可能：

```bash
# 1. 新しいブランチを作成
git checkout -b feature/#123-emergency-fix

# 2. 作業実施
# ...

# 3. プッシュして PR 作成
git push -u origin feature/#123-emergency-fix
gh pr create --title "#123 緊急修正" --body "Worktree が使用できないため直接ブランチで作業"
```

**注意:** この方法は最後の手段であり、通常は worktree を使用すること。

---

## トラブルシューティング

### Worktree が削除できない

```bash
# Worktree を強制削除
git worktree remove --force worktrees/123-feature-name

# それでも削除できない場合
rm -rf worktrees/123-feature-name
git worktree prune
```

### Worktree のブランチが古い

```bash
cd worktrees/123-feature-name
git fetch origin main
git rebase origin/main
```

### Worktree 一覧確認

```bash
git worktree list
```

---

## 関連ドキュメント

- **CLAUDE.md**: プロジェクト全体の開発ガイド
- **pre-commit hooks**: `.pre-commit-config.yaml` で定義されているフック
- **GitHub CLI ドキュメント**: https://cli.github.com/manual/

---

## 更新履歴

- 2026-01-22: `.claude/rules/` に Git Workflow ルールを分離
