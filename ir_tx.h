#pragma once
#include <Arduino.h>

// Hardware PWM-based IR transmitter for RTL8720CF
// ESPHome's built-in remote_transmitter crashes the MCU due to
// software bit-bang carrier generation hitting the watchdog on
// the 100MHz Cortex-M33. This uses analogWrite() for the 38kHz
// carrier instead.
//
// Available functions:
//   ir_send_raw(timings, count)        - Send raw timing array (int32_t)
//   ir_send_nec(address, command)      - Send NEC protocol
//   ir_send_raw_pronto(pronto_string)  - Send Pronto hex code

#define IR_TX_PIN 4

// Send raw timing array (positive = mark in us, negative = space in us)
// Uses int32_t to support long gaps in repeat codes (e.g. -39712)
static void ir_send_raw(const int32_t* timings, size_t count) {
  analogWriteFrequency(38000);
  for (size_t i = 0; i < count; i++) {
    if (timings[i] > 0) {
      analogWrite(IR_TX_PIN, 128);
      delayMicroseconds(timings[i]);
    } else {
      analogWrite(IR_TX_PIN, 0);
      delayMicroseconds(-timings[i]);
    }
  }
  analogWrite(IR_TX_PIN, 0);
}

// Send NEC protocol (address + command)
static void ir_send_nec(uint16_t address, uint16_t command) {
  analogWriteFrequency(38000);

  uint32_t data = 0;
  data |= (address & 0xFF);
  data |= ((~address & 0xFF) << 8);
  data |= ((command & 0xFF) << 16);
  data |= ((~command & 0xFF) << 24);

  // AGC burst: 9ms on, 4.5ms off
  analogWrite(IR_TX_PIN, 128);
  delayMicroseconds(9000);
  analogWrite(IR_TX_PIN, 0);
  delayMicroseconds(4500);

  // 32 data bits
  for (int i = 0; i < 32; i++) {
    analogWrite(IR_TX_PIN, 128);
    delayMicroseconds(562);
    analogWrite(IR_TX_PIN, 0);
    delayMicroseconds((data & (1UL << i)) ? 1687 : 562);
  }

  // Final burst
  analogWrite(IR_TX_PIN, 128);
  delayMicroseconds(562);
  analogWrite(IR_TX_PIN, 0);
}

// Send Pronto hex string
static void ir_send_raw_pronto(const char* pronto_str) {
  uint16_t vals[256];
  int count = 0;
  const char* p = pronto_str;
  while (*p && count < 256) {
    while (*p == ' ') p++;
    if (!*p) break;
    vals[count++] = (uint16_t)strtol(p, (char**)&p, 16);
  }
  if (count < 6) return;

  float period_us = (float)vals[1] * 0.241246f;
  uint16_t freq = (uint16_t)(1000000.0f / period_us);
  int intro_pairs = vals[2];
  int total_pairs = intro_pairs + vals[3];

  analogWriteFrequency(freq);

  for (int i = 0; i < total_pairs; i++) {
    int idx = 4 + i * 2;
    if (idx + 1 >= count) break;
    uint32_t on_us = (uint32_t)(vals[idx] * period_us);
    uint32_t off_us = (uint32_t)(vals[idx + 1] * period_us);
    analogWrite(IR_TX_PIN, 128);
    delayMicroseconds(on_us);
    analogWrite(IR_TX_PIN, 0);
    delayMicroseconds(off_us);
  }
}
