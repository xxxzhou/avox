# 产物级护栏: 静态链 webrtc(BoringSSL) 的宿主二进制, 禁止引用 OpenSSL 符号(SSL_*/EVP_*/OPENSSL_*)
# 与加载 libssl/libcrypto —— 两套 TLS 实现混链会让 BoringSSL 对象被 OpenSSL 3 消费, 运行期以
# 野指针 SIGSEGV 告终(mac rtc 2026-09-25 排查, 见 cmake/FindZLMediaKit.cmake 注释)。
# 把这类事故从运行期提前到链接期拦截。
# 用法: include(AVOXNoOpensslCheck) 后, 对宿主可执行目标调用 avox_check_no_openssl(<target>)

function(avox_check_no_openssl target)
    if(NOT APPLE OR NOT TARGET ${target})
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DAVOX_NOSSL_BIN=$<TARGET_FILE:${target}>
            -DAVOX_NOSSL_NM=${CMAKE_NM}
            -DAVOX_NOSSL_OTOOL=${CMAKE_OTOOL}
            -P ${CMAKE_CURRENT_LIST_DIR}/AVOXNoOpensslCheck.cmake
        VERBATIM
        COMMENT "avox: check ${target} for OpenSSL leakage")
endfunction()

# ---- 以下为脚本模式(-P)入口 ----
if(NOT CMAKE_SCRIPT_MODE_FILE)
    return()
endif()

# MACOSX_BUNDLE 时 TARGET_FILE 可能给到 .app 目录, 取 Contents/MacOS 下唯一可执行
if(IS_DIRECTORY "${AVOX_NOSSL_BIN}")
    file(GLOB _avox_nossl_bins "${AVOX_NOSSL_BIN}/Contents/MacOS/*")
    list(LENGTH _avox_nossl_bins _avox_nossl_n)
    if(_avox_nossl_n EQUAL 1)
        list(GET _avox_nossl_bins 0 AVOX_NOSSL_BIN)
    endif()
endif()

set(_avox_nossl_hit "")
if(AVOX_NOSSL_NM)
    execute_process(COMMAND ${AVOX_NOSSL_NM} -u "${AVOX_NOSSL_BIN}"
        OUTPUT_VARIABLE _avox_nossl_undef ERROR_QUIET)
    if(_avox_nossl_undef MATCHES " _(SSL_|EVP_|OPENSSL_)")
        set(_avox_nossl_hit "imports OpenSSL symbols (SSL_*/EVP_*/OPENSSL_*)")
    endif()
endif()
if(NOT _avox_nossl_hit AND AVOX_NOSSL_OTOOL)
    execute_process(COMMAND ${AVOX_NOSSL_OTOOL} -L "${AVOX_NOSSL_BIN}"
        OUTPUT_VARIABLE _avox_nossl_lc ERROR_QUIET)
    if(_avox_nossl_lc MATCHES "libssl|libcrypto")
        set(_avox_nossl_hit "links libssl/libcrypto")
    endif()
endif()

if(_avox_nossl_hit)
    message(FATAL_ERROR
        "avox_no_openssl: ${AVOX_NOSSL_BIN} ${_avox_nossl_hit}\n"
        "主二进制静态链 webrtc(BoringSSL) 时禁止 OpenSSL 混链(运行期野指针崩溃)。\n"
        "排查方向: 链接行谁带进了 libssl/libcrypto(FindZLMediaKit 不再加; Agent 走 BoringSSL 分支)。")
endif()
message(STATUS "avox_no_openssl: ${AVOX_NOSSL_BIN} clean")
