#pragma once

#include "stdint.h"

#define US_IN_MS (1000)

uint64_t timer_get_ms_since_boot();
uint64_t timer_diff(uint64_t time_a, uint64_t time_b);
bool     timer_expired(uint64_t time_in_ms, uint64_t expiration_period_in_ms);
