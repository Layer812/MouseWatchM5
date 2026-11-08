# MouseWatchM5 R2 FIX13 — BLE表示復帰 + Cat Clock（CPU0 オフスクリーン描画）

FIX12が実機で成功したJPEG/PNGの480×480相当の全面表示、中央クロップ、FlashのMWCACHE.BIN、USBのNVS再起動ハンドオフ、BLE HID、リングスクロール、APP-ONLY更新は維持。

## FIX13
- Single Imageの上端に **MouseWatchM5**、下端に **BLE Connected / BLE Waiting** を表示。BLE変化時に再描画。
- Watch Face → CAT CLOCK を選べるようにした（以前のNO FILES制限を廃止）。
- 猫はユーザー作成の `CLOCK_CAT_R9_6FRAME_STANDALONE.html` から32フレームの基本シルエットと6フレームの横払いを内蔵。長針・短針・猫は別描画。
- 29分に寝ている時、長針が止まり、34分に猫の手で前進する特殊動作の基本ロジックを移植。歩行・走行・座る・寝る・起きる・近寄る・去る・特別動作を含む。
- RTCが正常ならRTC時刻を使用。RTCが未初期化と判定された場合のみビルド日時から初期設定します。ビルド時刻由来なので厳密な時刻合わせには別途設定が必要な場合があります。
- CPU0低優先度タスクでPSRAMの16-bitキャンバスに時計を生成。CPU1上の通常ループが完成フレームだけLCDに転送。USB・BLE・M5.updateは従来のCPU1側。
- 外周リング、左右クリック、タッチ座標、画像選択と画像キャッシュはFIX12から変更していません。起動時は物理ボタンでマウスを開始、中心タップで設定。

**素材の来歴に関する重要事項:** 以前のStopWatch版 `ClockCat_StopWatch_R3F8` / `R3F9` ZIPはライブラリから生バイトを取得できませんでした。今回内蔵したのはClock Cat **R9ブラウザ版**から取り出した32＋6スプライトです。StopWatch版R10の「厳密に37コマ」「高精細文字盤と針」「4プレビュー」を原本どおり移植した、と主張するものではありません。R3F8原本そのものの完全移植は未完です。

**未確認:** ESP32-S3実機ビルド、Cat ClockのPSRAM描画の実機性能、BLE+iPadとの同時動作。ホストソース検証のみ。

## 書き込み

```cmd
"F:\build\tmp\MouseWatchM5_R2_FIX13_CAT_CLOCK_DUAL_CORE\RUN_THIS_MOUSEWATCHM5_APP_ONLY.cmd" COM16
```
