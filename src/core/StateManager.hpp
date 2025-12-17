#pragma once
#include <nlohmann/json.hpp>
#include <mutex>
#include <fstream>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <vector>
#include "core/Logger.hpp"

using json = nlohmann::json;

class StateManager {
private:
    json state_;
    std::mutex mutex_;
    const std::string log_path_ = "state/scan-ui.events.jsonl";
    
    json default_state_;

public:
    StateManager() {
        std::filesystem::create_directories("state");
        
        // 1. 定義初始狀態 (修正 JSON 括號)
        default_state_ = {
            {"stage", "empId"},
            {"info", {
                {"employeeId", ""}, {"employeeName", ""}, {"workOrder", ""},
                {"productItem", ""}, {"workStep", ""}, 
                {"panel_num", 0}, {"OK_num", 0}, {"NG_num", 0},
                {"cmd236_flag", false}
            }},
            {"plc", {
                {"up_in", -1}, {"up_out", -1}, {"dn_in", -1}, {"dn_out", -1}, {"start_message", -1}
            }},
            {"requested_2DID_dict", json::object()},
            {"expected_2DID_dict", json::object()},
            {"scanned_2DID_dict", json::object()},
            {"other_2DID_dict", json::object()},
            {"scan_history", json::array()},
            {"side_history_2DID_list", {{"left", json::array()}, {"right", json::array()}}},
            {"history_2DID_dict", json::object()},
            {"forceCommand", {{"command", false}, {"triggerTime", nullptr}}},
            // 修正這裡的括號層級
            {"up_platform_2DID", {
                {"left", {{"pdcode",""},{"ret_type",""},{"detail",""}}}, 
                {"right", {{"pdcode",""},{"ret_type",""},{"detail",""}}}
            }},
            {"dn_platform_2DID", {
                {"left", {{"pdcode",""},{"ret_type",""},{"detail",""}}}, 
                {"right", {{"pdcode",""},{"ret_type",""},{"detail",""}}}
            }}
        };

        state_ = default_state_;
        load_from_disk();
    }

    json get_copy() {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    void apply_patch(const json& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            std::ofstream file(log_path_, std::ios::app);
            if (file.is_open()) {
                json log_evt = event;
                if (!log_evt.contains("ts")) {
                    log_evt["ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                }
                file << log_evt.dump() << "\n";
            }
        } catch (...) {
            spdlog::error("[StateManager] Write log failed");
        }
        apply_patch_internal(event);
    }

    void update_plc_memory(const json& plc_data) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_["plc"] = plc_data;
    }

    template<typename T>
    T get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_.value(key, T());
    }

private:
    void apply_patch_internal(const json& ev) {
        try {
            std::string op = ev.value("op", "");
            
            if (op == "RESET_ALL") {
                state_ = default_state_;
                return;
            }

            json* target = &state_;
            std::vector<std::string> path;
            if (ev.contains("path") && ev["path"].is_array()) {
                path = ev["path"].get<std::vector<std::string>>();
            }

            for (size_t i = 0; i < path.size() - 1; ++i) {
                if (!target->contains(path[i])) (*target)[path[i]] = json::object();
                target = &((*target)[path[i]]);
            }
            std::string last_key = path.empty() ? "" : path.back();

            if (op == "SET" || op == "DICT_SET") {
                if (ev.contains("key")) {
                    if (!last_key.empty()) target = &((*target)[last_key]);
                    (*target)[ev["key"].get<std::string>()] = ev["value"];
                } else {
                    if (!last_key.empty()) (*target)[last_key] = ev["value"];
                }
            }
            else if (op == "DICT_DEL") {
                if (!last_key.empty()) target = &((*target)[last_key]);
                if (ev.contains("key")) target->erase(ev["key"].get<std::string>());
            }
            else if (op == "DICT_CLEAR") {
                if (!last_key.empty()) (*target)[last_key] = json::object();
            }
            else if (op == "LIST_UNSHIFT") {
                if (!last_key.empty()) target = &((*target)[last_key]);
                if (target->is_array()) {
                    target->insert(target->begin(), ev["value"]);
                    int max_len = ev.value("max_len", 500);
                    if (target->size() > max_len) {
                        target->erase(target->begin() + max_len, target->end());
                    }
                }
            }
            else if (op == "LIST_CLEAR") {
                if (!last_key.empty()) (*target)[last_key] = json::array();
            }

        } catch (const std::exception& e) {
            spdlog::error("[StateManager] Apply patch error: {}", e.what());
        }
    }

    void load_from_disk() {
        if (!std::filesystem::exists(log_path_)) return;
        
        std::ifstream f(log_path_);
        std::string line;
        int count = 0;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            try {
                json ev = json::parse(line);
                apply_patch_internal(ev); 
                count++;
            } catch (...) {}
        }
        spdlog::info("[StateManager] Restored {} events from disk.", count);
    }
};