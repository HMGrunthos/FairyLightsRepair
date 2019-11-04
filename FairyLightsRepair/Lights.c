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
#include <avr/wdt.h>

#include <stdlib.h>

#include "Random.h"
// #include "Serial.h"

#define BUTTONPIN PINB1
// #define CALCLOCK

#define TICKPERIOD (1.139203319e-3)
#define TOTICKS(x) ((uint_fast32_t)((x)/TICKPERIOD + 0.5))
// #define ONDURATION TOTICKS(60*60*9)
// #define DAYPERIOD TOTICKS(60*60*(uint_fast32_t)24)
// #define ONDURATION (16)
// #define DAYPERIOD (20)
#define ONDURATION TOTICKS(10)
#define DAYPERIOD TOTICKS(20)

// #define BREATHINGDIVIDER(x) (3*((x) >> 2))
#define BREATHINGDIVIDER(x) ((3*(x) + (1<<2)) >> 3)
#define BREATH_DXINXSCALE_ASPOWER 3
#define BREATH_GRADSCALEPWR 2
static const uint8_t breathTable[33] PROGMEM = {255, 243, 200, 164, 135, 110, 90, 74, 60, 50, 41, 34, 28, 23, 18, 15, 12, 9, 7, 6, 6, 8, 11, 16, 24, 36, 52, 72, 99, 131, 172, 221, 255};

enum DisaplyMode {
	Breathe,
	Freeze,
	Flicker,
	Flash,
	Off,
	Wait
};

static volatile uint32_t sleepCounter = 0;
static volatile uint_fast8_t timerWake = 0;
static volatile uint_fast8_t startWaiting = 0;

static void initHW(void);
static void exitWaitMode(void);
static void prepareForWaitMode(void);
static void prepareForOffMode(void);
static uint8_t getBreathIntensity(const uint8_t findMe);
static void waitForButtonRelease(void);
static void synchroniseToTimer(void);

int main(void)
{
	initHW();

	random16InitFromSeed(0xBEEF);

	waitForButtonRelease();

	// Time from the button release
	cli();
		sleepCounter = 0;
		startWaiting = 0;
	sei();

	uint16_t modeTimer = 0;
	uint8_t butonState = 0xFE;
	uint8_t currentIntensity = 0xFF;
	enum DisaplyMode currentMode = Breathe;
	enum DisaplyMode lastMode = Breathe;
	while(1) {
		uint_fast8_t wakeUp = 0;

		// Definition of input control
		butonState = (butonState << 1) | (PINB & (1 << BUTTONPIN)) | (0xE0); // De-bounce the input with some don't cares (so that the button remains responsive)
		if(butonState == 0xF0) { // Button pressed
			currentMode++; // Next mode on button pressed
		}

		uint_fast32_t sleepStep;
		cli();
			sleepStep = sleepCounter;
		sei();
		if(sleepStep >= ONDURATION) {
			lastMode = currentMode;
			currentMode = Wait;
		}

		// Definition of output behavior
		switch(currentMode) {
			case Breathe:
				currentIntensity = getBreathIntensity(BREATHINGDIVIDER(modeTimer));
			case Freeze: // Hold the current intensity, leave OCR0A unchanged
				OCR0A = currentIntensity;
				break;
			case Flicker:
				// Each time the mode timer comes around then choose a new state based on random16()
				if((modeTimer & 0x3) == 0) {
					if((random16() + (((uint16_t)1<<15) - 1)) & 0x8000) {
						OCR0A = currentIntensity;
					} else {
						OCR0A = 0x02;
					}
				}
				break;
			case Flash:
				// Each time a mode timer bit changes state then change the light state
				if(modeTimer & 0x8) {
					OCR0A = 0x02;
				} else {
					OCR0A = currentIntensity;
				}
				break;
			case Off:
				// Get ready to turn off
				OCR0A = 0x01; // Low intensity while we hold the button down
				waitForButtonRelease();
				prepareForOffMode();
				break;
			case Wait:
				// Get ready to wait
				prepareForWaitMode();
				break;
		}
		modeTimer++;

ReSleep:
		do {
			sleep_mode();
			cli();
				wakeUp = timerWake;
			sei();
		} while(!wakeUp);
		cli();
			timerWake = 0;
		sei();

		if(currentMode == Wait) {
			if(wakeUp == 1) { // We're still waking because the tick timer is going off
				goto ReSleep;
			} else { // wakeUp must be == 2 (at least I hope it is)
				exitWaitMode();
				currentMode = lastMode;
				butonState = 0xFE;
			}
		}

		// On waking from off mode
		if(currentMode == Off) { // This will probably never happed since we should reset from the interrupt handler
			wdt_enable(WDTO_15MS);
		   for(;;);
		}
	}
}

static void waitForButtonRelease(void)
{
	uint8_t butonStateUp = 0xFC; // Assue the button is pressed to begin with
	do {
		butonStateUp = (butonStateUp << 1) | (PINB & (1 << BUTTONPIN)); // De-bounce the input
		synchroniseToTimer();
	} while(butonStateUp != 0x7E); // This loop waits for the button to go high (i.e. be released.)
}

static void synchroniseToTimer()
{
	// Wait for the next timer tick
	uint_fast8_t wakeCmd;
	do {
		cli();
			wakeCmd = timerWake;
		sei();
	} while(wakeCmd == 0);
	cli();
		timerWake = 0;
	sei();
}

static void initHW(void)
{
	MCUSR = 0;
	wdt_disable();

	// Lights are on PB0
	// Switch input is on PB1
	DDRB = (1 << DDB0); // Lights pin set as an output
	PORTB = (1 << PORTB1) | (1 << PORTB2) | (1 << PORTB4) | (1 << PORTB5); // Enable the pullups on all the digital inputs (except PB3 which is our clock input)

	TCCR0B = (1 << CS00); // Divide input clock by 1 to give a clock at 447681.28Hz and a TOV0 at 877.8064314Hz at 447681.28Hz
	#ifdef CALCLOCK
		TCCR0A = (1 << COM0A1) | (0 << COM0A0) | (1 << WGM01) | (1 << WGM00); // PWM(Fast), for calibration
		OCR0A = 0x80;
		for(;;);
	#else
		TCCR0A = (1 << COM0A1) | (0 << COM0A0) | (1 << WGM00); // PWM(Phase Correct)
		// OCR0A = 0xFF;
	#endif
	TIMSK0 = (1 << TOIE0); // Enable the overflow interrupt

	PRR = (1 << PRADC); // We don't need the ADC so turn it off

	sei();
}

static void exitWaitMode(void)
{
	synchroniseToTimer();
	cli();
		sleepCounter -= DAYPERIOD;
		startWaiting = 0;
	sei();
	set_sleep_mode(SLEEP_MODE_IDLE);
	GIMSK &= ~(1 << INT0); // Disable external interrupts on INT0
	TIMSK0 |= (1 << TOIE0); // Turn back on timer interrupt
	TCCR0A |= (1 << COM0A1) | (0 << COM0A0); // Turn on the PWM pin
}

static void prepareForWaitMode(void)
{
	cli();
		startWaiting = 1;
	sei();
	set_sleep_mode(SLEEP_MODE_IDLE);
	GIMSK |= (1 << INT0); // Enable external interrupts on INT0
	TCCR0A &= ~(1 << COM0A1) & ~(0 << COM0A0); // Turn off timer controlled PWM pin
	PORTB &= ~(1 << PORTB0); // Lights off
}

static void prepareForOffMode(void)
{
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	GIMSK |= (1 << INT0); // Enable external interrupts on INT0
	TIMSK0 &= ~(1 << TOIE0); // Turn off timer interrupt
	TCCR0A &= ~(1 << COM0A1) & ~(0 << COM0A0); // Turn off timer controlled PWM pin
	PORTB &= ~(1 << PORTB0); // Lights off
}

static uint8_t getBreathIntensity(const uint8_t findMe)
{
	const uint8_t idxLow = findMe >> 3; // Find the lower lookup bin
	const uint8_t valLow = pgm_read_byte(breathTable + idxLow); // Lower adjacent bin
	const uint8_t valHigh = pgm_read_byte(breathTable + idxLow + 1); // Upper adjacent bin

	uint8_t grad;
	uint8_t offSet;
	uint8_t val;
	if(valHigh >= valLow) { // If gradient is >= 0
		grad = valHigh - valLow;
	} else { // If gradient is < 0
		grad = valLow - valHigh;
	}
	grad = (grad + (1<<(BREATH_DXINXSCALE_ASPOWER - BREATH_GRADSCALEPWR - 1))) >> (BREATH_DXINXSCALE_ASPOWER - BREATH_GRADSCALEPWR);
	offSet = (grad*(findMe & 0x7) + (1 << (BREATH_GRADSCALEPWR - 1))) >> BREATH_GRADSCALEPWR;
	// Apply offset based on the sign of grad
	if(valHigh >= valLow) {
		val = valLow + offSet;
	} else {
		val = valLow - offSet;
	}

	return val;
}

ISR(TIM0_OVF_vect) // Takes about 8.947% max 5.438% normally
{
	sleepCounter++;
	if(sleepCounter > DAYPERIOD) {
		timerWake = 2;
		return;
	}
	if(((*(uint8_t*)&sleepCounter) & 0x0F) == 0) {
		if(!startWaiting) {
			timerWake = 1; // Wake at 54.9Hz when running with clock divider set to 1
			return;
		}
	}
}

ISR(INT0_vect)
{
	wdt_enable(WDTO_15MS);
	for(;;);
}

// #include <avr/eeprom.h>
// static const uint8_t oscCal4M8 EEMEM = 0x65;
/*
	// Apply the RC calibration
	uint8_t oscFix = eeprom_read_byte(&oscCal4M8);
	if(oscFix != 0xFF) {
		while(OSCCAL != oscFix) {
			OSCCAL += OSCCAL > oscFix ? -1 : 1;
		}
	}
*/
