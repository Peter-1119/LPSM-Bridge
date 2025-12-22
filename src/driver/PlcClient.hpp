#pragma once
#include <algorithm>
#include <boost/asio.hpp>
#include <vector>
#include "core/MessageBus.hpp"
#include <spdlog/spdlog.h>

using boost::asio::ip::tcp;

class PlcClient {
private:
    boost::asio::io_context& ioc_;
    tcp::socket socket_;
    tcp::endpoint endpoint_;
    std::shared_ptr<MessageBus> bus_;
    boost::asio::steady_timer timer_;
    bool connected_ = false;

    int start_addr_ = 0;
    int read_count_ = 0;

public:
    PlcClient(boost::asio::io_context& ioc, std::shared_ptr<MessageBus> bus, std::string ip, int port) : ioc_(ioc), socket_(ioc), bus_(bus), timer_(ioc) {
        
        endpoint_ = tcp::endpoint(boost::asio::ip::make_address(ip), port);
        
        // ✅ 自動計算讀取範圍
        auto& pts = Config::get().points;
        int min_addr = std::min({pts.up_in, pts.up_out, pts.dn_in, pts.dn_out, pts.start});
        int max_addr = std::max({pts.up_in, pts.up_out, pts.dn_in, pts.dn_out, pts.start});
        
        // 設定起始位置為 最小值的整百位 (例如 503 -> 500)，確保涵蓋範圍
        start_addr_ = (min_addr / 100) * 100;
        
        // 計算需要的長度 (多抓一點 buffer，例如到 max_addr 後面 + 16 點)
        // 注意：一次讀取不能超過 PLC 上限 (通常 960點/bit)，這裡設安全值
        int needed = max_addr - start_addr_ + 20; 
        read_count_ = (needed < 100) ? 100 : needed; // 至少讀 100 點
        
        spdlog::info("[PLC] Auto-Range: Start M{}, Count {}", start_addr_, read_count_);
    }

    void start() { do_connect(); }

    // 寫入 M 點位 (例如 M86, M87)
    void write_bit(int address, bool on) {
        if (!connected_) return;
        auto packet = build_write_packet(address, on);
        auto buf = std::make_shared<std::vector<uint8_t>>(packet);
        boost::asio::async_write(socket_, boost::asio::buffer(*buf),
            [this, buf](boost::system::error_code ec, std::size_t) {
                if (ec) spdlog::error("[PLC] Write error: {}", ec.message());
            });
    }

private:
    void do_connect() {
        socket_.async_connect(endpoint_, [this](boost::system::error_code ec) {
            if (!ec) {
                spdlog::info("[PLC] Connected.");
                connected_ = true;
                do_poll_loop(); 
            } else {
                spdlog::error("[PLC] Connect failed. Retry in 3s.");
                timer_.expires_after(std::chrono::seconds(3));
                timer_.async_wait([this](boost::system::error_code){ do_connect(); });
            }
        });
    }

    // 建立 3E Frame 讀取封包
    std::vector<uint8_t> build_read_packet(int start_addr, int count) {
        // Subheader(5000), Net(00), PC(FF), IO(FF03), Station(00), Len(0C00), Timer(1000)
        // Cmd(0104), SubCmd(0001 - bit unit), Device(start_addr), DevCode(90=M), Count
        std::vector<uint8_t> packet = {
            0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0C, 0x00, 0x10, 0x00,
            0x01, 0x04, 0x01, 0x00,
            (uint8_t)(start_addr & 0xFF), (uint8_t)((start_addr >> 8) & 0xFF), 0x00,
            0x90, // M Code
            (uint8_t)(count & 0xFF), (uint8_t)((count >> 8) & 0xFF)
        };
        return packet;
    }

    std::vector<uint8_t> build_write_packet(int addr, bool on) {
        // Cmd(0114 - Write)
        std::vector<uint8_t> packet = {
            0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0C, 0x00, 0x10, 0x00,
            0x01, 0x14, 0x01, 0x00,
            (uint8_t)(addr & 0xFF), (uint8_t)((addr >> 8) & 0xFF), 0x00,
            0x90,
            0x01, 0x00, // 1 point
            (uint8_t)(on ? 0x10 : 0x00) // ON/OFF
        };
        return packet;
    }

    void do_poll_loop() {
        // ✅ 使用 start_addr_ 和 read_count_
        auto packet = build_read_packet(start_addr_, read_count_); 
        auto buf = std::make_shared<std::vector<uint8_t>>(packet);
        
        boost::asio::async_write(socket_, boost::asio::buffer(*buf),
            [this, buf](boost::system::error_code ec, std::size_t) {
                if (!ec) do_read_response();
                else { connected_ = false; socket_.close(); do_connect(); }
            });
    }

    void do_read_response() {
        auto buf = std::make_shared<std::vector<uint8_t>>(1024);
        socket_.async_read_some(boost::asio::buffer(*buf),
            [this, buf](boost::system::error_code ec, std::size_t len) {
                if (!ec && len > 11) {
                    std::vector<uint8_t> data(buf->begin() + 11, buf->begin() + len);
                    // ✅ 傳送當前的 start_addr_ 給 Controller 解析
                    bus_->push({"PLC", "STATUS", json{{"raw", data}, {"start_addr", start_addr_}}});
                    
                    timer_.expires_after(std::chrono::milliseconds(200));
                    timer_.async_wait([this](boost::system::error_code){ do_poll_loop(); });
                }
            });
    }
};