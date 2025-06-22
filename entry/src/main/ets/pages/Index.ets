import GMHttp from '@chenchl/gmcurl';
import hilog from '@ohos.hilog';
import { util } from '@kit.ArkTS';


@Entry
@Component
struct Index {
  @State message: string = 'Hello World';
  private certPath = getContext().resourceDir + '/cert/';
  private downloadPath = getContext().filesDir + '/';
  @State percent: number = 0;
  @State percentDownload1: number = 0;
  @State percentUpload: number = 0;
  @State percentUpload1: number = 0;

  build() {
    Column({ space: 20 }) {
      Text('国际ssl')
        .fontSize(20)
        .fontWeight(FontWeight.Bold)
        .onClick(() => {
          //国际
          GMHttp.request({
            url: "https://www.aliyun.com",
            method: 'GET',
            headers: {
              "Content-Type": "application/json",
              "Cookie": "name=value; name3=value3"
            },
            debug: true,
            connectTimeout: 1,
            readTimeout: 1,
            performanceTiming: true
          }).then((res) => {
            hilog.error(0, 'test', `response code: ${res.responseCode}`)
            hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
            hilog.error(0, 'test', `response body: ${res.body}`)
            hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
          }).catch((err: GMHttp.HttpResponseError) => {
            hilog.error(0, 'test', `response error code: ${err.code}`)
            hilog.error(0, 'test', `response error message: ${err.message}`)
          })
        })
      Text('国密ssl')
        .fontSize(20)
        .fontWeight(FontWeight.Bold)
        .onClick(() => {
          //国密双向
          GMHttp.request({
            url: "https://172.16.1.108:8446/post?test=3&num=2",
            method: 'POST',
            headers: {
              "Content-Type": "application/json",
              "Accept": "application/json",
              "Cookie": "name=value; name2=value2"
            },
            connectTimeout: 10,
            readTimeout: 10,
            //extraData: '{"test":"hhh","test2":{"test3":"hhh3"},"test6":1,"test4":["hhh4","hhh5"]}',
            extraData: {
              "test": "hhh",
              "test2": {
                "test3": "hhh3"
              },
              test6: 1,
              "test4": [
                "hhh4",
                "hhh5"
              ]
            },
            caPath: this.certPath + 'sm2.trust.pem',
            clientCertPath: this.certPath,
            isTLCP: true,
            debug: true,
            performanceTiming: true
          }).then((res) => {
            hilog.error(0, 'test', `response code: ${res.responseCode}`)
            hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
            if (res.body instanceof ArrayBuffer) {
              hilog.error(0, 'test', `response body: ${util.TextDecoder.create('utf-8')
                .decodeToString(new Uint8Array(res.body as ArrayBuffer))}`);
            } else {
              hilog.error(0, 'test', `response body: ${res.body}`);
            }
            hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
          }).catch((err: GMHttp.HttpResponseError) => {
            hilog.error(0, 'test', `response error code: ${err.code}`)
            hilog.error(0, 'test', `response error message: ${err.message}`)
          })

          //国密单向
          GMHttp.request({
            url: "https://demo.gmssl.cn:1443",
            method: 'GET',
            headers: {
              "Content-Type": "application/json",
              "Accept": "application/json",
              "Cookie": "name=value; name4=value4"
            },
            connectTimeout: 10,
            readTimeout: 10,
            //extraData: '{"test":"hhh","test2":{"test3":"hhh3"},"test6":1,"test4":["hhh4","hhh5"]}',
            extraData: {
              "test": "hhh",
              "test2": {
                "test3": "hhh3"
              },
              test6: 1,
              "test4": [
                "hhh4",
                "hhh5"
              ]
            },
            caPath: this.certPath + 'sm2.trust.pem',
            isTLCP: true,
            debug: true,
            performanceTiming: true
          }).then((res) => {
            hilog.error(0, 'test', `response code: ${res.responseCode}`)
            hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
            hilog.error(0, 'test', `response body: ${res.body}`)
            hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
          }).catch((err: GMHttp.HttpResponseError) => {
            hilog.error(0, 'test', `response error code: ${err.code}`)
            hilog.error(0, 'test', `response error message: ${err.message}`)
          })
        })
    }
    .height('100%')
    .width('100%')
    .alignItems(HorizontalAlign.Center)
    .justifyContent(FlexAlign.Center)
  }
}