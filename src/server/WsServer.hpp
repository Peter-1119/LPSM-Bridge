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
    uWS::App* app_ptr = nullptr; // 儲存 App 指標以便廣播

public:
    WsServer(std::shared_ptr<MessageBus> bus) : bus_(bus) {}

    // ✅ 新增公開廣播介面
    void broadcast(const std::string& message) {
        if (app_ptr) {
            // uWS::App::publish 是 thread-safe 的
            app_ptr->publish("broadcast", message, uWS::OpCode::TEXT, false);
            spdlog::info("[WS] SEND Broadcast: {}", message);
        }
    }

    void run(int port) {
        uWS::App app;
        app_ptr = &app; // 儲存指標

        // 1. Heartbeat 生產者 (維持原樣)
        std::thread hb_thread([this](){
            while(running_) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                bus_->push({"SYS", "HEARTBEAT", json{{"ts", std::time(nullptr)}}});
            }
        });

        // ❌ 移除 broadcast_thread，不再跟 Controller 搶訊息

        // 2. uWebSockets Server
        app.ws<PerSocketData>("/*", {
            .open = [](auto *ws) {
                ws->subscribe("broadcast"); 
                spdlog::info("[WS] Client connected");
                json welcome = {{"type", "info"}, {"message", "Connected to LPSM Backend"}};
                ws->send(welcome.dump(), uWS::OpCode::TEXT, false);
            },
            .message = [this](auto *ws, std::string_view message, uWS::OpCode opCode) {
                std::string msg_str(message);
                spdlog::info("[WS] RECV: {}", msg_str);
                try {
                    auto j = json::parse(message);
                    if (j.contains("command") && j["command"] == "HEARTBEAT") {
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

        running_ = false;
        app_ptr = nullptr;
        if(hb_thread.joinable()) hb_thread.join();
    }
};