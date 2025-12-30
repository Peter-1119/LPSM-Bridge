#pragma once
#include <memory>
#include <fstream>      // ✅ [新增] 檔案讀寫
#include <filesystem>   // ✅ [新增] 檢查檔案存在
#include "core/MessageBus.hpp"
#include "driver/PlcClient.hpp"
#include "core/Config.hpp"
#include "server/WsServer.hpp"

class Controller {
private:
    std::shared_ptr<MessageBus> bus_;
    std::shared_ptr<PlcClient> plc_;
    std::shared_ptr<WsServer> ws_server_;

    // ✅ 新增：紀錄上一次的 PLC 狀態與 Log 時間
    json last_plc_state_;
    std::chrono::steady_clock::time_point last_log_time_;

    const std::string CACHE_FILE = "offline_data.json";

public:
    Controller(std::shared_ptr<MessageBus> bus, std::shared_ptr<PlcClient> plc, std::shared_ptr<WsServer> ws) : bus_(bus), plc_(plc), ws_server_(ws) {
        last_log_time_ = std::chrono::steady_clock::now();
    }

    void run() {
        Message msg;
        while (bus_->pop(msg)) {
            try {
                // 1. PLC 狀態更新 (由後端主動推播)
                if (msg.source == "PLC" && msg.type == "STATUS") {
                    handle_plc_update(msg.payload);
                }
                // 2. 前端指令處理
                else if (msg.source == "WS" && msg.type == "CMD") {
                    handle_ws_command(msg.payload);
                }
                else if (msg.source == "WS" && msg.type == "DISCONNECTED") {
                    spdlog::warn("[Controller] UI Disconnected. Safety Reset Triggered.");
                    plc_->reset_safe_signals();
                }
                // 3. 掃碼槍輸入 (純轉發，邏輯在前端)
                else if (msg.source == "SCANNER") {
                    spdlog::info("[Controller] Scanner Input Triggered");
                }

                // 4. 統一廣播路由 (Routing)
                // 決定哪些訊息要轉發給前端
                bool should_broadcast = (
                    msg.source == "SYS" || 
                    msg.source == "WS" || 
                    // msg.source == "PLC_MONITOR" || // ⚠️ 修改：PLC 改由 handle_plc_update 內部控制廣播
                    msg.source == "SCANNER" ||
                    msg.source.rfind("CAMERA", 0) == 0 
                );

                if (should_broadcast) {
                    json wrapper;
                    if (msg.type == "HEARTBEAT") {
                        if (msg.source == "SYS") continue; 
                    } 
                    else if (msg.type == "STATE_SYNC") {
                        wrapper = {{"type", "control"}, {"command", "STATE_SYNC"}, {"payload", msg.payload}};
                    }
                    else {
                        wrapper = {{"type", "data"}, {"source", msg.source}, {"payload", msg.payload}};
                    }
                    
                    if (!wrapper.empty()) {
                        ws_server_->broadcast(wrapper.dump());
                    }
                }

            } catch (const std::exception& e) {
                spdlog::error("Controller error: {}", e.what());
            }
        }
    }

private:
    // 處理 PLC 訊號 -> 判斷是否變更 -> 廣播 & Log
    void handle_plc_update(const json& payload) {
        auto raw = payload["raw"].get<std::vector<uint8_t>>();
        int base_addr = payload["start_addr"].get<int>();
        auto& pts = Config::get().points;

        // ✅ 通用 Bit 解析函式
        auto get_bit = [&](int target_addr) {
            int offset = target_addr - base_addr;
            if (offset < 0) return false;

            int byte_idx = offset / 2;
            if (byte_idx >= raw.size()) return false;

            uint8_t val = raw[byte_idx];
            
            // ✅ [關鍵修正] Mitsubishi 規則：偶數 Offset 在高位，奇數 Offset 在低位
            bool is_even_offset = (offset % 2 == 0);

            if (is_even_offset) {
                // 偶數偏移 (例如 M542, M500) -> 取高 4 位
                return (bool)((val >> 4) & 0x01);
            } else {
                // 奇數偏移 (例如 M503, M545) -> 取低 4 位
                return (bool)(val & 0x01);
            }
        };

        // 使用 DB 設定的點位來取值
        bool up_in  = get_bit(pts.up_in);
        bool up_out = get_bit(pts.up_out);
        bool dn_in  = get_bit(pts.dn_in);
        bool dn_out = get_bit(pts.dn_out);
        bool start  = get_bit(pts.start);

        json plc_data = {
            {"up_in", up_in ? 1 : 0},
            {"up_out", up_out ? 1 : 0},
            {"dn_in", dn_in ? 1 : 0},
            {"dn_out", dn_out ? 1 : 0},
            {"start_message", start ? 1 : 0}
        };

        // ✅ 優化 1: 只有狀態「變更」時才廣播給前端 (減少網路與 Log 垃圾)
        if (plc_data != last_plc_state_) {
            spdlog::info("[PLC] Status Changed: {}", plc_data.dump());
            
            // 手動觸發廣播 (因為 run() loop 裡把 PLC_MONITOR 的自動廣播關了)
            json wrapper = {{"type", "data"}, {"source", "PLC_MONITOR"}, {"payload", plc_data}};
            ws_server_->broadcast(wrapper.dump());
            
            last_plc_state_ = plc_data;
        }

        // ✅ 優化 2: 每 5 秒在 Terminal 顯示一次狀態 (Heartbeat Log)
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time_).count() >= 5) {
            spdlog::info("[PLC] Monitor (5s): {}", plc_data.dump());
            last_log_time_ = now;
        }
    }

    void handle_ws_command(const json& cmd) {
        std::string command = cmd.value("command", "");

        if (command == "GO_NOGO") {
            int val = cmd.value("payload", 0); // 1=OK, 0=NG
            auto& pts = Config::get().points;

            spdlog::info("[Controller] Writing GO_NOGO: {}", val ? "OK" : "NG");

            plc_->write_pulse_pair(pts.write_trigger, val == 1, pts.write_result, true);
        }
        else if (command == "STEP_UPDATE") {
            // 純 Log 或者是未來擴充用
            std::string step = cmd.value("payload", "");
            spdlog::info("[Controller] Step updated to: {}", step);
        }
        // ✅ [修改] 累積模式：附加一筆資料到檔案末尾
        else if (command == "APPEND_OFFLINE_CACHE") {
            json item = cmd.value("payload", json::object());
            if (!item.empty()) {
                save_offline_cache_append(item);
            }
        }
        // ✅ [新增] 清空模式：上傳成功後清空檔案
        else if (command == "CLEAR_OFFLINE_CACHE") {
            clear_offline_cache();
        }
        // ✅ [載入] 讀取所有累積的資料
        else if (command == "LOAD_OFFLINE_CACHE") {
            load_offline_cache();
        }
    }

    // ✅ [實作] 附加寫入 (Accumulate)
    // 採用 Line-delimited JSON (每一行一個 JSON 物件)，這樣即使程式崩潰也不會壞檔
    void save_offline_cache_append(const json& item) {
        try {
            // std::ios::app 是關鍵，它會寫在檔案最後面
            std::ofstream o(CACHE_FILE, std::ios::app); 
            if (o.is_open()) {
                // dump(-1) 輸出成單行字串 (compact)
                o << item.dump(-1) << "\n"; 
                o.close();
                spdlog::info("[Controller] Offline record appended.");
            } else {
                spdlog::error("[Controller] Failed to append cache file.");
            }
        } catch (const std::exception& e) {
            spdlog::error("[Controller] Append Cache Exception: {}", e.what());
        }
    }

    // ✅ [實作] 清空檔案
    void clear_offline_cache() {
        try {
            // std::ios::trunc 會清空檔案內容
            std::ofstream o(CACHE_FILE, std::ios::trunc);
            o.close();
            spdlog::info("[Controller] Offline cache CLEARED.");
        } catch (const std::exception& e) {
            spdlog::error("[Controller] Clear Cache Exception: {}", e.what());
        }
    }

    // ✅ [實作] 載入檔案 (讀取每一行並組成 Array)
    void load_offline_cache() {
        if (!std::filesystem::exists(CACHE_FILE)) return;

        try {
            std::ifstream i(CACHE_FILE);
            if (i.is_open()) {
                json json_array = json::array();
                std::string line;
                
                // 一行一行讀取
                while (std::getline(i, line)) {
                    if (!line.empty()) {
                        try {
                            // 解析每一行的 JSON
                            json_array.push_back(json::parse(line));
                        } catch (...) {
                            spdlog::warn("[Controller] Skipped corrupted line in cache file");
                        }
                    }
                }
                i.close();

                if (json_array.empty()) return; // 沒資料就不廣播

                json response_payload = { {"type", "OFFLINE_CACHE_LOADED"}, {"data", json_array} };
                json wrapper = { {"type", "data"}, {"source", "SYS"}, {"payload", response_payload} };
                
                ws_server_->broadcast(wrapper.dump());
                spdlog::info("[Controller] Offline cache loaded. Records: {}", json_array.size());
            }
        } catch (const std::exception& e) {
            spdlog::error("[Controller] Load Cache Exception: {}", e.what());
        }
    }
};