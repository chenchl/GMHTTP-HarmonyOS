import { hilog } from '@kit.PerformanceAnalysisKit';
import { describe, beforeAll, beforeEach, afterEach, afterAll, it, expect } from '@ohos/hypium';
import GMHttp from '../../../../Index';
import { util } from '@kit.ArkTS';

export default function GmCurlTest() {
  let certPath = '';
  let downloadPath = '';
  describe('GmCurlTest', () => {
    // Defines a test suite. Two parameters are supported: test suite name and test suite function.
    beforeAll(() => {
      certPath = getContext().resourceDir + '/cert/';
      downloadPath = getContext().filesDir + '/';
      // Presets an action, which is performed only once before all test cases of the test suite start.
      // This API supports only one parameter: preset action function.
    })
    beforeEach(() => {
      // Presets an action, which is performed before each unit test case starts.
      // The number of execution times is the same as the number of test cases defined by **it**.
      // This API supports only one parameter: preset action function.
    })
    afterEach(() => {
      // Presets a clear action, which is performed after each unit test case ends.
      // The number of execution times is the same as the number of test cases defined by **it**.
      // This API supports only one parameter: clear action function.
    })
    afterAll(() => {
      // Presets a clear action, which is performed after all test cases of the test suite end.
      // This API supports only one parameter: clear action function.
    })
    /*it('assertContain', 0, () => {
      // Defines a test case. This API supports three parameters: test case name, filter parameter, and test case function.
      hilog.info(0x0000, 'testTag', '%{public}s', 'it begin');
      let a = 'abc';
      let b = 'b';
      // Defines a variety of assertion methods, which are used to declare expected boolean conditions.
      expect(a).assertContain(b);
      expect(a).assertEqual(a);
    })*/
    //国际ssl
    it("tlsTest", 0, () => {
      return GMHttp.request({
        url: "https://www.aliyun.com",
        method: 'GET',
        headers: {
          "Content-Type": "application/json",
          "Cookie": "name=value; name3=value3"
        },
        debug: true,
        connectTimeout: 10,
        readTimeout: 10,
        performanceTiming: true
      }).then((res) => {
        expect(res.responseCode).assertEqual(200)
        hilog.error(0, 'test', `response code: ${res.responseCode}`)
        hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
        hilog.error(0, 'test', `response body: ${res.body}`)
        hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
      }).catch((err: GMHttp.HttpResponseError) => {
        expect(err).assertEqual(200)
        hilog.error(0, 'test', `response error code: ${err.code}`)
        hilog.error(0, 'test', `response error message: ${err.message}`)
      })
    })

    //国密ssl 单向
    it("tlcpTest_single", 0, () => {
      return GMHttp.request({
        url: "https://172.16.1.108:8445",
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
        caPath: certPath + 'sm2.trust.pem',
        isTLCP: true,
        debug: true,
        performanceTiming: true
      }).then((res) => {
        expect(res.responseCode).assertEqual(200)
        hilog.error(0, 'test', `response code: ${res.responseCode}`)
        hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
        hilog.error(0, 'test', `response body: ${res.body}`)
        hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
      }).catch((err: GMHttp.HttpResponseError) => {
        expect(err).assertEqual(200)
        hilog.error(0, 'test', `response error code: ${err.code}`)
        hilog.error(0, 'test', `response error message: ${err.message}`)
      })
    })
    //国密ssl 双向
    it("tlcpTest_two_way", 0, () => {
      return GMHttp.request({
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
        caPath: certPath + 'sm2.trust.pem',
        clientCertPath: certPath,
        isTLCP: true,
        debug: true,
        performanceTiming: true
      }).then((res) => {
        expect(res.responseCode).assertEqual(200)
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
        expect(err).assertEqual(200)
        hilog.error(0, 'test', `response error code: ${err.code}`)
        hilog.error(0, 'test', `response error message: ${err.message}`)
      })
    })
    //国际SSL 上传
    it("tlsTest_upload", 0, () => {
      return GMHttp.request({
        url: "https://172.16.1.108:8446/upload",
        method: 'POST',
        requestID: 123456,
        headers: {
          "token": "5698d50519664d0690ab86656f62d171-6B4644B1A83041FC8F36EF3A14F9FC42",
          "platform": "ANDROID",
          //"Expect": ""
        },
        connectTimeout: 30,
        readTimeout: 30,
        caPath: certPath + 'sm2.trust.pem',
        clientCertPath: certPath,
        isTLCP: true,
        uploadFilePath: certPath + `test-large.docx`,
        onProgress: (current, total) => {
          let percent = Math.floor(current * 100 / total)
          hilog.error(0, 'test', `response upload progress: ${percent}% [${current}/${total}]`)
        },
        debug: true,
        performanceTiming: true,
      }).then((res) => {
        expect(res.responseCode).assertLargerOrEqual(200)
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
        expect(err).assertEqual(200)
        hilog.error(0, 'test', `response error code: ${err.code}`)
        hilog.error(0, 'test', `response error message: ${err.message}`)
      })
    })
    //国际SSL GET
    it("cloudTest_get", 0, () => {
      return GMHttp.request({
        url: "https://172.16.1.108:8446/tenant/info",
        method: 'GET',
        headers: {
          "Content-Type": "application/json",
          "token": "5698d50519664d0690ab86656f62d171-6B4644B1A83041FC8F36EF3A14F9FC42",
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
        //caPath: this.certPath + 'sm2.trust.pem',
        //clientCertPath: this.certPath,
        //isTLCP: true,
        debug: true,
        performanceTiming: true
      }).then((res) => {
        expect(res.responseCode).assertEqual(200)
        hilog.error(0, 'test', `response code: ${res.responseCode}`)
        hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
        hilog.error(0, 'test', `response body: ${res.body}`)
        hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
      }).catch((err: GMHttp.HttpResponseError) => {
        expect(err).assertEqual(200)
        hilog.error(0, 'test', `response error code: ${err.code}`)
        hilog.error(0, 'test', `response error message: ${err.message}`)
      })
    })
    //国际SSL POST
    it("cloudTest_post", 0, () => {
      let requestId = 1234567890;
      setTimeout(() => {
        GMHttp.cancelRequest(requestId);
      }, 300)
      return GMHttp.request({
        url: "https://172.16.1.108:8446/v2/item/list",
        method: 'POST',
        headers: {
          "Content-Type": "application/json",
          "token": "5698d50519664d0690ab86656f62d171-6B4644B1A83041FC8F36EF3A14F9FC42",
          "platform": "ANDROID"
        },
        connectTimeout: 10,
        readTimeout: 10,
        //extraData: '{"test":"hhh","test2":{"test3":"hhh3"},"test6":1,"test4":["hhh4","hhh5"]}',
        extraData: `{"parentId":"b61fa9eac9c84341b01b0ef45b53bb3b","sortBy":"itemType, updateDate desc, itemId","itemType":"ALL","sourceId":"7a79c79b559f4a33b1067380fa7c9327","source":"COOPERATION","current":1,"size":30,"timestamp":"","fileType":[],"excludeFileType":[]}`,
        //caPath: this.certPath + 'sm2.trust.pem',
        //isTLCP: true,
        debug: true,
        performanceTiming: true,
        requestID: requestId
      }).then((res) => {
        //expect(res.body).assertContain("Request canceled by user")
        hilog.error(0, 'test', `response code: ${res.responseCode}`)
        hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
        hilog.error(0, 'test', `response body: ${res.body}`)
        hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
      }).catch((err: GMHttp.HttpResponseError) => {
        expect(err.code).assertEqual(42)
        expect(err.message).assertContain("Request canceled by user")
        hilog.error(0, 'test', `response error code: ${err.code}`)
        hilog.error(0, 'test', `response error message: ${err.message}`)
      })
    })
    //国际SSL post form
    it("cloudTest_post_form", 0, () => {
      return GMHttp.request({
        url: "https://172.16.1.108:8446/feedback/feedbackAdd",
        method: 'POST',
        headers: {
          "Content-Type": "multipart/form-data",
          "token": "5698d50519664d0690ab86656f62d171-6B4644B1A83041FC8F36EF3A14F9FC42",
          "platform-token": "489e6a3a0d224d1e8b73780b55575a7c",
          "platform": "ANDROID",
          //"Expect": ""
        },
        connectTimeout: 30,
        readTimeout: 30,
        //caPath: this.certPath + 'sm2.trust.pem',
        //isTLCP: true,
        debug: true,
        onProgress: (current, total) => {
          let percentUpload = Math.floor(current * 100 / total)
          hilog.error(0, 'test', `response upload progress: ${percentUpload}% [${current}/${total}]`)
        },
        multiFormDataList: [
          {
            name: 'deviceModel',
            data: "AGT-AN00",
            contentType: "text/plain;charset=utf-8"
          },
          {
            name: 'deviceName',
            data: "HONOR AGT-AN00",
            contentType: "text/plain;charset=utf-8"
          },
          {
            name: 'platform',
            data: "ANDROID",
            contentType: "text/plain;charset=utf-8"
          },
          {
            name: 'systemVersion',
            data: "Android 15",
            contentType: "text/plain;charset=utf-8"
          },
          {
            name: 'version',
            data: "V1.1",
            contentType: "text/plain;charset=utf-8"
          },
          {
            name: 'module',
            data: "HOME",
            contentType: "text/plain;charset=utf-8"
          },
          {
            name: 'files',
            remoteFileName: "test1",
            filePath: certPath + "test-large.docx",
            contentType: 'image/png'
          },
        ],
        performanceTiming: true,
      }).then((res) => {
        expect(res.responseCode).assertEqual(200)
        hilog.error(0, 'test', `response code: ${res.responseCode}`)
        hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
        hilog.error(0, 'test', `response body: ${res.body}`)
        hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
      }).catch((err: GMHttp.HttpResponseError) => {
        expect(err).assertEqual(200)
        hilog.error(0, 'test', `response error code: ${err.code}`)
        hilog.error(0, 'test', `response error message: ${err.message}`)
      })
    })
    //国际SSL 下载
    it("cloudTest_download", 0, () => {
      return GMHttp.request({
        url: "https://172.16.1.108:8446/item/preview?itemId=464d3291caf340ed87b7ff8ff450007b",
        method: 'GET',
        headers: {
          "token": "5698d50519664d0690ab86656f62d171-6B4644B1A83041FC8F36EF3A14F9FC42",
          "platform-token": "489e6a3a0d224d1e8b73780b55575a7c",
          "platform": "ANDROID",
          //"Expect": ""
        },
        connectTimeout: 30,
        readTimeout: 30,
        //caPath: this.certPath + 'sm2.trust.pem',
        //isTLCP: true,
        downloadFilePath: downloadPath + `test-large.docx`,
        onProgress: (current, total) => {
          let percent = Math.floor(current * 100 / total)
          hilog.error(0, 'test', `response download progress: ${percent}% [${current}/${total}]`)
        },
        debug: true,
        performanceTiming: true,
      }).then((res) => {
        expect(res.responseCode).assertLargerOrEqual(200)
        hilog.error(0, 'test', `response code: ${res.responseCode}`)
        hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
        hilog.error(0, 'test', `response body: ${res.body}`)
        hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
      }).catch((err: GMHttp.HttpResponseError) => {
        expect(err).assertEqual(200)
        hilog.error(0, 'test', `response error code: ${err.code}`)
        hilog.error(0, 'test', `response error message: ${err.message}`)
      })
    })
    //国密SSL 下载
    it("tlcp_download", 0, () => {
      return GMHttp.request({
        url:"https://172.16.1.108:8447/ccc",
        method: 'GET',
        headers: {
          "token": "5698d50519664d0690ab86656f62d171-6B4644B1A83041FC8F36EF3A14F9FC42",
          "platform": "ANDROID",
          //"Expect": ""
        },
        connectTimeout: 30,
        readTimeout: 30,
        caPath: certPath + 'sm2.trust.pem',
        clientCertPath: certPath,
        isTLCP: true,
        downloadFilePath: downloadPath + `test-large1.docx`,
        onProgress: (current, total) => {
          let percentDownload1 = Math.floor(current * 100 / total)
          hilog.error(0, 'test', `response download progress: ${percentDownload1}% [${current}/${total}]`)
        },
        debug: true,
        performanceTiming: true
      }).then((res) => {
        expect(res.responseCode).assertLargerOrEqual(200)
        hilog.error(0, 'test', `response code: ${res.responseCode}`)
        hilog.error(0, 'test', `response header: ${JSON.stringify(res.headers)}`)
        hilog.error(0, 'test', `response body: ${res.body}`)
        hilog.error(0, 'test', `response performanceTiming: ${JSON.stringify(res.performanceTiming)}`)
      }).catch((err: GMHttp.HttpResponseError) => {
        expect(err).assertEqual(200)
        hilog.error(0, 'test', `response error code: ${err.code}`)
        hilog.error(0, 'test', `response error message: ${err.message}`)
      })
    })
  })
}