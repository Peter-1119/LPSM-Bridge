#pragma once
#include <boost/asio.hpp>
#include <spdlog/spdlog.h>
#include "core/MessageBus.hpp"
#include "core/Config.hpp" // 需要讀取 Config

using boost::asio::ip::tcp;

class CamSession : public std::enable_shared_from_this<CamSession> {
    tcp::socket socket_;
    std::shared_ptr<MessageBus> bus_;
    char data_[1024];
    boost::asio::steady_timer timeout_timer_;
    std::string client_id_;

public:
    CamSession(tcp::socket socket, std::shared_ptr<MessageBus> bus, boost::asio::io_context& ioc) : socket_(std::move(socket)), bus_(bus), timeout_timer_(ioc) {
        // ✅ [關鍵] 取得 IP 並映射到 Config 中的名稱 (e.g. CAMERA_LEFT_1)
        try {
            std::string ip = socket_.remote_endpoint().address().to_string();
            auto& mapping = Config::get().camera_mapping;
            
            // 如果 Config 有設定這個 IP，就使用設定的名稱 (如 CAMERA_LEFT_1)
            // 這樣前端 App.vue: if (source.startsWith("CAMERA_LEFT")) 才能正確運作
            if (mapping.count(ip)) {
                client_id_ = mapping[ip];
            } else {
                client_id_ = "CAMERA_UNKNOWN_" + ip;
                spdlog::warn("[CAM] Unknown Camera IP: {}, please check config.json", ip);
            }
        } catch(...) {
            client_id_ = "CAMERA_ERROR";
        }
    }

    void start() {
        spdlog::info("[CAM] Connected: {}", client_id_);
        do_read();
        reset_timeout();
    }

private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_), [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                std::string barcode(data_, length);
                // 移除換行符
                barcode.erase(std::remove(barcode.begin(), barcode.end(), '\n'), barcode.end());
                barcode.erase(std::remove(barcode.begin(), barcode.end(), '\r'), barcode.end());
                
                if (!barcode.empty()) {
                    spdlog::info("[CAM] {} Recv: {}", client_id_, barcode);
                    // ✅ 直接送字串，Controller 不用處理，WsServer 會自動轉發給前端
                    bus_->push({ client_id_, "BARCODE", barcode });
                }
                
                reset_timeout();
                do_read();
            }
        });
    }

    void reset_timeout() {
        timeout_timer_.expires_after(std::chrono::seconds(1));
        timeout_timer_.async_wait([this, self=shared_from_this()](boost::system::error_code ec){
            if (!ec) {
                // Send timeout signal
                bus_->push({ client_id_ + "_MONITOR", "TIMEOUT", "TIMEOUT_BLANK" });
                // Start timer again
                reset_timeout();
            }
        });
    }
};

class CamServer {
    boost::asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<MessageBus> bus_;

public:
    CamServer(boost::asio::io_context& ioc, std::shared_ptr<MessageBus> bus, int port) : ioc_(ioc), acceptor_(ioc, tcp::endpoint(tcp::v4(), port)), bus_(bus) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<CamSession>(std::move(socket), bus_, ioc_)->start();
            }
            do_accept();
        });
    }
};