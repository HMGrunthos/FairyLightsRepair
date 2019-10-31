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

#define TIMEROUTPUT

enum DisaplyMode {
	High,
	Medium,
	Low,
	Breathe,
	Pulse,
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

volatile enum DisaplyMode currentMode;
volatile int8_t pulseCommand = 0;

int main(void)
{
	initHW();
	
	random16InitFromSeed(0xBEEF);

	uint8_t modeState;
	uint8_t butonState = 0xFE;
	currentMode = Off;
	while(1) {
		// Definition of input control
		butonState = (butonState << 1) | (PINB & (1 << PINB1)) | (0xE0); // De-bounce the input with some don't cares so that the buttons are responsive
		if(butonState == 0xF0) { // Button pressed
			currentMode++; // Next mode on button pressed
			modeState = 0;
			cli();
				pulseCommand = 0; // Reset the pulse command on entry to a new mode
				OCR0A = 0;
			sei();
		}
		
		// Definition of output behavior
		switch(currentMode & MODEMASK) {
			case High:
				cli();
					#ifdef TIMEROUTPUT
						OCR0A = 0xFF;
					#endif
				sei();
				#ifndef TIMEROUTPUT
					PORTB |= (1 << PORTB0); // Lights on
				#endif
				break;
			case Medium:
				cli();
					#ifdef TIMEROUTPUT
						OCR0A = 0x80;
					#endif
				sei();
				#ifndef TIMEROUTPUT
					PINB |= (1 << PINB0); // Lights toggle
				#endif
				break;
			case Low:
				cli();
					#ifdef TIMEROUTPUT
						OCR0A= 0x40;
					#endif
				sei();
				#ifndef TIMEROUTPUT
					modeState++;
					if((modeState & 0x1) == 0) {
						PINB |= (1 << PINB0); // Lights toggle slower
					}
				#endif
				break;
			case Breathe:
				cli();
					#ifdef TIMEROUTPUT
						if(OCR0A >= (256-BREATHEPERIOD)) {
							pulseCommand = -BREATHEPERIOD;
						} else if(OCR0A < 32) {
							pulseCommand = BREATHEPERIOD;
						}
					#endif
				sei();
				#ifndef TIMEROUTPUT
					PORTB |= (1 << PORTB0); // Lights on
				#endif
				break;
			case Pulse:
				cli();
					#ifdef TIMEROUTPUT
						pulseCommand = 1;
					#endif
				sei();
				#ifndef TIMEROUTPUT
					PINB |= (1 << PINB0); // Lights toggle
				#endif
				break;
			case Flicker:
				#ifdef TIMEROUTPUT
					modeState++;
					if((modeState & 0x3) == 0) {
						if((random16() + (1<<15)) < (1<<15)) {
							cli();
								OCR0A = 0xFF;
							sei();
						} else {
							cli();
								OCR0A = 0x00;
							sei();
						}
					}
				#endif
				#ifndef TIMEROUTPUT
					PORTB |= (1 << PORTB0); // Lights on
				#endif
				break;
			case Flash:
				cli();
					#ifdef TIMEROUTPUT
						modeState++;
						if((modeState & 0x7) == 0) {
							if(OCR0A == 0xFF) {
								OCR0A = 0x00;	
							} else {
								OCR0A = 0xFF;
							}
						}
					#endif
				sei();
				#ifndef TIMEROUTPUT
					PINB |= (1 << PINB0); // Lights toggle
				#endif
				break;
			case Off:
				cli();
					#ifdef TIMEROUTPUT
						pulseCommand = 0;
						OCR0A = 0x10;
					#endif
				sei();
				#ifndef TIMEROUTPUT
					PORTB &= ~(1 << PORTB0); // Lights off
				#endif
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
			#ifdef TIMEROUTPUT
				OCR0A = 0xFF;
			#else
				PORTB |= (1 << PORTB0); // Lights on
			#endif
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

	#ifdef TIMEROUTPUT
		TCCR0A = (1<<COM0A1) | (0<<COM0A0) | (1 << WGM00); // PWM(Phase Correct)
		TCCR0B = (1 << CS00); // Divide input clock by 1
	#endif

	prepareForOffMode(); // The system starts off turned off

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