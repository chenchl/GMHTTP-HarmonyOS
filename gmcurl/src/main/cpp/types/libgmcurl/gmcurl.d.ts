/**
 * HTTP方法枚举
 */
export type HttpMethod = 'GET' | 'POST' | 'PUT' | 'DELETE';

/**
 * HTTP请求头接口
 */
export type HttpHeaders = {
  /**
   * 请求头
   */
  [key: string]: string;
}

/**
 * 进度回调
 */
export type ProgressCallback = (currentSize: number, totalSize: number) => void;

/**
 * 网络性能指标(毫秒)
 */
export interface PerformanceTiming {
  /**
   * 从request请求到DNS解析完成耗时
   */
  dnsTiming: number;

  /**
   * 从request请求到TCP连接完成耗时
   */
  tcpTiming: number;

  /**
   * 从request请求到TLS连接完成耗时
   */
  tlsTiming: number;

  /**
   * 从request请求到开始发送第一个字节的耗时
   */
  firstSendTiming: number;

  /**
   * 从request请求到接收第一个字节的耗时
   */
  firstReceiveTiming: number;

  /**
   * 从request请求到完成请求的耗时
   */
  totalFinishTiming: number;

  /**
   * 从request请求到完成所有重定向步骤的耗时
   */
  redirectTiming: number;

  /**
   * 从request请求回调到应用程序的耗时
   */
  totalTiming: number;
}

/**
 * 多部分表单数据接口
 */
export interface MultiFormData {
  /**
   * 字段名
   */
  name: string;

  /**
   * 文件路径
   */
  filePath?: string;

  /**
   * 远程文件名
   */
  remoteFileName?: string;

  /**
   * 文件类型
   */
  contentType: string;

  /**
   * 数据（支持多种类型）
   */
  data?: string | Object | ArrayBuffer;
}

/**
 * HTTP请求选项接口
 */
export interface HttpRequestOptions {
  /**
   * 请求URL
   */
  url: string;

  /**
   * HTTP方法 'GET' | 'POST' | 'PUT' | 'DELETE'
   */
  method?: HttpMethod;

  /**
   * 请求体数据（支持多种类型）
   */
  extraData?: string | Object | ArrayBuffer;

  /**
   * 请求头（默认根据方法自动设置）
   */
  headers?: HttpHeaders;

  /**
   * 读取超时时间（秒）
   */
  readTimeout?: number;

  /**
   * 连接超时时间（秒）
   */
  connectTimeout?: number;

  /**
   * CA证书路径(全路径包含文件名 PEM格式)
   */
  caPath?: string;

  /**
   * 客户端证书路径
   *
   * 国际TLS 命名client.crt/client.key
   * 国密TLCP 命名client_enc.crt/client_enc.key/client_sign.crt/client_sign.key
   */
  clientCertPath?: string;

  /**
   * 验证服务器证书(默认true验证)
   */
  verifyServer?: boolean;

  /**
   * 是否使用国密协议(默认false不使用)
   */
  isTLCP?: boolean;

  /**
   * 调试模式(默认false不使用)
   */
  debug?: boolean;

  /**
   * 请求ID
   */
  requestID?: number;

  /**
   * 多部分表单数据列表
   * post请求且'content-Type'为'multipart/form-data'时生效
   */
  multiFormDataList?: MultiFormData[];

  /**
   * 下载文件路径
   */
  downloadFilePath?: string;

  /**
   * 上传文件路径
   */
  uploadFilePath?: string;

  /**
   * 下载进度回调
   * @param current 当前大小
   * @param total 总大小
   */
  onProgress?: ProgressCallback;

  /**
   * 性能统计(默认false不使用)
   */
  performanceTiming?: boolean;
}

/**
 * HTTP响应接口
 */
export interface HttpResponse {
  /**
   * 响应状态码
   */
  responseCode: number;

  /**
   * 响应头
   */
  headers: HttpHeaders;

  /**
   * 响应体（根据Content-Type自动转换）
   */
  body: string | ArrayBuffer;

  /**
   * 性能数据
   */
  performanceTiming?:  PerformanceTiming;
}

/**
 * HTTP错误接口
 */
export interface HttpResponseError {
  /**
   * 错误码
   */
  code: number;

  /**
   * 错误信息
   */
  message: string;
}

/**
 * 发起HTTP请求
 * @param options
 * @returns
 */
export function request(options: HttpRequestOptions): Promise<HttpResponse>;

/**
 * 取消HTTP请求
 * @param requestID
 */
export function cancelRequest(requestID: number): void;