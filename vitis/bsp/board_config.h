#ifndef TDD_NEXYS4_BOARD_CONFIG_H
#define TDD_NEXYS4_BOARD_CONFIG_H
/* Values agreed with PEDP. These do not imply calibration or test approval. */
#define TDD_BOARD_FREQ_CODE 2u /* 0=10 kHz, 1=100 MHz/6666, 2/3=20 kHz */
#define TDD_BOARD_VDC 24.0f
#define TDD_BOARD_DEADTIME 100u /* 1 us at 100 MHz */
#define TDD_BOARD_SYNC_PHASE 1000u
/* Fill from sensor calibration, PLECS and board measurements before enabling. */
#define TDD_SENSING_CONFIRMED 0
#define TDD_PR_CONFIRMED 0
#define TDD_MODULATION_LIMIT_CONFIRMED 0
#define TDD_PROTECTION_CONFIRMED 0
#define TDD_TIMING_CONFIRMED 0
#define TDD_ADC_OFFSET 0.0f
#define TDD_ADC_GAIN 0.0f
#define TDD_RAW_MIN 0u
#define TDD_RAW_MAX 4095u
#define TDD_M_MAX 0.0f
#define TDD_B0 0.0f
#define TDD_B1 0.0f
#define TDD_B2 0.0f
#define TDD_A1 0.0f
#define TDD_A2 0.0f
/* Candidate budgets; measure the complete INTC/BSP/ISR before approval. */
#define TDD_SAMPLE_AGE_MIN 900u
#define TDD_SAMPLE_AGE_MAX 2000u
#define TDD_WRITE_BUDGET 200u
#define TDD_LATCH_MARGIN 100u
#endif
