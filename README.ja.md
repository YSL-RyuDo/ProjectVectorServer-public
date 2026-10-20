# CodeName VAIS / Server

[English](README.md), [Korean](README.ko.md), [Japanese](README.ja.md)

チームで開発中のマルチプレイヤーFPS。C++サーバーと試合状態をUnityのプレイ体験につなぎます。

| プロジェクト情報 | 内容 |
|---|---|
| 開発状況 | 開発中 |
| 使用技術 | C++, Windows IOCP, C#, Unity |
| 期間とチーム構成 | 2026.03 ~ 現在, 10名：ソフトウェア5名、グラフィック5名 |
| 担当 | サーバー開発・Unityマルチプレイヤー連携 |

[開発事例を見る](https://ysl-ryudo.github.io/ja/work/vector/), [Portfolio](https://ysl-ryudo.github.io/ja/)

[![CodeName VAIS](https://i.ytimg.com/vi/WqRBn-yWVdo/hqdefault.jpg)](https://www.youtube.com/watch?v=WqRBn-yWVdo)

[Lobby UI](https://www.youtube.com/watch?v=WqRBn-yWVdo), [Store system](https://www.youtube.com/watch?v=OBFWkZE2rzo), [Profile system](https://www.youtube.com/watch?v=A3CkQvnQEGI), [Earlier Project Vector demo](https://www.youtube.com/watch?v=oga4ai_1Ugg)

## 担当した内容

ゲームクライアント向けのIOCP接続処理、セッション、パケットディスパッチを実装しました。

ルーム・試合のライフサイクル、プロフィール・キャリア情報、ソーシャル機能、ショップサービスとクライアント契約を担当しました。

再接続時の表示状態復元と、パーティー・チーム音声の権限制御を追加しました。ReconnectJournalはダメージ・報酬・プレイヤー命令の再実行ではなく、持続する状態を保持します。

## 貢献の根拠

[IOCP](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/IocpCore.cpp)

[ReconnectJournal](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/3066fe6d90fb4a56a6c440af56c4e119188e73fc/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h)

[Validation sources](https://github.com/YSL-RyuDo/ProjectVectorServer/tree/3066fe6d90fb4a56a6c440af56c4e119188e73fc/Tests)

コミットは記載した変更の根拠であり、貢献率の点数ではありません。外部素材・チームメンバーの貢献は区別します。

## 実装と設計

マルチプレイヤーFPSでは通信、試合のライフサイクル、プレイヤーに見える状態をつなぐ必要があります。再接続では復元する状態と再実行しない操作を区別します。

IOCPサーバーのセッション・パケット処理から、Unityの試合・プロフィール・ショップ連携、再接続時の表示状態復元まで担当しました。ReconnectJournalは持続状態を保持し、ダメージ・報酬・プレイヤー命令の再実行を除外します。

| 領域 | 実装 |
|---|---|
| 接続・通信 | IOCP・セッション・パケットディスパッチ |
| ゲームサービス | ルーム・試合のライフサイクル、キャリア・プロフィール・ショップ・ソーシャルデータ |
| Unity連携 | ゲームパケット、ロビー・結果、Sentinel能力、音声権限 |
| 復元の境界 | 最新の持続表示状態を保持し、ゲーム操作の再実行を除外 |

### ReconnectJournal

**復元する状態を選ぶ**

Observeは明示された持続表示パケットを収集します。ダメージ・報酬・プレイヤー命令は復元記録に含めません。

**最新状態に置き換える**

パケット種別とエンティティIDをキーに保存します。更新は同じ項目を置き換え、操作履歴の順次再生とは分離します。

**ライフサイクルの変更を反映**

新しい試合・ラウンドのパケットで記録を初期化します。削除パケットは対応状態を消し、相互排他的な状態は以前の項目を整理します。

**残り時間で復元**

Snapshotは保存期間からsteady clockの経過時間を引き、0以上に制限します。0秒の状態パケットも残し、クライアントの古い状態を解除します。

正式名称はCodeName VAIS、Project Vectorは旧仮称です。リポジトリ名は維持しています。

## コードの動作と実装構造の理由

確認した実装が扱う制約と利点・代償を説明します。設計効果の説明はコード分析であり、比較する全代案を当時試作したという意味ではありません。担当リンクは共同制作内の変更を示し、ソースの閲覧権限は維持します。

### 購入応答が失われても二重に決済しない

**実装の流れ**

ショップ要求に要求IDを付け、サーバーは通貨を引く前に保存済みの取引履歴を確認します。新規取引では報酬を計算し、CareerManagerで残高、所有権、取引履歴をまとめて保存します。同じ要求の再送には記録済みの報酬を返し、保存失敗時はメモリ上の残高を戻して失敗を返します。

**この構造が必要な理由**

購入ボタンの無効化だけでは応答の紛失に対応できません。クライアントからは未処理の要求と、決済済みで応答だけ失われた要求を区別できないためです。要求IDと結果を保存すると再接続や再起動後も区別できます。既存のウォレットを共有することで、試合報酬とショップの残高を別々に同期する必要も減ります。

**代案と現在の実装の折合い**

ボタンのロックは簡単ですが画面内だけです。メモリでの重複排除は再起動で情報を失います。現在の要求・結果の永続化は再起動にも対応する代わりに台帳管理が必要です。一意な要求キーとDBトランザクションも代案ですが、DB運用と移行の費用が加わります。

**利点・代償と検証範囲**

同じロックを使う処理は直列化されるため、保存が遅いと他の処理も待ちます。画面は直近20件のみですが、重複決済防止の台帳は保持するため保存容量の上限ではありません。SharedShopWalletTestsには再起動後の再送、同時の重複要求、保存失敗時の復元を検査するコードがあります。これはテストソースの範囲であり、新しい負荷試験結果ではありません。

[ProjectVectorServer/ShopService.h](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/ShopService.h), [ProjectVectorServer/CareerManager.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/CareerManager.cpp), [ProjectVectorServer/SharedShopWalletTests.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/Tests/SharedShopWalletTests.cpp), [projectVector/ShopGachaImplementation.md](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Docs/ShopGachaImplementation.md)

### 抽選結果と演出の寿命を分ける理由

**実装の流れ**

Unityクライアントはサーバー結果を受けてからGachaManagerの演出を開始します。コルーチンは準備、移動、ヒット、実体化、結果表示をunscaled時間で進めます。スキップ時はコルーチンを停止し、一時オブジェクト、RenderTexture、Materialを解放して同じ確定結果を表示します。UIはシーンに保存した参照を使い、3Dプレビューの一時オブジェクトは別に生成します。

**この構造が必要な理由**

演出は中断できますが、中断のたびに購入を再抽選するべきではありません。寿命を分けるとスキップや閉じる操作が報酬の付与・取消を起こしません。unscaled時間はゲーム時間停止中にも演出を進めるために使います。シーンにUIを保存することで、生成コードを実行せず配置や参照を編集できます。

**代案と現在の実装の折合い**

コルーチンは時間制御と解放をコードで一緒に追えます。Timelineは制作者の視覚的編集、Tweenは個別動作の簡潔な記述に向きます。現在の方式はコードでの制御を得る代わりに視覚編集支援が少なくなります。どの方式も取消処理と同じ確定報酬の表示が必要です。

**利点・代償と検証範囲**

コルーチンでは流れをコードで追えますが、段階の順序と中断時の解放を明示的に管理します。TimelineやTweenに移しても、シークやスキップが購入を再実行しない境界は必要です。Unityのガベージコレクションだけで描画リソースの明示的な解放を置き換えることもできません。

[projectVector/GachaManager.cs](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/KSW/Script/Shop/GachaManager.cs), [projectVector/ShopGachaImplementation.md](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Docs/ShopGachaImplementation.md)

### ソケットの再接続だけでは復帰が終わらない

**実装の流れ**

サーバーは認証済みの試合中セッションを復帰期限まで保持し、新しい通信接続を既存の論理セッションに結びます。世代番号で古い切断処理を無効にします。UnityのNetworkReconnectはシーンとプレイヤーオブジェクトの準備を待ち、復元段階を確認してから通常入力を再開します。ReconnectJournalはダメージや報酬を再実行せず、持続する表示状態の最新値を保持します。

**この構造が必要な理由**

通信接続と試合参加者の寿命は異なります。再接続のたびに参加者を作り直すと試合状態を失い、オブジェクト生成前にパケットを適用すると復元が途中になります。段階を分けることでシーン、オブジェクト、位置、試合状態の依存関係を明確にします。最新状態の保持は過去の行動を新しい命令として実行する問題も避けます。

**代案と現在の実装の折合い**

新規セッション開始は単純ですが進行中の試合を維持できません。全イベント再生は再実行の意味を厳密に管理する必要があります。現在は論理セッションを保持し必要状態を段階的に復元して副作用を減らす代わりに、復元プロトコルを維持します。

**利点・代償と検証範囲**

セッション保持には期限までリソースが必要で、段階的復元にはタイムアウトとプロトコル版の管理が加わります。新しい持続効果も復元規則に追加する必要があります。以下のクライアントはYSL、サーバーはmainの確認版であり、すべての組合せで実試合の検証が完了したという意味ではありません。

[ProjectVectorServer/Session.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/Session.cpp), [ProjectVectorServer/ReconnectJournal.h](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/ReconnectJournal.h), [projectVector/NetworkReconnect.cs](https://github.com/YSL-RyuDo/projectVector/blob/5e5c3c53b010fb2807f6b53c0eb0f5ceec8d1350/Assets/Scripts/Network/NetworkReconnect.cs)

### 死亡レポートを固定し閲覧条件を確認する理由

**実装の流れ**

CombatReportServiceは適用されたダメージをバリアと体力に分けて記録し、致死ダメージの記録後にレポートを固定します。閲覧時は同じルームの認証済み死亡プレイヤーで観戦者ではないことを確認し、その本人だけに返します。

**この構造が必要な理由**

致死ダメージより先に固定すると最も重要な最後のイベントが抜けます。記録後の固定なら、試合が続いても死亡UIに一定時点の情報を示せます。ボタンを隠すだけでは生存中のクライアントの要求を防げないため、サーバー側でも条件を確認します。

**代案と現在の実装の折合い**

現在の累積値を直接読むと簡単ですが死亡画面中も内容が変わり得ます。全戦闘イベントの保存は分析範囲を広げる代わりに容量と情報公開の管理が必要です。死亡時の固定レポートはUIに必要な安定した情報を閲覧規則とともに提供します。

**利点・代償と検証範囲**

戦闘コードの変更後もダメージ記録と死亡通知の順序を維持する必要があります。スキルが使用者より長く残る場合があるため、元のオブジェクトがなくても代替識別情報で記録を残します。ここで説明する範囲は戦闘レポートの閲覧境界です。

[ProjectVectorServer/CombatReportService.h](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/ProjectVectorServer/ProjectVectorServer/CombatReportService.h), [ProjectVectorServer/CombatReportTests.cpp](https://github.com/YSL-RyuDo/ProjectVectorServer/blob/d2d2e6261b93ca93beceec715f49015af8e2759c/Tests/CombatReportTests.cpp)

## 実行・コードの確認

Windows C++ワークロードを備えたVisual Studioで[ProjectVectorServer/ProjectVectorServer.sln](ProjectVectorServer/ProjectVectorServer.sln)を開いてください。構成・データパス・依存関係・接続設定を確認し、対応するクライアントを起動します。既存アカウントデータは維持してください。

[projectVector](https://github.com/YSL-RyuDo/projectVector)

## 開発・検証範囲

開発中です。再接続・試合・ショップ・ソーシャル・音声契約のテストソースはありますが、存在だけでは現行版の成功を意味しません。音声の検証記録と残る2台PC検証はDocs/VoiceChatSetup.mdを参照してください。現行TCPにはTLSがなく、文書上の開発環境は信頼できるLAN/VPNです。IOCP採用を実測の拡張性と同一視しません。

## 詳細資料

[Docs/VoiceChatSetup.md](Docs/VoiceChatSetup.md)

[Docs/CombatReportProtocol.md](Docs/CombatReportProtocol.md)

## 関連リポジトリ

[ProjectVectorServer](https://github.com/YSL-RyuDo/ProjectVectorServer), 非公開・アクセス権が必要

[projectVector](https://github.com/YSL-RyuDo/projectVector), 非公開・アクセス権が必要
