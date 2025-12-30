#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <thread>
#include <string>
#include <cstdlib> 
#include <atomic>
#include <csignal>
#include <boost/asio.hpp> 

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

void KillProcessOnPort(int port) {
    std::string cmd = "for /f \"tokens=5\" %a in ('netstat -aon ^| find \":" + std::to_string(port) + "\" ^| find \"LISTENING\"') do taskkill /f /pid %a > nul 2>&1";
    system(cmd.c_str());
}

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
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

// 修改原本的 OpenChromeOnWindows
void OpenEdgeOnWindows(std::string url) {
    #ifdef _WIN32
        // 將 "chrome.exe" 改為 "msedge.exe"
        ShellExecuteA(NULL, "open", "msedge.exe", url.c_str(), NULL, SW_SHOW);
    #else
        std::cout << "無法自動開啟 Edge 此功能僅支援 Windows" << std::endl;
        return;
    #endif
}

std::string GetLocalIP() {
    try {
        boost::asio::io_context io_context;
        boost::asio::ip::udp::resolver resolver(io_context);
        boost::asio::ip::udp::socket socket(io_context);
        
        // ✅ [修正] 使用 make_address 替代 from_string
        socket.connect(boost::asio::ip::udp::endpoint(boost::asio::ip::make_address("8.8.8.8"), 53));
        
        return socket.local_endpoint().address().to_string();
    } catch (std::exception& e) {
        spdlog::error("Failed to get local IP: {}", e.what());
        return "127.0.0.1"; 
    }
}

int main() {
    SetConsoleOutputCP(65001);

    // 1. 環境清理 (包含關閉殘留的 Edge)
    system("taskkill /F /IM msedge.exe >nul 2>&1");
    KillProcessOnPort(8181);  // Websocket port
    KillProcessOnPort(6060);  // Camera port
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 2. 開啟網頁 (請確認現場是否連得到這個 IP)
    // OpenEdgeOnWindows("http://10.8.32.64:2102/");
    OpenEdgeOnWindows("http://localhost:5173/");
    
    // 給瀏覽器足夠時間啟動，避免 Port 搶佔問題
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        std::cerr << "錯誤: 無法註冊控制台處理程序" << std::endl;
    }

    Logger::init();

    // 3. 取得本機 IP 並載入設定
    std::string my_ip = GetLocalIP();
    spdlog::info("Detected Local IP: {}", my_ip);

    if (!Config::load_from_db(my_ip)) {
        spdlog::error("❌ 無法從資料庫載入設定！可能是 IP 未註冊或 DB 連線失敗。");
        spdlog::error("程式將在 10 秒後退出...");
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return -1; // 強制結束
    }

    spdlog::info("LPSM System Starting...");
    
    auto bus = std::make_shared<MessageBus>();
    boost::asio::io_context ioc; 

    // 4. 使用動態 IP 建立元件
    auto plc = std::make_shared<PlcClient>(ioc, bus, Config::get().plc_ip, Config::get().plc_port);
    auto cam = std::make_shared<CamServer>(ioc, bus, 6060);
    auto ws_server = std::make_shared<WsServer>(bus);
    auto controller = std::make_shared<Controller>(bus, plc, ws_server);

    // 5. 啟動所有執行緒
    plc->start();
    std::thread io_thread([&ioc](){ 
        auto work = boost::asio::make_work_guard(ioc);
        ioc.run(); 
    });

    KeyboardHook scanner_hook(bus);
    scanner_hook.start();

    std::thread logic_thread([controller](){ controller->run(); });
    std::thread ws_thread([ws_server](){ ws_server->run(8181); });

    spdlog::info("LPSM System Started. Press [X] to exit.");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    spdlog::info("[System] Stopping services and exiting...");
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}