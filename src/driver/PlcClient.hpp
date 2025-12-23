#pragma once
#include <algorithm>
#include <boost/asio.hpp>
#include <vector>
#include <queue>
#include <mutex>
#include "core/MessageBus.hpp"
#include <spdlog/spdlog.h>

using boost::asio::ip::tcp;

class PlcClient {
private:
    boost::asio::io_context& ioc_;
    tcp::socket socket_;
    tcp::endpoint endpoint_;
    std::shared_ptr<MessageBus> bus_;
    boost::asio::steady_timer timer_;    // 用於重連延遲
    boost::asio::steady_timer op_timer_; // ✅ 新增：用於單次操作超時
    bool connected_ = false;
    
    int start_addr_ = 0;
    int read_count_ = 0;

    struct WriteCommand {
        int address;
        bool on;
    };
    std::queue<WriteCommand> write_queue_;

public:
    PlcClient(boost::asio::io_context& ioc, std::shared_ptr<MessageBus> bus, std::string ip, int port) 
        : ioc_(ioc), socket_(ioc), bus_(bus), timer_(ioc), op_timer_(ioc) { // 初始化 op_timer_
        
        try {
            endpoint_ = tcp::endpoint(boost::asio::ip::make_address(ip), port);
        } catch (...) {
            spdlog::error("[PLC] Invalid IP Address: {}", ip);
        }

        auto& pts = Config::get().points;
        int min_addr = std::min({pts.up_in, pts.up_out, pts.dn_in, pts.dn_out, pts.start});
        int max_addr = std::max({pts.up_in, pts.up_out, pts.dn_in, pts.dn_out, pts.start});
        
        start_addr_ = (min_addr / 100) * 100;
        int needed = max_addr - start_addr_ + 20;
        read_count_ = (needed < 100) ? 100 : needed; 
        
        spdlog::info("[PLC] Auto-Range: Start M{}, Count {}", start_addr_, read_count_);
    }

    void start() { 
        boost::asio::post(ioc_, [this]() { do_connect(); });
    }

    void write_bit(int address, bool on) {
        boost::asio::post(ioc_, [this, address, on]() {
            write_queue_.push({address, on});
            // 如果當前斷線中，連線恢復後會自動消化 Queue，這裡不用做額外處理
        });
    }

private:
    void do_connect() {
        spdlog::info("[PLC] Connecting to {}...", endpoint_.address().to_string());
        
        // 設定連線超時 3秒
        op_timer_.expires_after(std::chrono::seconds(3));
        op_timer_.async_wait([this](boost::system::error_code ec){
            if (ec != boost::asio::error::operation_aborted) {
                // 時間到，強制關閉 Socket 觸發 Connect 錯誤
                socket_.close();
            }
        });

        socket_.async_connect(endpoint_, [this](boost::system::error_code ec) {
            op_timer_.cancel(); // 取消超時計時

            if (!ec) {
                spdlog::info("[PLC] Connected.");
                connected_ = true;
                process_next_action(); 
            } else {
                // 這裡不要用 handle_error，因為還沒連上
                spdlog::error("[PLC] Connect failed: {}. Retry in 3s.", ec.message());
                timer_.expires_after(std::chrono::seconds(3));
                timer_.async_wait([this](boost::system::error_code){ do_connect(); });
            }
        });
    }

    void process_next_action() {
        if (!connected_) return;

        if (!write_queue_.empty()) {
            auto cmd = write_queue_.front();
            write_queue_.pop();
            do_write_request(cmd.address, cmd.on);
        } else {
            do_read_request();
        }
    }

    // ✅ 設定操作超時 (2秒)
    void start_op_timeout() {
        op_timer_.expires_after(std::chrono::milliseconds(2000));
        op_timer_.async_wait([this](boost::system::error_code ec){
            if (ec != boost::asio::error::operation_aborted) {
                spdlog::warn("[PLC] Operation Timeout! Resetting connection...");
                socket_.close(); // 強制斷線，觸發 async_write/read 的 error handler
            }
        });
    }

    // --- 寫入流程 ---
    void do_write_request(int addr, bool on) {
        auto packet = build_write_packet(addr, on);
        auto buf = std::make_shared<std::vector<uint8_t>>(packet);

        start_op_timeout(); // 啟動計時

        boost::asio::async_write(socket_, boost::asio::buffer(*buf),
            [this, buf, addr, on](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    do_write_response(addr, on);
                } else {
                    handle_error(ec);
                }
            });
    }

    void do_write_response(int addr, bool on) {
        auto buf = std::make_shared<std::vector<uint8_t>>(1024);
        socket_.async_read_some(boost::asio::buffer(*buf),
            [this, buf, addr, on](boost::system::error_code ec, std::size_t len) {
                op_timer_.cancel(); // 操作完成，取消計時

                if (!ec) {
                    spdlog::info("[PLC] Write Success: M{} -> {}", addr, on ? "ON" : "OFF");
                    schedule_next_cycle(50); 
                } else {
                    handle_error(ec);
                }
            });
    }

    // --- 讀取流程 ---
    void do_read_request() {
        auto packet = build_read_packet(start_addr_, read_count_);
        auto buf = std::make_shared<std::vector<uint8_t>>(packet);
        
        start_op_timeout(); // 啟動計時

        boost::asio::async_write(socket_, boost::asio::buffer(*buf),
            [this, buf](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    do_read_response();
                } else {
                    handle_error(ec);
                }
            });
    }

    void do_read_response() {
        auto buf = std::make_shared<std::vector<uint8_t>>(1024);
        socket_.async_read_some(boost::asio::buffer(*buf),
            [this, buf](boost::system::error_code ec, std::size_t len) {
                op_timer_.cancel(); // 操作完成，取消計時

                if (!ec) {
                    if (len > 11) { 
                        std::vector<uint8_t> data(buf->begin() + 11, buf->begin() + len);
                        bus_->push({"PLC", "STATUS", json{{"raw", data}, {"start_addr", start_addr_}}});
                    }
                    schedule_next_cycle(200);
                } else {
                    handle_error(ec);
                }
            });
    }

    void schedule_next_cycle(int ms) {
        timer_.expires_after(std::chrono::milliseconds(ms));
        timer_.async_wait([this](boost::system::error_code ec){
            if (!ec) process_next_action();
        });
    }

    void handle_error(boost::system::error_code ec) {
        // ✅ 修正：如果是被 Timer 強制 close 的，ec 會是 operation_aborted (或其他)，這時我們不能 return，要繼續跑重連流程
        if (ec == boost::asio::error::operation_aborted && connected_) {
            // 這是被 timeout 觸發的 abort，需要重連
            spdlog::warn("[PLC] Operation aborted due to timeout.");
        } 
        else if (ec == boost::asio::error::operation_aborted) {
            // 這是程式關閉時的 abort，直接退出
            return;
        }
        else {
            // 其他網路錯誤
            spdlog::error("[PLC] Error: {} (Code: {})", ec.message(), ec.value());
        }
        
        connected_ = false;
        socket_.close();
        op_timer_.cancel(); // 確保計時器也停掉
        
        // 3秒後重連
        timer_.expires_after(std::chrono::seconds(3));
        timer_.async_wait([this](boost::system::error_code){ do_connect(); });
    }

    // (封包建立函式保持不變)
    std::vector<uint8_t> build_read_packet(int start_addr, int count) {
        std::vector<uint8_t> packet = {
            0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0C, 0x00, 0x10, 0x00,
            0x01, 0x04, 0x01, 0x00,
            (uint8_t)(start_addr & 0xFF), (uint8_t)((start_addr >> 8) & 0xFF), 0x00,
            0x90, (uint8_t)(count & 0xFF), (uint8_t)((count >> 8) & 0xFF)
        };
        return packet;
    }

    std::vector<uint8_t> build_write_packet(int addr, bool on) {
        std::vector<uint8_t> packet = {
            0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
            0x0D, 0x00, // ✅ [修正] 將 0x0C 改為 0x0D (13 bytes)
            0x10, 0x00,
            0x01, 0x14, 0x01, 0x00, 
            (uint8_t)(addr & 0xFF), (uint8_t)((addr >> 8) & 0xFF), 0x00,
            0x90, 
            0x01, 0x00, 
            (uint8_t)(on ? 0x01 : 0x00) 
        };
        return packet;
    }
};