#include "nkro.hpp"
#include "stm32g4xx_hal.h" // HALライブラリ (Flash操作用)

// Flash Page 31 (Last Page of **64KB** model? or 128KB model?)
// STM32G431KB = 128KB Flash. 
// Bank 1 Only. Page Size = 2KB. 128/2 = 64 Pages (0 to 63).

// 動作確認のため、あえてページをずらしてみる (Page 63 -> Page 62)
// 0x0801 F800 - 0x800(2KB) = 0x0801 F000
#define FLASH_USER_START_ADDR   0x0801F000
#define FLASH_PAGE_INDEX        62
#define FLASH_MAGIC_NUMBER      0xDEADBEEF 

RapidTriggerKeyboard::RapidTriggerKeyboard() {
    flash_status = -1; // 初期値
    init();
}

void RapidTriggerKeyboard::init() {
    // 1. マッピング配列の初期化 (-1 で埋める)
    for (int src = 0; src < 2; src++) {
        for (int i = 0; i < MUX_CH_COUNT; i++) {
            keyMapping[src][i] = -1;
        }
    }

    // 2. キー状態の初期化
    int keyCounter = 0;
    
    // ラムダ式が使えない環境(C++11未満)に備えてベタ書きするか、C++11以上ならautoで書く
    // STM32CubeIDEのデフォルトはC++14/17のはず
    auto registerKey = [&](int source, int muxChannel, uint8_t hidCode) {
        if (keyCounter >= TOTAL_KEY_COUNT) return;
        
        // マッピング登録
        keyMapping[source][muxChannel] = keyCounter;
        
        // 初期状態設定
        keyStates[keyCounter].high_peak = 0;       // 最も浅い位置 (更新されていく)
        keyStates[keyCounter].low_peak = 4096;     // 最も深い位置 (更新されていく)
        keyStates[keyCounter].is_active = false;
        
        // ベースライン (初期値) は後で取得
        keyStates[keyCounter].baseline = 0; 
        keyStates[keyCounter].calibrated = false; 
        
        // sensitivity: ON/OFF判定に必要な変化量
        // 少し鈍感にしてばたつきを防ぐ (30 -> 50)
        keyStates[keyCounter].sensitivity = 50;
        
        keyStates[keyCounter].keycode = hidCode;

        keyCounter++;
    };

    // --- MUX1 (ADC1) configuration ---
    // Source id: 0
    registerKey(0, 0,  0x62); // Keypad 0
    registerKey(0, 15, 0x63); // Keypad .
    registerKey(0, 14, 0x58); // Keypad Enter
    registerKey(0, 13, 0x5B); // Keypad 3
    registerKey(0, 12, 0x5A); // Keypad 2
    registerKey(0, 11, 0x59); // Keypad 1
    registerKey(0, 10, 0x5E); // Keypad 6
    registerKey(0, 9,  0x5D); // Keypad 5
    registerKey(0, 8,  0x5C); // Keypad 4
    
    // --- MUX2 (ADC2) configuration ---
    // Source id: 1
    registerKey(1, 15, 0x57); // Keypad +
    registerKey(1, 14, 0x61); // Keypad 9
    registerKey(1, 13, 0x60); // Keypad 8
    registerKey(1, 12, 0x5F); // Keypad 7
    registerKey(1, 11, 0x56); // Keypad -
    registerKey(1, 10, 0x55); // Keypad *
    registerKey(1, 9,  0x54); // Keypad /
    registerKey(1, 8,  0x53); // Keypad NumLock
}

void RapidTriggerKeyboard::updateKeyByMux(int muxIndex, int source, uint32_t adcValue) {
    if (source < 0 || source > 1 || muxIndex < 0 || muxIndex >= MUX_CH_COUNT) return;
    
    int stateIndex = keyMapping[source][muxIndex];
    if (stateIndex != -1) {
        updateRapidTriggerState(keyStates[stateIndex], adcValue);
    }
}

void RapidTriggerKeyboard::setSensitivity(int keyIndex, uint32_t value) {
    // 範囲チェック (valueの上限などは適宜決める)
    if (value < 1) value = 1;
    if (value > 1000) value = 1000; // 仮の上限

    if (keyIndex == 255 || keyIndex == -1) {
        // 全キー適用
        for (int i = 0; i < TOTAL_KEY_COUNT; i++) {
            keyStates[i].sensitivity = value;
        }
    } else {
        // 個別適用
        if (keyIndex >= 0 && keyIndex < TOTAL_KEY_COUNT) {
            keyStates[keyIndex].sensitivity = value;
        }
    }
}

uint32_t RapidTriggerKeyboard::getSensitivity(int keyIndex) {
    if (keyIndex >= 0 && keyIndex < TOTAL_KEY_COUNT) {
        return keyStates[keyIndex].sensitivity;
    }
    return 0;
}

#include <stdio.h>

void RapidTriggerKeyboard::saveToFlash() {
    HAL_FLASH_Unlock();
    
    flash_status = 0; 
    flash_error_code = 0;

    // 1. Erase Page
    FLASH_EraseInitTypeDef EraseInitStruct;
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.Banks = FLASH_BANK_1;
    EraseInitStruct.Page = FLASH_PAGE_INDEX;
    EraseInitStruct.NbPages = 1;

    uint32_t PageError = 0;
    if (HAL_FLASHEx_Erase(&EraseInitStruct, &PageError) != HAL_OK) {
        flash_status = 1; // Erase Error
        flash_error_code = PageError;
        HAL_FLASH_Lock();
        return; 
    }

    uint32_t address = FLASH_USER_START_ADDR;

    // 2. Write Magic Number
    uint64_t magic_data = FLASH_MAGIC_NUMBER;
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, magic_data) != HAL_OK) {
        flash_status = 2; // Write Error (Magic)
        flash_error_code = HAL_FLASH_GetError();
        HAL_FLASH_Lock();
        return;
    }
    
    // Verify Magic
    if (*(__IO uint32_t*)address != FLASH_MAGIC_NUMBER) {
        flash_status = 4; // Magic Mismatch (Verify Error)
        flash_debug_val = *(__IO uint32_t*)address;
        HAL_FLASH_Lock();
        return;
    }
    address += 8;

    // 3. Write Sensitivities
    for (int i = 0; i < TOTAL_KEY_COUNT; i++) {
        uint64_t data = keyStates[i].sensitivity;
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, data) != HAL_OK) {
             flash_status = 3; // Write Error (Data)
             flash_error_code = HAL_FLASH_GetError();
             flash_debug_val = (uint32_t)i;
             break; 
        }
        
        // Verify Data
        if (*(__IO uint32_t*)address != (uint32_t)data) {
             flash_status = 5; // Data Mismatch
             flash_debug_val = *(__IO uint32_t*)address;
             break;
        }

        address += 8;
    }

    HAL_FLASH_Lock();
}

void RapidTriggerKeyboard::loadFromFlash() {
    uint32_t address = FLASH_USER_START_ADDR;
    
    // Check Magic Number
    uint32_t magic = *(__IO uint32_t*)address;
    
    // Debug Print (printfが使えるか怪しいが、Setup後なら出るはず)
    // printf("Flash Load Addr: %08X, Magic: %08X (Expected: %08X)\n", address, magic, FLASH_MAGIC_NUMBER);

    if (magic != FLASH_MAGIC_NUMBER) {
        // printf("Flash Load: No valid data (Magic: %lx)\n", magic);
        return; // No saved data or invalid
    }
    // printf("Flash Load: Valid data found.\n");
    address += 8; // Skip Magic

    for (int i = 0; i < TOTAL_KEY_COUNT; i++) {
        // Read stored sensitivity
        uint32_t val = *(__IO uint32_t*)address; 
        
        // Sanity Check
        if (val >= 1 && val <= 1000) {
            keyStates[i].sensitivity = val;
        }
        
        address += 8;
    }
}

void RapidTriggerKeyboard::resetDefaults() {
    for (int i = 0; i < TOTAL_KEY_COUNT; i++) {
        keyStates[i].sensitivity = 50;  // デフォルト感度
    }
    flash_status = 0;
}

void RapidTriggerKeyboard::updateRapidTriggerState(RapidTriggerState& state, uint32_t currentVal) {
    // 0. 初期化 (Calibration)
    if (!state.calibrated) {
        state.baseline = currentVal;
        state.low_peak = currentVal;
        state.high_peak = currentVal;
        state.calibrated = true;
        return;
    }

    // 磁気センサー (ホール効果) : N極/S極の向きによって挙動が異なる
    // 観測結果: Idle(~2300) -> Pressed(~3200)  (値が増加するタイプ)
    
    // high_peak = 最大値 (最も深い位置)
    // low_peak  = 最小値 (最も浅い位置)
    
    // 1. ピークの更新
    if (currentVal > state.high_peak) {
        state.high_peak = currentVal;
    }
    if (currentVal < state.low_peak) {
        state.low_peak = currentVal;
    }
    
    // 2. リセット処理 (ドリフト対策)
    // ベースラインより下がったら、ベースライン自体を更新する（温度変化などで下がる場合があるため）
    if (currentVal < state.baseline) {
        state.baseline = currentVal;
    }

    // 安全マージン（ノイズでONにならないように、ベースライン+少しのマージンを設ける）
    // デッドゾーンとは異なり、「初期値からの変化」を見るための最低ライン
    uint32_t noise_margin = 30; // ノイズ耐性
    uint32_t activation_floor = state.baseline + noise_margin;

    if (state.is_active) {
        // 現在ON (押されている) -> OFF判定 (Release)
        // 「最も深い位置 (high_peak)」から「感度」分だけ戻ったら(値が減ったら) OFF
        if (currentVal < (state.high_peak - state.sensitivity)) {
            state.is_active = false;
            // リセット: Releaseされた位置を新たな「最も浅い位置」の基準にする
            state.low_peak = currentVal; 
        }
    } else {
        // 現在OFF (離されている) -> ON判定 (Actuation)
        // 1. 「最も浅い位置 (low_peak)」から「感度」分だけ押し込まれたら(値が増えたら) ON
        // 2. ただし、ベースライン+マージンを超えていること
        
        bool movedEnough = (currentVal > (state.low_peak + state.sensitivity));
        bool aboveBaseline = (currentVal > activation_floor); 

        if (movedEnough && aboveBaseline) {
            state.is_active = true;
            // Actuationされた位置を新たな「最も深い位置」の基準にする
            state.high_peak = currentVal; 
        }
        
        // (安全策) ベースライン付近に戻ったら強制リセット
        if (currentVal <= activation_floor) {
             state.is_active = false;
             state.high_peak = currentVal; 
             state.low_peak = currentVal;
        }
    }
}

KeyboardReport* RapidTriggerKeyboard::getReport() {
    // レポートのリセット
    memset(&report, 0, sizeof(KeyboardReport));

    // Bitmap生成
    // 0x00-0x03 は Modifier/Reserve で使われるため不要だが、
    // 配列 KEYS[0] は Usage ID 0x00-0x07、KEYS[1] は 0x08-0x0F... と続く
    // Usage ID X のビット位置:
    // Byte Index = X / 8
    // Bit Index  = X % 8
    
    for (int i = 0; i < TOTAL_KEY_COUNT; i++) {
        if (keyStates[i].is_active) {
            uint8_t code = keyStates[i].keycode;
            
            // 安全策: 配列外アクセス防止 (KEYSは15バイト = 120ビット => Usage ID 119まで)
            if (code < 120) {
                int byteIndex = code / 8;
                int bitIndex  = code % 8;
                
                report.KEYS[byteIndex] |= (1 << bitIndex);
            }
        }
    }
    
    return &report;
}
