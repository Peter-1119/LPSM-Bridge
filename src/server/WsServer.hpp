#pragma once
#include "App.h"
#include "core/MessageBus.hpp"
#include "core/Logger.hpp" 
#include <thread>
#include <atomic>
#include <chrono>
#include <string>

class WsServer {
    std::shared_ptr<MessageBus> bus_;
    struct PerSocketData {}; 
    std::atomic<bool> running_{true};
    
    // ✅ 儲存 uWS 的 Loop 指標，用來做跨執行緒排程
    uWS::Loop *loop_ = nullptr;
    uWS::App* app_ptr = nullptr;

public:
    WsServer(std::shared_ptr<MessageBus> bus) : bus_(bus) {}

    // ✅ 修改後的廣播介面：使用 defer 將任務丟回 WS 執行緒
    void broadcast(const std::string& message) {
        // 必須檢查 loop_ 是否存在 (Server 啟動後才有)
        if (loop_ && app_ptr) {
            // copy message (因為是非同步執行，必須複製一份字串)
            // defer 會讓這個 lambda 在 WS 執行緒的安全時間點執行
            loop_->defer([this, msg = message]() {
                if (app_ptr) {
                    app_ptr->publish("broadcast", msg, uWS::OpCode::TEXT, false);
                    spdlog::info("[WS] SEND Broadcast: {}", msg);
                }
            });
        }
    }

    void run(int port) {
        // ✅ 1. 獲取當前執行緒的 Event Loop
        // 注意：這行必須在 run 的這個執行緒內呼叫
        loop_ = uWS::Loop::get();

        uWS::App app;
        app_ptr = &app;

        std::thread hb_thread([this](){
            while(running_) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                // 這裡只負責推 Event 到 Bus，不直接廣播，所以是安全的
                bus_->push({"SYS", "HEARTBEAT", json{{"ts", std::time(nullptr)}}});
            }
        });

        app.ws<PerSocketData>("/*", {
            .open = [](auto *ws) {
                ws->subscribe("broadcast"); 
                spdlog::info("[WS] Client connected");
                json welcome = {{"type", "info"}, {"message", "Connected to LPSM Backend"}};
                ws->send(welcome.dump(), uWS::OpCode::TEXT, false);
            },
            .message = [this](auto *ws, std::string_view message, uWS::OpCode opCode) {
                std::string msg_str(message);
                // spdlog::info("[WS] RECV: {}", msg_str); // 怕太吵可以註解掉
                try {
                    auto j = json::parse(message);
                    if (j.contains("command") && j["command"] == "HEARTBEAT") {
                        // 回傳 ACK
                        json ack = {{"type", "control"}, {"command", "HEARTBEAT_ACK"}, {"payload", {{"server_ts", std::time(nullptr) * 1000}, {"client_ts", j["payload"].value("ts", 0)}}}};
                        ws->send(ack.dump(), uWS::OpCode::TEXT, false);
                        return; 
                    }
                    bus_->push({ "WS", "CMD", j });
                } catch(...) {}
            },
            .close = [](auto *ws, int code, std::string_view message) {
                spdlog::info("[WS] Client disconnected");
            }
        }).listen("0.0.0.0", port, [port](auto *listen_socket) {
            if (listen_socket) spdlog::info("[WS] Server listening on port {}", port);
            else spdlog::error("[WS] FAILED to listen on port {}!", port);
        }).run();

        // 結束時清理
        running_ = false;
        app_ptr = nullptr;
        loop_ = nullptr; // ✅ 清空 Loop 指標
        
        if(hb_thread.joinable()) hb_thread.join();
    }
};