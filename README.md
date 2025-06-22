# GMSSL  HTTPS 通信模块项目概述

## 项目整体结构

```
GMSSL HTTPS 通信模块
├── AppScope                  # 应用全局资源配置
├── entry                     # demo模块
├── gmcurl                    # 国密通信模块
├── hvigor                    # 构建配置
├── product                   # 产品发布相关
├── testReoprt/gmcurl         # 测试报告
├── build-profile.json5       # 构建配置文件
├── code-linter.json5         # 代码规范配置
├── hvigorfile.ts             # 构建脚本
├── oh-package-lock.json5     # 依赖锁定文件
└── oh-package.json5          # 包管理配置
```

## 模块详细说明

### entry 模块

```
entry
├── libs                      # 本地依赖库
│   └── gmcurl-1.0.0.har
├── src/main/ets              # 主程序代码
│   ├── entryability        # 应用能力实现
│   ├── entrybackupability  # 备份能力实现
│   └── pages                 # 页面组件
│       └── Index.ets
├── resources                 # 资源文件
│   ├── base                  # 基础资源
│   ├── dark                  # 深色主题
│   └── resfile/cert          # 证书文件
├── build-profile.json5       # 模块构建配置
└── oh-package.json5        # 模块依赖配置
```

**核心功能**：

- 实现应用主界面和导航结构
- 管理应用生命周期
- 集成国密通信模块进行安全认证
- 提供国密通信模块的各接口/场景调用示例

### gmcurl 模块（国密通信核心）

```
gmcurl
├── libs                      # 原生库文件
│   ├── arm64-v8a             # ARM64架构专用库
│   │   └── libcrypto.so.3    # 国密算法加密库
│   └── x86_64                # X86_64架构专用库
│       ├── libcurl.so.4      # 核心通信库
│       ├── libssl.so.3       # 安全协议库
│       └── libnghttp2.so.14  # HTTP/2协议支持
├── src/main/cpp              # C++核心实现
│   ├── include               # 头文件目录
│   │   ├── curl.h            # 主接口定义
│   │   ├── easy.h            # 简易API接口
│   │   ├── options.h         # 协议选项配置
│   │   └── websockets.h      # WebSocket支持
│   ├── types/libgmcurl       # 类型定义
│   │   └── gmcurl.d.ts       # 类型声明文件
│   └── napi_gmcurl.cpp       # NAPI接口实现
├── src/main/ets              # ETS接口层
│   └── components/security/https
│       └── GmCurl.ets         # 安全通信组件
├── test                      # 测试套件
│   ├── GmCurl.test.ets       # 单元测试
│   └── GmCurl_performance_test.test.ets  # 性能测试
├── README.md                 # 模块说明文档
└── oh-package.json5        # 模块配置
```

**核心功能**：

1. **原生库(libs)**
    - `libcrypto.so.3`: 实现SM2/SM3/SM4国密算法套件
    - `libssl.so.3`: 提供TLS/TLCP国密扩展协议实现
    - `libcurl.so.4`: 基于国密协议的网络通信核心
    - `libnghttp2.so.14`: HTTP/2协议支持

2. **C++实现层**
    - `curl.h/easy.h`: 提供符合国密标准的HTTPS通信API
    - `options.h`: 定义国密协议特有选项（如证书链验证模式）
    - `napi_gmcurl.cpp`: 通过NAPI封装，提供JS调用接口

3. **ETS接口层**
    - `GmCurl.ets`: 提供异步安全通信接口，封装以下核心功能：
        - 国密双向认证
        - 安全会话管理
        - 数据完整性校验

4. **测试验证**
    - `GmCurl.test.ets`: 覆盖证书验证、协议握手、数据传输等场景
    - `GmCurl_performance_test.test.ets`: 吞吐量和延迟基准测试

**安全特性**：

- 符合GB/T 38636-2020安全标准
- 支持SM2证书双向认证
- 实现国密SSL VPN协议扩展

**构建配置**：

- `oh-package.json5`: 定义模块依赖关系
- `BuildProfile.ets`: 构建参数配置
- `hvigorfile.ts`: 定制化构建流程