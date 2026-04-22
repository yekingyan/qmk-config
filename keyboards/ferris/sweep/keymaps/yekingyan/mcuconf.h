// Ferris Sweep RP2040 - 关闭 ADC 外设
// GP26-29 用作 direct pin 数字输入，必须禁用 ADC 避免引脚冲突
#pragma once

#include_next <mcuconf.h>

#undef RP_ADC_USE_ADC1
#define RP_ADC_USE_ADC1 FALSE
