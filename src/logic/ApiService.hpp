#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <future> // 為了模擬 async

using json = nlohmann::json;

class ApiService {
public:
    // 模擬驗證工號
    static json validate_emp(const std::string& id) {
        // 真實情況：使用 HTTP Client (如 cpr 或 beast) 呼叫 API
        if (id == "12868") return {{"valid", true}, {"name", "王巨成"}};
        return {{"valid", false}};
    }

    // 模擬驗證工單
    static json validate_workorder(const std::string& wo) {
        if (wo == "Y04900132") {
            return {
                {"valid", true},
                {"item", "YD18379-04-A-A"},
                {"work_step", "STEP_A"},
                {"panel_num", 10},
                // 模擬回傳的產品資料 (sht_no, panel_no, steps...)
                {"panels", {
                    {{"sht_no", "4240912012548"}, {"panel_no", "P01"}, {"twodid_type", "N"}, {"twodid_step", "STEP_A"}},
                    {{"sht_no", "4240912013144"}, {"panel_no", "P02"}, {"twodid_type", "N"}, {"twodid_step", "STEP_A"}}
                }}
            };
        }
        return {{"valid", false}};
    }

    // 模擬上傳 NG/OK
    static void upload_result(const json& data) {
        // Async HTTP POST here
        // write2did API logic
    }
};