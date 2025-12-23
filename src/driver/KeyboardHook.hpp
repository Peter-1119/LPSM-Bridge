#pragma once
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include "core/MessageBus.hpp"
#include "core/Logger.hpp"

// 全域變數是 Windows Hook 的限制，因為 Callback 必須是靜態的
static HHOOK g_hHook = NULL;
static std::shared_ptr<MessageBus> g_bus_ref = nullptr;
static std::string g_barcode_buffer;
static std::mutex g_hook_mutex;

// 鍵盤鉤子回呼函式
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT *)lParam;

        if (wParam == WM_KEYDOWN) {
            DWORD vkCode = p->vkCode;

            // 1. 處理 Enter (掃碼結束)
            if (vkCode == VK_RETURN) {
                std::lock_guard<std::mutex> lock(g_hook_mutex);
                if (!g_barcode_buffer.empty()) {
                    // ✅ [需求2] 掃到條碼直接輸出文字
                    spdlog::info("[Scanner] Detected: {}", g_barcode_buffer);
                    
                    if (g_bus_ref) {
                        if (g_barcode_buffer.compare("1") == 0) {
                            spdlog::info("[Broadcast] Camera Test Input: 4240912013144");
                            g_bus_ref->push({"CAMERA_LEFT_1", "DATA", "4240912013144"});
                        }
                        else if (g_barcode_buffer.compare("2") == 0) {
                            spdlog::info("[Broadcast] Camera Test Input: 4240912012548");
                            g_bus_ref->push({"CAMERA_RIGHT_1", "DATA", "4240912012548"});
                        }
                        else if (g_barcode_buffer.compare("3") == 0) {
                            spdlog::info("[Broadcast] Camera Test Input: 4240913025717");
                            g_bus_ref->push({"CAMERA_RIGHT_1", "DATA", "4240913025717"});
                        }
                        else if (g_barcode_buffer.compare("4") == 0) {
                            spdlog::info("[Broadcast] Camera Test Input: 4240914001156");
                            g_bus_ref->push({"CAMERA_RIGHT_2", "DATA", "4240914001156"});
                        }
                        else if (g_barcode_buffer.compare("0") == 0) {
                            spdlog::info("[Broadcast] Camera Test Input: TIMEOUT_BLANK");
                            g_bus_ref->push({"CAMERA_LEFT_GROUP_MONITOR", "DATA", "TIMEOUT_BLANK"});
                            g_bus_ref->push({"CAMERA_RIGHT_GROUP_MONITOR", "DATA", "TIMEOUT_BLANK"});
                        }
                        else {
                            spdlog::info("[Broadcast] Keyboard Input: {}", g_barcode_buffer);
                            g_bus_ref->push({"SCANNER", "DATA", g_barcode_buffer});
                        }
                    }
                    g_barcode_buffer.clear();
                }
            }
            // 2. 處理一般字元 (0-9, A-Z)
            else {
                // 取得鍵盤狀態 (處理 Shift 大小寫)
                BYTE keyboardState[256];
                GetKeyboardState(keyboardState);
                WORD asciiBuffer[2];
                
                // 轉換 Virtual Key 為 ASCII
                int len = ToAscii(vkCode, p->scanCode, keyboardState, asciiBuffer, 0);
                if (len == 1) {
                    char c = (char)asciiBuffer[0];
                    // 過濾掉不可見字元，只留條碼常用的數字與字母
                    if (isalnum(c) || c == '-' || c == '_') {
                        std::lock_guard<std::mutex> lock(g_hook_mutex);
                        g_barcode_buffer += c;
                    }
                }
            }
        }
    }
    // 3. 重要：傳遞給下一個鉤子，確保不阻擋使用者打字
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

class KeyboardHook {
public:
    KeyboardHook(std::shared_ptr<MessageBus> bus) {
        g_bus_ref = bus;
    }

    // 啟動 Hook 執行緒
    void start() {
        std::thread t([](){
            spdlog::info("[Hook] Starting Global Keyboard Hook...");
            
            // 安裝鉤子
            g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
            
            if (!g_hHook) {
                spdlog::error("[Hook] Failed to install hook!");
                return;
            }

            // Windows Hook 需要 Message Loop 才能運作
            MSG msg;
            while (GetMessage(&msg, NULL, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            UnhookWindowsHookEx(g_hHook);
        });
        t.detach(); // 讓它在背景跑
    }
};