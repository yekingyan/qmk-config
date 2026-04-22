// Ferris Sweep RP2040 - 关闭 ADC 驱动
// GP26-29 用作 direct pin 数字输入，不需要 ADC
#pragma once

#include_next <halconf.h>

#undef HAL_USE_ADC
#define HAL_USE_ADC FALSE
