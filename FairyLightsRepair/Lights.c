/*
 * SleepyChristmasLightRepair.c
 *
 * Created: 30/10/2019 23:23:50
 * Author : Sam
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include <stdlib.h>

#include "Random.h"
// #include "Serial.h"

enum DisaplyMode {
	Breathe,
	Freeze,
	Flicker,
	Flash,
	Off
};

#define BREATH_DXINXSCALE_ASPOWER 3
#define BREATH_GRADSCALEPWR 2
static const uint8_t breathTable[33] PROGMEM = {255, 245, 203, 165, 136, 112, 91, 75, 62, 51, 42, 35, 29, 24, 20, 16, 13, 10, 8, 7, 7, 8, 11, 17, 25, 36, 52, 72, 97, 131, 170, 216, 255};

static void initHW(void);
static void prepareForOffMode(void);
static void prepareForOnModes(void);
static uint8_t getBreathIntensity(const uint8_t findMe);
static void waitForButtonRelease(void);

int main(void)
{
	initHW();

	random16InitFromSeed(0xBEEF);

	prepareForOffMode(); // The system immediately goes into low power mode so we prepare for this

	uint16_t modeTimer = 0;
	uint8_t butonState = 0xFE;
	uint8_t currentIntensity = 0xFF;
	enum DisaplyMode currentMode = Off;
	while(1) {
		// Definition of input control
		butonState = (butonState << 1) | (PINB & (1 << PINB1)) | (0xE0); // De-bounce the input with some don't cares (so that the button remains responsive)
		if(butonState == 0xF0) { // Button pressed
			currentMode++; // Next mode on button pressed
		}

		// Definition of output behavior
		switch(currentMode) {
			case Breathe:
				currentIntensity = getBreathIntensity(2*(modeTimer >> 2));
				OCR0A = currentIntensity;
				break;
			case Freeze: // Hold the current intensity
				break;
			case Flicker:
				if((modeTimer & 0x3) == 0) {
					if((random16() + (1<<15)) < (1<<15)) {
						OCR0A = currentIntensity;
					} else {
						OCR0A = 0x02;
					}
				}
				break;
			case Flash:
				if(modeTimer & 0x8) {
					OCR0A = 0x02;
				} else {
					OCR0A = currentIntensity;
				}
				break;
			case Off:
				OCR0A = 0x01;
				waitForButtonRelease();
				prepareForOffMode();
				break;
		}
		modeTimer++;

		// Go to sleep and wait for the next io update
		if(currentMode == Off) {
			sleep_enable();
			sleep_bod_disable();
			sleep_cpu();
			sleep_disable();
		} else {
			sleep_mode();
		}

		// On waking from off mode
		if(currentMode == Off) { // On waking from the off state go into the breathing state
			prepareForOnModes();
			currentIntensity = getBreathIntensity(2*(modeTimer >> 2));
			OCR0A = currentIntensity;
			waitForButtonRelease(); // Wait for the button to be released
			currentMode = Breathe;
			butonState = 0xFE;
		}
	}
}

static void initHW(void)
{
	// Lights are on PB0
	// Switch input is on PB1
	PORTB &= ~(1 << PORTB0); // Lights off
	DDRB |= (1 << DDB0); // Lights pin set as an output
	PORTB |= (1 << PORTB1) | (1 << PORTB2) | (1 << PORTB3) | (1 << PORTB4) | (1 << PORTB5); // Enable the pullups on all the digital inputs

	TCCR0A = (1<<COM0A1) | (0<<COM0A0) | (1 << WGM00); // PWM(Phase Correct)
	TCCR0B = (1 << CS00); // Divide input clock by 1

	sei();
}

static void prepareForOffMode(void)
{
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	WDTCR &= ~(1 << WDTIE); // Watchdog timer off
	GIMSK |= (1 << INT0); // Enable external interrupts on INT0
}

static void prepareForOnModes(void)
{
	set_sleep_mode(SLEEP_MODE_IDLE);
	WDTCR |= (1 << WDTIE); // Watchdog timer trips every 16ms-ish so we keep monitoring the button and updating the timer
	GIMSK &= ~(1 << INT0); // Disable external interrupts on INT0
}

static uint8_t getBreathIntensity(const uint8_t findMe)
{
	const uint8_t idxLow = findMe >> 3;
	const uint8_t valLow = pgm_read_byte(breathTable + idxLow);
	const uint8_t valHigh = pgm_read_byte(breathTable + idxLow + 1);

	uint8_t grad;
	uint8_t offSet;
	uint8_t val;
	if(valHigh >= valLow) {
		grad = valHigh - valLow;
	} else {
		grad = valLow - valHigh;
	}
	grad = (grad + (1<<(BREATH_DXINXSCALE_ASPOWER - BREATH_GRADSCALEPWR - 1))) >> (BREATH_DXINXSCALE_ASPOWER - BREATH_GRADSCALEPWR);
	offSet = (grad*(findMe & 0x7) + (1 << (BREATH_GRADSCALEPWR - 1))) >> BREATH_GRADSCALEPWR;
	if(valHigh >= valLow) {
		val = valLow + offSet;
	} else {
		val = valLow - offSet;
	}

	return val;
}

static void waitForButtonRelease(void)
{
	uint8_t butonStateUp = 0xFC;
	do {
		butonStateUp = (butonStateUp << 1) | (PINB & (1 << PINB1)); // De-bounce the input
		_delay_ms(16);
	} while(butonStateUp != 0x7E); // This loop waits for the button to go high (i.e. be released.)
}

ISR(WDT_vect)
{
}

ISR(INT0_vect)
{
}
