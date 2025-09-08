# bleak と pythoncom の相性問題を回避するための設定
import sys
sys.coinit_flags = 0  # MTA (Multi Threaded Apartment) モードに設定

import asyncio
from   bleak import BleakClient, BleakError # BLEライブラリ
import pythoncom # COMライブラリ
import win32com.client
import threading
import struct

# BLEデバイスのMACアドレス
BLE_ADDRESS = "C7:66:0E:39:B6:29"

# BLEのサービスとキャラクタリスティックUUID
SVC_PPT_CTRL_UUID = "ba21ce66-9974-4ecd-b2e5-ab6d1497a7f0"
CHR_COMMAND_UUID  = "ba21ce66-9974-4ecd-b2e5-ab6d1497a7f1"
CHR_RESPONSE_UUID = "ba21ce66-9974-4ecd-b2e5-ab6d1497a7f2"

# 真偽値の定義 (boolの代わりに明示的に整数を使用)
TRUE  = 1
FALSE = 0

# スライドショーの状態定数
ppSlideShowRunning      = 1 # スライドショー実行中
ppSlideShowPaused       = 2 # スライドショー一時停止中
ppSlideShowBlackScreen  = 3 # スライドショーブラックアウト中
ppSlideShowWhiteScreen  = 4 # スライドショーホワイトアウト中
ppSlideShowDone         = 5 # スライドショー終了

# コマンドとレスポンスのキュー (スレッド間通信)
command_queue  = asyncio.Queue()
response_queue = asyncio.Queue()

#######################################################
#  COM動作スレッド側の関数群
#######################################################

# PowerPointアプリケーションを取得または起動
def get_ppt():
    ppt = win32com.client.Dispatch("PowerPoint.Application")
    ppt.Visible = True
    return ppt

# アクティブなプレゼンテーションを取得
def get_active_presentation(ppt):
    if ppt.Presentations.Count == 0:
        return None
    return ppt.ActivePresentation

# スライドショーのビューを取得
def get_slideshow_view(ppt, pres):
    if ppt.SlideShowWindows.Count == 0:
        return None
    return pres.SlideShowWindow.View

# スライドショーの状態を応答
def send_slideshow_status(ppt, pres, loop):
    # アクティブなプレゼンテーションがない時
    if ppt is None or pres is None:
        status = struct.pack("<BBBHH", FALSE, FALSE, FALSE, 0, 0)
        note_bytes = b"\0"
        response = status + note_bytes

    # アクティブなプレゼンテーションがある時
    else:
        view = get_slideshow_view(ppt, pres)
        # スライドショーが実行されていない時
        if view is None:
            total_pages = pres.Slides.Count
            status = struct.pack("<BBBHH", TRUE, FALSE, FALSE, 0, total_pages)
            note_bytes = b"\0"
            response = status + note_bytes
        # スライドショーが実行されているが終了している時
        elif view.State == ppSlideShowDone:
            total_pages = pres.Slides.Count
            status = struct.pack("<BBBHH", TRUE, FALSE, FALSE, 0, total_pages)
            note_bytes = b"\0"
            response = status + note_bytes
        # スライドショーが実行されている時
        else:
            is_blackout = TRUE if view.State == ppSlideShowBlackScreen else FALSE # ブラックアウト中か
            slide = view.Slide
            current_page = slide.SlideIndex
            total_pages = pres.Slides.Count
            status = struct.pack("<BBBHH", TRUE, TRUE, is_blackout, current_page, total_pages)

            # ノートテキストの取得
            note_page = slide.NotesPage
            try:
                # 通常、Placeholders(2)がノートテキスト
                note_text = note_page.Shapes.Placeholders(2).TextFrame.TextRange.Text
                # 改行コードをCRLFに変換
                note_text = note_text.replace('\r\n', '\n').replace('\r', '\n').replace('\n', '\r\n')
            except Exception:
                note_text = ""
            # ノートテキストをバイト列に変換し、最大300バイトに切り詰め
            note_bytes = note_text.encode('utf-8')[:299] + b'\0'  # NULL終端
            response = status + note_bytes

    # BLEデバイスへの応答をキューに追加
    asyncio.run_coroutine_threadsafe(
        response_queue.put(response), loop
    )

# COM操作を行うスレッド
def com_thread_runner(loop):
    pythoncom.CoInitialize() # COMライブラリの初期化
    try:
        while True:
            if command_queue.empty():
                # キューが空の場合はメッセージポンプを回す
                pythoncom.PumpWaitingMessages()
                asyncio.sleep(0.1)
                continue
            else:
                # コマンドをキューから取得
                command = command_queue.get_nowait()
                
                # PowerPointアプリケーションとアクティブなプレゼンテーションを取得
                ppt = get_ppt()
                pres = get_active_presentation(ppt)
                if pres is None:
                    print("COM Thread: No active presentation")
                    send_slideshow_status(ppt, pres, loop)
                    continue

                # スライドショーの開始/終了コマンド
                if command == "start":
                    if ppt.SlideShowWindows.Count == 0:
                        # スライドショーの開始
                        pres.SlideShowSettings.Run()
                        print("COM Thread: Slideshow started")
                    else:
                        # スライドショーの終了
                        view = get_slideshow_view(ppt, pres)
                        if view is None:
                            print("COM Thread: No slideshow running")
                        else:
                            view.Exit()
                            print("COM Thread: Slideshow ended")
                # その他のコマンド
                else:
                    if ppt.SlideShowWindows.Count == 0:
                        print("COM Thread: No slideshow running")
                    else:
                        view = get_slideshow_view(ppt, pres)
                        # 次のスライド
                        if command == "next":
                            view.Next()
                            print("COM Thread: Next slide")
                        # 前のスライド
                        elif command == "prev":
                            view.Previous()
                            print("COM Thread: Previous slide")
                        # ブラックアウト/解除
                        elif command == "black":
                            if view.State == ppSlideShowRunning or view.State == ppSlideShowPaused:
                                view.State = ppSlideShowBlackScreen
                                print("COM Thread: Blackout")
                            else:
                                view.State = ppSlideShowRunning
                                print("COM Thread: Resume slideshow")
                        # スライドショーの状態確認のみ
                        elif command == "check":
                            print("COM Thread: Check slideshow status")
                        else:
                            print(f"COM Thread: Unknown action: {command}")
                        
                # ステータスを送信
                send_slideshow_status(ppt, pres, loop)

            # if command_queue.empty() else ココマデ
        # while True ココマデ
    finally:
        pythoncom.CoUninitialize() # COMライブラリの終了
        print("COM thread shut down.")

#######################################################
#  メインスレッド側の関数群
#######################################################

# コマンド受信時のコールバック
def handle_notify(sender, data):
    # コマンド文字列を取得
    command = data.decode(errors="ignore").strip()
    print(f"[Notify] {command}")
    # コマンドをキューに追加
    asyncio.run_coroutine_threadsafe(command_queue.put(command), asyncio.get_event_loop())

# BLEデバイスに接続してNotifyを待機、切断されたら再接続
async def connect_and_listen():
    while True:
        try:
            # BLEデバイスに接続
            print("Connecting...")
            async with BleakClient(BLE_ADDRESS) as client:
                print("Connected:", client.is_connected)
                
                # Notifyの開始
                await client.start_notify(CHR_COMMAND_UUID, handle_notify)
                print("Waiting for notify...")

                # 接続が続く限り待機
                while client.is_connected:
                    try:
                        # キューから応答データを取得
                        response = await asyncio.wait_for(response_queue.get(), timeout=1.0)
                        if response:
                            # 応答データをBLEデバイスに送信
                            await client.write_gatt_char(CHR_RESPONSE_UUID, response)
                            # print(f"Main Loop: Wrote '{response}' to BLE.")
                    except asyncio.TimeoutError:
                        pass # キューが空でタイムアウトしたら待機を続ける
                    except BleakError as e:
                        print(f"Main Loop: BLE write error: {e}")
                        break # BLEエラー時は再接続
                
                # 切断されたとき
                await client.stop_notify(CHR_COMMAND_UUID)
                print("Disconnected.")

        # BLE接続エラー時(デバイスが見つからないなど)は少し待ってからリトライ
        except BleakError as e:
            print(f"Connection Error: {e}")
            await asyncio.sleep(1)

# メイン関数
async def main():
    # イベントループを取得
    loop = asyncio.get_running_loop()
    # COMスレッドを起動 (イベントループを引数に渡す)
    com_thread = threading.Thread(target=com_thread_runner, daemon=True, args=(loop,))
    com_thread.start()
    # BLE接続とNotify待機
    await connect_and_listen()

if __name__ == "__main__":
    asyncio.run(main())
