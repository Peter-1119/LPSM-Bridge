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

# E. MariaDB
find_path(MARIADB_INCLUDE_DIR NAMES mysql.h PATHS /usr/include/mariadb /usr/local/include/mariadb C:/msys64/ucrt64/include/mariadb)
find_library(MARIADB_LIBRARY NAMES mariadb libmariadb mariadbclient PATHS /usr/lib /usr/local/lib C:/msys64/ucrt64/lib)

if (MARIADB_INCLUDE_DIR AND MARIADB_LIBRARY)
    message(STATUS "Found MariaDB: ${MARIADB_LIBRARY}")
    include_directories(${MARIADB_INCLUDE_DIR})
else()
    message(FATAL_ERROR "Could NOT find MariaDB.")
endif()

# F. LibUV (新增查找，確保找到 library)
find_library(LIBUV_LIBRARY NAMES uv libuv)

# ==============================================================================
# 2. uWebSockets 建置
# ==============================================================================
set(LIBUS_NO_SSL ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/uWebSockets/uSockets uSockets_build)
include_directories(third_party/uWebSockets/src)

# ==============================================================================
# 3. 專案原始碼
# ==============================================================================
include_directories(src)
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(lpsm_app ${SOURCES})

# ==============================================================================
# 4. 連結設定 (Linking) - 修正重點
# ==============================================================================

target_link_libraries(lpsm_app PRIVATE
    # 專案依賴
    uSockets
    Boost::boost
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    ${MARIADB_LIBRARY}
    ZLIB::ZLIB
    
    # [修正 1] 顯式連結 libuv (解決 undefined reference to `uv_...`)
    uv
    
    # [修正 2] 連結 mswsock (解決 undefined reference to `AcceptEx`)
    mswsock
    
    # Windows 系統底層庫
    ws2_32
    crypt32
    iphlpapi
    userenv
    psapi
    user32  # <--- 建議加入這個，確保 Hook API 可用
    gdi32
)

# 複製設定檔與建立資料夾
configure_file(${CMAKE_SOURCE_DIR}/config.json ${CMAKE_BINARY_DIR}/config.json COPYONLY)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/state)