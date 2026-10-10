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
    SEQUENCE_MODE = 2,
    ANZAN_MODE = 3
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
    return limit ? static_cast<uint8_t>(random32() % limit) : 0;
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

        bool on = digit >= 0 &&

                  digit <= 9 &&

                  ((digitSegments[digit] >> s) & 1u);

        gpio_put(segmentPins[s], on ? false : true);

    }

}


bool matrixCellOn(uint8_t row, uint8_t col, uint64_t now);

uint8_t matrixIndex = 0;

uint64_t lastMatrixScanUs = 0;

void scanOneMatrixCell(uint64_t now) {


    for (uint8_t r = 0; r < ROWS; ++r) {
        gpio_put(rowPins[r], ROW_OFF);
    }

    for (uint8_t c = 0; c < COLS; ++c) {
        gpio_put(colPins[c], COL_OFF);
    }

    uint8_t row = matrixIndex / COLS;
    uint8_t col = matrixIndex % COLS;

    if (matrixCellOn(row, col, now)) {
        gpio_put(colPins[col], COL_ON);
        gpio_put(rowPins[row], ROW_ON);
    }

    matrixIndex = (matrixIndex + 1u) % (ROWS * COLS);
}

//timer
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

uint8_t timerRemainingMinutes() {
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
uint8_t simonSelectedSection = 0;
bool simonLit = false;
bool simonWon = false;
uint64_t simonNextAt = 0;
void beginSimonPlayback(uint64_t now) {
    simonPhase = SIMON_PLAYBACK;
    simonPlayIndex = 0;
    simonInputIndex = 0;
    simonLit = false;
    simonNextAt = now;
}

void startSimon(uint64_t now) {
    for (uint8_t i = 0; i < 9; ++i) {
        simonSequence[i] = randomBetween(3);
    }
    simonLevel = 1;
    simonSelectedSection = 0;
    beginSimonPlayback(now);
}

void updateSimon(uint64_t now) {

    if (simonPhase != SIMON_PLAYBACK || now < simonNextAt) {
        return;
    }

    if (!simonLit) {
        if (simonPlayIndex >= simonLevel) {
            simonPhase = SIMON_INPUT;
            simonInputIndex = 0;
            simonSelectedSection = 0;
            return;
        }

        simonLit = true;
        simonNextAt = now + 450u;
    } else {
        simonLit = false;
        ++simonPlayIndex;
        simonNextAt = now + 180u;
    }
}

//flash anzan

enum AnzanPhase : uint8_t {

    ANZAN_FLASH,
    ANZAN_INPUT,
    ANZAN_RESULT

};

AnzanPhase anzanPhase = ANZAN_FLASH;

uint8_t anzanNumbers[3] = {};
uint8_t anzanSum = 0;
uint8_t anzanIndex = 0;
uint8_t anzanGuess = 0;
uint8_t anzanShownValue = 0;

bool anzanShowing = true;
bool anzanCorrect = false;
uint64_t anzanNextAt = 0;
void startAnzanQuestion(uint64_t now) {
    anzanSum = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        anzanNumbers[i] = static_cast<uint8_t>(1u + randomBetween(3));
        anzanSum += anzanNumbers[i];
    }

    anzanIndex = 0;
    anzanGuess = 0;
    anzanShownValue = anzanNumbers[0];
    anzanShowing = true;
    anzanPhase = ANZAN_FLASH;
    anzanNextAt = now + 300u;

}

void updateAnzan(uint64_t now) {

    if (anzanPhase != ANZAN_FLASH || now < anzanNextAt) {
        return;
    }

    if (anzanShowing) {
        anzanShowing = false;
        anzanNextAt = now + 130u;
    } else {
        ++anzanIndex;
        if (anzanIndex >= 3) {
            anzanPhase = ANZAN_INPUT;
            anzanGuess = 0;
            return;
        }
        anzanShownValue = anzanNumbers[anzanIndex];
        anzanShowing = true;
        anzanNextAt = now + 300u;
    }
}

//update

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
            startSimon(now);
            break;

        case ANZAN_MODE:
            startAnzanQuestion(now);
            break;
    }
}

void updateCurrentMode(uint64_t now) {
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
                    ++simonLevel;
                    beginSimonPlayback(now);
                } else {

                    startSimon(now);
                }
            }
            break;

        case ANZAN_MODE:
            updateAnzan(now);
            if (anzanPhase == ANZAN_RESULT && now >= anzanNextAt) {
                startAnzanQuestion(now);
            }
            break;
    }
}

//buttons

void handleModeButtons(uint64_t now) {
    const bool sw1 = buttons[0].shortPress;
    const bool sw2 = buttons[1].shortPress;
    const bool sw3 = buttons[2].shortPress;
    const bool sw3Long = buttons[2].longPress;

    switch (currentMode) {
        case IDLE_MODE:
            //menu
            break;

        case TIMER_MODE:
            //timer
            if (!timerRunning && sw1) {
                timerPresetMinutes = (timerPresetMinutes >= 9)
                    ? 1 : static_cast<uint8_t>(timerPresetMinutes + 1u);
                resetTimer(now);
            }
            if (!timerRunning && sw2) {
                timerPresetMinutes = (timerPresetMinutes <= 1)
                    ? 9 : static_cast<uint8_t>(timerPresetMinutes - 1u);
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
                    simonSelectedSection =
                        static_cast<uint8_t>((simonSelectedSection + 1u) % 3u);
                }
                if (sw2) {
                    //sequence
                    beginSimonPlayback(now);
                }
                if (sw3 && simonPhase == SIMON_INPUT) {
                    if (simonSelectedSection != simonSequence[simonInputIndex]) {
                        simonWon = false;
                        simonPhase = SIMON_RESULT;
                        simonNextAt = now + RESULT_MS;
                    } else {
                        ++simonInputIndex;
                        if (simonInputIndex >= simonLevel) {
                            simonWon = true;
                            simonPhase = SIMON_RESULT;
                            simonNextAt = now + RESULT_MS;
                        }
                    }
                }
            }
            break;

        case ANZAN_MODE:
            if (sw1 && anzanPhase != ANZAN_RESULT) {
                startAnzanQuestion(now);
            } else if (anzanPhase == ANZAN_INPUT) {
                if (sw2) {
                    anzanGuess = static_cast<uint8_t>((anzanGuess + 1u) % 10u);
                }
                if (sw3) {
                    anzanCorrect = (anzanGuess == anzanSum);
                    anzanPhase = ANZAN_RESULT;
                    anzanNextAt = now + RESULT_MS;
                }
            }
            break;
    }
}

//render matrix

bool sectionCell(uint8_t row, uint8_t section) {
    return row / 2u == section;
}

bool matrixCellOn(uint8_t row, uint8_t col, uint64_t now) {
    (void)now;

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
                return sectionCell(row, simonSelectedSection);
            }
            if (simonPhase == SIMON_RESULT && simonWon) {
                return true;
            }
            return false;

        case ANZAN_MODE: {
            uint8_t value = 0;
            if (anzanPhase == ANZAN_FLASH && anzanShowing) {
                value = anzanShownValue;
            } else if (anzanPhase == ANZAN_INPUT) {
                value = anzanGuess;
            } else {
                return false;
            }
            return row == ROWS - 1 && col < 4 && ((value >> col) & 1u);
        }
    }

    return false;
}
int8_t digitToShow(uint64_t now) {
    if (inMenu) {
        return ((now / 500u) % 2u == 0)
            ? static_cast<int8_t>(menuChoice) : -1;
    }

    switch (currentMode) {
        case IDLE_MODE:
            return 0;

        case TIMER_MODE:
            return static_cast<int8_t>(timerRemainingMinutes());

        case SEQUENCE_MODE:
            if (simonPhase == SIMON_RESULT) {
                return simonWon ? 1 : 0;
            }
            return static_cast<int8_t>(simonLevel);

        case ANZAN_MODE:
            if (anzanPhase == ANZAN_FLASH) {
                return anzanShowing ? static_cast<int8_t>(anzanShownValue) : -1;
            }
            if (anzanPhase == ANZAN_INPUT) {
                return static_cast<int8_t>(anzanGuess);
            }
            return anzanCorrect ? 1 : 0;
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
                menuChoice = static_cast<uint8_t>(
                    menuChoice >= ANZAN_MODE
                    ? static_cast<uint8_t>(IDLE_MODE)
                    : static_cast<uint8_t>(menuChoice + 1u)
                );
            }

            if (buttons[2].shortPress) {
                startMode(static_cast<Mode>(menuChoice), now);
                inMenu = false;
            }

        } else {

            handleModeButtons(now);
            updateCurrentMode(now);

        }
        updateSevenSegment(now);
        uint64_t nowUs = time_us_64();
        if (nowUs - lastMatrixScanUs >= MATRIX_CELL_US) {
            lastMatrixScanUs = nowUs;
            scanOneMatrixCell(now);
        }
        sleep_us(20);
    }
}
