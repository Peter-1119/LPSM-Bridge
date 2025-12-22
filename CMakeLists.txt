cmake_minimum_required(VERSION 3.20)
project(LPSM_System CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# 1. 依賴庫查找 (Dependencies)
# ==============================================================================

# A. Boost (Asio)
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# B. nlohmann_json
find_package(nlohmann_json REQUIRED)

# C. spdlog
find_package(spdlog REQUIRED)

# D. ZLIB
find_package(ZLIB REQUIRED)

# E. MariaDB (Database) - 這是 Config::load_from_db 的關鍵
find_path(MARIADB_INCLUDE_DIR NAMES mysql.h PATHS 
    C:/msys64/ucrt64/include/mariadb 
    /usr/include/mariadb 
    /usr/local/include/mariadb
)
find_library(MARIADB_LIBRARY NAMES mariadb libmariadb mariadbclient PATHS 
    C:/msys64/ucrt64/lib 
    /usr/lib 
    /usr/local/lib
)

if (MARIADB_INCLUDE_DIR AND MARIADB_LIBRARY)
    message(STATUS "Found MariaDB Lib: ${MARIADB_LIBRARY}")
    include_directories(${MARIADB_INCLUDE_DIR})
else()
    message(FATAL_ERROR "Could NOT find MariaDB. Please check your MSYS2 installation.")
endif()

# F. LibUV (uWebSockets 的底層依賴)
find_library(LIBUV_LIBRARY NAMES uv libuv PATHS C:/msys64/ucrt64/lib)
if (LIBUV_LIBRARY)
    message(STATUS "Found LibUV: ${LIBUV_LIBRARY}")
else()
    message(WARNING "Could NOT find LibUV. uSockets might fail to link.")
endif()

# ==============================================================================
# 2. uWebSockets 建置
# ==============================================================================
# 關閉 SSL 以簡化依賴 (如果內部網路不需要加密)
set(LIBUS_NO_SSL ON CACHE BOOL "" FORCE)

# 請確保您的目錄結構中有 third_party/uWebSockets/uSockets
add_subdirectory(third_party/uWebSockets/uSockets uSockets_build)
include_directories(third_party/uWebSockets/src)

# ==============================================================================
# 3. 專案原始碼
# ==============================================================================
include_directories(src)
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 建立執行檔 target
add_executable(lpsm_app ${SOURCES})

# ==============================================================================
# 4. 連結設定 (Linking)
# ==============================================================================

target_link_libraries(lpsm_app PRIVATE
    # 1. 專案核心依賴
    uSockets                        # WebSocket 底層
    ${LIBUV_LIBRARY}                # 顯式連結 LibUV (解決 undefined reference)
    Boost::boost                    # Asio
    nlohmann_json::nlohmann_json    # JSON 解析
    spdlog::spdlog                  # Log 系統
    ${MARIADB_LIBRARY}              # MySQL 連線 (Config 用)
    ZLIB::ZLIB                      # 壓縮
    
    # 2. Windows 系統底層庫 (重要!)
    ws2_32      # Winsock
    mswsock     # Windows Socket Extensions
    iphlpapi    # IP Helper API
    userenv     # User Environment
    psapi       # Process Status API
    
    # [必要] 用於鍵盤鉤子 (SetWindowsHookEx, GetMessage)
    user32      
    
    # [必要] 用於開啟 Chrome (ShellExecuteA)
    shell32     
    
    gdi32
    crypt32
)

# 建立 state 資料夾 (如果程式有用到 log 輸出目錄)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/state)