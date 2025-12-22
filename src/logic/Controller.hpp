#pragma once
#include <memory>
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
        bool up_in  = (raw[0] >> 3) & 1;
        bool up_out = (raw[0] >> 6) & 1;
        bool dn_in  = (raw[5] >> 2) & 1;
        bool dn_out = (raw[5] >> 5) & 1;
        bool start  = (raw[16] >> 2) & 1;

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
            int val = cmd.value("payload", 0);
            plc_->write_bit(86, val == 1);
        }
        else if (command == "STEP_UPDATE") {
            // 純 Log 或者是未來擴充用
            std::string step = cmd.value("payload", "");
            spdlog::info("[Controller] Step updated to: {}", step);
        }
    }
};