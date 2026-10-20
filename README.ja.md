# CodeName VAIS — Server

[English](README.md) · [Korean](README.ko.md) · [Japanese](README.ja.md)

チームで開発中のマルチプレイヤーFPS。C++サーバーと試合状態をUnityのプレイ体験につなぎます。

**C++ · Windows IOCP · C# · Unity** · 開発中

**2026.03 - 現在 · 10名：ソフトウェア5名、グラフィック5名**

サーバー開発・Unityマルチプレイヤー連携

[開発事例を見る](https://ysl-ryudo.github.io/ja/work/vector/) · [Portfolio](https://ysl-ryudo.github.io/ja/)

[ProjectVectorServer](https://github.com/YSL-RyuDo/ProjectVectorServer) (非公開・アクセス権が必要) · [projectVector](https://github.com/YSL-RyuDo/projectVector) (非公開・アクセス権が必要)

## システム別の担当内容

| 領域 | 実装 |
|---|---|
| 接続・通信 | IOCP・セッション・パケットディスパッチ |
| ゲームサービス | ルーム・試合のライフサイクル、キャリア・プロフィール・ショップ・ソーシャルデータ |
| Unity連携 | ゲームパケット、ロビー・結果、Sentinel能力、音声権限 |
| 復元の境界 | 最新の持続表示状態を保持し、ゲーム操作の再実行を除外 |

## ReconnectJournal

再接続したプレイヤーに何を再表示し、どのゲーム操作を繰り返してはいけないか。

1. **復元する状態を選ぶ** — Observeは明示された持続表示パケットを収集します。ダメージ・報酬・プレイヤー命令は復元記録に含めません。
2. **最新状態に置き換える** — パケット種別とエンティティIDをキーに保存します。更新は同じ項目を置き換え、操作履歴の順次再生とは分離します。
3. **ライフサイクルの変更を反映** — 新しい試合・ラウンドのパケットで記録を初期化します。削除パケットは対応状態を消し、相互排他的な状態は以前の項目を整理します。
4. **残り時間で復元** — Snapshotは保存期間からsteady clockの経過時間を引き、0以上に制限します。0秒の状態パケットも残し、クライアントの古い状態を解除します。

正式名称はCodeName VAIS、Project Vectorは旧仮称です。リポジトリ名は維持しています。

## 開発デモ動画

- [Lobby UI](https://www.youtube.com/watch?v=WqRBn-yWVdo)
- [Store system](https://www.youtube.com/watch?v=OBFWkZE2rzo)
- [Profile system](https://www.youtube.com/watch?v=A3CkQvnQEGI)
- [Earlier Project Vector demo](https://www.youtube.com/watch?v=oga4ai_1Ugg)

## 担当した内容

- ゲームクライアント向けのIOCP接続処理、セッション、パケットディスパッチを実装しました。
- ルーム・試合のライフサイクル、プロフィール・キャリア情報、ソーシャル機能、ショップサービスとクライアント契約を担当しました。
- 再接続時の表示状態復元と、パーティー・チーム音声の権限制御を追加しました。ReconnectJournalはダメージ・報酬・プレイヤー命令の再実行ではなく、持続する状態を保持します。

## 貢献の根拠

- [IOCP](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/IocpCore.cpp)
- [ReconnectJournal](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h)
- [Validation sources](https://github.com/YSL-RyuDo/ProjectVectorServer/tree/3066fe6d90fb4a56a6c440af56c4e119188e73fc/Tests)

コミットは記載した変更の根拠であり、貢献率の点数ではありません。外部素材・チームメンバーの貢献は区別します。

## 実行・コードの確認

Windows C++ワークロードを備えたVisual Studioで[ProjectVectorServer/ProjectVectorServer.sln](ProjectVectorServer/ProjectVectorServer.sln)を開いてください。構成・データパス・依存関係・接続設定を確認し、対応するクライアントを起動します。既存アカウントデータは維持してください。

[projectVector](https://github.com/YSL-RyuDo/projectVector)

## 開発・検証範囲

開発中です。再接続・試合・ショップ・ソーシャル・音声契約のテストソースはありますが、存在だけでは現行版の成功を意味しません。音声の検証記録と残る2台PC検証はDocs/VoiceChatSetup.mdを参照してください。現行TCPにはTLSがなく、文書上の開発環境は信頼できるLAN/VPNです。IOCP採用を実測の拡張性と同一視しません。

## 詳細資料

- [Docs/VoiceChatSetup.md](Docs/VoiceChatSetup.md)
- [Docs/CombatReportProtocol.md](Docs/CombatReportProtocol.md)
