#!/bin/bash
# ==============================================================================
# CubeMX再生成後に実行するスクリプト
# Feature Report対応のカスタムコードを Middleware に自動適用します
#
# 使い方: CubeMXでコード生成した後に実行
#   cd <project_root>
#   bash scripts/apply_usb_patches.sh
# ==============================================================================

set -e

PROJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHID_SRC="$PROJ_DIR/Middlewares/ST/STM32_USB_Device_Library/Class/CustomHID/Src/usbd_customhid.c"
CHID_INC="$PROJ_DIR/Middlewares/ST/STM32_USB_Device_Library/Class/CustomHID/Inc/usbd_customhid.h"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========================================"
echo " USB Feature Report パッチ適用スクリプト"
echo "========================================"

# ---------- Backup ----------
backup() {
    local f="$1"
    if [ ! -f "$f.orig" ]; then
        cp "$f" "$f.orig"
        echo -e "${YELLOW}[BACKUP]${NC} $f → $f.orig"
    fi
}

backup "$CHID_INC"
backup "$CHID_SRC"

ERRORS=0

# ============================================================
# Patch 1: usbd_customhid.h — Feature_buf & IsFeatureReportAvailable
# ============================================================
echo ""
echo "--- Patch 1: usbd_customhid.h ---"

if grep -q "Feature_buf" "$CHID_INC"; then
    echo -e "${GREEN}[SKIP]${NC} Feature_buf already present"
else
    # Add Feature_buf after Report_buf line
    sed -i '/Report_buf\[USBD_CUSTOMHID_OUTREPORT_BUF_SIZE\]/a\  uint8_t  Feature_buf[33];  /* Feature Report buffer (32B + safety) */' "$CHID_INC"
    if grep -q "Feature_buf" "$CHID_INC"; then
        echo -e "${GREEN}[OK]${NC} Added Feature_buf[33]"
    else
        echo -e "${RED}[FAIL]${NC} Could not add Feature_buf"
        ERRORS=$((ERRORS + 1))
    fi
fi

if grep -q "IsFeatureReportAvailable" "$CHID_INC"; then
    echo -e "${GREEN}[SKIP]${NC} IsFeatureReportAvailable already present"
else
    # Add IsFeatureReportAvailable after IsReportAvailable line
    sed -i '/IsReportAvailable;/a\  uint32_t IsFeatureReportAvailable;' "$CHID_INC"
    if grep -q "IsFeatureReportAvailable" "$CHID_INC"; then
        echo -e "${GREEN}[OK]${NC} Added IsFeatureReportAvailable"
    else
        echo -e "${RED}[FAIL]${NC} Could not add IsFeatureReportAvailable"
        ERRORS=$((ERRORS + 1))
    fi
fi

# ============================================================
# Patch 2: usbd_customhid.c — GET_REPORT handler
# ============================================================
echo ""
echo "--- Patch 2: usbd_customhid.c GET_REPORT ---"

if grep -q "Feature_buf" "$CHID_SRC"; then
    echo -e "${GREEN}[SKIP]${NC} Feature Report handling already present"
else
    # Replace GET_REPORT (originally empty/default) with Feature_buf send
    # Find the GET_IDLE break and add GET_REPORT after it
    sed -i '/case CUSTOM_HID_REQ_GET_IDLE:/,/break;/{
        /break;/a\
\
        case CUSTOM_HID_REQ_GET_REPORT:\
          /* GET_REPORT: Send Feature Report data to host */\
          (void)USBD_CtlSendData(pdev, hhid->Feature_buf,\
                                 MIN(32U, req->wLength));\
          break;
    }' "$CHID_SRC"

    if grep -q "CUSTOM_HID_REQ_GET_REPORT" "$CHID_SRC"; then
        echo -e "${GREEN}[OK]${NC} Added GET_REPORT handler"
    else
        echo -e "${RED}[FAIL]${NC} Could not add GET_REPORT handler"
        ERRORS=$((ERRORS + 1))
    fi
fi

# ============================================================
# Patch 3: usbd_customhid.c — SET_REPORT with Feature detection
# ============================================================
echo ""
echo "--- Patch 3: usbd_customhid.c SET_REPORT ---"

if grep -q "IsFeatureReportAvailable" "$CHID_SRC"; then
    echo -e "${GREEN}[SKIP]${NC} SET_REPORT Feature detection already present"
else
    # Replace the simple SET_REPORT with Feature-aware version
    # Original: just does USBD_CtlPrepareRx(pdev, hhid->Report_buf, ...)
    # New: checks wValue high byte for Feature (0x03) vs Output report
    sed -i '/case CUSTOM_HID_REQ_SET_REPORT:/,/break;/{
        /hhid->IsReportAvailable = 1U;/a\
          /* Check if this is a Feature Report (wValue high byte = 0x03) */\
          if ((req->wValue >> 8) == 0x03U)\
          {\
            hhid->IsFeatureReportAvailable = 1U;\
            (void)USBD_CtlPrepareRx(pdev, hhid->Feature_buf, req->wLength);\
          }\
          else\
          {
        s/(void)USBD_CtlPrepareRx(pdev, hhid->Report_buf,/(void)USBD_CtlPrepareRx(pdev, hhid->Report_buf,/
        /USBD_CtlPrepareRx.*Report_buf/a\
          }
    }' "$CHID_SRC"

    if grep -q "IsFeatureReportAvailable" "$CHID_SRC"; then
        echo -e "${GREEN}[OK]${NC} Added SET_REPORT Feature detection"
    else
        echo -e "${RED}[FAIL]${NC} Could not modify SET_REPORT"
        echo -e "${YELLOW}[INFO]${NC} Manual patching may be required — see CUBEMX_CUSTOM_PATCHES.md"
        ERRORS=$((ERRORS + 1))
    fi
fi

# ============================================================
# Patch 4: usbd_customhid.c — EP0_RxReady Feature dispatch
# ============================================================
echo ""
echo "--- Patch 4: usbd_customhid.c EP0_RxReady ---"

if grep -q "IsFeatureReportAvailable == 1U" "$CHID_SRC"; then
    echo -e "${GREEN}[SKIP]${NC} EP0_RxReady Feature dispatch already present"
else
    # In EP0_RxReady, before the existing IsReportAvailable check,
    # add the IsFeatureReportAvailable check
    sed -i '/if (hhid->IsReportAvailable == 1U)/i\
  if (hhid->IsFeatureReportAvailable == 1U)\
  {\
    /* Feature Report received via SET_REPORT */\
    ((USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData)->OutEvent(hhid->Feature_buf[0],\
                                                               hhid->Feature_buf[1]);\
    hhid->IsFeatureReportAvailable = 0U;\
    hhid->IsReportAvailable = 0U;\
  }\
  else' "$CHID_SRC"

    # Change "if" to "else if" for the original report check
    # (The 'else' we added above + the existing 'if' becomes 'else if')

    if grep -q "IsFeatureReportAvailable == 1U" "$CHID_SRC"; then
        echo -e "${GREEN}[OK]${NC} Added EP0_RxReady Feature dispatch"
    else
        echo -e "${RED}[FAIL]${NC} Could not modify EP0_RxReady"
        echo -e "${YELLOW}[INFO]${NC} Manual patching may be required — see CUBEMX_CUSTOM_PATCHES.md"
        ERRORS=$((ERRORS + 1))
    fi
fi

# ============================================================
# Summary
# ============================================================
echo ""
echo "========================================"
if [ $ERRORS -eq 0 ]; then
    echo -e "${GREEN}全パッチ適用完了!${NC}"
else
    echo -e "${RED}${ERRORS}件のパッチが失敗しました${NC}"
    echo "CUBEMX_CUSTOM_PATCHES.md を参照して手動で適用してください"
fi
echo "========================================"
