#include <Arduino.h>
#include <TaskScheduler.h>

// LED 핀 정의 (변경 없음)
#define LED_RED     5   // 빨간 LED
#define LED_YELLOW  6   // 노란 LED
#define LED_GREEN   7   // 초록 LED
#define LED_BLINK   8   // 깜빡임용 LED

// 버튼 핀 정의 (외부 인터럽트 핀 재배치)
// 기존: 시스템 OFF/ON 버튼 → 핀2, 비상 버튼 → 핀3
// → 변경: 비상 버튼을 핀2 (INT0)로, 시스템 OFF/ON 버튼을 핀3 (INT1)로 재배정합니다.
#define BUTTON_EMERGENCY 2  // 비상 모드: 외부 인터럽트 INT0 사용
#define BUTTON_RESET     3  // 시스템 OFF/ON 모드: 외부 인터럽트 INT1 사용
#define BUTTON_BLINKING  4  // 깜빡임 모드: 폴링 방식

// 모드 플래그 (ISR 또는 폴링에 의해 변경되므로 volatile)
volatile bool emergencyMode = false;  // true이면 비상 모드 (빨간 LED 전용)
volatile bool systemOffMode = false;  // true이면 시스템 OFF (모든 LED OFF)
volatile bool blinkingMode  = false;  // true이면 깜빡임 모드 (모든 LED 0.5초 주기 깜빡임)

// 외부 인터럽트 디바운스 (micros 단위)
const unsigned long debounceDelayMicro = 200000; // 200ms (200,000µs)
volatile unsigned long lastEmergencyInterruptTime = 0;
volatile unsigned long lastResetInterruptTime     = 0;

// 깜빡임 모드 버튼 (폴링) 디바운스 변수 (millis 단위)
unsigned long lastBlinkingDebounceTime = 0;
const unsigned long blinkingDebounceDelay = 50; // 50ms
int lastBlinkingButtonState = HIGH;  // 초기 상태는 내부 풀업으로 HIGH

// TaskScheduler 및 신호등 주기 관련
Scheduler runner;
const unsigned long cycleDuration = 6000;  // 6초 주기
unsigned long cycleStart = 0;

// 프로토타입 및 100ms 주기 태스크 등록
void updateTrafficLights();
Task trafficTask(100, TASK_FOREVER, &updateTrafficLights, &runner, true);

// ------------------- 외부 인터럽트 서비스 루틴 -------------------

// [비상 모드] — 버튼(비상) 연결: 디지털 핀 2 (INT0)
void emergencyISR() {
  unsigned long current = micros();
  if (current - lastEmergencyInterruptTime >= debounceDelayMicro) {
    emergencyMode = !emergencyMode;  // 모드 토글
    lastEmergencyInterruptTime = current;
  }
}

// [시스템 OFF/ON 모드] — 버튼(전체 전원 제어) 연결: 디지털 핀 3 (INT1)
void resetISR() {
  unsigned long current = micros();
  if (current - lastResetInterruptTime >= debounceDelayMicro) {
    systemOffMode = !systemOffMode;  // 모드 토글
    lastResetInterruptTime = current;
  }
}

// ------------------- 폴링 방식 깜빡임 모드 버튼 -------------------
void pollBlinkingButton() {
  int currentState = digitalRead(BUTTON_BLINKING);
  unsigned long currentTime = millis();
  if ((lastBlinkingButtonState == HIGH) && (currentState == LOW) &&
      (currentTime - lastBlinkingDebounceTime > blinkingDebounceDelay)) {
    blinkingMode = !blinkingMode;
    lastBlinkingDebounceTime = currentTime;
  }
  lastBlinkingButtonState = currentState;
}

// ------------------- setup() -------------------
void setup() {
  // LED 핀 출력 설정
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLINK, OUTPUT);
  
  // 모두 초기 OFF
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLINK, LOW);
  
  // 버튼 핀 입력 (내부 풀업 활성화)
  pinMode(BUTTON_EMERGENCY, INPUT_PULLUP);
  pinMode(BUTTON_RESET, INPUT_PULLUP);
  pinMode(BUTTON_BLINKING, INPUT_PULLUP);
  
  // 외부 인터럽트 부착
  attachInterrupt(digitalPinToInterrupt(BUTTON_EMERGENCY), emergencyISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_RESET), resetISR, FALLING);
  // 깜빡임 모드는 폴링으로 처리
  
  // 신호등 정상 동작 기준 시간 설정
  cycleStart = millis();
}

// ------------------- loop() -------------------
void loop() {
  runner.execute();      // 100ms마다 updateTrafficLights() 실행
  pollBlinkingButton();  // 깜빡임 버튼 폴링
}

// ------------------- 신호등 업데이트 함수 -------------------
// 우선순위: 시스템 OFF (최우선) > 비상 모드 > 깜빡임 모드 > 정상 동작
void updateTrafficLights() {
  // 1. 시스템 OFF 모드: 모든 LED OFF
  if (systemOffMode) {
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_BLINK, LOW);
    return;
  }
  
  // 2. 비상 모드: 빨간 LED만 ON
  if (emergencyMode) {
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_BLINK, LOW);
    return;
  }
  
  // 3. 깜빡임 모드: 모든 LED가 0.5초 주기로 깜빡임
  if (blinkingMode) {
    bool blinkState = ((millis() / 500) % 2) == 0;
    digitalWrite(LED_RED,     blinkState ? HIGH : LOW);
    digitalWrite(LED_YELLOW,  blinkState ? HIGH : LOW);
    digitalWrite(LED_GREEN,   blinkState ? HIGH : LOW);
    digitalWrite(LED_BLINK,   blinkState ? HIGH : LOW);
    return;
  }
  
  // 4. 정상 동작: 순차 신호등 (총 6000ms)
  unsigned long now = millis();
  unsigned long elapsed = (now - cycleStart) % cycleDuration;
  
  // 우선 모든 LED OFF 후 신호에 따른 ON 처리
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLINK, LOW);
  
  if (elapsed < 2000) {
    // 0 ~ 2000ms: 빨간 LED ON (2초)
    digitalWrite(LED_RED, HIGH);
  }
  else if (elapsed < 2500) {
    // 2000 ~ 2500ms: 노란 LED ON (0.5초)
    digitalWrite(LED_YELLOW, HIGH);
  }
  else if (elapsed < 4500) {
    // 2500 ~ 4500ms: 초록 LED ON (2초)
    digitalWrite(LED_GREEN, HIGH);
  }
  else if (elapsed < 5500) {
    // 4500 ~ 5500ms: 1초 동안 100ms 단위로 깜빡임 (tick 0,3,6일 때 LED_BLINK ON)
    int tick = (elapsed - 4500) / 100;
    if (tick == 0 || tick == 3 || tick == 6) {
      digitalWrite(LED_BLINK, HIGH);
    }
  }
  else {
    // 5500 ~ 6000ms: 노란 LED ON (0.5초)
    digitalWrite(LED_YELLOW, HIGH);
  }
}
