# LPSM ControlHub (Central Control Unit)

![C++](https://img.shields.io/badge/Language-C++17-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%20(MSYS2)-lightgrey)
![License](https://img.shields.io/badge/License-MIT-green.svg)

## 📖 專案簡介 (Overview)

**ControlHub** (`lpsm_app.exe`) 是 LPSM 2DID 系統的硬體控制中樞。它運作於 Windows 產線電腦上，負責整合異質硬體設備，並透過 WebSocket 提供即時數據給前端介面。

本專案採用 **"Dumb Bridge" (極簡橋接)** 架構：後端不處理複雜的檢測邏輯，僅負責 I/O 訊號的高效轉發與設備管理。

### 🌟 核心功能 (Key Features)

* **動態資料庫配置 (Dynamic Configuration)**:
    * 程式啟動時自動偵測 **本機 IP**。
    * 連線至 MySQL 資料庫 (`sfdb4070`)，根據 IP 拉取專屬的 PLC 點位與相機設定。
    * 支援 **SSL 憑證繞過** (解決 Error 0x800B0109)，確保內網連線穩定。
* **高效能 PLC 通訊**:
    * 支援 **Mitsubishi MC Protocol (3E Frame)**。
    * **智慧掃描**: 根據資料庫設定的點位，自動計算記憶體讀取範圍 (Auto-Range)，減少通訊封包大小。
* **多相機協同作業**:
    * 內建 TCP Server (Port 6060)，支援多台工業相機 (Keyence/Cognex) 同時連線。
    * 自動根據 IP 識別相機角色 (如 `CAMERA_LEFT_1`)。
* **全域周邊整合**:
    * **鍵盤掛鉤 (Keyboard Hook)**: 攔截 USB 掃碼槍輸入，即使視窗未聚焦也能讀取條碼。
    * **自動啟動**: 程式啟動後自動開啟 Chrome 瀏覽器並導向指定的前端頁面。

## 🏗 系統架構 (Architecture)

```mermaid
graph TD
    DB[("MySQL Database")]
    Frontend("Vue.js Frontend")
    
    subgraph "ControlHub (C++ Backend)"
        Config[Config Loader]
        PlcDriver["PLC Client (Asio)"]
        CamDriver["Cam Server (Asio)"]
        Hook[Keyboard Hook]
        WS[WebSocket Server]
        Bus[MessageBus]
    end

    Config <-- "Load Config by Local IP" --> DB
    PlcDriver <--> "MC Protocol" <--> PLC[Mitsubishi PLC]
    CamDriver <--> "TCP/IP" <--> Camera[Industrial Cameras]
    Hook <--> "USB HID" <--> Scanner[Barcode Scanner]
    
    PlcDriver --> Bus
    CamDriver --> Bus
    Hook --> Bus
    
    Bus --> WS
    WS <--> "Real-time Events (Port 8181)" <--> Frontend
```

---

## ⚙️ 環境需求 (Prerequisites)
* 作業系統: Windows 10 / 11 (64-bit)
* 編譯環境: MSYS2 (UCRT64)
* 相依套件:
  * `Boost` (Asio)
  * `uWebSockets` (需搭配 `libuv`, `zlib`)
  * `libmariadb` (MySQL Connector C)
  * `spdlog` (日誌系統)
  * `nlohmann-json`

---

## 💾 資料庫設定 (Database Setup)
ControlHub 依賴資料庫來決定如何運作。請確保 MySQL 中存在以下表格：

1. 上位機配置表 (`2did_machine_config`)
定義每台電腦 (Hub) 對應的 PLC 參數。

```SQL
CREATE TABLE IF NOT EXISTS `2did_machine_config` (
    `hub_ip` VARCHAR(50) NOT NULL,          -- 本機 IP (Primary Key)
    `plc_ip` VARCHAR(50) NOT NULL,          -- PLC IP
    `plc_port` INT DEFAULT 1285,            -- PLC Port
    `plc_type` VARCHAR(20) DEFAULT 'FX5U',
    
    -- 動態點位設定 (M-Code Address)
    `addr_up_in` INT DEFAULT 503,           -- 上層進料
    `addr_up_out` INT DEFAULT 506,          -- 上層出料
    `addr_dn_in` INT DEFAULT 542,           -- 下層進料
    `addr_dn_out` INT DEFAULT 545,          -- 下層出料
    `addr_start` INT DEFAULT 630,           -- 啟動訊號
    `addr_go_nogo` INT DEFAULT 86,          -- 寫入訊號 (GO/NOGO)
    
    PRIMARY KEY (`hub_ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```
2. 相機配置表 (`2did_machine_cameras`)
定義每台電腦掛載的相機清單。

```SQL
CREATE TABLE IF NOT EXISTS `2did_machine_cameras` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `hub_ip` VARCHAR(50) NOT NULL,          -- 外鍵
    `camera_ip` VARCHAR(50) NOT NULL,       -- 相機 IP (需固定)
    `camera_role` VARCHAR(50) NOT NULL,     -- 角色 (e.g., CAMERA_LEFT_1)
    
    -- 複合唯一鍵：同一台電腦下相機 IP 不可重複
    UNIQUE KEY `idx_hub_camera_ip` (`hub_ip`, `camera_ip`),
    CONSTRAINT `fk_hub_ip` FOREIGN KEY (`hub_ip`) REFERENCES `2did_machine_config`(`hub_ip`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

---

## 🚀 編譯與部署 (Build & Deploy)1. 編譯 (Build)請在 MSYS2 UCRT64 終端機執行：Bashmkdir build

```Bash
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

2. 打包 (Bundle)
由於使用了動態連結，請務必使用專用的 PowerShell 腳本打包 DLL，避免環境衝突。
```PowerShell
# 在 build 目錄下執行
.\Bundle-MsysApp.ps1 -Target ".\lpsm_app.exe"
```

產出物將位於 `dist/lpsm_app` 資料夾中。

---

## 📖 使用說明 (Usage)
1. 環境檢查: 確保本機 IP 已註冊在資料庫的 2did_machine_config 表中。
2. 啟動程式: 執行 lpsm_app.exe。
3. 啟動流程:
  - [System] 清理舊的 Chrome 與 Port 佔用 (8181, 6060)。
  - [Config] 偵測本機 IP -> 連線 DB 下載設定。
  - [Browser] 自動開啟 Chrome 至 `http://10.8.32.64:2102/` (可於 main.cpp 修改)。
  - [Services] 啟動 WebSocket, PLC Client, Cam Server, Keyboard Hook。
4. 關閉程式: 點擊 Console 視窗右上角的 `[X]` 即可安全退出。

---

## 📡 通訊端口 (Ports)
| Port | 類型 | 用途 |
| ---- | ---- | ---- |
| 8181 | WebSocket | 前端介面通訊 (Listening) |
| 6060 | TCP | 工業相機連線 (Listening) |
| 1285 | TCP | 三菱 PLC 連線 (Client, 可由 DB變更) |
| 3306 | TCP | MySQL 資料庫連線 (Client) |

---

## ⚠️ 常見問題 (Troubleshooting)
* **無法載入設定 (Config Load Failed):**
  * 檢查本機 IP 是否與 DB hub_ip 一致。
  * 檢查資料庫連線帳號密碼 (src/core/Config.hpp)。

* PLC 連線失敗:
  * 確認 PLC 的 IP 與 Port 設定正確，且已開放 MC Protocol (3E Frame)。

* 相機無反應:
  * 確認相機已設定為 Client Mode 並指向本機 IP 的 Port 6060。
  * 確認相機 IP 是否已登錄在 2did_machine_cameras 表中。

---

## 📝 License
MIT License