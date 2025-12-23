#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm> // for std::min, std::max
#include <mysql.h>   // MySQL C API
#include "core/Logger.hpp"

// DB 連線設定
const char* CFG_DB_HOST = "10.8.32.64";
const char* CFG_DB_USER = "sfuser";
const char* CFG_DB_PASS = "1q2w3e4R";
const char* CFG_DB_NAME = "sfdb4070";

class Config {
public:
    struct PlcPoints {
        int up_in = 503;
        int up_out = 506;
        int dn_in = 542;
        int dn_out = 545;
        int start = 630;
        // ✅ [修正] 拆分寫入點位
        int write_result = 87;  // M87
        int write_trigger = 86; // M86
    };

    struct AppConfig {
        std::string hub_ip = ""; 
        std::string plc_ip = "10.8.142.137";
        int plc_port = 1285;
        PlcPoints points;
        std::unordered_map<std::string, std::string> camera_mapping;
    };

    static AppConfig& get() {
        static AppConfig instance;
        return instance;
    }

    // 唯一的載入入口：從 DB 讀取
    static bool load_from_db(const std::string& local_ip) {
        MYSQL* con = mysql_init(NULL);
        if (con == NULL) {
            spdlog::error("[Config] MySQL init failed");
            return false;
        }

        // ✅ [新增] 設定超時與關閉 SSL 驗證 (解決 0x800B0109)
        int timeout = 3;
        mysql_options(con, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        
        my_bool ssl_verify = 0; 
        mysql_options(con, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &ssl_verify);

        if (!mysql_real_connect(con, CFG_DB_HOST, CFG_DB_USER, CFG_DB_PASS, CFG_DB_NAME, 3306, NULL, 0)) {
            spdlog::error("[Config] DB Connection Failed: {}", mysql_error(con));
            mysql_close(con);
            return false;
        }

        auto& cfg = get();
        cfg.hub_ip = local_ip;

        // 1. 讀取 PLC 配置
        std::string sql = "SELECT plc_ip, plc_port, addr_up_in, addr_up_out, addr_dn_in, addr_dn_out, addr_start, addr_write_result, addr_write_trigger FROM 2did_machine_config WHERE hub_ip = '" + local_ip + "'";
        if (mysql_query(con, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(con);
            if (MYSQL_ROW row = mysql_fetch_row(res)) {
                if(row[0]) cfg.plc_ip = row[0];
                if(row[1]) cfg.plc_port = std::stoi(row[1]);
                if(row[2]) cfg.points.up_in = std::stoi(row[2]);
                if(row[3]) cfg.points.up_out = std::stoi(row[3]);
                if(row[4]) cfg.points.dn_in = std::stoi(row[4]);
                if(row[5]) cfg.points.dn_out = std::stoi(row[5]);
                if(row[6]) cfg.points.start = std::stoi(row[6]);
                // ✅ [修正] 讀取 M87 和 M86
                if(row[7]) cfg.points.write_result = std::stoi(row[7]);
                if(row[8]) cfg.points.write_trigger = std::stoi(row[8]);
                
                spdlog::info("[Config] PLC Loaded from DB. PLC IP: {}, Port: {}", cfg.plc_ip, cfg.plc_port);
            } else {
                spdlog::error("[Config] CRITICAL: No config found for this Hub IP: {} in table 2did_machine_config", local_ip);
                mysql_free_result(res);
                mysql_close(con);
                return false; 
            }
            mysql_free_result(res);
        } else {
            spdlog::error("[Config] Query PLC Failed: {}", mysql_error(con));
            mysql_close(con);
            return false;
        }

        // 2. 讀取相機配置
        sql = "SELECT camera_ip, camera_role FROM 2did_machine_cameras WHERE hub_ip = '" + local_ip + "'";
        if (mysql_query(con, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(con);
            cfg.camera_mapping.clear();
            while (MYSQL_ROW row = mysql_fetch_row(res)) {
                if (row[0] && row[1]) {
                    cfg.camera_mapping[row[0]] = row[1];
                    spdlog::info("[Config] Camera Mapped: {} -> {}", row[0], row[1]);
                }
            }
            mysql_free_result(res);
        }

        mysql_close(con);
        return true;
    }
};