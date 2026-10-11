#include "pico/stdlib.h"
#include <stdint.h>

constexpr uint8_t ROWS = 6;
constexpr uint8_t COLS = 5;
constexpr uint8_t BUTTONS = 3;
const uint8_t rowPins[ROWS] = {2, 3, 4, 5, 6, 7};
const uint8_t colPins[COLS] = {8, 9, 10, 11, 12};
const uint8_t segmentPins[7] = {13, 14, 15, 16, 17, 18, 19};
const uint8_t buttonPins[BUTTONS] = {20, 21, 22};
constexpr bool ROW_ON  = true;
constexpr bool ROW_OFF = false;
constexpr bool COL_ON  = false;
constexpr bool COL_OFF = true;
constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t LONG_PRESS_MS = 800;
constexpr uint32_t MATRIX_CELL_US = 200;
constexpr uint32_t RESULT_MS = 1200;

enum Mode : uint8_t {
    IDLE_MODE = 0,
    TIMER_MODE = 1,
    SEQUENCE_MODE = 2
};

Mode currentMode = IDLE_MODE;
bool inMenu = false;
uint8_t menuChoice = IDLE_MODE;
uint32_t randomState = 0xA341316Cu;

uint32_t random32() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}

uint8_t randomBetween(uint8_t limit) {
    if (limit == 0) {
        return 0; 
    } else {
        uint32_t bigRandomNumber = random32();
        uint8_t remainder = bigRandomNumber % limit;
        return remainder;
    }
}


uint64_t nowMs() {
    return time_us_64() / 1000u;
}

struct Button {
    bool lastRaw = true;
    bool stable = true;
    bool shortPress = false;
    bool longPress = false;
    bool longHandled = false;
    uint64_t changedAt = 0;
    uint64_t pressedAt = 0;
};

Button buttons[BUTTONS];

void updateButtons(uint64_t now) {
    for (uint8_t i = 0; i < BUTTONS; ++i) {
        Button &b = buttons[i];
        b.shortPress = false;
        b.longPress = false;
        bool reading = gpio_get(buttonPins[i]);
        if (reading != b.lastRaw) {
            b.lastRaw = reading;
            b.changedAt = now;
        }
        if ((now - b.changedAt) >= DEBOUNCE_MS && reading != b.stable) {
            b.stable = reading;
            if (!b.stable) {
                b.pressedAt = now;
                b.longHandled = false;
            } else if (!b.longHandled) {
                b.shortPress = true;
            }
        }
        if (!b.stable &&
            !b.longHandled &&
            (now - b.pressedAt) >= LONG_PRESS_MS) {
            b.longPress = true;
            b.longHandled = true;
        }
    }
}

//7 segment

const uint8_t digitSegments[10] = {
    0b0111111, // 0
    0b0000110, // 1
    0b1011011, // 2
    0b1001111, // 3
    0b1100110, // 4
    0b1101101, // 5
    0b1111101, // 6
    0b0000111, // 7
    0b1111111, // 8
    0b1101111  // 9
};

void showDigit(int8_t digit) {
for (uint8_t s = 0; s < 7; ++s) {
    if (digit >= 0 && digit <= 9) {
        uint8_t currentDigitPattern = digitSegments[digit];
        uint8_t shiftedPattern = currentDigitPattern >> s;
        bool bitIsOn = (shiftedPattern & 1) == 1;
        if (bitIsOn) {
            gpio_put(segmentPins[s], false);
        } else {
            gpio_put(segmentPins[s], true);
        }
    } else {
        gpio_put(segmentPins[s], true);
    }
}

}

bool CellOn(uint8_t row, uint8_t col);
uint8_t matrixIndex = 0;
uint64_t lastMatrixScanUs = 0;

void scanCell() {
    for (uint8_t r = 0; r < ROWS; ++r) {
        gpio_put(rowPins[r], ROW_OFF);
    }
    for (uint8_t c = 0; c < COLS; ++c) {
        gpio_put(colPins[c], COL_OFF);
    }
    uint8_t row = matrixIndex / COLS;
    uint8_t col = matrixIndex % COLS;
    if (CellOn(row, col)) {
        gpio_put(colPins[col], COL_ON);
        gpio_put(rowPins[row], ROW_ON);
    }
    matrixIndex = (matrixIndex + 1u) % (ROWS * COLS);
}

uint8_t timerPresetMinutes = 5;
bool timerRunning = false;
uint32_t timerRemainingMs = 5u * 60u * 1000u;
uint64_t timerLastUpdate = 0;

uint32_t timerDurationMs() {
    return static_cast<uint32_t>(timerPresetMinutes) * 60u * 1000u;
}

void resetTimer(uint64_t now) {
    timerRunning = false;
    timerRemainingMs = timerDurationMs();
    timerLastUpdate = now;
}

void updateTimer(uint64_t now) {
    if (!timerRunning) {
        timerLastUpdate = now;
        return;
    }
    uint64_t elapsed = now - timerLastUpdate;
    timerLastUpdate = now;
    if (elapsed >= timerRemainingMs) {
        timerRemainingMs = 0;
        timerRunning = false;
    } else {
        timerRemainingMs -= static_cast<uint32_t>(elapsed);
    }
}

uint8_t timerRemainingMin() {
    return static_cast<uint8_t>((timerRemainingMs + 59999u) / 60000u);
}

//memory game

enum SimonPhase : uint8_t {
    SIMON_PLAYBACK,
    SIMON_INPUT,
    SIMON_RESULT
};

SimonPhase simonPhase = SIMON_PLAYBACK;
uint8_t simonSequence[9] = {};
uint8_t simonLevel = 1;
uint8_t simonPlayIndex = 0;
uint8_t simonInputIndex = 0;
uint8_t simonSelected = 0;
bool simonLit = false;
bool simonWon = false;
uint64_t simonNextAt = 0;

void startSimon(uint64_t now) {
    simonPhase = SIMON_PLAYBACK;
    simonPlayIndex = 0;
    simonInputIndex = 0;
    simonSelected = 0;
    simonLit = false;
    simonWon = false;
    simonNextAt = now;
}

void startNewSimon(uint64_t now) {
    for (uint8_t i = 0; i < 9; ++i) {
        simonSequence[i] = randomBetween(3);
    }
    simonLevel = 1;
    simonSelected = 0;
    startSimon(now);
}

void updateSimon(uint64_t now) {
    if (simonPhase != SIMON_PLAYBACK || now < simonNextAt) {
        return;
    }
    if (!simonLit) {
        if (simonPlayIndex >= simonLevel) {
            simonPhase = SIMON_INPUT;
            simonInputIndex = 0;
            simonSelected = 0;
            return;
        }
        simonLit = true;
        simonNextAt = now + 450u;
    } else {
        simonLit = false;
        simonPlayIndex=simonPlayIndex+1;
        simonNextAt = now + 180u;
    }
}

void startMode(Mode mode, uint64_t now) {
    currentMode = mode;
    switch (currentMode) {
        case IDLE_MODE:
            break;
        case TIMER_MODE:
            timerPresetMinutes = 5;
            resetTimer(now);
            break;
        case SEQUENCE_MODE:
            startNewSimon(now);
            break;
    }
}

void updateMode(uint64_t now) {
    switch (currentMode) {
        case IDLE_MODE:
            break;
        case TIMER_MODE:
            updateTimer(now);
            break;
        case SEQUENCE_MODE:
            updateSimon(now);
            if (simonPhase == SIMON_RESULT && now >= simonNextAt) {
                if (simonWon && simonLevel < 9) {
                    simonLevel = simonLevel + 1;
                    startSimon(now);
                } else {
                    startNewSimon(now);
                }
            }
            break;
    }
}

//buttons
void modeButtons(uint64_t now) {
    const bool sw1 = buttons[0].shortPress;
    const bool sw2 = buttons[1].shortPress;
    const bool sw3 = buttons[2].shortPress;
    const bool sw3Long = buttons[2].longPress;
    switch (currentMode) {
        case IDLE_MODE:
            break;
        case TIMER_MODE:
if (!timerRunning && sw1) {
    if (timerPresetMinutes >= 9) {
        timerPresetMinutes = 1;
    } else {
        timerPresetMinutes = timerPresetMinutes + 1;
    }
    
    resetTimer(now);
}

if (!timerRunning && sw2) {
    if (timerPresetMinutes <= 1) {
        timerPresetMinutes = 9;
    } else {
        timerPresetMinutes = timerPresetMinutes - 1;
    }
    resetTimer(now);
}

            if (sw3Long) {
                resetTimer(now);
            } else if (sw3) {
                if (timerRemainingMs == 0) {
                    resetTimer(now);
                }
                timerRunning = !timerRunning;
                timerLastUpdate = now;
            }
            break;
        case SEQUENCE_MODE:
            if (simonPhase == SIMON_INPUT) {
                if (sw1) {
                    if (simonSelected >= 2) {
                        simonSelected = 0;
                    } else {
                        simonSelected = simonSelected + 1;
                    }
                }
                if (sw2) {
                    startNewSimon(now);
                }
                if (sw3 && simonPhase == SIMON_INPUT) {
                    if (simonInputIndex >= simonLevel || simonInputIndex >= 9) {
                        simonWon = true;
                        simonPhase = SIMON_RESULT;
                        simonNextAt = now + RESULT_MS;
                    } else if (simonSelected != simonSequence[simonInputIndex]) {
                        simonWon = false;
                        simonPhase = SIMON_RESULT;
                        simonNextAt = now + RESULT_MS;
                    } else {
                        simonInputIndex = simonInputIndex + 1;
                        if (simonInputIndex >= simonLevel) {
                            simonWon = true;
                            simonPhase = SIMON_RESULT;
                            simonNextAt = now + RESULT_MS;
                        }
                    }
                }
            }
            break;
    }
}

//render matrix
bool sectionCell(uint8_t row, uint8_t section) {
    return row / 2u == section;
}

bool CellOn(uint8_t row, uint8_t col) {
    switch (currentMode) {
        case IDLE_MODE:
            return false;
        case TIMER_MODE: {
            uint32_t total = timerDurationMs();
            if (total == 0) {
                return false;
            }
            uint32_t litCount = (timerRemainingMs * 30u) / total;
            uint8_t index = static_cast<uint8_t>(row * COLS + col);
            return index < litCount;
        }
        case SEQUENCE_MODE:
            if (simonPhase == SIMON_PLAYBACK && simonLit) {
                return sectionCell(row, simonSequence[simonPlayIndex]);
            }
            if (simonPhase == SIMON_INPUT) {
                return sectionCell(row, simonSelected);
            }
            if (simonPhase == SIMON_RESULT && simonWon) {
                return true;
            }
            return false;
    }
    return false;
}

int8_t digitToShow(uint64_t now) {
if (inMenu) {
    uint32_t halfSecond = now / 500;
    uint32_t remainder = halfSecond % 2;
    if (remainder == 0) {
        return static_cast<int8_t>(menuChoice);
    } else {
        return -1;
    }
}

    switch (currentMode) {
        case IDLE_MODE:
            return 0;
        case TIMER_MODE:
            return static_cast<int8_t>(timerRemainingMin());
        case SEQUENCE_MODE:
if (simonPhase == SIMON_RESULT) {
    if (simonWon == true) {
        return 1;
    } else {
        return 0;
    }
}
            return static_cast<int8_t>(simonLevel);
    }
    return -1;
}

void updateSevenSegment(uint64_t now) {
    showDigit(digitToShow(now));
}

int main() {
    stdio_init_all();
    for (uint8_t r = 0; r < ROWS; ++r) {
        gpio_init(rowPins[r]);
        gpio_set_dir(rowPins[r], GPIO_OUT);
        gpio_put(rowPins[r], ROW_OFF);
    }
    for (uint8_t c = 0; c < COLS; ++c) {
        gpio_init(colPins[c]);
        gpio_set_dir(colPins[c], GPIO_OUT);
        gpio_put(colPins[c], COL_OFF);
    }
    for (uint8_t s = 0; s < 7; ++s) {
        gpio_init(segmentPins[s]);
        gpio_set_dir(segmentPins[s], GPIO_OUT);
        gpio_put(segmentPins[s], true); // common-anode segments off
    }
    for (uint8_t i = 0; i < BUTTONS; ++i) {
        gpio_init(buttonPins[i]);
        gpio_set_dir(buttonPins[i], GPIO_IN);
        gpio_pull_up(buttonPins[i]);
    }
    randomState = time_us_32() | 1u;
    uint64_t now = nowMs();
    startMode(IDLE_MODE, now);
    while (true) {
        now = nowMs();
        updateButtons(now);
        if (buttons[0].longPress) {
            inMenu = !inMenu;
            menuChoice = currentMode;
            timerLastUpdate = now;
        }
        if (inMenu) {
            if (buttons[1].shortPress) {
        if (menuChoice >= SEQUENCE_MODE) {
         menuChoice = IDLE_MODE;
        } else {
        menuChoice = menuChoice + 1;
        }
            }
            if (buttons[2].shortPress) {
                startMode(static_cast<Mode>(menuChoice), now);
                inMenu = false;
            }
        } else {
            modeButtons(now);
            updateMode(now);
        }
        updateSevenSegment(now);
        uint64_t nowUs = time_us_64();
        if (nowUs - lastMatrixScanUs >= MATRIX_CELL_US) {
            lastMatrixScanUs = nowUs;
            scanCell();
        }
        sleep_us(20);
    }
}