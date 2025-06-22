#include "curl.h"
#include "hilog/log.h"
#include "napi/native_api.h"
#include <fstream>
#include <map>
#include <sstream>
#include <string>

/**
 * @file napi_gmcurl.cpp
 * @brief 基于 libcurl 的 N-API HTTP 请求模块实现
 *
 * 本文件实现了基于 libcurl 的 HTTP/HTTPS 请求处理模块，封装了同步与异步请求、TLS/SSL 加密通信、
 * 国密协议（TLCP）支持、客户端证书双向认证等功能，并通过 Node.js N-API 提供给上层 JS 调用。
 *
 * 主要功能特性：
 * - 支持 GET / POST / PUT / DELETE 等常见 HTTP 方法
 * - 支持 JSON、表单数据、ArrayBuffer 等多种请求体格式
 * - 完整支持 SSL/TLS 及国密协议（TLCP），可配置 CA 证书与客户端双证书
 * - 异步 Promise 编程模型，适用于现代前端调用方式
 * - 支持下载进度监听及请求取消机制
 * - 支持断点下载（需服务端支持range字段）
 * - 自动解析响应头并根据 Content-Type 返回不同类型结果（string 或 ArrayBuffer）
 * - 集成系统日志输出，便于调试和追踪请求过程
 * - 支持multipart/form-data表单提交，包含文件上传和二进制数据传输
 * - 完善的线程安全机制，通过napi_call_threadsafe_function保证回调安全
 * - 支持性能指标监控，便于分析请求耗时和网络状态
 * - 支持压缩，支持gzip、deflate算法
 *
 * 模块结构概览：
 * - HttpRequestParams：请求参数存储结构体，包含 URL、方法、头信息、证书路径、超时设置等
 * - RequestCallbackData：异步回调上下文结构，用于传递请求状态和 Promise 控制
 * - ExecuteRequest：核心 cURL 执行函数，负责构建并发送 HTTP 请求
 * - CompleteCB：异步操作完成后的回调函数，用于返回结果或错误信息给 JS 层
 * - convertRequestHeader / convertRequestData：辅助函数，用于从 JS 对象中提取请求头和请求体
 * - progress_callback：进度回调函数，支持实时检查是否取消请求
 * - cancelRequest：N-API 接口函数，用于取消指定 ID 的请求
 *
 * 依赖库：
 * - libcurl：底层网络请求引擎
 * - N-API：Node.js 原生插件接口
 * - HiLog：HarmonyOS 系统日志组件
 *
 * 使用说明：
 * 该模块被编译为 gmcurl Node.js 插件，JS 层可通过如下方式调用：
 *
 * 示例（带文件下载）：
 * gmcurl.request({
 *     url: 'https://example.com/download',
 *     method: 'GET',
 *     downloadFilePath: '/path/to/save/file',
 *     onProgress: (current, total) => {
 *         console.log(`Downloaded ${current}/${total} bytes`);
 *     }
 * }).then(res => {
 *     console.log('Response:', res);
 * }).catch(err => {
 *     console.error('Error:', err);
 * });
 *
 * gmcurl.cancelRequest(requestId); // 取消指定请求
 *
 * 注意事项：
 * - 所有请求均在异步线程中执行，避免阻塞主线程
 * - 进度回调采用节流机制（1秒间隔或完成时触发），防止高频回调
 * - 使用 std::map<std::int32_t, bool> 存储请求标识符，支持动态取消机制
 * - 资源管理自动完成，包括文件流缓冲区和内存缓冲区的自动释放
 * - 支持两种数据格式自动转换：JSON字符串和ArrayBuffer二进制数据
 *
 */

/**
 * @brief 进度数据结构体
 * 用于存储进度数据
 */
typedef struct ProgressData {
    int64_t currentSize; ///< 当前进度
    int64_t totalSize;   ///< 总进度
} ProgressData;

/**
 * @brief 表单数据结构体
 * 用于存储表单数据
 */
typedef struct FormData {
    std::string name;               ///< 表单数据名称
    std::string remoteFileName;     ///< 表单数据远程文件名
    std::string filePath;           ///< 表单数据文件路径
    std::string contentType;        ///< 表单数据类型
    std::string dataStr;            ///< 文本类型请求体数据
    void *dataBuffer = nullptr;     ///< 二进制请求体数据指针
    size_t dataBufferSize = 0;      ///< 二进制数据大小
    bool isDataArrayBuffer = false; ///< 数据类型标识
} FormData;

/**
 * @brief 性能指标结构体
 * 用于存储性能指标数据
 */
typedef struct PerformanceTiming {
    // 性能指标字段
    double dnsTiming = -1;                           ///< DNS解析耗时（毫秒）
    double tcpTiming = -1;                           ///< TCP连接耗时（毫秒）
    double tlsTiming = -1;                           ///< TLS连接耗时（毫秒）
    double firstSendTiming = -1;                     ///< 首字节发送耗时（毫秒）
    double firstReceiveTiming = -1;                  ///< 首字节接收耗时（毫秒）
    double totalFinishTiming = -1;                   ///< 完整请求耗时（毫秒）
    double redirectTiming = -1;                      ///< 重定向耗时（毫秒）
    int32_t totalTiming = -1;                        ///< 总耗时（毫秒）
    std::chrono::steady_clock::time_point startTime; ///< 请求开始时间
} PerformanceTiming;

// 宏定义用于简化字段设置
#define SET_PERF_FIELD(name)                                                                                           \
    if (callbackData->params.performanceTiming.name >= 0) {                                                            \
        napi_value val;                                                                                                \
        int32_t data = static_cast<int32_t>(callbackData->params.performanceTiming.name * 1000);                       \
        napi_create_int32(env, data, &val);                                                                            \
        napi_set_named_property(env, performanceObj, #name, val);                                                      \
    }
/**
 * @brief HTTP请求参数结构体
 * 存储完整的请求配置和上下文信息
 */
typedef struct HttpRequestParams {
    std::string url;                                ///< 请求目标URL
    std::string method;                             ///< HTTP方法(GET/POST/PUT/DELETE)
    std::string extraDataStr;                       ///< 文本类型请求体数据
    std::string downloadFilePath;                   ///< 下载文件路径
    std::string uploadFilePath;                     ///< 上传文件路径
    std::ofstream *downloadFile;                    ///< 下载文件流
    std::ifstream *uploadFile;                      ///< 上传文件流
    int64_t resumeFromOffset = 0;                   ///< 续传起始位置
    char *buffer;                                   ///< 缓冲区指针
    void *extraDataBuffer = nullptr;                ///< 二进制请求体数据指针
    size_t extraDataBufferSize = 0;                 ///< 二进制数据大小
    bool isExtraDataArrayBuffer = false;            ///< 数据类型标识
    std::map<std::string, std::string> headers;     ///< 请求头集合
    int readTimeout;                                ///< 读取超时时间(秒)
    int connectTimeout;                             ///< 连接超时时间(秒)
    std::string caPath;                             ///< CA证书路径
    std::string clientCertPath;                     ///< 客户端证书路径
    std::string response;                           ///< 响应正文
    int responseCode;                               ///< HTTP状态码
    std::string responseHeaders;                    ///< 响应头原始数据
    std::string errorMsg;                           ///< 错误信息
    bool isDebug;                                   ///< 调试模式开关
    bool isTLCP;                                    ///< 国密协议开关
    bool verifyServer;                              ///< 服务器验证开关
    std::int32_t requestId;                         ///< 请求ID
    std::vector<FormData> formData;                 ///< 表单数据集合
    int64_t lastProgress = 0;                       ///< 上次进度
    std::chrono::steady_clock::time_point lastTime; ///< 上次进度时间
    bool isPerformanceTiming = false;               ///< 性能指标开关
    PerformanceTiming performanceTiming;            ///< 性能指标数据
} HttpRequestParams;

/**
 * @brief 异步请求回调数据结构
 * 用于在异步操作中传递上下文信息
 */
typedef struct {
    napi_async_work asyncWork;     ///< NAPI异步工作对象
    napi_deferred deferred;        ///< Promise延迟对象
    HttpRequestParams params;      ///< 请求参数
    napi_threadsafe_function tsfn; ///< 线程安全函数对象
} RequestCallbackData;

/**
 * @brief 存储取消请求的标识符
 */
static std::map<std::int32_t, bool> mCancelRequestMap;

/**
 * @brief 互斥锁，用于控制取消请求操作
 */
static std::mutex mCancel_mtx;

/**
 * @brief 获取文件大小
 * @param filePath
 * @return
 */
std::ifstream::pos_type getFileSize(const std::string &filePath) {
    std::ifstream in(filePath, std::ifstream::ate | std::ifstream::binary);
    return in.tellg();
}

/**
 * @brief 将NAPI对象转换为JSON字符串
 * @param env NAPI环境对象
 * @param obj 需要转换的对象
 * @return JSON格式字符串
 */
std::string ObjectToJson(napi_env env, napi_value obj) {
    napi_value json;
    napi_value stringify;

    // 获取全局对象
    napi_value global;
    napi_get_global(env, &global);

    // 获取全局 JSON 对象
    napi_get_named_property(env, global, "JSON", &json);

    // 获取 JSON.stringify 函数
    napi_get_named_property(env, json, "stringify", &stringify);

    // 构造参数数组 [obj]
    napi_value args[] = {obj};
    napi_value result;

    // 调用 JSON.stringify(obj)
    napi_call_function(env, json, stringify, 1, args, &result);

    // 将结果转为 C 字符串
    size_t len;
    char buffer[4096 * 24];
    napi_get_value_string_utf8(env, result, buffer, sizeof(buffer), &len);

    return std::string(buffer, len);
}

/**
 * @brief 解析响应头字符串为键值对集合
 * @param headerStr 原始响应头字符串
 * @return 头部字段映射表
 */
std::map<std::string, std::string> ParseHeaders(const std::string &headerStr) {
    std::map<std::string, std::string> headers;
    std::istringstream stream(headerStr);
    std::string line;

    while (std::getline(stream, line, '\n')) {
        size_t colonPos = line.find(":");
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            // 去除前后空格
            key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](int ch) { return !std::isspace(ch); }));
            key.erase(std::find_if(key.rbegin(), key.rend(), [](int ch) { return !std::isspace(ch); }).base(),
                      key.end());

            value.erase(value.begin(),
                        std::find_if(value.begin(), value.end(), [](int ch) { return !std::isspace(ch); }));
            value.erase(std::find_if(value.rbegin(), value.rend(), [](int ch) { return !std::isspace(ch); }).base(),
                        value.end());

            headers[key] = value;
        }
    }

    return headers;
}

/**
 * @brief 文件上传的读取回调函数
 * @param ptr 缓冲区指针
 * @param size 单个数据块大小
 * @param nmemb 数据块数量
 * @param userp 用户数据指针（HttpRequestParams）
 * @return 实际读取的字节数
 */
size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userp) {
    auto *params = static_cast<HttpRequestParams *>(userp);
    if (!params->uploadFile || !params->uploadFile->is_open()) {
        return 0; // 文件未打开，返回0表示EOF
    }

    size_t bufferSize = size * nmemb;
    params->uploadFile->read(static_cast<char *>(ptr), bufferSize);
    size_t bytesRead = params->uploadFile->gcount();
    return bytesRead;
}

/**
 * @brief cURL响应体写入回调函数
 * @param contents 数据指针
 * @param size 单个数据块大小
 * @param nmemb 数据块数量
 * @param userp 用户数据指针
 * @return 写入的字节数
 */
size_t WriteDownloadCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t totalSize = size * nmemb;
    std::ofstream *file = static_cast<std::ofstream *>(userp);
    if (file && file->is_open()) {
        file->write(static_cast<char *>(contents), totalSize);
    }
    return totalSize;
}

/**
 * @brief cURL响应体写入回调函数
 * @param contents 数据指针
 * @param size 单个数据块大小
 * @param nmemb 数据块数量
 * @param userp 用户数据指针
 * @return 写入的字节数
 */
size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

/**
 * @brief cURL响应头处理回调函数
 * @param contents 头部数据指针
 * @param size 单个数据块大小
 * @param nmemb 数据块数量
 * @param userp 用户数据指针
 * @return 写入的字节数
 */
size_t HeaderCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

/**
 * @brief js线程安全回调
 * @param env NAPI环境对象
 * @param js_callback
 * @param context
 * @param data
 */
void ThreadSafeCallback(napi_env env, napi_value js_callback, void *context, void *data) {
    // 获取进度数据结构体
    ProgressData *progress = static_cast<ProgressData *>(data);
    // 创建参数数组
    napi_value args[2];
    napi_create_int64(env, progress->currentSize, &args[0]);
    napi_create_int64(env, progress->totalSize, &args[1]);
    // 调用回调函数
    napi_value global;
    napi_get_global(env, &global);
    napi_call_function(env, global, js_callback, 2, args, nullptr);
    delete progress;
}

/**
 * @brief 调试信息回调函数
 * 输出TLS握手等调试信息到系统日志
 * @param handle cURL句柄
 * @param type 信息类型
 * @param data 数据指针
 * @param size 数据大小
 * @param userp 用户数据
 * @return 处理结果
 */
static int debug_callback(CURL *handle, curl_infotype type, char *data, size_t size, void *userp) {
    try {
        if (type == CURLINFO_TEXT) {
            // 文本类型信息，包含握手等信息
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "GMCURL", "request info: %{public}s", data);
        } else if (type == CURLINFO_HEADER_OUT) {
            // 输入请求头信息
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "GMCURL", "request header: %{public}s", data);
        } else if (type == CURLINFO_HEADER_IN) {
            // 输出响应头信息
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "GMCURL", "response header: %{public}s", data);
        } else if (type == CURLINFO_DATA_OUT) {
            // 输出请求数据
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "GMCURL", "request data: \n%{public}s", data);
        } else if (type == CURLINFO_DATA_IN) {
            // 输出响应数据
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "GMCURL", "response data: \n%{public}s", data);
        }
    } catch (...) {
    }
    return 0;
}

/**
 * @brief 进度回调函数
 * @param clientp 用户自定义数据指针
 * @param dltotal 总下载量
 * @param dlnow 当前下载量
 * @param ultotal 总上传量
 * @param ulnow 当前上传量
 * @return 进度回调结果
 * @note
 * 函数返回非零值时，cURL将中断传输。
 */
static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                             curl_off_t ulnow) {
    // clientp 可以是你的自定义结构体指针
    auto *callback = static_cast<RequestCallbackData *>(clientp);
    auto now = std::chrono::steady_clock::now();
    // 处理上传进度
    if (callback && (!callback->params.uploadFilePath.empty() || !callback->params.formData.empty()) &&
        callback->tsfn && ultotal > 0 && callback->params.lastProgress != ulnow &&
        (std::chrono::duration_cast<std::chrono::seconds>(now - callback->params.lastTime).count() >= 1 ||
         ulnow == ultotal)) {
        // 获取已上传数量
        int64_t currentSize = ulnow;
        int64_t totalSize = ultotal;
        // 打印上传进度
        if (callback->params.isDebug && ultotal > 0) {
            int percent = static_cast<int>((currentSize * 100) / totalSize);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "GMCURL", "upload %{public}d%% (%{public}ld/%{public}ld bytes)",
                         percent, currentSize, totalSize);
        }
        // 调用线程安全函数返回上传进度
        ProgressData *data = new ProgressData();
        data->currentSize = currentSize;
        data->totalSize = totalSize;
        napi_status call_status = napi_call_threadsafe_function(callback->tsfn, data, napi_tsfn_nonblocking);
        if (call_status == napi_queue_full) {
            // 处理队列已满的情况（仅适用于 nonblocking 模式）
            free(data);
        }
        // 更新时间戳和进度
        callback->params.lastTime = now;
        callback->params.lastProgress = ulnow;
    }
    // 调用线程安全函数返回下载进度(1s间隔或完成下载)
    if (callback && !callback->params.downloadFilePath.empty() && callback->tsfn && dltotal > 0 &&
        callback->params.lastProgress != dlnow &&
        (std::chrono::duration_cast<std::chrono::seconds>(now - callback->params.lastTime).count() >= 1 ||
         dlnow == dltotal)) {
        // 获取已下载数量
        int64_t currentSize = callback->params.resumeFromOffset + dlnow;
        int64_t totalSize = callback->params.resumeFromOffset + dltotal;
        // 打印下载进度
        if (callback->params.isDebug && dltotal > 0) {
            int percent = static_cast<int>((currentSize * 100) / totalSize);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "GMCURL", "Download %{public}d%% (%{public}ld/%{public}ld bytes)",
                         percent, currentSize, totalSize);
        }
        // 调用线程安全函数返回下载进度
        ProgressData *data = new ProgressData();
        data->currentSize = currentSize;
        data->totalSize = totalSize;
        napi_status call_status = napi_call_threadsafe_function(callback->tsfn, data, napi_tsfn_nonblocking);
        if (call_status == napi_queue_full) {
            // 处理队列已满的情况（仅适用于 nonblocking 模式）
            free(data);
        }
        // 更新时间戳和进度
        callback->params.lastTime = now;
        callback->params.lastProgress = dlnow;
    }

    // 检查是否被取消
    if (callback && callback->params.requestId != 0) {
        std::lock_guard<std::mutex> lock(mCancel_mtx);
        auto it = mCancelRequestMap.find(callback->params.requestId);
        if (it != mCancelRequestMap.end() && it->second) {
            // 删除map中的key
            callback->params.errorMsg = "Request canceled by user";
            mCancelRequestMap.erase(it);
            return 1; // 返回非零值中断传输
        }
    }

    return 0; // 继续传输
}

/**
 * @brief 执行HTTP请求的核心函数
 * @param env NAPI环境对象
 * @param data 回调数据指针
 */
void ExecuteRequest(napi_env env, void *data) {
    RequestCallbackData *callbackData = reinterpret_cast<RequestCallbackData *>(data);
    CURL *curl = curl_easy_init();

    if (!curl) {
        callbackData->params.errorMsg = "Curl initialization failed";
        callbackData->params.responseCode = 102;
        return;
    }

    try {
        // 设置请求URL
        curl_easy_setopt(curl, CURLOPT_URL, callbackData->params.url.c_str());

        // 设置压缩格式
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");

        // 设置SSL证书路径
        if (!callbackData->params.caPath.empty()) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, callbackData->params.caPath.c_str());
        }

        // 设置SSL验证
        if (callbackData->params.verifyServer) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
        } else {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
        }

        // 设置SSL版本和证书路径
        if (callbackData->params.isTLCP) {
            curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_NTLSv1_1);
            if (!callbackData->params.clientCertPath.empty()) {
                auto encCert = callbackData->params.clientCertPath + "client_enc.crt";
                auto encKey = callbackData->params.clientCertPath + "client_enc.key";
                auto signCert = callbackData->params.clientCertPath + "client_sign.crt";
                auto signKey = callbackData->params.clientCertPath + "client_sign.key";
                // 设置客户端双证书
                curl_easy_setopt(curl, CURLOPT_SSLENCCERT, encCert.c_str());
                curl_easy_setopt(curl, CURLOPT_SSLENCKEY, encKey.c_str());
                curl_easy_setopt(curl, CURLOPT_SSLSIGNCERT, signCert.c_str());
                curl_easy_setopt(curl, CURLOPT_SSLSIGNKEY, signKey.c_str());
            }
        } else { // 非tlcp
            if (!callbackData->params.clientCertPath.empty()) {
                // 设置客户端证书
                auto cert = callbackData->params.clientCertPath + "client.crt";
                auto key = callbackData->params.clientCertPath + "client.key";
                curl_easy_setopt(curl, CURLOPT_SSLCERT, cert.c_str());
                curl_easy_setopt(curl, CURLOPT_SSLKEY, key.c_str());
            }
        }
        // 设置调试模式
        if (callbackData->params.isDebug) {
            // 开启调试模式
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
            // 设置调试回调函数
            curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debug_callback);
        }
        // 设置进度监听
        if (callbackData->params.requestId != 0 || !callbackData->params.downloadFilePath.empty() ||
            !callbackData->params.uploadFilePath.empty() || !callbackData->params.formData.empty()) {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); // 必须设为 0 来启用进度功能
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, callbackData); // 传递参数
            // 下载文件配置
            if (!callbackData->params.downloadFilePath.empty()) {
                curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 131072);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            }
            // 上传文件配置
            if (!callbackData->params.uploadFilePath.empty()) {
                curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 131072);
                curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE, 131072);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            }
        }
        // 设置请求方法
        if (callbackData->params.method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        } else if (callbackData->params.method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        } else if (callbackData->params.method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }

        // 设置超时时间
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, callbackData->params.readTimeout);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, callbackData->params.connectTimeout);

        // 设置请求头
        struct curl_slist *headers = NULL;
        // 上传文件
        if (!callbackData->params.uploadFilePath.empty()) {
            // 打开文件流
            callbackData->params.uploadFile = new std::ifstream(callbackData->params.uploadFilePath, std::ios::binary);
            if (!callbackData->params.uploadFile->is_open()) {
                callbackData->params.errorMsg = "Failed to open file for upload";
                callbackData->params.responseCode = 101;
                return;
            }
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            // 设置读取回调
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
            curl_easy_setopt(curl, CURLOPT_READDATA, &callbackData->params);
            // 获取文件大小
            std::ifstream::pos_type fileSize = getFileSize(callbackData->params.uploadFilePath);
            if (fileSize > 0) {
                curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(fileSize));
            }
            headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        }
        if (!callbackData->params.headers.empty()) {
            for (const auto &pair : callbackData->params.headers) {
                std::string header = pair.first + ": " + pair.second;
                headers = curl_slist_append(headers, header.c_str());
            }
        }

        // 设置默认Content-Type
        if (callbackData->params.headers.empty()) {
            if (callbackData->params.method == "POST" || callbackData->params.method == "PUT" ||
                callbackData->params.method == "DELETE") {
                headers = curl_slist_append(headers, "Content-Type: application/json");
            } else {
                headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
            }
        }
        //  设置请求头
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        //  判断是否为 multipart/form-data 请求
        bool isMultipart = false;
        if (callbackData->params.method == "POST" && !callbackData->params.headers.empty()) {
            auto it = callbackData->params.headers.find("Content-Type");
            if (it != callbackData->params.headers.end() &&
                it->second.find("multipart/form-data") != std::string::npos) {
                isMultipart = true;
            }
        }

        // 处理请求体
        struct curl_httppost *formPost = nullptr;
        struct curl_httppost *lastPost = nullptr;
        if (isMultipart) {
            for (const auto &form : callbackData->params.formData) {
                if (!form.filePath.empty()) {
                    curl_formadd(&formPost, &lastPost, CURLFORM_COPYNAME, form.name.c_str(), CURLFORM_FILE,
                                 form.filePath.c_str(), CURLFORM_FILENAME, form.remoteFileName.c_str(),
                                 CURLFORM_CONTENTTYPE, form.contentType.c_str(), CURLFORM_END);
                } else if (!form.isDataArrayBuffer) {
                    curl_formadd(&formPost, &lastPost, CURLFORM_COPYNAME, form.name.c_str(), CURLFORM_COPYCONTENTS,
                                 form.dataStr.c_str(), CURLFORM_FILENAME, form.remoteFileName.c_str(),
                                 CURLFORM_CONTENTTYPE, form.contentType.c_str(), CURLFORM_END);
                } else {
                    curl_formadd(&formPost, &lastPost, CURLFORM_COPYNAME, form.name.c_str(), CURLFORM_PTRCONTENTS,
                                 form.dataBuffer, CURLFORM_CONTENTSLENGTH, form.dataBufferSize, CURLFORM_CONTENTTYPE,
                                 form.contentType.c_str(), CURLFORM_END);
                }
            }
            // 设置 curl 选项
            curl_easy_setopt(curl, CURLOPT_HTTPPOST, formPost);
        } else {
            if ((callbackData->params.isExtraDataArrayBuffer && callbackData->params.extraDataBufferSize > 0 &&
                 callbackData->params.method != "GET" && callbackData->params.method != "DELETE") ||
                (!callbackData->params.extraDataStr.empty() && callbackData->params.method != "GET" &&
                 callbackData->params.method != "DELETE")) {

                if (callbackData->params.isExtraDataArrayBuffer) {
                    // 发送 ArrayBuffer 数据
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, callbackData->params.extraDataBuffer);
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, callbackData->params.extraDataBufferSize);
                } else {
                    // 发送字符串/JSON 数据
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, callbackData->params.extraDataStr.c_str());
                }
            }
        }

        // 设置响应头接收缓冲区
        std::string responseHeaders;
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);

        std::string responseBody;
        // 设置下载文件接收缓冲区
        if (!callbackData->params.downloadFilePath.empty()) {
            // 创建文件流并设置缓冲区
            // 设置为不自动清空文件，允许追加写入
            std::ios_base::openmode mode = std::ios::out | std::ios::binary;
            callbackData->params.downloadFile =
                new std::ofstream(callbackData->params.downloadFilePath,
                                  callbackData->params.resumeFromOffset > 0 ? (mode | std::ios::app) : mode);
            if (!callbackData->params.downloadFile->is_open()) {
                callbackData->params.errorMsg = "Failed to open downloadFile";
                callbackData->params.responseCode = 101;
                return;
            }

            // 设置文件流缓冲区（64KB）
            const size_t bufferSize = 131072; // 64KB
            char *buffer = new char[bufferSize];
            callbackData->params.downloadFile->rdbuf()->pubsetbuf(buffer, bufferSize);

            // 保存缓冲区指针用于后续释放
            callbackData->params.buffer = buffer; // 需要在DownloadContext中添加char* buffer;成员变量
            // 如果不是首次下载，启用断点续传
            if (callbackData->params.resumeFromOffset > 0) {
                std::ostringstream range;
                range << callbackData->params.resumeFromOffset << "-";
                curl_easy_setopt(curl, CURLOPT_RANGE, range.str().c_str());
            }
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDownloadCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, callbackData->params.downloadFile);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // 返回错误时不写入文件
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0); // 下载设置超时时间为无限大，表示不设置超时
        } else {                                        // 设置响应体接收缓冲区
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        }

        // 执行请求
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            // 获取响应码
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            callbackData->params.responseCode = response_code;
            // 保存响应头和响应体
            callbackData->params.responseHeaders = responseHeaders;
            if (!callbackData->params.downloadFilePath.empty()) {
                //  下载完成
                callbackData->params.response = "download finished";
            } else {
                //  保存响应体
                callbackData->params.response = responseBody;
            }
            // 获取性能数据
            if (callbackData->params.isPerformanceTiming) {
                curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &callbackData->params.performanceTiming.dnsTiming);
                curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &callbackData->params.performanceTiming.tcpTiming);
                curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &callbackData->params.performanceTiming.tlsTiming);
                curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME,
                                  &callbackData->params.performanceTiming.firstSendTiming);
                curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME,
                                  &callbackData->params.performanceTiming.firstReceiveTiming);
                curl_easy_getinfo(curl, CURLINFO_REDIRECT_TIME, &callbackData->params.performanceTiming.redirectTiming);
                curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &callbackData->params.performanceTiming.totalFinishTiming);
            }
        } else {
            callbackData->params.responseCode = res;
            if (res == CURLE_HTTP_RETURNED_ERROR) { // CURLOPT_FAILONERROR为1时返回具体错误码
                long response_code;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                callbackData->params.responseCode = response_code;
            } else {
                callbackData->params.responseCode = res;
            }
            if (callbackData->params.errorMsg.empty()) {
                callbackData->params.errorMsg = std::string(curl_easy_strerror(res));
            }
        }
        // 清理
        curl_slist_free_all(headers);
        if (isMultipart) {
            curl_formfree(formPost);
        }
        if (callbackData->params.downloadFile) {
            //  关闭文件流并释放资源
            callbackData->params.downloadFile->close();
            delete callbackData->params.downloadFile;
            callbackData->params.downloadFile = nullptr;
        }
        if (callbackData->params.uploadFile) {
            callbackData->params.uploadFile->close();
            delete callbackData->params.uploadFile;
            callbackData->params.uploadFile = nullptr;
        }
        curl_easy_cleanup(curl);
    } catch (const std::exception &e) {
        if (callbackData->params.downloadFile) {
            //  关闭文件流并释放资源
            callbackData->params.downloadFile->close();
            delete callbackData->params.downloadFile;
            callbackData->params.downloadFile = nullptr;
        }
        if (callbackData->params.uploadFile) {
            callbackData->params.uploadFile->close();
            delete callbackData->params.uploadFile;
            callbackData->params.uploadFile = nullptr;
        }
        curl_easy_cleanup(curl);
        callbackData->params.responseCode = 2000;
        callbackData->params.errorMsg = std::string(e.what());
    }
}

/**
 * @brief 响应错误回调
 * 处理Promise拒绝
 * @param env NAPI环境对象
 * @param callbackData 回调数据指针
 */
void ResponseErrorCB(napi_env &env, RequestCallbackData *&callbackData) {
    napi_value error;
    napi_create_object(env, &error);

    // 解析错误码
    napi_value errorCodeVal;
    napi_create_int32(env, callbackData->params.responseCode, &errorCodeVal);
    napi_set_named_property(env, error, "code", errorCodeVal);

    //  解析错误信息
    napi_value errorMsgVal;
    napi_create_string_utf8(env, callbackData->params.errorMsg.c_str(), NAPI_AUTO_LENGTH, &errorMsgVal);
    napi_set_named_property(env, error, "message", errorMsgVal);

    napi_reject_deferred(env, callbackData->deferred, error);
}

/**
 * @brief 异步操作完成回调
 * 处理Promise解析/拒绝和资源清理
 * @param env NAPI环境对象
 * @param status 异步状态
 * @param data 回调数据指针
 */
void CompleteCB(napi_env env, napi_status status, void *data) {
    RequestCallbackData *callbackData = reinterpret_cast<RequestCallbackData *>(data);
    try {
        if (status != napi_ok) {
            // 错误码+1000防止和curl错误冲突
            callbackData->params.responseCode = status + 1000;
            ResponseErrorCB(env, callbackData);
        } else if (!callbackData->params.errorMsg.empty()) {
            ResponseErrorCB(env, callbackData);
        } else {
            // 解析Promise
            napi_value result;
            napi_create_object(env, &result);

            // 解析响应码
            napi_value responseCodeVal;
            napi_create_int32(env, callbackData->params.responseCode, &responseCodeVal);
            napi_set_named_property(env, result, "responseCode", responseCodeVal);

            // 解析响应头
            std::map<std::string, std::string> headersMap = ParseHeaders(callbackData->params.responseHeaders);
            napi_value responseHeadersObj;
            napi_create_object(env, &responseHeadersObj);

            for (const auto &header : headersMap) {
                napi_value value;
                napi_create_string_utf8(env, header.second.c_str(), header.second.length(), &value);
                napi_set_named_property(env, responseHeadersObj, header.first.c_str(), value);
            }

            napi_set_named_property(env, result, "headers", responseHeadersObj);

            // 根据Content-Type返回不同的响应体格式
            std::string contentType;
            auto it = headersMap.find("Content-Type");
            if (it != headersMap.end()) {
                contentType = it->second;
            }

            // 解析响应体
            if ((contentType.find("application/octet-stream") != std::string::npos ||
                 contentType.find("image/") != std::string::npos) &&
                callbackData->params.downloadFilePath.empty()) {
                // 返回 ArrayBuffer
                napi_value arrayBuffer;
                void *bufferData;
                napi_create_arraybuffer(env, callbackData->params.response.size(), &bufferData, &arrayBuffer);
                if (bufferData != nullptr && !callbackData->params.response.empty()) {
                    memcpy(bufferData, callbackData->params.response.data(), callbackData->params.response.size());
                }
                napi_set_named_property(env, result, "body", arrayBuffer);
            } else {
                napi_value responseBodyVal;
                napi_create_string_utf8(env, callbackData->params.response.c_str(),
                                        callbackData->params.response.length(), &responseBodyVal);
                napi_set_named_property(env, result, "body", responseBodyVal);
            }

            // 创建性能指标对象
            if (callbackData->params.isPerformanceTiming &&
                callbackData->params.performanceTiming.totalFinishTiming >= 0) {
                napi_value performanceObj;
                napi_create_object(env, &performanceObj);

                // 计算总耗时
                auto now = std::chrono::steady_clock::now();
                callbackData->params.performanceTiming.totalTiming =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - callbackData->params.performanceTiming.startTime)
                        .count();

                // 设置性能指标字段
                SET_PERF_FIELD(dnsTiming)
                SET_PERF_FIELD(tcpTiming)
                SET_PERF_FIELD(tlsTiming)
                SET_PERF_FIELD(firstSendTiming)
                SET_PERF_FIELD(firstReceiveTiming)
                SET_PERF_FIELD(totalFinishTiming)
                SET_PERF_FIELD(redirectTiming)
                // SET_PERF_FIELD(totalTiming)

                // totalTime单独设置
                napi_value totalTiming;
                napi_create_int32(env, callbackData->params.performanceTiming.totalTiming, &totalTiming);
                napi_set_named_property(env, performanceObj, "totalTiming", totalTiming);

                napi_set_named_property(env, result, "performanceTiming", performanceObj);
            }

            napi_resolve_deferred(env, callbackData->deferred, result);
        }
    } catch (const std::exception &e) {
        callbackData->params.responseCode = 2000;
        callbackData->params.errorMsg = std::string(e.what());
        ResponseErrorCB(env, callbackData);
    }
    // 清除requestID数据
    if (callbackData && callbackData->params.requestId != 0) {
        std::lock_guard<std::mutex> lock(mCancel_mtx);
        auto it = mCancelRequestMap.find(callbackData->params.requestId);
        if (it != mCancelRequestMap.end()) {
            // 删除map中的key
            mCancelRequestMap.erase(it);
        }
    }
    //  释放线程安全函数 避免内存泄漏
    if (callbackData->tsfn) {
        napi_release_threadsafe_function(callbackData->tsfn, napi_tsfn_abort);
    }
    napi_delete_async_work(env, callbackData->asyncWork);
    // 释放内存
    delete callbackData->params.buffer;
    delete callbackData;
}

/**
 * @brief 转换请求头对象为内部结构
 * @param env NAPI环境对象
 * @param callbackData 回调数据
 * @param headersProp 请求头对象
 */
void convertRequestHeader(napi_env &env, RequestCallbackData *&callbackData, napi_value &headersProp) {
    napi_value headerKeys;
    napi_get_property_names(env, headersProp, &headerKeys);

    // 获取headersProp的键值对数量
    uint32_t length;
    napi_get_array_length(env, headerKeys, &length);

    // 遍历headersProp的键值对
    for (uint32_t i = 0; i < length; i++) {
        //  获取键值对
        napi_value key;
        napi_get_element(env, headerKeys, i, &key);

        //  获取键
        char keyStr[256];
        size_t keyLen;
        napi_get_value_string_utf8(env, key, keyStr, sizeof(keyStr), &keyLen);

        //  获取值
        napi_value value;
        napi_get_property(env, headersProp, key, &value);

        char valueStr[1024];
        size_t valueLen;
        napi_get_value_string_utf8(env, value, valueStr, sizeof(valueStr), &valueLen);

        // 将键值对添加到headers中
        callbackData->params.headers[keyStr] = valueStr;
    }
    //  设置Content-Type
    if (callbackData->params.isExtraDataArrayBuffer) {
        callbackData->params.headers["Content-Type"] = "application/octet-stream";
    }
}

/**
 * @brief 转换请求数据为内部格式
 * @param env NAPI环境对象
 * @param callbackData 回调数据
 * @param extraDataProp 请求数据对象
 */
void convertRequestData(napi_env &env, RequestCallbackData *&callbackData, napi_value &extraDataProp) {
    napi_valuetype dataType;
    napi_typeof(env, extraDataProp, &dataType);

    if (dataType == napi_string) {
        // 处理字符串类型
        char dataStr[4096 * 24];
        size_t dataLen;
        napi_get_value_string_utf8(env, extraDataProp, dataStr, sizeof(dataStr), &dataLen);
        callbackData->params.extraDataStr = std::string(dataStr, dataLen);
    } else if (dataType == napi_object) {
        // 检查是否为 ArrayBuffer
        bool isArrayBuffer = false;
        napi_is_arraybuffer(env, extraDataProp, &isArrayBuffer);

        if (isArrayBuffer) {
            // 处理 ArrayBuffer 类型数据
            void *buffer;
            size_t bufferLen;
            napi_get_arraybuffer_info(env, extraDataProp, &buffer, &bufferLen);
            callbackData->params.extraDataBuffer = buffer;
            callbackData->params.extraDataBufferSize = bufferLen;
            callbackData->params.isExtraDataArrayBuffer = true;
        } else {
            // 处理普通对象，转换为JSON字符串
            callbackData->params.extraDataStr = ObjectToJson(env, extraDataProp);
        }
    }
}

/**
 * 处理 extraFormData 参数
 * @param env napi 环境
 * @param callbackData
 * @param extraFormDataProp
 */
void convertFormData(struct napi_env__ *env, RequestCallbackData *callbackData, napi_value &formDataArray) {
    uint32_t length = 0;
    napi_get_array_length(env, formDataArray, &length);

    for (uint32_t i = 0; i < length; ++i) {
        napi_value item;
        napi_get_element(env, formDataArray, i, &item);

        // 解析 name
        napi_value nameValue;
        napi_get_named_property(env, item, "name", &nameValue);
        char name[256] = {0};
        size_t nameLen = 0;
        napi_get_value_string_utf8(env, nameValue, name, sizeof(name), &nameLen);

        // 解析contentType
        napi_value contentTypeValue;
        napi_get_named_property(env, item, "contentType", &contentTypeValue);
        char contentType[256] = {0};
        size_t contentTypeLen = 0;
        napi_get_value_string_utf8(env, contentTypeValue, contentType, sizeof(contentType), &contentTypeLen);

        // 解析filePath
        bool filePathExist = false;
        napi_value filePathValue;
        char filePath[1024] = {0};
        size_t filePathLen = 0;
        napi_has_named_property(env, item, "filePath", &filePathExist);
        if (filePathExist) {
            napi_get_named_property(env, item, "filePath", &filePathValue);
            napi_get_value_string_utf8(env, filePathValue, filePath, sizeof(filePath), &filePathLen);
        }

        // 解析remoteFileName
        bool remoteFileNameExist = false;
        napi_value remoteFileNameValue;
        char remoteFileName[1024] = {0};
        size_t remoteFileNameLen = 0;
        napi_has_named_property(env, item, "remoteFileName", &remoteFileNameExist);
        if (remoteFileNameExist) {
            napi_get_named_property(env, item, "remoteFileName", &remoteFileNameValue);
            napi_get_value_string_utf8(env, remoteFileNameValue, remoteFileName, sizeof(remoteFileName),
                                       &remoteFileNameLen);
        }

        // 封装数据
        FormData field;
        field.name = std::string(name, nameLen);
        field.contentType = std::string(contentType, contentTypeLen);
        if (filePathExist) {
            field.filePath = std::string(filePath, filePathLen);
        }
        if (remoteFileNameExist) {
            field.remoteFileName = std::string(remoteFileName, remoteFileNameLen);
        } else {
            field.remoteFileName = std::string(name, nameLen);
        }

        // 解析 data
        bool dataExist = false;
        napi_has_named_property(env, item, "data", &dataExist);
        if (dataExist) {
            napi_value dataValue;
            napi_get_named_property(env, item, "data", &dataValue);
            napi_valuetype dataType;
            napi_typeof(env, dataValue, &dataType);
            if (dataType == napi_string) {
                // 文本类型值
                char valueStr[4096 * 24];
                size_t valueLen;
                napi_get_value_string_utf8(env, dataValue, valueStr, sizeof(valueStr), &valueLen);
                field.dataStr = std::string(valueStr, valueLen);
            } else if (dataType == napi_object) {
                // ArrayBuffer 类型值
                bool isArrayBuffer = false;
                napi_is_arraybuffer(env, dataValue, &isArrayBuffer);

                if (isArrayBuffer) {
                    void *buffer;
                    size_t bufferSize;
                    napi_get_arraybuffer_info(env, dataValue, &buffer, &bufferSize);

                    field.dataBuffer = buffer;
                    field.dataBufferSize = bufferSize;
                    field.isDataArrayBuffer = true;
                } else {
                    // 处理普通对象，转换为JSON字符串
                    field.dataStr = ObjectToJson(env, dataValue);
                }
            }
        }

        // 添加到列表
        callbackData->params.formData.push_back(field);
    }
}

/**
 * @brief 主请求处理函数
 * 创建并配置异步请求对象
 * @param env NAPI环境对象
 * @param info 回调信息
 * @return Promise对象
 */
napi_value Request(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // 创建Promise
    napi_value promise;
    napi_deferred deferred;
    napi_create_promise(env, &deferred, &promise);

    // 创建回调数据
    RequestCallbackData *callbackData = new RequestCallbackData();
    callbackData->deferred = deferred;

    // 解析参数
    if (argc >= 1 && args[0] != nullptr) {
        napi_valuetype type;
        napi_typeof(env, args[0], &type);

        if (type == napi_object) {
            // 解析performanceTiming
            napi_value performanceTimingProp;
            napi_get_named_property(env, args[0], "performanceTiming", &performanceTimingProp);
            bool isPerformanceTiming;
            if (napi_get_value_bool(env, performanceTimingProp, &isPerformanceTiming) == napi_ok &&
                isPerformanceTiming) {
                callbackData->params.isPerformanceTiming = isPerformanceTiming;
                auto now = std::chrono::steady_clock::now();
                PerformanceTiming timing;
                timing.startTime = now;
                timing.totalTiming = 0;
                callbackData->params.performanceTiming = timing;
            } else {
                callbackData->params.isPerformanceTiming = false;
            }

            // 解析url
            napi_value urlProp;
            napi_get_named_property(env, args[0], "url", &urlProp);

            char url[1024];
            size_t urlLen;
            napi_get_value_string_utf8(env, urlProp, url, sizeof(url), &urlLen);
            callbackData->params.url = std::string(url);

            // 解析method
            bool hasMethodProp;
            napi_has_named_property(env, args[0], "method", &hasMethodProp);
            if (hasMethodProp) {
                napi_value methodProp;
                napi_get_named_property(env, args[0], "method", &methodProp);
                char method[32];
                size_t methodLen;
                napi_get_value_string_utf8(env, methodProp, method, sizeof(method), &methodLen);
                callbackData->params.method = std::string(method);
            } else {
                callbackData->params.method = "GET";
            }

            // 解析readTimeout
            bool hasReadTimeoutProp;
            napi_has_named_property(env, args[0], "readTimeout", &hasReadTimeoutProp);
            if (hasReadTimeoutProp) {
                napi_value readTimeoutProp;
                napi_get_named_property(env, args[0], "readTimeout", &readTimeoutProp);
                int32_t readTimeout;
                napi_get_value_int32(env, readTimeoutProp, &readTimeout);
                callbackData->params.readTimeout = readTimeout;
            } else {
                callbackData->params.readTimeout = 15;
            }

            // 解析connectTimeout
            bool hasConnectTimeoutProp;
            napi_has_named_property(env, args[0], "connectTimeout", &hasConnectTimeoutProp);
            if (hasReadTimeoutProp) {
                napi_value connectTimeoutProp;
                napi_get_named_property(env, args[0], "connectTimeout", &connectTimeoutProp);
                int32_t connectTimeout;
                napi_get_value_int32(env, connectTimeoutProp, &connectTimeout);
                callbackData->params.connectTimeout = connectTimeout;
            } else {
                callbackData->params.connectTimeout = 15;
            }

            // 解析extraData
            bool hasExtraDataProp;
            napi_has_named_property(env, args[0], "extraData", &hasExtraDataProp);
            //  POST或PUT请求
            if (hasExtraDataProp && (callbackData->params.method == "POST" || callbackData->params.method == "PUT")) {
                napi_value extraDataProp;
                napi_get_named_property(env, args[0], "extraData", &extraDataProp);

                convertRequestData(env, callbackData, extraDataProp);
            }

            // 解析formdata
            bool hasFormDataProp;
            napi_has_named_property(env, args[0], "multiFormDataList", &hasFormDataProp);
            if (hasFormDataProp && callbackData->params.method == "POST") {
                napi_value extraFormDataProp;
                napi_get_named_property(env, args[0], "multiFormDataList", &extraFormDataProp);
                napi_valuetype formDataType;
                napi_typeof(env, extraFormDataProp, &formDataType);
                if (formDataType == napi_object) {
                    // 调用上面定义的 convertFormData
                    convertFormData(env, callbackData, extraFormDataProp);
                }
            }

            // 解析headers
            bool hasHeadersProp;
            napi_has_named_property(env, args[0], "headers", &hasHeadersProp);
            if (hasHeadersProp) {
                napi_value headersProp;
                napi_get_named_property(env, args[0], "headers", &headersProp);
                napi_valuetype headersType;
                napi_typeof(env, headersProp, &headersType);
                if (headersType == napi_object) {
                    // 获取headersProp的键值对
                    convertRequestHeader(env, callbackData, headersProp);
                }
            }

            // 解析caPath
            napi_value caPathProp;
            napi_get_named_property(env, args[0], "caPath", &caPathProp);

            char caPath[1024];
            size_t caPathLen;
            if (napi_get_value_string_utf8(env, caPathProp, caPath, sizeof(caPath), &caPathLen) == napi_ok) {
                callbackData->params.caPath = std::string(caPath);
            }

            // 解析客户端证书路径
            napi_value clientCertPathProp;
            napi_get_named_property(env, args[0], "clientCertPath", &clientCertPathProp);

            char clientCertPath[1024];
            size_t clientCertPathLen;
            if (napi_get_value_string_utf8(env, clientCertPathProp, clientCertPath, sizeof(clientCertPath),
                                           &clientCertPathLen) == napi_ok) {
                callbackData->params.clientCertPath = std::string(clientCertPath);
            }

            // 解析tlcp
            napi_value tlcpProp;
            napi_get_named_property(env, args[0], "isTLCP", &tlcpProp);
            bool isTLCP;
            if (napi_get_value_bool(env, tlcpProp, &isTLCP) == napi_ok) {
                callbackData->params.isTLCP = isTLCP;
            } else {
                callbackData->params.isTLCP = false;
            }

            // 解析verifyServer
            napi_value verifyServerProp;
            napi_get_named_property(env, args[0], "verifyServer", &verifyServerProp);
            bool verifyServer;
            if (napi_get_value_bool(env, verifyServerProp, &verifyServer) == napi_ok && !verifyServer) {
                callbackData->params.verifyServer = verifyServer;
            } else {
                callbackData->params.verifyServer = true;
            }

            // 解析debug
            napi_value debugProp;
            napi_get_named_property(env, args[0], "debug", &debugProp);
            bool debug;
            if (napi_get_value_bool(env, debugProp, &debug) == napi_ok && debug) {
                callbackData->params.isDebug = debug;
                OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "GMCURL", "Curl version: %{public}s", curl_version());
            } else {
                callbackData->params.isDebug = false;
            }

            // 解析请求ID
            napi_value requestIdProp;
            napi_get_named_property(env, args[0], "requestID", &requestIdProp);
            int32_t requestId;
            if (napi_get_value_int32(env, requestIdProp, &requestId) == napi_ok) {
                callbackData->params.requestId = requestId;
                std::lock_guard<std::mutex> lock(mCancel_mtx);
                mCancelRequestMap[requestId] = false;
            } else {
                callbackData->params.requestId = 0;
            }

            // 解析下载参数
            bool hasDownloadProp;
            napi_has_named_property(env, args[0], "downloadFilePath", &hasDownloadProp);
            if (hasDownloadProp) {
                // 解析下载路径
                napi_value downloadProp;
                napi_get_named_property(env, args[0], "downloadFilePath", &downloadProp);
                char downloadPath[1024];
                size_t downloadPathLen;
                napi_get_value_string_utf8(env, downloadProp, downloadPath, sizeof(downloadPath), &downloadPathLen);
                callbackData->params.downloadFilePath = std::string(downloadPath);
                // 判断文件是否存在并解析文件大小
                if (!callbackData->params.downloadFilePath.empty()) {
                    std::ifstream file(callbackData->params.downloadFilePath, std::ios::binary);
                    if (file.good()) {
                        callbackData->params.resumeFromOffset = getFileSize(callbackData->params.downloadFilePath);
                    }
                    file.close();
                }
            }

            // 解析上传参数
            bool hasUploadProp;
            napi_has_named_property(env, args[0], "uploadFilePath", &hasUploadProp);
            if (hasUploadProp) {
                napi_value uploadProp;
                napi_get_named_property(env, args[0], "uploadFilePath", &uploadProp);
                char uploadPath[1024];
                size_t uploadPathLen;
                napi_get_value_string_utf8(env, uploadProp, uploadPath, sizeof(uploadPath), &uploadPathLen);
                callbackData->params.uploadFilePath = std::string(uploadPath, uploadPathLen);
            }

            // 解析进度回调
            bool hasProgressCBProp;
            napi_has_named_property(env, args[0], "onProgress", &hasProgressCBProp);
            if (hasProgressCBProp) {
                napi_value progressCallback;
                napi_get_named_property(env, args[0], "onProgress", &progressCallback);
                napi_value resourceName = nullptr;
                napi_create_string_utf8(env, "Thread-safe Progress CB", NAPI_AUTO_LENGTH, &resourceName);
                //  创建线程安全函数
                napi_create_threadsafe_function(env, progressCallback, nullptr, resourceName, 8, 1, nullptr, nullptr,
                                                callbackData, ThreadSafeCallback, &callbackData->tsfn);
            }
        }
    }

    // 创建异步任务
    napi_value resourceName;
    napi_create_string_utf8(env, "RequestCallback", NAPI_AUTO_LENGTH, &resourceName);
    napi_create_async_work(env, nullptr, resourceName, ExecuteRequest, CompleteCB, callbackData,
                           &callbackData->asyncWork);
    napi_queue_async_work(env, callbackData->asyncWork);

    return promise;
}

/**
 * 请求取消
 *
 * @param env
 * @param info
 * @return
 */
static napi_value cancelRequest(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc == 1) {
        napi_valuetype type;
        napi_typeof(env, args[0], &type);
        if (type == napi_number) {
            int32_t requestId;
            napi_get_value_int32(env, args[0], &requestId);
            std::lock_guard<std::mutex> lock(mCancel_mtx);
            if (mCancelRequestMap.count(requestId) > 0) {
                mCancelRequestMap[requestId] = true;
            }
        }
    }
    return nullptr;
}

EXTERN_C_START
static napi_value gmsslInit(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"request", nullptr, Request, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"cancelRequest", nullptr, cancelRequest, nullptr, nullptr, nullptr, napi_default, nullptr}};
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module gmsslModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = gmsslInit,
    .nm_modname = "gmcurl",
    .nm_priv = ((void *)0),
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterGMSSLModule(void) { napi_module_register(&gmsslModule); }
