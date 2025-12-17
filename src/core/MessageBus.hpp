// src/core/MessageBus.hpp
#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Message {
    std::string source;  // "PLC", "CAM_L", "WS"
    std::string type;    // "DATA", "CMD", "LOG"
    json payload;        // 統一使用 JSON 傳遞數據
};

class MessageBus {
private:
    std::queue<Message> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool stop_ = false;

public:
    void push(const Message& msg) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(msg);
        }
        cond_.notify_one();
    }

    // 阻塞式獲取，適合 Logic Thread 使用
    bool pop(Message& msg) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]{ return !queue_.empty() || stop_; });
        
        if (stop_ && queue_.empty()) return false;
        
        msg = queue_.front();
        queue_.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cond_.notify_all();
    }
};