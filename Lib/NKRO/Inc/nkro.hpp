#ifndef NKRO_HPP
#define NKRO_HPP

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vector> // C++機能を利用

// NKRO対応のレポート構造体 (17バイト)
typedef struct {
    uint8_t MODIFIER;
    uint8_t RESERVED;
    uint8_t KEYS[15]; // 120 keys / 8 = 15 bytes
} KeyboardReport;

// Rapid Trigger State for a single key
struct RapidTriggerState {
    uint32_t high_peak;      // 押し込みの最深点
    uint32_t low_peak;       // 戻りの最浅点
    bool is_active;          // 現在のキー状態
    
    uint32_t baseline;       // 初期値 (アイドル値)
    bool calibrated;         // 初期値取得済みフラグ
    uint32_t sensitivity;    // 感度
    uint8_t keycode;         // 対応するUSBキーコード
};

class RapidTriggerKeyboard {
public:
    static const int MUX_CH_COUNT = 16;
    
    // 現在使用している有効なキーの数（MUX1: 9個, MUX2: 8個 = 17個）
    static const int TOTAL_KEY_COUNT = 17;

    RapidTriggerKeyboard();
    void init();
    
    // MUXインデックスとADCソースを指定して更新
    // muxIndex: 0-15 (MUXのチャンネル)
    // source: 0 for MUX1(ADC1), 1 for MUX2(ADC2)
    void updateKeyByMux(int muxIndex, int source, uint32_t adcValue);
    
    // 設定変更
    // keyIndex: 0-16. -1 (or 255) for All Keys.
    void setSensitivity(int keyIndex, uint32_t value);
    
    // 現在の感度を取得 (Debug用)
    uint32_t getSensitivity(int keyIndex);

    // Flash保存・読み込み
    void saveToFlash();
    void loadFromFlash();
    void resetDefaults();
    
    // デバッグ用: Flash操作の結果コード
    // 0: 未実行/成功, 1: Erase Error, 2: Program Error, 3: Verify Error, 4: Magic Error
    int flash_status;
    uint32_t flash_error_code;
    uint32_t flash_debug_val; // デバッグ用に読み出した値などを保持

    // 現在の状態からレポートを生成して返す
    KeyboardReport* getReport();

private:
    // マッピング管理用: [Source][MuxIndex] -> StateIndex (-1 if unused)
    int keyMapping[2][MUX_CH_COUNT];
    
    // 全キーの状態配列
    RapidTriggerState keyStates[TOTAL_KEY_COUNT];
    
    KeyboardReport report;
    
    void updateRapidTriggerState(RapidTriggerState& state, uint32_t currentVal);
};

#endif // NKRO_HPP
