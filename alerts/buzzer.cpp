#include <Arduino.h>

#include "buzzer.h"
#include "../hardware/config.h"

void buzzerBegin() {
  if (BUZZER_PIN < 0) {
    Serial.println("Buzzer pin is not set in config.h");
    return;
  }

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

void buzzerOn() {
  if (BUZZER_PIN >= 0) {
    digitalWrite(BUZZER_PIN, HIGH);
  }
}

void buzzerOff() {
  if (BUZZER_PIN >= 0) {
    digitalWrite(BUZZER_PIN, LOW);
  }
}