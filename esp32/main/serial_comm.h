#pragma once
#include <stdbool.h>

// ESP32-P4 开发套件 - CH343 USB转串口
// GPIO37=U0_TXD, GPIO38=U0_RXD
#define UART_PORT   UART_NUM_0
#define UART_TX_PIN 37
#define UART_RX_PIN 38

void serial_comm_init(void);
void serial_comm_send(const char *msg);
