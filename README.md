# ControlHub (LPSM-Bridge)

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)](https://www.microsoft.com/windows)

**ControlHub** 是一個高效能的工業物聯網 (IIoT) 中介軟體，旨在作為工廠硬體設備與現代 Web 前端 (Vue.js) 之間的通訊橋樑。

本專案採用 **"Dumb Bridge" (極簡橋接)** 架構設計，C++ 後端不包含複雜的業務邏輯，僅負責硬體訊號的即時轉發 (Pass-through) 與狀態的持久化儲存 (Persistence)，所有的邏輯判斷與流程控制皆交由前端處理。

## 🏗 系統架構 (Architecture)

系統運作於 Windows 環境，透過多執行緒處理不同的硬體 I/O，並統一透過 MessageBus 匯聚資訊。
```Markdown
[ Hardware Layer ]        [ Middleware (C++) ]          [ Presentation Layer ]
+----------------+       +----------------------+       +--------------------+
| Barcode Scanner| --->  | Keyboard Hook (Win32)| --+-> |                    |
+----------------+       +----------------------+   |   |                    |
                                                    |   |                    |
+----------------+       +----------------------+   |   |   Web Frontend     |
| TCP Cameras    | --->  | CamServer (Asio)     | --+-> |     (Vue.js)       |
+----------------+       +----------------------+   |   |                    |
                                                    |   |   (Logic Brain)    |
+----------------+       +----------------------+   |   |                    |
| Mitsubishi PLC | <---> | PlcClient (Asio)     | --+   |                    |
+----------------+       +----------------------+       +--------------------+
                                     ^                            ^
                                     |                            |
                             +----------------+           +----------------+
                             | StateManager   | <-------> | WebSocket Srv  |
                             | (JSONL Store)  |           | (uWebSockets)  |
                             +----------------+           +----------------+
```

## ✨ 核心功能 (Features)
* **⚡ 高效能非同步 I/O：使用 `Boost.Asio` 處理 PLC 與相機連線，確保高併發下的低延遲。**
* **🔌 全域鍵盤監聽：透過 Windows API Hook (`SetWindowsHookEx`) 攔截 USB 掃碼槍輸入，即使視窗未聚焦也能讀取。**
* **📡 即時廣播：整合 `uWebSockets`，將所有硬體訊號封裝為 JSON 並即時推播至前端。**
* **💾 狀態持久化：實作 Append-only 的 `JSONL` 儲存機制，支援斷電恢復與狀態回朔 (State Patching)。**
* **🛡️ 執行緒安全：透過 `MessageBus` 與 `Condition Variable` 實現生產者-消費者模式，確保多執行緒資料安全。**

## 🛠 技術棧 (Tech Stack)
* **語言: C++17**
* **網路: Boost.Asio, uWebSockets**
* **JSON 處理: nlohmann/json**
* **日誌: spdlog**
* **系統 API: Windows API (User32.lib)**

## 🚀 編譯與執行 (Build & Run)
本專案使用 CMake 進行建置，建議在 Windows 環境下使用 MSYS2 (MinGW) 或 MSVC。

### 前置需求
* CMake >= 3.20
* C++ Compiler (支持 C++17)
* Boost Libraries
* zlib, libuv (uWebSockets 依賴)

### 編譯步驟
```Bash
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

### 執行
請確保 `config.json` 位於執行檔同級目錄下。
```Bash
./lpsm_app.exe
```

### ⚙️ 設定檔 (Configuration)
`config.json` 用於定義網路相機 IP 與識別名稱的映射關係。
```JSON
{
    "camera_mapping": {
        "172.23.128.100": "CAMERA_LEFT_1",
        "172.23.128.101": "CAMERA_LEFT_2",
        "172.23.128.102": "CAMERA_RIGHT_1",
        "172.23.128.103": "CAMERA_RIGHT_2"
    }
}
```

### 📡 通訊協議 (Communication Protocol)
後端運作於 WebSocket `ws://0.0.0.0:8181`。

1. 接收 (Frontend -> Backend)
* **狀態更新 (Patch):**
    ```JSON
    { "command": "STATE_PATCH", "payload": { "events": [...] } }
    ```
* **控制指令 (Go/NoGo):**
    ```JSON
    { "command": "GO_NOGO", "payload": 1 }
    ```
* **載入狀態:**
    ```JSON
    { "command": "LOAD_STATE" }
    ```

2. 發送 (Backend -> Frontend)
* **掃碼槍資料:**
    ```JSON
    { "type": "data", "source": "SCANNER", "payload": "123456" }
    ```
* **PLC 狀態:**
    ```JSON
    { "type": "data", "source": "PLC_MONITOR", "payload": { "up_in": 1, "dn_out": 0, ... } }
    ```
* **相機資料:**
    ```JSON
    { "type": "data", "source": "CAMERA_LEFT_1", "payload": "BARCODE_STRING" }
    ```

## 📂 目錄結構
* **`src/core`: 核心基礎建設 (Logger, Config, MessageBus, StateManager).**
* **`src/driver`: 硬體驅動層 (PlcClient, CamServer, KeyboardHook).**
* **`src/server`: WebSocket 伺服器 (WsServer).**
* **`src/logic`: 業務邏輯控制 (Controller - 負責路由與轉發).**

## 📝 License
MIT License