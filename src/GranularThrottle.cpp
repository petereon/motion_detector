#include <Arduino.h>

enum ReadType { DIGITAL, ANALOG };
enum CallbackMode { ON_TIMER, ON_CHANGE, ON_TIMER_AND_CHANGE, ON_TIMER_OR_CHANGE };

struct SinglePinThrottle {
  uint8_t                  pin;
  ReadType                 readType;
  CallbackMode             callbackMode;
  unsigned long            delayTime;
  std::function<void(int)> callback;
  unsigned long            lastTime;
  int                      lastValue;
};

class GranularPinThrottle {
private:
  uint lastIndex = 0;

public:
  SinglePinThrottle *throttles[100];
  GranularPinThrottle() {
    for (int i = 0; i < 100; i++) {
      throttles[i] = nullptr;
    }
  }

  void registerPinCallback(uint8_t pin, ReadType readType, CallbackMode callbackMode, unsigned long delayTime,
                           std::function<void(int)> callback) {
    auto *throttle = new SinglePinThrottle();

    throttle->pin = pin;
    throttle->readType = readType;
    throttle->callbackMode = callbackMode;
    throttle->delayTime = delayTime;
    throttle->callback = callback;
    throttle->lastTime = 0;
    throttle->lastValue = -1;

    lastIndex++;
    throttles[lastIndex] = throttle;
  }

  void processPins() {
    for (uint i = 0; i <= lastIndex; i++) {
      if (throttles[i] == nullptr) {
        continue;
      }

      SinglePinThrottle *throttle = throttles[i];
      int currentValue = throttle->readType == DIGITAL ? digitalRead(throttle->pin) : analogRead(throttle->pin);

      bool isTime = millis() - throttle->lastTime >= throttle->delayTime;
      bool hasChanged = currentValue != throttle->lastValue;

      bool shouldExecute;
      switch (throttle->callbackMode) {
      case ON_TIMER:
        shouldExecute = isTime;
        break;
      case ON_CHANGE:
        shouldExecute = hasChanged;
        break;
      case ON_TIMER_AND_CHANGE:
        shouldExecute = isTime && hasChanged;
        break;
      case ON_TIMER_OR_CHANGE:
        shouldExecute = isTime || hasChanged;
        break;
      default:
        shouldExecute = false;
        break;
      }

      if (shouldExecute) {
        if (throttle->callback != nullptr) {
          throttle->callback(currentValue);
          throttle->lastTime = millis();
          throttle->lastValue = currentValue;
        }
      }
    }
  }
};
