# 設定台帳

> **このファイルは唯一の設定台帳である。**
> 設定項目を追加する変更は、**この表への行追加を伴わなければならない。**
> 「反映条件」列は空欄禁止。分からない場合は調べてから書く。

## なぜこの台帳を持つか

設定フラグを機能ごとに場当たりで足していくと、
**「どのファイルの、どのキーを、いつ読んでいて、いつ反映されるか」が誰にも分からなくなる。**

- 同じような設定が、あるものは即時反映、あるものはプロセス再起動が必要、という状態になる
- 運用担当者は、設定を変えたあと何をすれば反映されるのか判断できない
- セクション名を間違えても誰も気づかない（既定値で動いてしまうため）

実務でこの状態を経験したため、fleetcore では最初から台帳を持つ。

## 方針

- 設定ファイルは **1つ**（`config/fleetcore.toml`）。プロセスごとにセクションを切る
- 例外は**データ駆動の表**（`config/forward.toml`、`config/scenarios/*.yaml`）。これらは設定ではなく定義
- 既定値は**コード側に持つ**。設定ファイルが無くても起動できること
- 反映条件は次の4種類のいずれか

| 反映条件 | 意味 |
|---|---|
| 即時 | 使用のたびに読む。ファイルを保存した瞬間から効く |
| 接続時 | 端末が次に接続してきたときに効く |
| 画面再表示 | 該当画面を開き直すと効く |
| **プロセス再起動** | プロセスを再起動しないと効かない |

---

## 台帳

| キー | セクション | 既定値 | 読込プロセス | 読込タイミング | 反映条件 | 説明 |
|---|---|---|---|---|---|---|
| `listen_port` | `[gateway]` | 9100 | gateway | 起動時1回 | プロセス再起動 | 保守端末からの受付ポート |
| `send_queue_size` | `[gateway]` | 256 | gateway | 起動時1回 | プロセス再起動 | 送信キュー長。溢れたら古いものから破棄 |
| `shm_key` | `[common]` | 0x46430001 | gateway/locator/termd | 起動時1回 | プロセス再起動 | 端末テーブル共有メモリキー |
| `max_terminals` | `[common]` | 64 | gateway/locator/termd | 起動時1回 | プロセス再起動 | 端末テーブルの要素数。変更時は全プロセス再起動 |
| `msgq_key_base` | `[common]` | 0x46431000 | gateway/locator/termd | 起動時1回 | プロセス再起動 | メッセージキーの基点 |
| `mq_recv_timeout_ms` | `[common]` | 200 | 全業務プロセス | 起動時1回 | プロセス再起動 | メッセージ受信のタイムアウト。終了フラグの確認間隔でもある |
| `heartbeat_sec` | `[termd]` | 30 | termd | 起動時1回 | プロセス再起動 | 死活監視間隔 |
| `offline_hold_max` | `[termd]` | 100 | termd | 起動時1回 | プロセス再起動 | store-and-forward の保持上限（Phase 4） |
| `db_path` | `[auth]` | `./fleetcore.db` | term_gui | 起動時1回 | プロセス再起動 | 認証 DB のパス |
| `kdf_memory_kb` | `[auth]` | 65536 | term_gui | 起動時1回 | プロセス再起動 | パスワードハッシュのメモリパラメータ（ADR-0002） |
| `kdf_iterations` | `[auth]` | 3 | term_gui | 起動時1回 | プロセス再起動 | 同上、反復回数 |
| `socket_path` | `[term_ipc]` | `/tmp/fleetcore/` | term_* | 起動時1回 | プロセス再起動 | QLocalSocket の接続先（ADR-0001、Phase 2） |
| `forward_table` | `[term_ipc]` | `config/forward.toml` | term_router | 起動時1回 | プロセス再起動 | 転送表のパス（Phase 2） |
| `log_level` | `[log]` | `info` | 全プロセス | **使用のたび** | **即時** | ログ出力レベル。稼働中に変更可能 |
| `trace_message` | `[log]` | `false` | term_router | **使用のたび** | **即時** | 電文トレース。ADR-0001 の代償に対する対策 |
| `position_interval_ms` | `[position]` | 1000 | term_sim / term_gui | 起動時1回 | プロセス再起動 | 位置報告間隔（Phase 5） |
| `route_provider` | `[route]` | `mock` | route_svc | 起動時1回 | プロセス再起動 | `mock` / `osrm` / `commercial`（ADR-0005、Phase 6） |
| `osrm_host` | `[route]` | （空） | route_svc | 起動時1回 | プロセス再起動 | `route_provider = "osrm"` のときのみ使用 |
| `map_tile_host` | `[map]` | （空） | maint_gui | 起動時1回 | プロセス再起動 | タイル提供元。**空なら地図非表示**（Phase 6） |

**API キー・パスワード等の秘匿情報はこのファイルに書かない。環境変数から読む。**

---

## 更新履歴

| 日付 | 内容 |
|---|---|
| 2026-08 | 初版。Phase 1 分を確定、Phase 2〜6 分は予定として記載 |
| 2026-08 | Step 2：`mq_recv_timeout_ms` を追加。プロセス基底クラス導入に伴う |
