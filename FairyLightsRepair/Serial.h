/*
 * Serial.h
 *
 * Created: 19/10/2019 11:20:21
 *  Author: Sam
 */ 

#ifndef SERIAL_H_
	#define SERIAL_H_

	char uartGetc(void);
	void uartPutc(char c);
	void uartPuts(const char *s);
	void uartPuts_P(const char *s);
#endif /* SERIAL_H_ */
