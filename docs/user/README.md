# gEngine の利用者向けドキュメント

gEngine Ver.0.1.0 は、プロジェクトの作成・開閉、gLang の非同期コンパイル、VM の実行と集合の状態表示を行う Windows 用 Editor です。Qt Widgets の画面と MCP から同じ操作を使えます。2D / 3D サンプルの単独実行、Editor 埋め込み型 PIE、初期シーンの編集・履歴・保存、両次元のマウス配置操作に対応します。通常ファイルと BinPackLib の pack から同じサンプル素材を読み込みます。素材の一覧・登録、対象への描画・音声素材の割当て、参照切れの診断に対応します。

- [gLang の README](../../components/gLang/README.md)：言語・VM を単独でビルドし、実行する手順。
- [gLang と gVM の仕様](../../components/gLang/docs/user/language-and-vm.md)：構文・型・制限、Rust / C / C++ の API。

gLang のコマンドを実行する作業ディレクトリは `components/gLang` です。gEngine の CMake ビルドは、この部品と描画・音声部品をソースから組み込みます。

## この開発版の対応範囲と制約

gEngine と gLang の版は別です。gEngine Ver.0.1.0 は gLang / gVM Ver.0.2.0 を組み込み、言語のバイトコード形式 0.3・C ABI 1 を使います。言語の型・集合・借用・実行予算の制約は [gLang の仕様](../../components/gLang/docs/user/language-and-vm.md) に従います。

編集対象は同梱の 2D / 3D ゲームが使う初期配置・ゲーム設定・カメラ・素材参照です。実行中の弾を個別に編集する機能、地形・モデリング・アニメーション編集、内蔵コードエディター、ライブ編集・ホットリロードはありません。シーンを変更するときは試遊を停止し、変更後に再実行します。試遊中の位置や HP は保存シーンへ書き戻しません。

ゲームの弾は各陣営最大 4,096 発です。言語単独の大量個体の更新性能と、ゲームで描画できる数・フレーム時間は異なります。性能は機器・描画条件・編集内容に依存し、すべての PC で一定の FPS を保証するものではありません。ゲーム別の進行保存に対応します。ネットワーク同期と Windows x64 以外の実行環境は対象外です。

## 配置済みの開発版を使う

[GitHub Releases](https://github.com/nkue-yst/gEngine/releases) から対象バージョンの Windows x64 用 ZIP を取得し、任意のフォルダーに展開します。

Windows x64 のデスクトップ、D3D12 対応 GPU、音声出力先、Microsoft Visual C++ 2022 x64 の実行ランタイムが必要です。配置済みの実行物を使うだけなら、Visual Studio、Rust、CMake、Python、Qt SDK は不要です。ゲーム用フォルダーを含め、必要なファイルをまとめて保持してください。

| 場所 | 用途 |
| --- | --- |
| `gEngine.lnk` | Editor の起動。実体は `Editor/gengine_editor.exe` |
| `Editor/` | Editor、compiler、ビルド用ツール、Qt、既定素材、バックエンド、原文表示・Qt 対応ソース |
| `Language/` | 単独実行する `glangc.exe`、`gvm.exe` と `minimal.glang` |
| `Games/` | Editor を必要としない両ゲームの実行物。`Play-2d.cmd` / `Play-3d.cmd` で起動 |
| `Samples/` | 編集して試す `shooter2d` / `shooter3d` のプロジェクト |
| `docs/user/`、`components/gLang/` | 本体の利用手順と言語の仕様・参照例。ソース一式のビルド環境ではない |
| `distribution.json` | 配置時の版・構成・元のコミット・追跡ファイルの未コミット変更の有無と、配置ファイルの SHA-256 一覧 |

ショートカットを使う場合は、ZIP の展開先で次のコマンドを実行して `gEngine.lnk` を生成します。ショートカットは生成時の絶対パスを参照するため、フォルダーを移動したら移動先で再生成してください。実体 EXE の直接起動と MCP 起動にはショートカットを使いません。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./Initialize-Distribution.ps1
```

このコマンドの実行ポリシー指定は、その PowerShell プロセスだけに適用します。組織のポリシーで実行できない場合は実体 EXE を直接起動してください。Editor の既定の作業場所は起動時の作業ディレクトリです。ショートカットから起動した場合は配置フォルダーになります。作業場所を固定するには、存在するフォルダーを `--workspace` に指定します。

最初の制作確認は、次の順に行えます。

1. `Samples/shooter2d/project.json` を「参照…」から開き、ソースを選択して「シーンを開く」を押します。原本を保つ場合は、先に `shooter2d` フォルダーを自分の作業フォルダーへコピーしてください。
2. 一覧のゲーム設定を選び、移動速度などを変更して「値を適用」→「保存」→「再読込み」を押します。保存した値が表示されることを確認します。
3. 「コンパイル」の成功後、実行方式を「PIE」、試遊条件を通常開始にして「実行」を押します。Enter で開始し、結果から Esc で開始画面へ戻れます。「試遊終了」で編集へ戻ります。
4. 「ビルド」の成功を確認します。実行方式を「スタンドアロン」にして「実行」を押すと、保存済み入力を改めてビルドし、その成功成果物で GameRunner が起動します。
5. ウィンドウを閉じて終了コードを確認するか、「試遊終了」で停止します。3D は `Samples/shooter3d/project.json` で同じ手順を使います。

MCP の起動コマンドは `Editor/gengine_editor.exe --mcp --workspace <作業フォルダー>` です。パスは絶対パスに置き換え、各引数を別の要素で渡します。[MCP 接続](#mcp-から接続する)の設定例に続き、`project.open` → `document.open` → `scene.set_settings` → `document.save` → `document.reload` → `compile.start` → `job.get` → `play.start` の順に操作します。PIE 終了後は `build.start` → `job.get` → `standalone.start` → `standalone.get_state` を使います。変更ごとに新しい `_operation_id` を使い、応答の revision を次の要求に渡してください。ジョブが `Succeeded` になる前に次の実行へ進めません。

単独の言語系は配置フォルダーで次のように使います。成功時は `main returned 0` を表示します。ゲーム用ソースはホストから呼び出す形式なので、この単独サンプルとは区別します。C / C++ 組込みには gLang のソースリポジトリを使います。

```powershell
./Language/glangc.exe ./Language/minimal.glang -o ./minimal.glbc
if ($LASTEXITCODE -ne 0) { throw "コンパイルに失敗しました" }
./Language/gvm.exe ./minimal.glbc
```

## ソースから配置用成果物を作る

下記のビルド環境で Qt 対応ソースを指定して構成した後、`cmake --build --preset release --target gengine_distribution --parallel 4` を実行します。既定の出力は `out/distribution/gEngine-<版>-Release/` です。Debug は `--preset debug` を使います。配置の作成には Python 3.11 以上と Windows PowerShell が必要です。

既存の出力フォルダーは上書きしません。別の出力先を使う場合は、構成時に `-DGENGINE_DISTRIBUTION_DIRECTORY="<出力先の親フォルダー>"` を指定してください。全入力・文書参照・Qt ソースの検査後にコピーし、完了したときだけ `distribution.json` を書きます。途中失敗で残ったフォルダーは完成版として使わず、原因を直して新しい場所へ作成します。通常ビルドの試験 EXE、内部資料、Git 履歴は配置しません。

## Editor をビルドして起動する

Windows x64、Visual Studio 2022 の C++23 対応 MSVC、Windows SDK、CMake 3.22 以上、Python 3.11 以上、gLang が指定する Rust ツールチェーンを使います。CMake を実行する前に、`components/` 配下の gLang・rEngine・aEngine・BinPackLib を、gEngine が記録する各コミットに揃えてください。公開済みの各部品の最新版を取得するだけでは、必要なコミットが揃わない場合があります。以下のコマンドはリポジトリのルートで実行します。Qt 6.12.0 の MSVC 2022 x64 版（Core / Gui / Widgets）をインストールしてください。自動試験を有効にする既定構成では Qt Test も必要です。配置試験のデスクトップ画像取得には Pillow を使います。

```powershell
cmake --preset windows -DCMAKE_PREFIX_PATH="<Qt 6.12.0 の msvc2022_64 フォルダー>"
cmake --build --preset release --parallel
New-Item -ItemType Directory -Force ./MyProjects | Out-Null
./out/build/Release/Editor/gengine_editor.exe --workspace ./MyProjects
```

Editor だけを構築する場合は、ビルドに `--target gengine_editor` を追加します。試験が不要なら構成時に `-DGENGINE_BUILD_TESTS=OFF` を指定できます。実体 EXE、`glangc.exe`、`bpack.exe`、`gengine_runner.exe`、Qt DLL、`platforms/`、`backends/`、`licenses/`、`ThirdParty/`、`qt.conf`、`SOURCE.txt` は `Editor/` 内へ自動配置されます。移動するときはフォルダー全体を同梱してください。GameRunner は Qt を使用しません。本体の Debug / Release は、それぞれの本体コードと同じ `/MD` CRT を使う Qt Release DLL に接続します。Qt Debug DLL を混在させないでください。

ビルド後は `gengine_editor.exe` をダブルクリックして起動できます。「参照…」からプロジェクト設定ファイルを選ぶと、そのファイルがあるフォルダーを作業フォルダーにします。読込みに失敗した場合は元の作業フォルダーを保ちます。初回の選択前は起動時のカレントディレクトリを使い、現在の作業フォルダーは画面上部に表示します。

`--workspace` で既存の作業フォルダーを明示した場合は、その配下だけを扱い、「参照…」で選んでも範囲を変更しません。MCP 起動でも指定した範囲を維持します。GUI 自体の起動・コンパイルに GPU や音声デバイスは不要です。

## 作成・コンパイル・再読込み

1. 「2D ゲーム」「3D ゲーム」「VM 検証」からテンプレートを選びます。GUI の既定は 2D です。パス欄へ `MyProject` などの新しいフォルダー名を入れ、「新規作成」を押します。既存フォルダーは上書きしません。`project.gengine.json` と `scripts/game.glang` が作成され、ゲーム用では `scene.json` も登録されます。
2. ソース一覧からファイルを選択し、「コンパイル」を押します。受付済み・実行中・成功・失敗・取消し完了を区別して表示します。
3. 編集する場合は「メモ帳で編集」、または任意の外部テキストエディターでソースを開きます。外部エディターで保存してから再コンパイルしてください。未保存の編集内容はコンパイルに含まれません。
4. コンパイルエラーでは元のソース名、行、列、理由を表示します。型や構文を修正して保存し、同じボタンから再実行できます。ファイル読込み等のエラーには行・列がない場合があります。
5. 「閉じる」で現在のプロジェクトを閉じます。パス欄の `MyProject/project.gengine.json` と「開く」、または「参照…」から再読込みできます。

コンパイル中はプロジェクトを閉じたり切り替えたりできません。「取消し」を押した後も、子プロセスの終了確認までは処理中です。完了したジョブへの取消し要求は結果を変更しません。Editor の終了・MCP の切断でも、実行中のコンパイラーを停止して後始末します。

成果物は、たとえば `MyProject/.gengine/build/scripts/game.glbc` です。元ソース名に対応する最新の成功結果を保持し、失敗・取消しでは前回の成功ファイルを置き換えません。その場合、今回のジョブに成果物を返すこともありません。成功前の古いファイルを今回の成功と取り違えないでください。

## VM を実行して集合を調べる

「VM 検証」テンプレートで作成したプロジェクトには、容量 128 の `particles2d` / `particles3d` と操作用の公開関数が入ります。既存プロジェクトのソースは変更しません。初期ソースの正本は [game.glang](../../apps/Editor/templates/game.glang) です。

1. コンパイルが成功したら「VM へ読込み」を押します。読込み直後の集合は空で、画面に `Ready` と読込み元のジョブを表示します。
2. 公開関数欄の `initialize` を「一回実行」します。各集合に 3 個体を生成します。
3. 「集合を表示」を押します。既定では `particles2d` の位置 2 成分、寿命、連番、真偽値を表示します。件数は 3、容量は 128 です。
4. 公開関数を `update` に変えて一回実行し、「表示を更新」を押します。先頭個体の 2D 位置は `(0, -2)` から `(1, -1.5)`、寿命は 3 から 2 になります。これは一回分の数値更新であり、実時間のフレーム進行ではありません。
5. 3D を確認するときは集合名を `particles3d`、列を `position:0,position:1,position:2,life` に変えます。更新後の先頭位置は `(1, 1.5, -1)` です。成分は 0 が x、1 が y、2 が z。scalar の成分は 0 で、省略もできます。
6. `update` を合計 3 回実行してから `remove_expired` を実行すると、寿命が 0 以下の個体を削除します。`spawn` は各集合へ 1 個体を追加します。満杯の集合へは追加されません。
7. 「集合表示を閉じる」の後も「一回実行」と「リセット」を使えます。「リセット」は宣言時の状態へ戻すため集合は空です。3 個体を再作成するには `initialize` を実行してください。
8. `fail` を実行するとゼロ除算の診断と `Faulted` を表示します。「リセット」で診断を消し、`Ready` へ戻ります。「VM を解放」またはプロジェクトを閉じると VM を破棄します。

集合は読み取り専用で、最大 64 個体・8 列です。先頭行と件数で範囲を選べます。列欄はカンマで区切り、空白を入れずに指定してください。空欄なら件数と容量だけを確認できます。行番号はその時点の配列上の位置であり、個体 ID ではありません。削除で順序・番号が変わる可能性があります。

表示は取得時点のコピーです。画面や MCP が VM の列の借用を保持することはありません。更新後の値は再取得してください。MCP 等の別の入口から状態を変えた場合も、表示済みの値が自動更新されることはありません。`Faulted` の集合取得・関数実行はできず、状態と診断の照会・リセット・解放を使えます。

この版の `vm.call` は引数なし・戻り値なしの `export fn name()` に対応します。引数や戻り値を持つ処理は、gLang 側にこの署名の公開関数を用意して呼び出してください。1 回の実行には既定で 1,000,000 fuel の上限があり、超過すると `Faulted` になります。「計測・予算」から 1～1,000,000 に変更できます。この VM 検証操作では、任意の import を必要とするモジュールの読込み、連続実行、描画には対応しません。ゲーム用テンプレートの実行と描画には PIE または GameRunner を使います。gVM の単独 API が持つ機能は、gLang の仕様を参照してください。

読込みには、現在開いているプロジェクトの最新の成功ジョブが必要です。ソース・設定・成果物のハッシュがコンパイル時と異なれば `Conflict` です。再コンパイルしてください。再コンパイルだけでは既存の VM を入れ替えません。読込み失敗時にも既存の VM は残り、読込み元のジョブ表示で区別できます。正常に読み直すと新しい `vm_id` になり、古い ID の要求は `NotFound` になります。VM 内の実行状態は保存しません。

## 実行予算と計測

「計測・予算」を開くと、実行の fuel と次回読込み時の所有量上限を指定できます。fuel は命令や値コピーの費用で、時間の単位ではありません。上限値は予算であり、あらかじめその量のメモリを確保する指示ではありません。

| 入力 | 既定値と許可範囲 | 適用される操作 |
| --- | --- | --- |
| 実行 fuel | 1,000,000。1～1,000,000 | 次の「一回実行」 |
| 所有量上限 | 67,108,864 bytes（64 MiB）。1～67,108,864 | 次の「VM へ読込み」。現在の VM は変更しない |

「計測を更新」で取得時点の数値を表示します。集合表示とは切り替えて使い、パネルを閉じても入力した予算はそのまま次の操作へ適用されます。新しい Editor の起動時には既定値へ戻ります。MCP の個別要求の予算は GUI の入力欄を変更しません。

- **VM のメモリ**：runtime が報告する所有量、準備時ピーク、module・state・集合データ・削除ビット・stack・kernel・管理領域の内訳。内訳の合計は所有量に一致します。集合と stack の予約済み領域は、未使用分も所有量に含みます。ピークは読込み元バイトコードとの共存・検証用一時領域を含む準備時の値です。
- **直前の実行**：関数名、成功・失敗、fuel の予算・消費・残量、`gvm_call` の時間（ns）。関数名の解決、通信、画面更新は計測外です。fuel 不足時に残量が 0 とは限りません。次の処理費用を賄えなければ、残量があっても停止します。
- **直前の集合取得**：集合名・取得範囲・列数、参照した scalar 値のバイト数、値の UTF-8 長、vector の予約領域、string の容量上界、取得と文字列変換の時間。成功した取得一件の値です。容量上界は小文字列格納を含み、実ヒープ確保量ではありません。
- **この画面での整形**：GUI が最後に取得した集合を表へ整形した時間と UTF-16 バッファー容量。MCP からの集合取得では更新しません。

VM の値はプロセス全体のメモリではありません。ホストの数値も表示用コピー一件の範囲であり、JSON、操作履歴、Qt 内部のコピー、OS・アロケーターの管理量、描画時間は含めません。各欄を足してプロセスのメモリ使用量にしないでください。これらは更新と表示の接続を調べる基礎計測で、ゲーム全体の性能保証ではありません。

VM 検証テンプレートの `initialize_batch` は通常の `spawn` を使って 2D / 3D を各 128 個体にします。`update` を実行し、集合表示の先頭行を 0 と 64、件数を 64 にして前半・後半を確認できます。既存プロジェクトのソースは自動更新しません。

失敗と復帰は次の手順で確認できます。

1. VM を読み込み、`initialize_batch` を実行します。実行 fuel を 1 にして `update` を呼ぶと、`FuelExhausted` と `Faulted` を表示します。「計測を更新」で失敗時の予算と残量を確認できます。
2. 「リセット」で宣言時の空の集合と `Ready` へ戻ります。診断と直前の計測も消えます。fuel を 1,000,000 に戻し、`initialize_batch` → `update` を実行してください。
3. 所有量上限を 1 にして「VM へ読込み」を押すと `MemoryLimit` で失敗します。現在の VM は保持されます。上限を 67,108,864 に戻して読み直せます。

`Faulted` になる前の更新が巻き戻る保証はありません。診断と計測を確認してから reset してください。reset 後も画面に表示済みのコピーは取得時点の値なので、必要な欄を再取得します。読込み・reset・解放の後に古い操作 ID を再送すると、以前の結果が返る場合があります。新しい実行には新しい操作 ID を使います。

## プロジェクト設定と制限

設定は UTF-8 JSON、形式版 1 です。新規作成時の `project_id` は自動発行します。

```json
{
  "format_version": 1,
  "project_id": "example-project",
  "sources": ["scripts/game.glang"],
  "scenes": []
}
```

ソースを追加する場合はプロジェクトを閉じ、設定の `sources` を外部エディターで変更してから開き直します。開いたまま設定を変更するとコンパイルを `Conflict` で拒否します。ソース本文の変更には開き直しは不要です。`scenes` にシーンのプロジェクト相対パスを登録します。Editor はその中の一文書を開いて編集します。シーン未登録の既存 VM プロジェクトも使えます。

| 項目 | 利用上の規則 |
| --- | --- |
| パス | 操作の `path` は workspace 基準。`--workspace` を省略した通常 GUI の「開く」「参照…」では、設定ファイルの絶対パスも受け付け、その親フォルダーを基準にする。`sources` / `scenes` はプロジェクト設定基準。手入力の区切りは `/` を使う |
| 参照範囲 | 設定内の絶対パス、`..`、デバイス名、代替データストリーム、シンボリックリンク・ジャンクション経由の参照を拒否する。`.gengine` は Editor 用の予約領域。MCP と `--workspace` を明示した起動では、プロジェクト選択にも同じ制限を適用する |
| 設定 | 最大 64 KiB。既知の項目だけを受け付ける。ソースは 1～64 件、シーン参照は 0～64 件 |
| ソース | 登録済みの異なる `.glang` ファイルから 1 件ずつコンパイル。1 ファイル最大 1 MiB。複数ファイルのリンクは行わない |
| 同時利用 | 1 Editor に 1 プロジェクト・1 コンパイルジョブ。同じプロジェクトを別の Editor が同時に開くと `Busy` |
| 入力と成果物 | 受付時のソース内容・SHA-256・プロジェクト revision を固定。成果物の SHA-256 も結果へ返す。以前のジョブと現在のファイルの対応はハッシュで照合できる |
| ジョブ | 最新 64 件の状態・診断をメモリに保持。期限外・終了後の結果保存は保証しない。コンパイラーの実行は最大 60 秒 |

外部エディターの未保存内容や強制終了時の復旧は管理しません。workspace の参照検証は OS のサンドボックスではなく、他プロセスによる悪意あるファイル差替えからの隔離を保証するものではありません。

## MCP から接続する

対応するプロトコルは **2025-11-25**、接続方式はローカルの stdio です。公式 MCP Python SDK 1.26.0 で接続を確認しています。2026-07-28 版の方式、既に起動した Editor への後接続、複数クライアント、ネットワーク接続には対応しません。

クライアントから次のコマンドと引数で起動します。実行ファイルと作業フォルダーのプレースホルダーを、その環境の絶対パスへ置き換えてください。設定ファイル全体の書式は利用するクライアントに従います。

```json
{
  "command": "<Editor の絶対パス>/gengine_editor.exe",
  "args": ["--mcp", "--workspace", "<作業フォルダーの絶対パス>"]
}
```

MCP 起動では `--workspace` が必須です。同じプロセスに Editor の画面も開きます。標準出力は MCP メッセージ専用です。引数・作業フォルダーの不備など、Editor が検出した起動失敗は標準エラー出力へ説明を返し、ダイアログを表示せずに終了します。

`Editor/platforms/qwindows.dll` が欠落している場合は、Qt の初期化前に診断を返して終了します。配置一式を復元してください。Qt 本体の DLL 欠落は Windows のローダーによる起動失敗となり、Editor の MCP メッセージは返せません。

| tool | 主な引数 | 結果 |
| --- | --- | --- |
| `action.list` | なし | 共通操作の定義。MCP では `items` 配列で返す |
| `action.get_availability` | `action_id`、`version`、`arguments` | 現在の型・状態で操作できるかと理由。ファイルの検証は実行時にも行う |
| `editor.get_state` | なし | 起動 ID、開いているプロジェクトと revision、実行中・最新の job ID、`vm_id`、`loaded_job`（未読込みは null） |
| `project.create` | `path`、任意の `template_name`（`2d` / `3d` / `vm`） | 新しいプロジェクトを作成して開く。テンプレート省略時は従来の `vm`。`path` は新規フォルダー |
| `project.open` | `path` | 設定ファイルを開く。別プロジェクトは先に閉じる |
| `project.close` | `project_id`、`expected_revision`、任意の `choice`、`document_revision` | 現在のプロジェクトを閉じる |
| `compile.start` | `project_id`、`expected_revision`、`source` | `Accepted`、`job_id`、入力版とソースハッシュ |
| `job.get` | `job_id` | `Queued` / `Running` / `Succeeded` / `Failed` / `Cancelled`、診断・成果物 |
| `job.cancel` | `job_id` | 取消し要求後の状態。終了確認は `job.get` で行う |
| `vm.load` | `job_id`、任意の `max_owned_bytes` | 検証済みの成果物から新しい VM を作り、Editor 状態を返す |
| `vm.unload` | `vm_id` | VM を破棄し、Editor 状態を返す |
| `vm.call` | `vm_id`、`function`、任意の `fuel` | 引数なし・戻り値なしの公開関数を一回実行し、`vm_id` と `Ready` を返す |
| `vm.get_state` | `vm_id` | `Ready` / `Faulted` と既存の VM 統計 |
| `vm.get_metrics` | `vm_id` | VM の状態・統計、読込み時の `limits`、`memory` 内訳、`last_call`、`last_inspection` |
| `vm.get_diagnostics` | `vm_id` | 診断の有無、code、元のソース名・行・列・関数 |
| `vm.reset` | `vm_id` | 宣言時の状態に戻し、`vm_id` と `Ready` を返す |
| `vm.inspect_pool` | `vm_id`、`pool`、`columns`、`offset`、`limit` | 件数・容量・取得範囲・指定列の表示用コピー |
| `operation.get` | `operation_id` | 保持範囲内の元の操作の状態・結果を照会する |

変更する tool には共通引数 `_operation_id` が必要です。1～128 文字の ASCII 英数字と `_`、`-`、`.`、`:` を使います。共通引数 `_version` は省略時 1。操作一覧のスキーマにもこれらを含めます。例として、`project.create` の引数は次の形です。

```json
{"path":"MyProject","_operation_id":"create-project-1"}
```

`editor.get_state` で取得した `project_id` と `revision` を `compile.start` へ渡し、返された `job_id` を `job.get` で照会します。`Accepted` はコンパイル成功ではありません。エラーは `isError: true` と構造化した `error`、成功結果は `structuredContent` に返します。文字列の `content` にも同じ結果を JSON で返します。

`vm.load` / `vm.unload` / `vm.call` / `vm.reset` も変更操作です。`fuel` / `max_owned_bytes` の省略または null は既定値です。0 と上限外は `InvalidArgument` で実行前に拒否します。次の例は 5,000 fuel で一回更新します。

```json
{"vm_id":"<VM の ID>","function":"update","fuel":5000,"_operation_id":"update-budget-1"}
```

`vm.get_metrics` の `last_call` は実際に実行した最新の一回、`last_inspection` は成功した最新の集合取得です。未取得・reset 後・読み直した直後は null。引数不正、署名不一致、同じ操作 ID の再送では新しい実行計測を作りません。最新の診断は `vm.get_diagnostics` で取得します。

`vm.inspect_pool` は次の引数を使います。`columns` が空配列なら件数・容量だけを取得します。全項目は公開スキーマどおり明示してください。

```json
{
  "vm_id": "<vm.load が返した ID>",
  "pool": "particles3d",
  "columns": [{"field": "position", "component": 2}, {"field": "life", "component": 0}],
  "offset": 0,
  "limit": 16
}
```

結果の `length` は集合全体の件数、`capacity` は固定容量、`count` は今回の取得数です。`offset` が末尾以降なら末尾へ丸め、`count` は 0 です。各列には `field`、`component`、`type`、`values` があり、`values` は **文字列の配列** です。たとえば f32 の `"-1.5"`、bool の `"true"` として返します。浮動小数点の特殊値も文字列で表し、型は `type` で区別します。これは表示用の値であり、書込みや計算用の生の列 API ではありません。存在しない集合・列・成分は失敗し、途中までの結果は返しません。

同じ `_operation_id` と同じ内容の再送は元の結果・job ID を返し、内容違いは `Conflict` です。最新 1,024 件かつ完了後 15 分未満の両条件を満たす操作を保持します。保持範囲外やプロセス再起動後の一回実行は保証しません。MCP の request ID と、この再送管理用 ID は別です。

MCP の取消し通知はまだ開始していない要求を対象とし、受付済みコンパイルの取消しには `job.cancel` を使います。EOF・出力切断・通信容量超過では新規受付を止め、未開始要求を実行せずに終了します。1 メッセージは最大 1 MiB、入出力キューはそれぞれ最大 128 件かつ 2 MiB、JSON の深さは最大 32 段です。操作引数にはさらに 64 KiB・16 段、保存する操作結果には 256 KiB の上限があります。

## 試験とライセンス

```powershell
ctest --preset release
```

全体試験には描画・音声部品の実機接続も含むため、対応 GPU と音声出力先が必要です。Editor の試験だけなら `ctest --preset release -L editor` を使えます。通信試験は実際の Editor プロセスを起動するため、Windows のデスクトップ環境が必要です。

Editor は Qt 6.12.0 の Core / Gui / Widgets と Windows プラットフォームプラグインを LGPL v3 で動的リンクします。「ヘルプ → Qt について」でも使用を確認できます。Qt と同梱部品の原文は [Qt のライセンス・帰属表示](../../ThirdParty/Qt/NOTICES.txt)、対応ソースと差替え方法は [SOURCE.txt](../../ThirdParty/Qt/SOURCE.txt) を参照してください。Qt の改変や、その改変をデバッグするためのリバースエンジニアリングを妨げる制限は設けません。

バイナリーを再配布する場合は、Qt の対応ソースも同じ配布場所から取得できるようにしてください。`SOURCE.txt` の SHA-256 と一致する Qt Base アーカイブを取得し、構成時に `-DGENGINE_QT_SOURCE_ARCHIVE="<アーカイブのパス>"` を指定すると、`Editor/Sources/` へ配置し、移設・差替えの受入試験も登録します。通常のビルドでソースを自動ダウンロードしません。ライセンス条件は [Qt の LGPL 説明](https://www.qt.io/development/open-source-lgpl-obligations)も参照してください。

JSON の解析に nlohmann/json 3.12.0 を使用しています。[MIT License](../../ThirdParty/nlohmann/LICENSE.MIT) を同梱します。ソースは [公式 v3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0) に基づき、通常のビルドでダウンロードは行いません。

## 2D / 3D サンプルを遊ぶ

`cmake --build --preset release` で構築した後、`out/build/Release/gengine_runner.exe` を直接起動します。引数なしでは 2D、`--game 3d` を渡すと 3D になります。同じフォルダーに `content` と `backends` が必要です。`content` にモジュール・シーン・画像・メッシュ・効果音を配置します。実行時に Editor やコンパイラーは使いません。

通常ファイルの `content` がある 2D サンプルは、`gengine_runner.exe` のダブルクリックでも起動できます。実行するのは CMake ビルド時に配置されたサンプルで、Editor でコンパイルしたプロジェクトの変更は自動反映されません。保存シーンと成果物を使うには、後述の `--project` / `--scene` / `--module`、または Editor のビルドとスタンドアロン実行を使います。pack だけを置いた `Games/` では `Play-2d.cmd` / `Play-3d.cmd` を起動してください。

- Enter：スタート／結果からの再試行。
- 2D は WASD で XY 移動、Space で +Y 方向へ射撃。
- 3D は A/D で X 方向、W/S で -Z/+Z 方向、Q/E で下降／上昇。Space で -Z 方向へ射撃。
- P：一時停止／再開。フォーカスを失った後も明示的に再開します。
- Escape：スタート画面へ戻ります。

敵の耐久値が 0 になるとクリア、自機が 0 になると失敗です。同じ更新で双方が 0 になった場合は失敗になります。帰還・再試行では入力・弾・耐久値・音声を初期化します。永続セーブはありません。

## 通常ファイルと pack を切り替える

ビルドすると `out/build/Release/content/` と圧縮済みの `out/build/Release/samples.bpack` を生成します。次のどちらでも同じサンプルを起動できます。`--game 2d` に替えると 2D になります。相対パスは起動時の作業ディレクトリを基準にします。引数なしの `content` は実行ファイルと同じ場所から解決します。

```powershell
./out/build/Release/gengine_runner.exe --game 3d --assets ./out/build/Release/content
./out/build/Release/gengine_runner.exe --game 3d --pack ./out/build/Release/samples.bpack
```

`--assets` と `--pack` は同時に指定できません。pack 起動時は、実行ファイル・`backends`・指定した pack があれば動作し、通常ファイルの `content` は不要です。Editor のプレビューと PIE、`--project` による保存シーン起動は、標準素材を実行ファイルと同じ場所の `content/assets`、明示した `visual` / `audio` をプロジェクトのルートから読みます。`--assets` / `--pack` 起動では、シーン内の論理パスを選択した供給元から解決します。弾・クリア音・失敗音には標準素材が必要です。Editor の「ビルド」からも pack を作成できます。

| 論理パス | 内容・制約 |
| --- | --- |
| `shooter2d.glbc` / `shooter3d.glbc` | 採用 gLang でコンパイルしたゲーム用モジュール。各 64 MiB 以下 |
| `scenes/shooter2d.json` / `scenes/shooter3d.json` | シーン形式 1。各 1 MiB 以下。選択した次元と一致すること |
| `assets/sprite.png` | 2D の RGBA 画像へ変換する PNG。16 MiB 以下、各辺 4096 以下かつ総画素数 4,194,304 以下 |
| `assets/sphere.gmesh` | 3D の `GMESH01` メッシュ。4 MiB 以下、頂点 65,536 以下、三角形 index 393,216 以下 |
| `assets/shot.wav` / `hit.wav` / `clear.wav` / `fail.wav` | 効果音。各 1 MiB 以下、aEngine が対応する PCM16 RIFF/WAVE |

pack 自体は 128 MiB 以下です。空ファイル、欠落、破損、未対応形式、バイトコードの版不一致は起動時に診断を表示します。圧縮エントリも展開後サイズを先に検査し、pack の内容は CRC32 で検証します。別の素材への自動切替えは行いません。通常ファイルの論理パスに絶対パス、親参照、リンク経由の参照は使えません。音声は全体をメモリへ読み込み、ストリーミングには対応しません。

サンプル素材を編集した後は同じ論理パスを保ち、BinPack CLI で pack を再作成できます。原本と生成方法は `samples/assets/` と `samples/generate_assets.py` です。次の `--overwrite` は指定した出力 pack を上書きします。

```powershell
./out/build/components/BinPackLib/Release/BinPack.exe pack ./out/build/Release/samples.bpack --base ./out/build/Release/content --overwrite --compress ./out/build/Release/content
```

## Editor 内で試遊する

1. `out/build/Release/Editor/gengine_editor.exe` をダブルクリックし、「参照…」から 2D は `samples/shooter2d/project.json`、3D は `samples/shooter3d/project.json` を選んで開きます。ファイルの種類は初期状態の「gEngine プロジェクト」で表示できます。画面上部の「作業フォルダー」が、選んだ設定ファイルのフォルダーに変わります。
2. シーン一覧の `scene.json` を「シーンを開く」で開き、`game.glang` をコンパイルします。成功後に実行方式「PIE」で「実行」を押します。次元はシーンから決まります。左下で試遊条件を選べます。
3. 埋め込みビューをクリックしてゲーム入力を渡します。操作は単独実行と共通です。
4. 「停止 / 再開」で一時停止状態を変更し、「再初期化」で選択済み条件をやり直します。「通常開始」はプリセットを解除します。「試遊終了」で編集へ戻ります。

パス欄から開く場合は、画面上部の作業フォルダーを基準にし、先頭の `./` を省いて入力します。「参照…」でサンプルを開いた後なら `project.json` です。`InvalidArgument: invalid or reserved project path component` が出た場合は、入力や設定内の参照に `.` や `..` の要素がないか確認してください。`--workspace` を明示した起動では範囲外のプロジェクトを開けないため、範囲外を選ぶ場合は Editor を閉じて通常起動し、「参照…」を使ってください。

条件は通常開始、プレイ開始、クリア直前、失敗確認、弾幕負荷です。通常開始以外は開発用の初期条件です。結果を強制設定せず、実際の更新と接触で勝敗を決めます。試遊中のコンパイルやプロジェクト変更は拒否します。ソース・設定・成果物がコンパイル後に変わった場合は再コンパイルしてください。

MCP でも `play.start`（`job_id`、0～31 の `preset`（モジュールの宣言範囲内）、任意の `game`（`2d` / `3d`）と文書を開いている場合に必須の `document_id`、`expected_revision`）、`play.pause`（`play_id` と `paused`）、`play.reset`（`play_id`、任意の `normal`）、`play.stop`（`play_id`）を使えます。変更操作には `_operation_id` を指定します。`play.get_state` は実行中の `game`・phase・tick・勝敗・診断・VM 所有量・直近更新の計測、発射見送り件数と破棄時間を返します。文書を開いていない場合だけ、文書指定の省略を受け付け、`game` 省略時は従来の 2D 既定条件を使います。文書の次元とモジュールの関数・列の型が一致しない場合は開始せず、診断を返します。手動検査用 VM と試遊用 VM は別です。

`viewport.capture` に `play_id` を渡すと、その試遊ビューの 640 × 360 PNG と状態・入力 revision を取得できます。編集中は `play_id` の代わりに `document_id` と `expected_revision` を渡すと、VM を動かさない編集プレビューを取得できます。結果は `document_id` と `input_revision` を含みます。画像取得ではゲームを進めません。画像本体は操作履歴に保存せず、同じ操作 ID で画像を再要求すると `CaptureExpired` になります。新しい画像取得には新しい要求を使ってください。

3D は右手系（X 右・Y 上・Z 手前）で、シーンのカメラを使います。球の見た目と当たり判定に XYZ 全成分を使い、描画の前後関係は深度で判定します。2D は PNG、3D は単位球のメッシュを読み、効果音は短い PCM16 WAV を使います。Windows x64、D3D12 と音声デバイスが必要です。ゲーム更新は 60 Hz、描画一回につき最大 4 更新です。大きな停止時間は追いつくための更新へ無制限に回さず、超過時間を破棄します。


## 初期シーンを編集・保存する

シーンを開くと、左に対象一覧、中央に初期配置のプレビュー、右に選択した対象のプロパティを表示します。「ゲーム設定」では移動範囲、HP、速度、自機の射撃間隔を編集します。対象では初期位置、敵の射撃間隔、2D の表示範囲、3D のカメラ位置・注視点・視野角を編集できます。ベクトルは各成分を入力し、「値を適用」でまとめて確定します。「VM 詳細・計測」で既存の VM 操作・集合・計測画面へ移り、「シーン編集へ戻る」で編集を再開できます。不正な入力は確定せず、入力欄の近くと「診断」に理由を示します。外部から値が変わった場合は「値を読み直す」で最新値を取得してください。

対象は `player_spawn`、`enemy_spawn`、`game_camera` が各一件までです。「追加」「削除」で構成を変えられ、不足した状態も編集・保存できます。PIE には三種類すべてが必要です。素材参照は `null` またはプロジェクトから解決する論理パスです。未割当ては同梱の PNG・球メッシュ・WAV を使います。プロパティ欄は参照を表示し、変更は「素材一覧」から行います。

2D ビューでは、対象を左クリックして選択します。選択したプレイヤー・敵の赤い水平ハンドルで X、緑の垂直ハンドルで Y、黄色い四角で平面内を移動します。ハンドルは表示倍率を変えても同じ画面上の大きさです。表示領域の端では、ハンドルを内側へ向けて操作できる範囲を確保します。位置は移動範囲の境界で止まり、離すと一件の編集として確定します。ドラッグ中は数値欄に仮位置を表示し、数値編集を止めます。Esc、フォーカス・キャプチャーの喪失、サイズ変更、文書切替え、試遊開始、他の文書編集では仮操作を取り消します。数値入力と MCP で範囲外の値を指定した場合は拒否します。

プレイヤー・敵を追加するには、種類を選んで「追加」を押し、ビュー内の移動範囲をクリックします。Esc で追加待ちを取り消せます。既にある種類は追加できません。カメラは「追加」で作成し、表示枠の線から選択してプロパティ欄で表示サイズを変更します。2D カメラに位置はありません。削除は「削除」またはビューにフォーカスがある場合の Delete キーを使います。未適用の数値入力がある場合は「値を適用」か「値を読み直す」を済ませてから選択・配置を操作してください。

ホイールはカーソル位置を基準に拡大縮小、中ボタンドラッグは表示範囲の移動です。「全体表示」で移動範囲とゲーム用カメラ枠を収めます。ホイールの表示高さは全体表示の 1/100～100 倍です。編集用の中心・表示高さはゲーム用カメラと別で、文書・保存・履歴へ入りません。保存・Undo / Redo・同じ文書の再読込み・PIE 復帰では保持し、別文書へ切り替えると初期化します。ビューの余白は選択・配置に使いません。

3D ビューも左クリックで選択し、赤い X・緑の Y・青い Z ハンドルで一軸ずつ移動します。手前の球を優先して選び、対象本体のクリックだけでは移動しません。視線とほぼ重なる軸は非表示になるため、右ボタンドラッグで視点を回してから操作してください。中ボタンは画面に沿ったパン、ホイールはカーソル位置を基準とするズームです。編集用カメラは正射影で、右手系の X・Y・Z を使います。ゲーム用カメラの透視投影と独立しています。全体表示は移動範囲・対象・カメラを示す線を収めます。

3D のプレイヤー・敵を追加するときは、Y=2 の水平面をクリック位置から求めます。2 が移動範囲外なら、その範囲内へ収めた高さを使います。真横から見て水平面と視線が平行な場合や、交点が移動範囲外の場合は追加できません。カメラは位置・向き・画角を示す短い錐台の線から選択し、プロパティ欄で編集します。カメラには移動ハンドルを表示しません。3D も一回のドラッグが一件の履歴となり、仮位置・取消し・競合・未適用入力の扱いは 2D と共通です。複数選択、スナップ、回転・拡縮ハンドルはありません。

「元に戻す」「やり直す」は確定した編集を対象にします。同じ値の適用では履歴を増やしません。履歴は最大 256 件かつ 8 MiB で、Undo 後の新しい編集は Redo 側を破棄します。パス横の `*` は保存内容との差がある印です。Undo で保存内容に戻ると消えます。選択・プレビュー・保存・試遊は履歴へ入りません。

「保存」は現在開いている登録済みシーンだけを書き換えます。別名保存はありません。外部変更は `Conflict`、書込み・置換失敗は `IoError` になり、編集内容と履歴を保持します。「再読込み」は候補の検証後に置き換え、履歴を初期化します。不正なファイルを読んでも現在の文書は失いません。文書切替え・再読込み・プロジェクト終了・ウィンドウ終了時は、未保存の変更を保存／破棄／中止から選びます。

正常な MCP 切断では未保存内容をプロジェクトの `.gengine` 復旧領域へ残します。同じ文書を開いた後に「復旧入力」から復元・破棄できます。復元内容は未保存として開き、保存済みファイルを自動上書きしません。復旧の基準ファイルが外部で変わっていた場合は `Conflict` です。強制終了・電源断の自動復旧は保証しません。

PIE は開始時の文書スナップショットを使います。試遊中は文書編集・履歴移動・切替え・再読込みを `Busy` で拒否します。再試行でも同じ初期条件を使い、「通常開始」でプリセットの上書きを解除します。「試遊終了」で編集へ戻り、VM の弾・被弾・経過時間は文書へ書き戻しません。

## 素材の登録・割当て・参照修正

プロジェクト内に素材原本を置き、シーン編集の「素材一覧」を開きます。プロジェクトからの相対パスを入力するか「参照…」で選び、「登録」を押してください。登録するのは論理パスだけです。原本の移動・上書き・形式変換は行いません。プロジェクト外のファイルは、先に利用者がプロジェクト内へ配置してください。

対応する素材は PNG（各辺 4096 以下かつ総画素数 4,194,304 以下、入力 16 MiB 以下）、GMESH01 メッシュ（4 MiB 以下）、PCM16 WAV（44.1 / 48 kHz、mono / stereo、1 MiB 以下）です。形式を検証してから登録し、一覧には寸法・頂点数・音声形式と、欠落・破損・未対応形式の理由を表示します。「再検証」で原本の外部変更を確認できます。登録数の上限は 256 件。プロジェクト設定の省略可能な `assets` 配列へ保存し、既存の形式 1 のプロジェクトも開けます。登録後はプロジェクト revision が変わるため、試遊前に再コンパイルしてください。

MCP では `asset.list` で一覧を再検証し、`asset.register` に `project_id`、`expected_revision`、`path` を渡して登録します。登録の前に `editor.get_state` から現在の revision を取得してください。登録済みの同じパスは重複追加せず、外部編集や保存失敗では旧設定を保ちます。

シーンでプレイヤーまたは敵を選んで「素材一覧」を開き、一覧の素材と「描画素材」または「音声素材」を選び、「選択素材を割当て」を押します。「標準素材に戻す」は選んだ欄を `null` に戻します。割当てはシーンの変更なので、取消し・やり直し・保存・復旧の対象です。カメラには割り当てられません。

| 対象・欄 | 対応素材と動作 |
| --- | --- |
| 2D の描画 | PNG。既存の表示サイズと色を使って対象を描画する |
| 3D の描画 | GMESH01。既存の配置・倍率・色を使って対象を描画する。接触判定の半径は変わらない |
| プレイヤーの音声 | WAV。射撃時の効果音 |
| 敵の音声 | WAV。敵弾がプレイヤーに命中したときの効果音 |

参照切れ・破損・形式違いは、対象 ID、`visual` / `audio`、パス、理由を表示します。原本を修復して「再検証」するか、別の有効な素材を割り当てるか、標準素材へ戻してください。編集中は欠落した描画素材を標準表示に置き換え、文書の読込み・他の値の編集・保存・履歴操作を続けられます。PIE と GameRunner は不正な割当てを理由付きで拒否します。新しく割り当てる素材は形式まで検証し、失敗時は文書と履歴を変更しません。

MCP の割当ては `scene.set_properties` の `properties.visual` / `properties.audio` にパスまたは `null` を渡します。有効なプロジェクト内の素材なら、登録一覧への追加前でも指定できます。`asset.list.references` は現在のシーンの参照と診断を返します。登録一覧と参照診断は「再検証」時に原本を読み直し、通常の画面更新では繰り返しデコードしません。

## シーン・履歴の MCP 操作

公開ツールは計 47 件です。`editor.get_state.document` または `document.get_state` で、文書 ID、revision、dirty、次の `undo_id` / `redo_id`、現在内容を取得できます。プロジェクトの revision と文書の revision は別です。シーンの編集だけではコンパイル成果物は失効しません。

| tool | 主な引数・動作 |
| --- | --- |
| `document.open` | 登録済みの `path`。切替え時は任意の `expected_revision`、`choice` |
| `document.get_state` | 引数なし。文書・履歴・復旧入力の有無 |
| `document.save` / `document.reload` | `document_id`、`expected_revision`。再読込みには任意の `choice` |
| `document.recover` | 文書 ID・revision と `choice: inspect / restore / discard` |
| `scene.find_objects` / `scene.get_properties` | `document_id`、任意の `object_id`、`kind`、`limit`、`cursor`。一覧は既定 100 件、最大 1,000 件 |
| `scene.create_object` | 文書 ID・revision、`kind`、任意の `properties`。生成された ID は `changed_ids` で返す |
| `scene.delete_objects` | 文書 ID・revision、`object_ids` |
| `scene.set_properties` | 文書 ID・revision、`object_id`、変更する `properties` |
| `scene.set_settings` | 文書 ID・revision、変更する `properties` |
| `scene.apply_batch` | 文書 ID・revision、最大 256 件の `edits`。全件成功時だけ一つの履歴として確定 |
| `history.undo` / `history.redo` | 文書 ID・revision と、次に戻す／やり直す `history_id` |
| `viewport.get_state` | 文書 ID・revision。編集ビューの `center`、`height`、`selected_id`、仮操作中の `transient`。3D は `yaw_degrees`・`pitch_degrees` も返す |
| `viewport.set_view` | 文書 ID・revision、ゲーム座標の中心 XY / XYZ を表す `center`、表示高さ `height`。3D は任意の `yaw_degrees`・`pitch_degrees` |
| `viewport.reset_view` | 文書 ID・revision。移動範囲とカメラ枠の全体表示 |

表の「文書 ID・revision」は `document_id` と `expected_revision` です。変更操作は `_operation_id` も必要です。読み取り以外の `document.*` も変更操作です。古い revision・一覧カーソル・履歴 ID は `Conflict`、同値の設定は `status: NoChange` です。Undo / Redo でも revision は増えます。未保存変更のある切替え等は `UnsavedChanges` と `current_revision`、`choices` を返します。現在の版と `choice: save / discard / cancel` を指定し、新しい操作 ID で明示的に再要求します。`project.close` ではプロジェクトの版に加え `document_revision` を指定します。

```json
{"document_id":"<文書 ID>","expected_revision":3,"properties":{"move_speed":6.25},"_operation_id":"speed-1"}
```

`properties` の数値は JSON の数値として渡します。HP は整数、速度・間隔は正の有限値、ベクトルは次元と一致する成分数が必要です。実数は f32 へ揃えて検証します。ID と kind は変更できません。バッチの各要素は `edit: settings / set / create / delete` と、種類に応じた `object_id`、`kind`、`properties` を指定します。途中失敗では一件も反映しません。

`viewport.set_view` の `center` は 2D で二つ、3D で三つの有限数値です。各成分の絶対値は 1e30 以下、`height` は 1e-6～1e30 の有限値で、横の表示範囲は高さの 16/9 倍です。3D の水平角 `yaw_degrees` は絶対値 1e6 以下、仰角 `pitch_degrees` は -85～85 度です。角度を省略すると現在値を保持し、全体表示では 45 / 30 度へ戻します。2D で角度を指定すると拒否し、状態取得では角度を `null` で返します。

これら三操作は停止中のシーンを対象とします。表示変更は `_operation_id` を必要としますが、文書の revision・dirty・履歴を変更しません。MCP の配置変更は選択に依存せず、既存の `scene.*` へ対象 ID とゲーム座標を指定します。ドラッグ・追加待ち・パン・視点回転中の `viewport.capture` は `Busy` です。確定または取消し後に取得してください。

## 保存したシーンを GameRunner で起動する

```powershell
./out/build/Release/gengine_runner.exe --project <project.json> --scene scene.json --module <コンパイル成果物.glbc>
```

`--scene` は設定に登録済みのプロジェクト相対パスです。プロジェクトとモジュールには絶対パスも使え、別の作業ディレクトリから起動できます。ゲームの次元はシーンから決まります。Editor で保存していない変更は反映されません。引数なし／`--game 2d|3d` は引き続き同梱の既定条件を使います。Editor のスタンドアロン起動では、下記のビルドで作成した pack と実行物を使用します。

ゲーム用モジュールには既存の export に加え、`configure_world(start, enemy, minimum, maximum)` と `configure_rules(player_health, enemy_health, speed, interval, hostile_interval)` が必要です。前者は 2D で四つの vec2、3D で四つの vec3、後者は i32・i32・f32・f32・f32 を受け取り、どちらも戻り値なしです。ホストは VM reset → 両設定関数 → `initialize(preset)` の順に呼びます。旧サンプルのバイトコードは再コンパイルしてください。API 不足・不一致では `recompile ... gEngine 0.0.8` を含む理由を表示します。gLang の言語仕様、バイトコード形式、C ABI は変更していません。


## Editor からビルドして単独実行する

1. ゲームのプロジェクトとシーンを開き、ソース一覧で対象を選びます。「ビルド」は選択したソースと開いているシーン一件を使います。外部エディターのソースは先に保存してください。
2. 「ビルド」を押します。シーンに未保存の変更があれば、保存して続けるか中止するかを選びます。ビルドは保存済みデータを使います。
3. 状態欄で Validating（文書・素材検証）、Compiling、Packing、Deploying の進行を確認します。失敗した段階と理由は診断欄に表示されます。「取消し」は子プロセスの終了確認後に完了します。
4. 実行方式を「スタンドアロン」にして「実行」を押すと、同じ手順で新しくビルドし、そのジョブの成功後だけ GameRunner を別ウィンドウで起動します。通常の開始画面からプレイできます。「試遊終了」で停止し、ウィンドウを閉じた場合は終了コードを Editor に表示します。
5. 「PIE」では従来どおり、先に「コンパイル」を成功させてから「実行」を押します。こちらは編集中のシーンと「試遊条件」のプリセットを使います。スタンドアロンにはプリセットを渡しません。

完成した成果物はプロジェクト内の `.gengine/builds/<job ID>/` にあります。`gengine_runner.exe`、`game.bpack`、`backends/`、使用ライブラリーの表示、`build.json` をまとめて保持してください。`build.json` は入力と実行物のハッシュを記録します。Editor からの起動前に欠落・改変を検出すると起動を拒否します。ソース、コンパイラー、Editor、Qt はゲーム実行用フォルダーには含まれません。Windows の必要ランタイムと描画・音声環境は必要です。

成功したビルドは job ID ごとのフォルダーへ保存されます。以前の成功フォルダーを上書きせず、失敗・取消しのジョブに成果物を付けません。失敗後に旧ジョブを「今回の成功」として起動する要求も拒否します。不要になった成功フォルダーは、使用していないことを確認してフォルダー単位で整理できます。ジョブの照会履歴はコンパイルと合わせて最新 64 件です。

ビルドは標準素材とシーンから明示参照する素材を含めます。登録一覧にあるだけで未使用の素材は含めません。pack 内ではプロジェクト素材を `project/` 以下に配置し、ビルド用シーンだけの参照を変換します。元の保存シーンは変えず、標準素材と同名のファイルも区別します。コピーするシーンと素材の合計、生成される pack 自体はそれぞれ 128 MiB 以下です。個別形式の上限は素材の読込み規則に従います。コンパイラーと pack 作成はそれぞれ 60 秒で打ち切ります。

| MCP tool | 引数と結果 |
| --- | --- |
| `build.start` | `project_id`、プロジェクトの `expected_revision`、登録済みの `source` と `scene`。未保存なら `choice: save` と `document_revision`、または `choice: cancel`。受付時は `Accepted` と `job_id` |
| `job.get` / `job.cancel` | `job_id`。`kind: Build`、入力版、`scene`、`game`、`document_revision`、`phase`、診断、成功時の `artifact` と `artifact_hash`、入力全体の `input_hash` を返す |
| `standalone.start` | 最新の成功したビルドの `job_id`。実行中の `run_id` と `process_id` を返す |
| `standalone.get_state` | 引数なし。最後の実行状態、`exit_code`、終了時の診断を返す |
| `standalone.stop` | 現在の `run_id`。停止要求時は `Stopping`、終了確認後に `Stopped` |

読取り以外には `_operation_id` が必要です。`standalone.start` の `Running` はプロセスを生成した状態であり、ゲーム初期化の成功保証ではありません。起動後の失敗は `Failed` と終了コード・診断、通常終了は `Exited` で確認します。Editor 終了や MCP 切断時にも、その Editor が起動した子を回収します。起動中のプロジェクト切替え・シーン編集・ビルドは `Busy` です。

成果物を直接起動する場合は、対象フォルダーで次のように指定します。3D は `--game 3d` に変更してください。別の作業ディレクトリから実行する場合は、EXE と pack のパスを明示します。

```powershell
./gengine_runner.exe --game 2d --pack ./game.bpack
```


## ゲームと Editor の性能を計測する

Editor の「計測 → ゲーム性能」で計測パネルを開きます。PIE を開始し、条件を選んで「開始」を押してください。30 フレームの準備運転後、120・300・600 フレームのいずれかを収集します。「結果を更新」で現在の結果を表示し、「中止」で収集済みの標本を保持したまま止めます。上限に達すると自動的に Complete になります。パネルの開閉はゲーム画面を隠しません。フォーカスを外すと従来どおり PIE は一時停止するため、ゲーム側にフォーカスを戻し、必要に応じて再開してください。

結果は平均・p95・p99 を ms で表示します。p95 は小さい順に並べた標本の 95% が収まる値です。未取得の値は「未計測」と表示します。結果の整形による負荷を避けるため自動更新しません。表示要求自体の費用は次の取得結果に反映されます。PIE を停止するとその計測結果も破棄するため、必要な結果は停止前に取得してください。

| MCP tool | 引数と動作 |
| --- | --- |
| `performance.start` | 現在の `play_id` と `config: {warmup: 30, samples: 300}`。`warmup` は 0～600、`samples` は 1～3,600。収集中の再開始は Busy |
| `performance.get` | 現在の `play_id`。状態、条件、収集数、時間分布、資源量を返す。収集済みの標本を変更しない |
| `performance.stop` | 現在の `play_id`。Warming / Collecting を Stopped にし、部分結果を保持する |

開始と中止には `_operation_id` が必要です。古い `play_id` は拒否します。計測中は GPU 読戻しを伴う `viewport.capture` を拒否するため、画像は計測の終了後に取得してください。結果の状態は Idle、Warming、Collecting、Complete、Stopped です。GUI と MCP は同じ収集器を使用します。

GameRunner の実行コストは、単独プロセスで計測します。次の例は通常のゲームを開始し、結果を新しい JSON ファイルへ書きます。`--metrics` の出力先は、既存ディレクトリー内の未作成ファイルを指定してください。既存ファイルは上書きしません。

```powershell
./gengine_runner.exe --game 2d --metrics ./performance.json --warmup 30 --samples 300
```

`--assets`、`--pack`、保存済みプロジェクトからの起動にも併用できます。`--warmup` と `--samples` は `--metrics` と組み合わせて指定し、省略時は 30 / 300 です。計測は自動入力や試遊プリセットを適用しません。通常どおり Enter で開始して操作します。収集の完了後もゲームは続きます。途中でウィンドウを閉じた場合は Stopped の部分結果を書きます。正常終了せず強制終了した場合の結果保存は保証しません。

| 時間の項目 | 対象 |
| --- | --- |
| `frame` | 更新・接触・反映・音声・描画を実行する処理時間。イベント待ちは含まない |
| `interval` | フレーム処理の開始間隔。イベント待ちを含むため `frame` と区別する |
| `vm` / `contact` / `apply` | フレーム内の VM 更新、接触判定、命中・削除・状態反映。固定更新が複数あれば合計、更新がなければ 0 |
| `conversion` | VM の列から描画配列への変換、カメラ・HUD・描画要求の準備 |
| `render_present` | 描画部品の呼出し。GPU 資源の再利用待ちと Present 待ちを含む CPU 経過時間 |
| `gpu` | 完了済みの GPU 描画時間。同じフレーム ID を重複集計しない。CPU と並行するため足し算しない |
| `audio` | 再生要求・更新・終了通知回収のホスト側処理。音声デバイスの実際の発音時間ではない |
| `editor_callback` | Editor のタイマー内の状態照会・更新など。Qt 全体の描画時間ではない |
| `metrics_display` | 計測パネルの結果取得・整形・表示要求。Qt の後続の描画処理は含まない |
| `scheduler_lateness` | フレームの予定時刻から UI スレッドで通知を処理するまでの遅延 |
| `present_cpu` | rEngine の Present 呼出しの CPU 経過時間。実際の表示完了までの時間ではない |
| `dxgi_present_interval` | DXGI が連続した提示を同じ vblank との対応で報告した場合の間隔。欠測・重複・不連続を含めない |

JSON の時間単位は ns、`timings` の標本数は項目ごとに異なります。フェーズ別のフレーム数、停止中のフレーム数、実際の更新 tick 数、描画領域と VSync 条件も `counters` に入ります。CPU フレーム処理だけから FPS を求めないでください。

PIE と Runner の描画要求は 60 Hz の予定で送ります。処理が追いつかない場合は未処理の要求を一件へまとめ、過去の描画を連続実行しません。ゲームの固定更新と描画開始間隔の時計は分かれており、Pause やリセットで `interval` の計測を短縮しません。最小化中も入力・表示設定の確認期限を処理し、GPU 描画を休止します。静止プレビューは変更時だけ描画します。

`frame_timer_high_resolution` は高分解能の待機タイマーを利用できた場合に 1。利用できない古い Windows では通常の待機タイマーを使い、同じ精度は保証しません。`frame_requests_coalesced` は現在の時計の開始以降にまとめた通知数です。`presentation_timing_supported` はバックエンドの計測拡張の有無、`presentation_queries` は計測区間の照会数です。`presentation_unavailable`・`presentation_disjoint`・`presentation_duplicates`・`presentation_gaps` は、未取得、不連続、重複、間隔に換算できない更新の件数です。理由の一部は重複するため合計しません。

`presentation_count`、`presentation_refresh_count`、`presentation_sync_refresh_count`、`presentation_sync_qpc`、`presentation_qpc_frequency` は最後の生の観測値です。表示側の標本が 0 件でも、表示遅延が 0 だったことを意味しません。複数モニターなどでは DXGI の統計を信頼できない場合があり、光学的な表示遅延の測定にも代用できません。取得のための追加待機は行いません。詳細は [DXGI の提示統計](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics) を参照してください。

Faulted からの計測開始は拒否します。計測中に Faulted になった場合は、結果取得時に部分結果を Stopped として確定します。Faulted では VM の列を参照できません。`vm_pool_live_valid: 0` のとき、live の値は未取得です。

メモリは VM の所有量・ピーク・列の使用量と予約量、ホストの配列容量、描画の CPU / GPU 資源量、PCM、素材の読込み・デコード・転送時の一時量を区別します。内訳と合計が同時に含まれるため、全項目の単純合計は使用量になりません。`process_begin` / `process_end` は計測開始／取得または終了時点の Private Bytes、Working Set、ハンドル・GDI・USER 数などです。CPU 時間はプロセス全スレッドの累計で、両時点の差を取ります。Working Set のピークはプロセスの生存期間全体を対象とします。VM の予約量一定や追加確保なしは、OS・Qt・ドライバーを含むプロセス全体の確保なしを意味しません。


## ゲーム内の選択と日英表示

選択を使うモジュールは次の export をすべて公開します。宣言がない従来の 2D／3D モジュールは従来どおり動作します。版の不一致、型の不一致、不足した関数は読込み時に拒否します。

| export | 型と意味 |
| --- | --- |
| `interaction_api_version` | `() -> u32`。値は 1 |
| `interaction_request` | `() -> u32`。0 は選択なし。選択ごとに異なる非 0 の要求番号 |
| `interaction_count` | `() -> u32`。要求中の候補数、1～3 |
| `interaction_option` | `(index: u32) -> u32`。0 始まりの候補位置から表示 ID を返す。候補内で重複させない |
| `interaction_choose` | `(index: u32) -> bool`。公開した候補を適用して true を返し、同じ要求を終了する |

読取り関数は状態を変えず、指定された実行予算内で終了してください。ホストは選択中の戦闘更新と経過時間の蓄積を止めます。選択は 1～3 キーで行い、選択後に入力を解除します。Pause 中・範囲外・古い要求の決定は適用しません。公開済み候補をゲームが拒否する、候補数が不正、決定済み要求を残す場合は Runtime エラーとして停止します。Reset は選択とラン内状態を破棄します。

この API を使うプロジェクトは `ui/game.json` と `ui/font.png` を同梱します。Editor のビルドにも含まれます。JSON の `version` は 1、`languages` は `ja` と `en`。各言語に `title`、`start`、`controls`、`choose`、`paused`、`clear`、`fail`、`back` の文字列と、表示 ID を添字に持つ `options` 配列を用意します。停止画面の操作案内は任意の `paused_controls` で分けられます。省略時は `back` を使います。ゲーム内で F1 を押すと日英を切り替えます。設定の永続保存・任意メニュー・マウス／ゲームパッドの選択はこの版にはありません。

フォント atlas は横 32 セル、1 セル 48 × 64 px、先頭セルは白い単色です。`glyphs` の各 BMP 文字を次のセルから並べ、`advances` に各文字の送り幅（0 より大きく 48 以下）を指定します。最大 1023 文字で、縦のセル数は必要数から決まります。UTF-8 の不正入力、制御文字、補助面の文字、欠字、atlas 寸法の不一致を許容しません。文字画像は起動時に一度アップロードし、表示内容が変わったときに配置を更新します。

C++ の `PlayWindow::Capture(width, height)` では最大 3840 × 2160 の画像を取得できます。引数省略時と既存 MCP は従来の 640 × 360 です。画像取得は戦闘を進めません。これはウィンドウの解像度設定・表示モード切替とは別の機能です。


## 複数個体とゲーム固有の HUD

既存の固定敵接続に加え、任意の敵や拾得物を `actors` 集合から描画・判定できます。`actor_api_version() -> u32` が 1 を返す場合に有効です。宣言がない既存ゲームは固定敵の動作を維持します。

| 列 | 型と条件 |
| --- | --- |
| `previous`、`position` | 2D は `vec2`、3D は `vec3`。一更新の始点と終点。各成分は有限値、絶対値 1,000,000 以下 |
| `color` | `vec3`。RGB 各成分 0～1 |
| `radius` | `f32`。0 より大きく 16 以下。描画の半径と接触候補の半径 |
| `angle` | 有限の `f32`。2D の描画回転角、ラジアン |
| `visual` | `u32`。0 は標準素材、1 はシーンの `enemy_spawn` に割り当てた描画素材。Ver.0.1.0 の 2D では 2～17 を下記の atlas へ割当て可能 |
| `mask` | `u32`。0 は描画のみ、1 は自機弾、2 は自機接触、3 は両方 |

集合の容量は最大 1,024。ゲーム固有の体力・種類・経過時間などの列も追加できます。通常の `step` で移動した後、ホストが移動区間の円／球接触を計算します。自機弾は半径 0.05 を加算し、一発につき最初に接触した集合の行を一つ通知します。距離順ではありません。自機との接触候補には半径 0.35 を加算します。必要ならゲーム側でより小さい当たり判定を確認します。

- `actor_apply_hit(shot: u32, target: u32)`：`shots` の行番号と `actors` の行番号。
- `actor_apply_contact(target: u32)`：自機に接触した `actors` の行番号。

全候補は通知前に確定します。通知関数では列の値だけを更新し、`actors`・`shots`・`threats` の追加、削除、行の入替えを行わないでください。無効になった体力や弾の命中済みフラグをゲーム側で確認し、削除は `finish_step` に集約します。各通知の直後に長さの変更を検出すると Runtime は停止します。同じ長さの行入替えの検出は保証しません。個体接続が有効なゲームには固定敵への `apply_hits` は通知せず、敵弾から自機への従来通知は維持します。表示・接触・再開始は PIE と GameRunner で共通です。

HUD には任意の数値を最大 4 項目表示できます。`status_api_version() -> u32` は 1、`status_count() -> u32` は 0～4。各項目を次の関数で返します。

| 関数 | 戻り値 |
| --- | --- |
| `status_label(index: u32)` | `u32`。0～255 の翻訳 ID |
| `status_value(index: u32)` | `i32`。表示する現在値 |
| `status_maximum(index: u32)` | `i32`。0 以上。0 は現在値だけ、正数は「現在値 / 最大値」 |

`ui/game.json` の各言語へ `status` の文字列配列を追加し、翻訳 ID を配列の添字に合わせます。数値はゲーム更新・選択の反映後に取得し、ホストは経験値や時間などの意味を解釈しません。必要な字形を同梱 atlas へ含めます。HUD 接続がないゲームの表示は従来どおりです。

 `ui/game.json` の `result_details: true` で結果画面へ経過時間と残り耐久を表示できます。各言語に `elapsed` と `remaining_hp` を追加します。`result_status` は結果へ載せる status の添字配列で最大 4 個、各値は 0～3 かつ実際の `status_count` 未満です。時間は停止・選択待ちを除く固定更新数から分秒へ変換します。省略したゲームの表示項目は変わりません。

### 2D の背景と sprite atlas

プロジェクトへ任意の `visual/game.json` を置くと、PIE と GameRunner で同じ画像を使います。Editor のビルドは定義と参照画像を検証して pack に含めます。静止シーン編集と 3D はこの定義を使いません。3D ゲームに指定した場合はエラーになります。

```json
{
  "version": 1,
  "atlas": "assets/sprites.png",
  "backdrop": "assets/background.png",
  "title_logo": "ui/logo.png",
  "sprites": [[0, 0, 0.5, 1], [0.5, 0, 0.5, 1]],
  "hostile": 1,
  "friendly": 2,
  "hostile_color": [1, 0.38, 0.3],
  "friendly_color": [0.4, 1, 0.9],
  "combat_brightness": 0.12
}
```

定義は最大 16 KiB。画像は PNG、各辺最大 4096 px、総画素数最大 4,194,304（展開後 RGBA は 16 MiB）、ファイルは各 16 MiB 以下です。参照はプロジェクト内の相対パスとし、絶対パス、親参照、空の区間、バックスラッシュは拒否します。`atlas` は必須、`backdrop` と `title_logo` は省略または空文字列にできます。

`sprites` は 1～16 個。各要素は左上の U/V、幅、高さの順で、有限の 0～1、幅と高さは非 0、領域全体を画像内に収めます。個体の `visual: 2` が先頭、以降も順に対応します。定義数を越えた参照は描画時にエラーになります。`hostile` と `friendly` は敵弾・自機弾の指定で、0 が従来素材、1 以降が atlas の要素です。色は任意の RGB 各 0～1。表示の差替えは位置・当たり判定を変えません。

背景は 16:9 の画面へ敷き、戦闘と結果では `combat_brightness`（既定 0.12、範囲 0～0.35）で暗くします。タイトル文字画像は縦横比を保ち、永続記録を持つタイトルではメニューを左の余白へ寄せます。画像自体へ操作案内を描き込まず、必要な翻訳は UI に含めてください。


同じ定義版 1 に任意の `detail_atlas`／`detail_sprites` と `environment_atlas`／`environments` を追加できます。パスと UV 配列は対で指定し、各配列は最大 16 領域。省略時は使いません。UV の条件は `sprites` と同じです。Actor API 2 を宣言した 2D の個体は `visual: 18`～`33` で detail の領域を選べます。API 2 は 2D 専用で、Actor 列に `opacity: f32`（有限の 0～1）を追加します。API 1 は不透明度 1 と従来の visual 上限を維持します。判定は opacity に依存しません。

### 場面の背景と短時間の案内

任意の `presentation_api_version() -> u32` が 1 を返す場合、次をすべて公開してください。読み取り関数は状態を変えません。

| export | 戻り値と条件 |
| --- | --- |
| `presentation_scene()` | `u32`。0 は従来の背景、1～16 は environments の領域。実際に定義した範囲内 |
| `presentation_cue()` | `u32`。0 は案内なし、1～64 は UI の文言 ID |
| `presentation_cue_seconds()` | `f32`。有限の 0～30 秒。0 で非表示、最後の 0.6 秒でフェード |

各言語の UI 定義に `scene.1`、`cue.1` 等の文字列を追加します。未定義の文言は表示しません。場面名は戦闘画面の下端、案内は短時間の半透明パネルに表示します。環境画像には暗い戦闘向けの絵を使い、Runtime は RGB へ 0.24 を掛けて弾を読みやすくします。通常のタイトル背景は従来どおりです。

### ステージによる窓の収縮と連動表示

2D モジュールで `window_api_version() -> u32` が 1 を返すと、以下を毎回の更新後に読み取ります。ウィンドウのサイズと移動範囲を一致させるゲーム規則はモジュール側で実装してください。

| export | 戻り値と条件 |
| --- | --- |
| `window_view_count()` | `u32`。0 は通常表示、1～3 は一時配置する実窓の数 |
| `window_scale()` | `f32`。有限の 0.5～1。作業領域へ収めた基準寸法に対する倍率 |
| `window_view_size()` | `vec2`。各窓が映す世界の幅・高さ。16:9、幅 4～64、高さ 2.25～36 |
| `window_view_center(index: u32)` | `vec2`。各窓の中心。index は 0～count-1、各座標は有限かつ絶対値 1,000,000 以下 |

単独実行の画面設定対応 UI が必要です。三窓の場合、主窓（index 0）を中央、index 1 を左、index 2 を右へ配置します。16:9 の描画領域、タイトルバーと作業領域を考慮し、最大三窓へ上限を設けます。全窓は同じ VM・音声・入力・保存を共有します。フォーカスが窓の間を移動しても進行し、グループの外へ移るとポーズします。追加窓を閉じる・最小化する操作もポーズへ接続します。再開で追加窓を復元し、タイトル帰還・結果・終了で破棄します。

演出中は一時的なウィンドウ表示に切り替えます。ポーズ・演出終了時に元の画面モードと位置へ戻し、一時配置は保存しません。PIE は外部窓を開かず、指定領域全体を一つの埋込みビューで見渡す表示です。実際の窓演出は単独 Runner で確認してください。`PlayWindow::ViewWindows()` は有効な窓を主・左・右の順に返し、`CaptureView(index, width, height)` で各窓の描画を取得できます。計測の CPU 描画時間と保持量は追加窓を合計し、GPU 時間は主窓の値です。

強化などの選択要求がある間も元の画面サイズへ戻し、全領域の一覧の上に選択 UI を表示します。決定後は同じ VM のまま戦闘用の窓配置へ戻ります。

局所試遊には任意の `play_preset_count() -> u32`（1～32）を使えます。省略時は従来の 5 件。`Reset` と MCP の `play.start` は宣言された範囲だけを受け付けます。0 は通常タイトル、1 以降は `initialize(preset)` で条件を準備してプレイへ入ります。Editor の保存分離を維持し、製品の通常起動へデバッグ用メニューを追加する機能ではありません。

開発用 GameRunner では `--project project.json --scene scene.json --module game.glbc --ephemeral 1 --preview-preset N` で宣言済みの局所プリセットへ入れます。通常セーブを許可した起動、プロジェクトを指定しない起動、製品専用 EXE では拒否します。`--managed 1` を併用すると起動失敗を標準エラーと終了コードで受け取れます。試遊パネルは自身が起動したプロセスだけを停止してください。


計測の `vm_owned_bytes` と `vm_pool_reserved_bytes` はゲーム固有の列を含む VM 全体の保持量です。従来の `vm_pool_live_bytes` は標準サンプルの弾構造から計算した値で、追加した個体・独自列の使用量を表しません。個体数は `actor_count`、ホストの個体・通知配列の予約量は `host_actor_reserved_bytes` で確認します。


## ゲーム別の永続記録と工房

永続値を使うモジュールは `profile_api_version() -> u32` で 1 を返し、以下をすべて公開します。宣言のない既存ゲームは保存を使いません。読み取り関数では状態を変えず、各呼出しはゲームの実行予算内で終了してください。

| export | 型と条件 |
| --- | --- |
| `profile_schema` | `() -> u32`。非 0 のゲーム保存版 |
| `profile_count` | `() -> u32`。1～32 個の永続値 |
| `profile_get` | `(index: u32) -> u32`。指定した値を取得 |
| `profile_set` | `(index: u32, value: u32) -> bool`。旧版の入力も含め、復元用の値を受け取る。拒否時は false |
| `profile_accept` | `(schema: u32, count: u32) -> bool`。全値を渡した後に呼ばれる。版・項目間の条件を検証し、必要な旧版移行を行って true。拒否時は false |
| `profile_action_count` | `() -> u32`。タイトルから実行できる購入等の数。0～4 |
| `profile_action_label` | `(index: u32) -> u32`。表示 ID、0～255 |
| `profile_action_rank`、`profile_action_maximum` | 各 `(index: u32) -> u32`。現在段階と上限。上限は非 0、現在段階は上限以下 |
| `profile_action_cost` | `(index: u32) -> u32`。表示する費用 |
| `profile_action_enabled` | `(index: u32) -> bool`。現在実行できるか |
| `profile_action` | `(index: u32) -> bool`。費用・上限を再検証して購入を反映し true。拒否時は false |

`Game::Reset` は VM を初期化した後、シーン設定、永続値の復元、ゲームの `initialize` の順に適用します。ランの強化は通常状態へ戻し、永続強化は `initialize` で反映してください。ホストは初期化後・購入後・結果確定時に永続値を取得します。ランの報酬はゲームの `finish_step` 等で一度だけ確定し、デバッグ用プリセットから通常の報酬を得られないよう区別します。ホストが自動で通貨を加算することはありません。

C++ の `RestoreProfile` と `ActOnProfile` はタイトルでのみ使えます。不正な復元は直前の値へ戻します。購入の拒否は部分的な変更も破棄し、成功した購入は通常の初期化へ反映します。ゲーム内の読み取り・購入処理が実行予算を超える等のエラーは通常の Runtime エラーとして扱います。

任意の `profile_reset() -> bool` を追加できます。ゲームが進行値を初期値へ戻して true を返すと、C++ の `Game::ResetProfile` が取得と通常の初期化を行います。タイトル以外と未宣言のゲームでは false。拒否時の部分更新は破棄し、失敗時は直前の正常な進行値を保持して Runtime エラーにします。既存の profile API 版 1 は維持します。

ゲーム内から操作する場合は `ui/game.json` に `profile_reset: true` と `input_settings: 1` を指定し、各言語へ `reset_progress` と `confirm_reset` を追加します。記録画面の初期化は「いいえ」を既定とする確認を経由します。確認文には消える進行・記録と、保持される設定を明記してください。言語・画面・音量・操作設定と保存ファイルは削除しません。保護された保存では初期化項目を表示せず、未来版・破損・排他の保護を解除しません。

保存を使う UI は `ui/game.json` に `profile` を追加します。`id` は英小文字から始まる 1～64 文字で、英小文字・数字・ハイフンだけを使い、他ゲームと重複させないでください。`balance_slot` と `reward_slot` は残高・結果報酬の永続値の添字、`record_slots` は記録画面に出す添字の配列（最大 4 個）です。すべて `profile_count` の範囲内とします。

各言語には `play`、`workshop`、`records`、`guide`、`credits`、`quit`、`return`、`balance`、`cost`、`locked`、`capped`、`navigation`、`preview`、`recovered`、`protected`、`save_failed`、`reward`、`guide_text`、`credits_text` の文字列、`upgrades` の表示 ID 順の文字列配列、`records_labels` の記録順の文字列配列を追加します。改行は表示に使えますが、`glyphs` 自体には改行を含めません。タイトルは出撃・工房・記録・遊び方・クレジット・終了を基本とし、表示設定を有効にすると記録の後に画面設定を追加します。W/S・上下キーで選び、Enter またはクリックで決定、Esc でタイトルへ戻ります。任意のページ構成には対応しません。ゲームパッドは後述の `input_settings: 1` を指定したゲームで利用できます。

標準 Runner はローカルアプリデータ内 `gEngine/Games/<id>` を使います。保存ファイルは形式 4 の `profile.json` と直前の正常値を保持する `profile.backup.json`。JSON は最大 64 KiB で、ゲーム ID、保存版、値の配列、日英の選択、確定済みの画面設定、音量・ミュート・操作の割当てを保存します。形式 1～3 の記録は進行と言語、存在する画面設定を保持し、追加した設定だけを既定値で補って読み込みます。結果・購入・言語・確定した画面設定・音量・操作の割当ての変更時に保存し、同じ値での再書込みは省略します。F2 で明示的に再試行できます。途中のランは保存しません。

保存は同じ場所の一時ファイルへ書き込み、フラッシュ成功後に交換します。本体を読めない場合は、ゲームによる値の検証も含めてバックアップを調べます。両方が不正、別ゲームの記録、ホスト形式またはゲーム保存版が新しい場合は上書きを拒否し、その起動中はメモリー上だけで遊びます。同時起動は排他ロックで通常記録を保護します。書込み失敗と復旧・保護の状態はゲーム内に表示します。停電や記憶装置の故障に対する完全な耐久性を保証するものではありません。

Runner の `--user-data directory` は検証用の保存先を指定し、`--ephemeral 1` は読み書きを無効にします。両者は併用できません。C++ の `PlayWindow` は既定で保存しません。明示した `ProfileStorageOptions` で単独起動の保存を有効にでき、埋込み PIE は常にメモリーのみです。Editor から管理する別プロセスも `--ephemeral 1` で起動します。`--managed 1` は診断の出力方法を指定するもので、保存の分離は指定しません。

## ゲームの画面設定

永続記録とゲーム UI を使うゲームは、`ui/game.json` の `display_settings` を 1 にすると、タイトルへ画面設定が追加されます。省略時は従来のメニュー・初期寸法を維持します。両言語へ `settings`、`display_mode`、`windowed`、`borderless`、`fullscreen`、`resolution`、`desktop_resolution`、`monitor`、`vsync`、`on`、`off`、`apply`、`keep`、`cancel`、`confirm_display`、`display_failed`、`display_unavailable`、`settings_navigation` の文字列を追加し、必要な字形を atlas に含めてください。

初期設定は 1920 × 1080 のウィンドウ、垂直同期有効です。上下キーで項目、左右キーで値を選び、適用後 15 秒以内に「この設定を使う」で確定します。Esc・取消し・期限切れでは前の設定とウィンドウ配置へ戻ります。未確定の設定は保存しません。タイトルの待機中や最小化中も確認期限は進みます。

- ウィンドウ：1920 × 1080、2560 × 1440、3840 × 2160 のクライアント領域。デスクトップより大きい設定では全体が収まらない場合があります。任意の手動リサイズには 16:9 の余白を付けます。
- ボーダーレス：選んだモニターのデスクトップ全体を使用し、デスクトップ解像度で描画します。解像度の項目はデスクトップへの追従表示になります。
- フルスクリーン：モニターが提供する上記の解像度で DXGI の全画面状態へ切り替えます。D3D12 の OS 最適化を用いるため、従来の排他的表示は保証しません。非対応モードや前面でない状態などで拒否された場合は前の表示へ戻します。

モニターが見つからない場合や OS が全画面を解除した場合は、利用可能なモニターの FHD ウィンドウへ戻します。フォーカスを失った戦闘は停止し、P で再開します。モニター番号は現在の列挙順、保存は表示デバイス名で行います。標準 Runner はモニター単位の DPI に対応します。埋込み PIE は画面設定を変更できず、利用者の表示設定も適用しません。垂直同期を無効にしても tearing / VRR は保証しません。


## ゲームの音楽と音量

`ui/game.json` に `audio_settings: 1` を追加すると、設定画面の「音声」から全体・音楽・効果音の音量（0～100%、キーでは 10% 刻み）とミュートを変更できます。変更は即時反映して保存し、効果音を試聴できます。永続記録と `display_settings: 1` を使うゲームが対象です。各言語に `audio`、`master`、`music`、`effects`、`muted`、`test_audio`、`audio_unavailable` を追加し、同梱 atlas に字形を含めます。

同じ素材入力へ `audio/game.json` を配置します。通常ファイルと pack、PIE と Runner で同じ定義を使います。Editor のビルドは参照音源を検査して pack へ取り込み、配布用の参照先へ書き換えます。素材原本は変更しません。

```json
{
  "version": 1,
  "events": ["assets/shot.wav", "assets/hit.wav", "assets/clear.wav", "assets/fail.wav", "assets/pickup.wav"],
  "tracks": ["assets/menu.wav", "assets/battle.wav"],
  "title_track": 1,
  "play_track": 2
}
```

`events` は 4～32 個の WAV パスです。空文字の項目は発音しません。`audio_events() -> u32` の各ビットが、0 始まりの配列位置に対応します。bit 0～3 は従来の射撃・被弾・成功・失敗で、bit 4～31 をゲーム側で定義できます。成功・失敗はホストも通知します。同じ描画更新に集まった同種イベントは一音にまとめます。ゲームは更新ごとにビットを初期化し、複数回の発生で桁上がりさせないでください。シーンの自機・敵に音声を割り当てた場合は、bit 0・1 の音源より優先します。効果音の入力上限は一つにつき 1 MiB です。

`tracks` は最大 8 曲、PCM16 WAV（44.1 / 48 kHz、mono / stereo）、入力は一曲 16 MiB 以下です。効果音と音楽の常駐 PCM 合計は 64 MiB 以下です。曲番号は 1 始まり、0 は無音です。`title_track` はタイトルの曲、`play_track` は通常プレイの既定曲です。結果画面では音楽を停止して勝敗の効果音を鳴らします。音楽は全体をループするため、素材の先頭・末尾をつなげてください。圧縮音楽のデコードとストリーミングは行いません。

ゲームは省略可能な `music_track() -> u32` でプレイ中の曲番号を返せます。読み取り専用とし、宣言済みの曲または 0 を返します。曲番号が変わった場合だけ切り替え、同じ曲への毎フレームの再生要求は行いません。新しい曲は 250 ms でフェードインします。宣言のない既存モジュールは `play_track` を使います。音声設定のないゲームは従来の 4 効果音を使います。

Pause とフォーカス喪失では音楽・効果音の位置を保持します。プレイ中は P で再開し、タイトル等はフォーカスを戻すと音声も再開します。音声デバイス不在・消失時もゲーム更新は続き、2 秒間隔で再初期化を試みます。復旧時は音量・ミュート・Pause を保ち、必要な曲を先頭から一つだけ再生します。失った効果音は再送しません。復旧には素材入力を読み直せることが必要です。音量画面でデバイスの待機状態を表示します。


## 操作設定とポーズメニュー

`ui/game.json` に `input_settings: 1` を追加すると、アクション入力、キー／パッドの再割当て、ポーズ・結果・終了確認のメニューを有効にします。ゲームの profile export と UI の `profile`、`display_settings: 1`、`audio_settings: 1` が必要です。省略した既存ゲームは従来のキー操作を維持します。追加の gLang export やバイトコード形式の変更はありません。

設定の入口は画面・音声・操作・言語・戻るです。操作からキーボードまたはコントローラーを選び、変更する行で決定して次の入力を割り当てます。既に使われているキーやボタンは二つのアクション間で交換します。割当ての取消しは Esc、パッドでは方向左も使えます。「標準の割当てに戻す」は表示中の入力機器だけを初期化します。変更は即時保存します。2D では下降・上昇を表示しません。

| アクション | キーボードの既定値 | パッドの既定値 |
| --- | --- | --- |
| 左・右・上・下へ移動 | A・D・W・S | 左スティックまたは方向パッド。固定 |
| 集中射撃 | Space | RT |
| 一時停止 | P | Start |
| 3D の下降・上昇 | Q・E | LB・RB |

キーボードへ割り当てられるのは A～Z、0～9、Space、Shift、Ctrl、方向キーです。Enter、Esc、F1、F2 はメニュー・言語・保存再試行のために予約します。パッドは XInput 1.4 に対応するコントローラーを一台使用します。A/B/X/Y、Start/Back、左右のスティック押込み、LB/RB、LT/RT を割り当てられます。方向パッド、右スティック、振動、DirectInput 専用機器の再割当てには対応しません。

メニューでは W/S または方向キー・パッドの方向で選択、Enter・A・クリックで決定、Esc・B で戻ります。表示値と言語は左右の画面ボタン、ON/OFF はスイッチ、音量はクリック／ドラッグするスライダーでも変更できます。キーの左右操作も維持します。音量のドラッグは即時反映し、解放時に保存します。操作矢印の半角 `<` と `>` も同梱 atlas に含めます。工房は項目の購入ボタンを使い、資金不足・最大段階では無効表示にします。任意の翻訳 `purchase` で購入ボタンの文字を指定でき、省略時は `cost` を使います。メニューはホバーで選択を示し、項目名や余白のクリックでは設定を変更しません。キー割当ては専用ダイアログで待機し、戻るボタンでも取り消せます。設定はタイトル／ポーズから開き、戦闘 HUD に設定値や常設のキー操作案内を表示しません。ゲーム用の再割当てでもメニューの A/B 操作を維持します。パッドの方向は長押しで反復し、決定は一回の押下につき一回です。強化選択は方向と決定、1～3 の直接指定、クリックに対応します。

通常プレイでは Esc または割り当てた一時停止キーから、再開・設定・遊び方・タイトル帰還・終了へ進みます。中断と終了の確認は「いいえ」を初期選択にします。タイトルバーの閉じる操作にも確認を出します。Editor の停止など管理用の `WM_CLOSE` は確認で妨げません。結果画面では再挑戦・タイトル帰還・終了を選べます。

フォーカス喪失や使用中のパッド切断で停止し、手動で再開します。接続・画面遷移後はキー・ボタンを離し、左スティックを中立へ戻してから操作してください。左スティックは遊びを除いた倒し量を速度に反映します。C++ の `InputState.horizontal` / `vertical` は -1～1 の有限値を受け取り、同じ軸の方向 bool がある場合はそちらを使います。斜め入力が単位長を超える場合だけ正規化します。

各言語に次の文字列を追加し、同梱 atlas に字形を含めます：`display`、`controls_settings`、`keyboard`、`controller`、`language`、`defaults`、`press_key`、`press_button`、`binding_invalid`、`resume`、`return_title`、`retry`、`confirm_quit`、`confirm_abandon`、`yes`、`no`、`pad_unavailable`、`menu_navigation_pad`、`move_left`、`move_right`、`move_up`、`move_down`、`focus_action`、`pause_action`、`descend_action`、`ascend_action`、`choice_navigation`、`choice_navigation_pad`。操作設定中の効果音試聴は戦闘音と別に再生し、停止中の音楽の位置を動かしません。

## ゲーム専用の起動と配布

プロジェクトのルートへ省略可能な `product.json` を置くと、Editor の「ビルド」と MCP の `build.start` が専用の実行ファイルを生成します。保存済みのシーンと指定したソースを使う点は従来どおりです。

```json
{
  "version": 1,
  "title": "My Game / 私のゲーム",
  "executable": "MyGame.exe",
  "icon": "assets/game.ico"
}
```

- 定義全体は 4 KiB 以下。`title` は 1～256 バイトの UTF-8 で、ASCII 制御文字・改行を含めません。
- `executable` は英字から始まる 1～64 文字の本体名と `.exe`。本体名には英数字・`-`・`_` を使えます。Windows の予約デバイス名は使えません。
- `icon` はプロジェクト内の相対 `.ico` パス。1 MiB 以下、1～8 個の重複しない 16～256 px の正方形 PNG を含む 32-bit ICO を使います。画像はディレクトリの順に格納し、領域を重ねません。DIB 形式の ICO は対象外です。パスの上位参照・リンク・絶対パスは拒否します。

成果物には専用 EXE、`game.bpack`、描画・音声バックエンド、部品の原文ライセンス、ファイルごとのハッシュを記録した `build.json` が入ります。コンパイラー・Editor・Qt・ソース・制作設定は含めません。ゲームで追加した素材やフォントのライセンス、利用ガイドは制作者が別途同梱してください。

専用 EXE を引数なしで起動すると、実行ファイルの隣にある `game.bpack` を開きます。作業ディレクトリーの指定は不要です。ウィンドウ名とアイコンは製品設定を使います。Editor のスタンドアロン起動・停止も同じ EXE を使い、起動前に成果物のハッシュを照合します。ビルド後に実行ファイルを変更した場合は再ビルドしてください。既存の `--managed`・`--ephemeral`・`--user-data`・計測用引数は維持します。配布用のプリセット起動は追加しません。

実行時の素材ルートに `game.json` があれば、標準 Runner の `--assets` と `--pack` も任意のモジュール・シーン名を使えます。

```json
{
  "version": 1,
  "title": "My Game / 私のゲーム",
  "module": "game.glbc",
  "scene": "scenes/game.json"
}
```

定義は 4 KiB 以下、パスは各 1～240 バイトの安全な相対パスとし、拡張子はそれぞれ `.glbc`・`.json` にします。シーンから 2D / 3D を判別し、明示した `--game` と食い違う場合は起動を拒否します。製品ビルドではこの定義を自動生成します。`product.json` のない既存プロジェクトと `game.json` のない標準サンプルは従来の名前・入口を維持します。製品 EXE の既定 pack に `game.json` がない場合はエラーです。製品名・実行ファイル名を変えても、保存先は UI の `profile.id` を使います。
