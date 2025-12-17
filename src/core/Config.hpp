#pragma once
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <unordered_map>
#include "core/Logger.hpp"

using json = nlohmann::json;

class Config {
public:
    struct AppConfig {
        std::string plc_ip = "10.8.142.137";
        int plc_port = 1285;
        std::unordered_map<std::string, std::string> camera_mapping;
    };

    static AppConfig& get() {
        static AppConfig instance;
        return instance;
    }

    static void load(const std::string& path = "config.json") {
        try {
            std::ifstream f(path);
            if (f.is_open()) {
                json j = json::parse(f);
                auto& cfg = get();
                // 讀取相機映射
                if (j.contains("camera_mapping")) {
                    cfg.camera_mapping = j["camera_mapping"].get<std::unordered_map<std::string, std::string>>();
                }
                spdlog::info("Config loaded.");
            } else {
                spdlog::warn("Config file not found, using defaults.");
            }
        } catch (const std::exception& e) {
            spdlog::error("Config load error: {}", e.what());
        }
    }
};