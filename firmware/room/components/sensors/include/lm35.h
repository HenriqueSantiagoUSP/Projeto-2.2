#pragma once

/*
 * LM35DZ room temperature, through the LM358 non-inverting stage (G = 5.5, single
 * 5 V supply) and an RC low-pass at the ADC pin (10 kOhm + 1 uF, fc ~ 16 Hz, settled
 * in ~50 ms), read by ADC1 and reported through sensor_events every LM35_PERIOD_MS.
 * The burst of LM35_SAMPLES readings LM35_SAMPLE_GAP_MS apart samples at 100 Hz, so
 * the filter sits below its 50 Hz Nyquist limit (hardware/README.md).
 *
 * A room's air takes tens of minutes to change, and the LM35 itself minutes to follow
 * it: one reading a minute is plenty for the temperature rule (10 min out of range). The
 * hub publishes a reading only when it changed (docs/protocolo.md §1).
 */

#include "esp_err.h"

#define LM35_PERIOD_MS              60000
#define LM35_SAMPLES                16
#define LM35_SAMPLE_GAP_MS          10

/*
 * End-to-end transfer of the conditioned chain (sensor + amplifier), fitted by
 * least squares on 22 bench points from 0 to 62.2 degC: V = 51.3335 * T + 144.739,
 * in mV (relatorios/Semestre 1/Projeto 2, eq. 18). Fitting the whole chain absorbs
 * the gain and offset of this LM35 and of the LM358 stage in one step.
 */
#define LM35_CHAIN_SLOPE_MV_PER_C   51.3335f
#define LM35_CHAIN_OFFSET_MV        144.739f

/*
 * Readings outside this range are a fault, not a temperature: below 0 degC an LM35
 * powered at 3.3 V does not work, and above ~57 degC the conditioned output passes
 * the ~3.1 V the ADC reads at 12 dB attenuation.
 */
#define LM35_MIN_VALID_C            0.0f
#define LM35_MAX_VALID_C            57.0f

/* Needs sensor_events_init() first. Fails if PIN_LM35 is not an ADC1 pin. */
esp_err_t lm35_init(void);
