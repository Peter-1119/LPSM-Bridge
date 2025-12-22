#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <thread>
#include <string>
#include <cstdlib> 
#include <atomic>
#include <csignal>

// 引入您自定義的標頭檔
#include "core/Logger.hpp"
#include "core/MessageBus.hpp"
#include "core/Config.hpp"
#include "driver/PlcClient.hpp"
#include "driver/CamServer.hpp"
#include "driver/KeyboardHook.hpp"
#include "server/WsServer.hpp"
#include "logic/Controller.hpp"

// 全域變數
std::atomic<bool> g_running{true};

// ---------------------------------------------------------
// 1. 強制殺死佔用 Port 的殭屍行程
// ---------------------------------------------------------
void KillProcessOnPort(int port) {
    std::string cmd = "for /f \"tokens=5\" %a in ('netstat -aon ^| find \":" + std::to_string(port) + "\" ^| find \"LISTENING\"') do taskkill /f /pid %a > nul 2>&1";
    system(cmd.c_str());
}

// ---------------------------------------------------------
// 2. 攔截右上角 X 與 Ctrl+C 訊號
// ---------------------------------------------------------
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        // ✅ [修正] 移除 Sleep，讓反應更即時
        // 我們只設定旗標，剩下的交給主迴圈的 TerminateProcess 處理
        g_running = false;
        return TRUE; 
    }
    return FALSE;
}

void OpenChromeOnWindows(std::string url) {
    #ifdef _WIN32
        ShellExecuteA(NULL, "open", "chrome.exe", url.c_str(), NULL, SW_SHOW);
    #else
        std::cout << "無法自動開啟 Chrome 此功能僅支援 Windows" << std::endl;
        return;
    #endif
}

int main() {
    // 1. 設定編碼
    SetConsoleOutputCP(65001);

    // 2. 清理舊的殭屍 (包含殺死 Chrome，這很重要，確保環境乾淨)
    // 建議加入殺死 chrome 的指令，避免舊的 Chrome 還是卡著 port
    system("taskkill /F /IM chrome.exe >nul 2>&1"); 
    KillProcessOnPort(8181); 
    KillProcessOnPort(6060); 
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // ✅ [關鍵修改] 3. 在任何 Socket 建立之前，先開啟網頁！
    // 這樣 Chrome 啟動時，我們手上還沒有任何 Socket，它就無法繼承，也不會卡住 Port。
    OpenChromeOnWindows("http://10.8.32.64:2102/");
    
    // 給瀏覽器一點時間啟動
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    // 4. 註冊關閉處理
    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        std::cerr << "錯誤: 無法註冊控制台處理程序" << std::endl;
    }

    // 5. 初始化與 Socket 建立 (現在執行這些是安全的)
    Logger::init();
    Config::load();
    spdlog::info("LPSM System Starting...");
    
    auto bus = std::make_shared<MessageBus>();
    boost::asio::io_context ioc; 

    // 這些物件內部會建立 Socket，現在 Chrome 已經開好了，不會影響這些新 Socket
    auto plc = std::make_shared<PlcClient>(ioc, bus, Config::get().plc_ip, Config::get().plc_port);
    auto cam = std::make_shared<CamServer>(ioc, bus, 6060);
    auto ws_server = std::make_shared<WsServer>(bus);
    auto controller = std::make_shared<Controller>(bus, plc, ws_server);

    plc->start();
    std::thread io_thread([&ioc](){ 
        auto work = boost::asio::make_work_guard(ioc);
        ioc.run(); 
    });

    // 5. 啟動全域鍵盤監聽
    KeyboardHook scanner_hook(bus);
    scanner_hook.start();

    // 給瀏覽器一點時間啟動
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    spdlog::info("LPSM System Started.");
    spdlog::info("--------------------------------------------------");
    spdlog::info("系統已就緒。現場人員可直接按右上角 [X] 關閉程式。");
    spdlog::info("--------------------------------------------------");

    // 啟動執行緒 (Socket 在這裡才真正被 bind)
    std::thread logic_thread([controller](){ controller->run(); });
    std::thread ws_thread([ws_server](){ ws_server->run(8181); }); // 這裡才佔用 8181

    // 6. 主迴圈：等待 g_running 變為 false (當按下 X 時)
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 7. 退出前的清理
    // 這裡我們只做個紀錄，不再嘗試停止 boost/ws，因為可能會卡住
    spdlog::info("[System] Stopping services and exiting...");
    
    // ✅ [修正 2] 核彈級退出：TerminateProcess
    // 解決「按 X 卡住」、「需要按兩次」的問題。
    // 這會直接告訴作業系統：立刻、馬上把這個 Process 從記憶體抹除。
    // 因為我們開機時有 KillProcessOnPort 保護，所以這裡暴力退出是安全的。
    TerminateProcess(GetCurrentProcess(), 0);

    return 0;
}