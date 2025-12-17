#pragma once
#include <memory>
#include "core/MessageBus.hpp"
#include "core/StateManager.hpp"
#include "driver/PlcClient.hpp"
#include "core/Config.hpp"
#include "server/WsServer.hpp"

class Controller {
private:
    std::shared_ptr<MessageBus> bus_;
    std::shared_ptr<StateManager> state_;
    std::shared_ptr<PlcClient> plc_;
    std::shared_ptr<WsServer> ws_server_;

public:
    Controller(std::shared_ptr<MessageBus> bus, std::shared_ptr<StateManager> state, std::shared_ptr<PlcClient> plc, std::shared_ptr<WsServer> ws)
        : bus_(bus), state_(state), plc_(plc), ws_server_(ws) {}

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
                    // 這裡其實不需要做什麼，因為下面的廣播邏輯會處理
                    // 但為了 debug，我們留個 log
                    spdlog::info("[Controller] Scanner Input Triggered");
                }

                // 4. 統一廣播路由 (Routing)
                // 決定哪些訊息要轉發給前端
                bool should_broadcast = (
                    msg.source == "SYS" || 
                    msg.source == "WS" || 
                    msg.source == "PLC_MONITOR" || 
                    msg.source == "SCANNER" ||
                    msg.source.rfind("CAMERA", 0) == 0 // 所有 CAMERA 開頭的都轉發
                );

                if (should_broadcast) {
                    json wrapper;
                    if (msg.type == "HEARTBEAT") {
                        if (msg.source == "SYS") continue; // 內部心跳不廣播
                    } 
                    else if (msg.type == "STATE_SYNC") {
                        wrapper = {{"type", "control"}, {"command", "STATE_SYNC"}, {"payload", msg.payload}};
                    }
                    else {
                        // 預設為 data 類型 (給前端 processControl / handleInfoInput 用)
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
    // 處理 PLC 訊號 -> 更新記憶體 -> 準備廣播
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

        // 更新後端記憶體 (以備前端 F5 重整時查詢)
        state_->update_plc_memory(plc_data);
        
        // 推入 Bus，讓 run() loop 中的廣播邏輯處理
        bus_->push({"PLC_MONITOR", "DATA", plc_data});
    }

    void handle_ws_command(const json& cmd) {
        std::string command = cmd.value("command", "");
        
        if (command == "LOAD_STATE") {
            // 只有這裡會主動發送 STATE_SYNC
            bus_->push({"SYS", "STATE_SYNC", state_->get_copy()});
        }
        else if (command == "GO_NOGO") {
            int val = cmd.value("payload", 0);
            plc_->write_bit(86, val == 1);
        }
        else if (command == "STATE_PATCH") {
            // 前端通知狀態變更 -> 寫入後端 DB (Persistence)
            // 這裡完全不廣播，因為前端自己知道
            auto payload = cmd["payload"];
            if (payload.contains("events") && payload["events"].is_array()) {
                for (const auto& evt : payload["events"]) {
                    state_->apply_patch(evt);
                }
            }
        }
        else if (command == "STEP_UPDATE") {
            // 純粹給後端更新記憶體中的 Stage (如果需要的話)
            // 不做任何廣播
            std::string step = cmd.value("payload", "");
            // state_->update_and_log("SET", "stage", step); // 其實連這行也可以不用，因為 STATE_PATCH 會做
        }
    }
};