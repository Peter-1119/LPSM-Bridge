#include <iostream>
#include <thread>
#include <string>

// 引入您自定義的標頭檔
#include "core/Logger.hpp"
#include "core/MessageBus.hpp"
#include "core/StateManager.hpp"
#include "core/Config.hpp"
#include "driver/PlcClient.hpp"
#include "driver/CamServer.hpp"
#include "driver/KeyboardHook.hpp"
#include "server/WsServer.hpp"
#include "logic/Controller.hpp"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <shellapi.h>
#endif

// 上位機 10.8.142.192@myuser no password

void OpenChromeOnWindows(std::string url) {
    std::string command;
    #ifdef _WIN32
        ShellExecuteA(NULL, "open", "chrome.exe", url.c_str(), NULL, SW_SHOW);
    
    #else
        std::cout << "無法自動開啟 Chrome 此功能僅支援 Windows" << std::endl;
        return;

    #endif
}

int main() {
    // 1. 初始化
    Logger::init();
    Config::load();
    
    auto bus = std::make_shared<MessageBus>();
    auto state = std::make_shared<StateManager>();
    boost::asio::io_context ioc; // 需要 #include <boost/asio.hpp>

    auto plc = std::make_shared<PlcClient>(ioc, bus, Config::get().plc_ip, Config::get().plc_port);
    auto cam = std::make_shared<CamServer>(ioc, bus, 6060);
    
    // ✅ 1. 先建立 WsServer (但還不跑 run)
    auto ws_server = std::make_shared<WsServer>(bus);

    // ✅ 2. 建立 Controller (注入 ws_server)
    auto controller = std::make_shared<Controller>(bus, state, plc, ws_server);
    
    // ✅ 3. 啟動 Controller 執行緒
    std::thread logic_thread([controller](){ controller->run(); });

    // ✅ 4. 啟動 WsServer 執行緒
    std::thread ws_thread([ws_server](){ ws_server->run(8181); });

    // 啟動 PLC/Cam IO
    plc->start();
    std::thread io_thread([&ioc](){ 
        auto work = boost::asio::make_work_guard(ioc);
        ioc.run(); 
    });

    // 5. ✅ 啟動全域鍵盤監聽 (Hook)
    KeyboardHook scanner_hook(bus);
    scanner_hook.start();
    OpenChromeOnWindows("http://localhost:5173/");

    spdlog::info("LPSM System Started. Scanner Hook Active.");
    spdlog::info("You can scan barcodes even if this window is not focused.");

    // 6. 主執行緒卡住防止結束
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // (理論上跑不到這裡，除非強制結束)
    return 0;
}