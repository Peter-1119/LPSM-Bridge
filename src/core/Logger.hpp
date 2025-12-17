#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>
#include <memory>

class Logger {
public:
    static void init() {
        // 使用異步 Logger 以避免 I/O 阻塞主執行緒
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        spdlog::init_thread_pool(8192, 1);
        auto logger = std::make_shared<spdlog::async_logger>("app", console_sink, spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [thread %t] %v");
        spdlog::set_level(spdlog::level::info);
    }
};