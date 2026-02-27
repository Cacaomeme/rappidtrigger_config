# CubeMX再生成時のカスタムコード保護ガイド

## 安全なファイル（CubeMXが触らない）
- `Lib/NKRO/` — 全て安全
- `App/config_tool/` — 全て安全
- `Src/main.cpp` — CubeMXは`main.c`を生成するため安全

## USER CODEブロック内のカスタムコード（再生成後も保持）
- `USB_Device/App/usbd_custom_hid_if.c`
  - `USER CODE BEGIN 0` ～ `USER CODE END 0`: HID Report Descriptor (NKRO + Feature Report定義)
  - `USER CODE BEGIN PRIVATE_VARIABLES`: `RxBuffer[33]`, `ProcessConfigPacket` extern宣言
  - `CUSTOM_HID_OutEvent_FS` 内の `USER CODE`: Feature Report処理呼び出し

## ⚠️ 再生成で失われるカスタムコード

### 1. `Middlewares/ST/.../usbd_customhid.h` (3箇所)

```c
// 構造体 USBD_CUSTOM_HID_HandleTypeDef に追加した2フィールド:
uint8_t  Feature_buf[33];  /* Feature Report buffer (32B + safety) */
uint32_t IsFeatureReportAvailable;
```

### 2. `Middlewares/ST/.../usbd_customhid.c` (3箇所)

**箇所A: CUSTOM_HID_REQ_GET_REPORT (約L477-481)**
```c
case CUSTOM_HID_REQ_GET_REPORT:
  /* GET_REPORT: Send Feature Report data to host */
  (void)USBD_CtlSendData(pdev, hhid->Feature_buf,
                         MIN(32U, req->wLength));
  break;
```

**箇所B: CUSTOM_HID_REQ_SET_REPORT (約L483-495)**
```c
case CUSTOM_HID_REQ_SET_REPORT:
  hhid->IsReportAvailable = 1U;
  /* Check if this is a Feature Report (wValue high byte = 0x03) */
  if ((req->wValue >> 8) == 0x03U)
  {
    hhid->IsFeatureReportAvailable = 1U;
    (void)USBD_CtlPrepareRx(pdev, hhid->Feature_buf, req->wLength);
  }
  else
  {
    (void)USBD_CtlPrepareRx(pdev, hhid->Report_buf, req->wLength);
  }
  break;
```

**箇所C: USBD_CUSTOM_HID_EP0_RxReady (約L741-748)**
```c
if (hhid->IsFeatureReportAvailable == 1U)
{
  /* Feature Report received via SET_REPORT */
  ((USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData)->OutEvent(hhid->Feature_buf[0],
                                                              hhid->Feature_buf[1]);
  hhid->IsFeatureReportAvailable = 0U;
  hhid->IsReportAvailable = 0U;
}
else if (hhid->IsReportAvailable == 1U)
{
  // ... 既存のOutput Report処理
}
```
