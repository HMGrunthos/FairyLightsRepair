/*
 * SleepyChristmasLightRepair.c
 *
 * Created: 30/10/2019 23:23:50
 * Author : Sam
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

#include "Random.h"

enum DisaplyMode {
	High,
	Medium1,
	Medium2,
	Low,
	Breathe,
	Flicker,
	Flash,
	Off
};
static const uint8_t MODEMASK = 0x07;

static const uint8_t BREATHEPERIOD = 1;

static void initHW(void);
static void prepareForOffMode(void);
static void prepareForOnModes(void);
static void waitForButtonRelease(void);

volatile int8_t pulseCommand = 0;

int main(void)
{
	initHW();
	
	random16InitFromSeed(0xBEEF);

	prepareForOffMode(); // The system immediately goes into low power mode so we prepare for this

	uint8_t modeState;
	uint8_t butonState = 0xFE;
	enum DisaplyMode currentMode = Off;
	while(1) {
		// Definition of input control
		butonState = (butonState << 1) | (PINB & (1 << PINB1)) | (0xE0); // De-bounce the input with some don't cares (so that the button remains responsive)
		if(butonState == 0xF0) { // Button pressed
			currentMode++; // Next mode on button pressed
		}
		
		// Definition of output behavior
		switch(currentMode & MODEMASK) {
			case High:
				cli();
					OCR0A = 0xFF;
				sei();
				break;
			case Medium1:
				cli();
					OCR0A = 0x90;
				sei();
				break;
			case Medium2:
				cli();
					OCR0A = 0x50;
				sei();
				break;
			case Low:
				cli();
					OCR0A = 0x20;
				sei();
				break;
			case Breathe:
				cli();
					if(OCR0A >= (256-BREATHEPERIOD)) {
						pulseCommand = -BREATHEPERIOD;
					} else if(OCR0A <= 0x20) {
						pulseCommand = BREATHEPERIOD;
					}
				sei();
				break;
			case Flicker:
				pulseCommand = 0;
				modeState++;
				if((modeState & 0x3) == 0) {
					if((random16() + (1<<15)) < (1<<15)) {
						cli();
							OCR0A = 0xFF;
						sei();
					} else {
						cli();
							OCR0A = 0x02;
						sei();
					}
				}
				break;
			case Flash:
				cli();
					modeState++;
					if(modeState & 0x8) {
						OCR0A = 0x02;
					} else {
						OCR0A = 0xFF;
					}
				sei();
				break;
			case Off:
				cli();
					OCR0A = 0x10;
				sei();
				waitForButtonRelease();
				prepareForOffMode();
				break;
		}
		
		cli();
			sleep_enable();
			if((currentMode & MODEMASK) == Off) {
				sleep_bod_disable();
			}
			sei();
			sleep_cpu();
			sleep_disable();
		sei();
		if((currentMode & MODEMASK) == Off) { // On waking from the off state go into the on state
			prepareForOnModes();
			OCR0A = 0xFF;
			waitForButtonRelease(); // Wait for the button to be released
			currentMode = High;
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
	OCR0A += pulseCommand;
}

ISR(INT0_vect)
{
}