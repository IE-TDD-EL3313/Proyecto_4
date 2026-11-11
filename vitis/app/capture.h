#ifndef TDD_CAPTURE_H
#define TDD_CAPTURE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#define TDD_CAPTURE_CAPACITY 2048u
#define TDD_CAPTURE_SHORT 1u
#define TDD_CAPTURE_FAULT 2u
#define TDD_CAPTURE_ADC_ERROR 4u
struct tdd_record { uint16_t raw; int16_t reference_ma; };
struct tdd_metadata {
    uint32_t capture_id, count, fs_hz, first_seq, first_ticks, period_ticks;
    float offset_code, gain_a_per_code;
    uint16_t flags;
};
struct tdd_bank {
    struct tdd_record records[TDD_CAPTURE_CAPACITY];
    uint32_t head, count, last_seq, last_ticks;
    uint16_t flags;
    struct tdd_metadata frozen;
};
struct tdd_capture {
    struct tdd_bank bank[2];
    struct tdd_metadata config;
    uint32_t active, next_id;
    bool stopped;
    /* Single writer per flag; acquire/release transfers ownership. */
    atomic_uint request, ready, release;
};
struct tdd_transmitter {
    unsigned bank;
    uint32_t position, total, crc;
    uint8_t header[48];
    bool active;
};
bool tdd_capture_init(struct tdd_capture *c, uint32_t fs, uint32_t period,
                      float offset, float gain);
bool tdd_capture_request(struct tdd_capture *c); /* main only */
bool tdd_capture_push(struct tdd_capture *c, uint16_t raw, float reference_a,
                      uint32_t seq, uint32_t ticks); /* ISR only */
void tdd_capture_fault(struct tdd_capture *c, bool adc_error); /* ISR only */
void tdd_capture_service(struct tdd_capture *c); /* ISR, including FAULT */
bool tdd_tx_begin(struct tdd_capture *c, struct tdd_transmitter *tx); /* main */
size_t tdd_tx_read(struct tdd_capture *c, struct tdd_transmitter *tx,
                    uint8_t *output, size_t capacity); /* main, bounded */
uint32_t tdd_crc32_update(uint32_t state, uint8_t byte);
#endif
