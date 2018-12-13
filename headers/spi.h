#ifndef SPI_H_
#define SPI_H_

#include "bit.h"

unsigned char newData;
unsigned char uC;

// Master code
void SPI_MasterInit() {
	// Set DDRB to have MOSI, SCK, and SS as output and MISO as input
	DDRB = 0xBF; PORTB = 0x40;
	// Set SPCR register to enable SPI, enable master, and use SCK frequency
	//   of fosc/16  (pg. 168)
	SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0 );
	
	// Make sure global interrupts are enabled on SREG register (pg. 9)
	SREG = 0x80;
	uC = 0x01;
}

void SPI_MasterTransmit(unsigned char cData) {
	// data in SPDR will be transmitted, e.g. SPDR = cData;
	// set SS low
	PORTB = SetBit( PORTB, 4, 0);

	/* Start transmission */
	SPDR = cData;
	/* Wait for transmission complete */
	while(!(SPSR & (1<<SPIF)));		PORTB = SetBit(PORTB, 4, 1);	sei();
}

// Servant code
void SPI_ServantInit() {
	// set DDRB to have MISO line as output and MOSI, SCK, and SS as input
	// set SPCR register to enable SPI and enable SPI interrupt (pg. 168)
	// make sure global interrupts are enabled on SREG register (pg. 9)
	
	 /* Set MISO output, all others input */
	 /* Enable SPI */
	 DDRB |= 0x40; //PORTB = 0xBF;
	 
	 SPCR |= (1 << SPE) | (1 << SPIE);
	 SREG= 0x80;
	 uC = 0x02;
	 sei();
}

ISR(SPI_STC_vect) { // this is enabled in with the SPCR register’s “SPI
	// Interrupt Enable”
	// SPDR contains the received data, e.g. unsigned char receivedData =
	// SPDR;
	newData = SPDR;
}



#endif /* SPI_H_ */