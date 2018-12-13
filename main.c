#include <avr/io.h>
#include <util/atomic.h>
#include <avr/eeprom.h>
#include <math.h>
#include "spi.h"
#include "keypad.h"
#include "scheduler.h"
#include "io.h"
#include "usart.h"

/* ----------  DEFINITIONS  ---------- */

#define uchar unsigned char
#define RIGHT UD > 800
#define LEFT UD < 150
#define DOWN LR > 850
#define UP LR < 150
#define PROFILER PIND & 0x02
#define WELCOMER !(PIND & 0x02)
#define VALID GetKeypadKey() != 'A' && GetKeypadKey() != 'B' && GetKeypadKey() != 'C' && GetKeypadKey() != 'D' && GetKeypadKey() != '\0'
#define input1234 GetKeypadKey() == '1' || GetKeypadKey() == '2' || GetKeypadKey() == '3' || GetKeypadKey() == '4'
#define OK_TO_WATER (plant1.dayTimeWaterOK == 0 && SUN_reading < plant1.sunLevel) && day >= frequency && MS_reading < plant1.moisture

#define ONE 0x14
#define TWO 0xB3
#define THREE 0xB6
#define FOUR 0xD4

#define SLOT1 1
#define SLOT2 7
#define SLOT3 13
#define SLOT4 19

/* ----------  STRUCTURES  ---------- */
typedef struct PlantProfile {
	uchar dayTimeWaterOK;
	uchar waterFrequency;
	unsigned short moisture;
	unsigned short sunLevel;
} PlantProfile;

/* -----------  GLOBALS  ----------- */
struct PlantProfile plant1;
unsigned short LR = 0;
unsigned short UD = 0;
unsigned short MS_reading;
unsigned short SUN_reading;
uchar enableScaler;

/* -----------  CONSTANTS  ----------- */
const uchar *profileQs[] = {
	"OK to water in  day?",
	"# of day betweenwatering?",
	"Moisture Sense: 1,2,3,4?:"
};

const char *verbose[] = {
	"Yes",
	"No",
	" "
};

const uchar *answer = " ";

/* ----------  SS VARIABLES  ---------- */
uchar stored;
uchar memSlot;
uchar num;
uchar control;
uchar gotit;


/* ----------  SYSTEM FUNCTIONS  ---------- */

void ADC_init() {
	ADMUX = (1 << REFS0);
	ADCSRA |= (1 << ADEN) | (1 << ADSC) | (1 << ADATE);
}

void setADCPin(unsigned char pinNum) {
	ADMUX = (pinNum <= 0x07) ? pinNum: ADMUX;
	static unsigned char i = 0;
	for (i = 0; i < 30; ++i) { asm("nop"); }
}

void readJoystick() {
	ADMUX = (4 <= 0x07) ? 4: ADMUX;
	static unsigned char i = 0;
	for (i = 0; i < 30; ++i) { asm("nop"); }
	LR = ADC;
	
	ADMUX = (5 <= 0x07) ? 5: ADMUX;
	i = 0;
	for (i = 0; i < 30; ++i) { asm("nop"); }
	UD = ADC;
}

void LCD_clearBottomRow() {
	LCD_DisplayString(17, "                ");
}

void transmit_data(uchar data) {
	uchar i;
	for (i = 0; i < 8; ++i) {
		PORTA = 0x08;
		PORTA |= ((data >> i) & 0x01);
		PORTA |= 0x04;
	}
	PORTA |= 0x02;
	PORTA = 0x00;
}

void saveMS(unsigned short m, uchar slot) {
	if (slot == SLOT1) {slot = SLOT1;}
	if (slot == SLOT2) {slot = SLOT2;}
	if (slot == SLOT3) {slot = SLOT3;}
	if (slot == SLOT4) {slot = SLOT4;}
	eeprom_write_word(slot+2, m);
}

void saveSun(unsigned short s, uchar slot) {
	if (slot == SLOT1) {slot = SLOT1;}
	if (slot == SLOT2) {slot = SLOT2;}
	if (slot == SLOT3) {slot = SLOT3;}
	if (slot == SLOT4) {slot = SLOT4;}
	eeprom_write_word(slot+4, s);
}

void savePlantProfile(PlantProfile p, uchar slot) {
	if (slot == SLOT1) {slot = SLOT1;}
	if (slot == SLOT2) {slot = SLOT2;}
	if (slot == SLOT3) {slot = SLOT3;}
	if (slot == SLOT4) {slot = SLOT4;}
	eeprom_write_byte(slot, p.dayTimeWaterOK);
	eeprom_write_byte(slot+1, p.waterFrequency);
	eeprom_write_word(slot+2, p.moisture);
	eeprom_write_word(slot+4, p.sunLevel);
}

void retrievePlantProfile(uchar slot) {
	if (slot == SLOT1) {slot = SLOT1;}
	if (slot == SLOT2) {slot = SLOT2;}
	if (slot == SLOT3) {slot = SLOT3;}
	if (slot == SLOT4) {slot = SLOT4;}
	plant1.dayTimeWaterOK = eeprom_read_byte(slot);
	plant1.waterFrequency = eeprom_read_byte(slot+1);
	plant1.moisture = eeprom_read_word(slot+2);
	plant1.sunLevel = eeprom_read_word(slot+4);
}

void readMoisture() {
	ADMUX = (6 <= 0x07) ? 6: ADMUX;
	static unsigned char i = 0;
	for (i = 0; i < 30; ++i) { asm("nop"); }
	MS_reading = ADC;
}

void readSun() {
	ADMUX = (7 <= 0x07) ? 7: ADMUX;
	static unsigned char i = 0;
	for (i = 0; i < 30; ++i) { asm("nop"); }
	SUN_reading = ADC;
}

double rounder(double d) {
	return floor(d + 0.5);
}

double scaler(double x, double input_min, double input_max, double output_min, double output_max) {
	double slope = 1.0 * (output_max - output_min) / (input_max - input_min);
	double output = output_min + rounder(slope * (x - input_min));
	return (unsigned short)output;
}

void convertToDec(uchar pos, unsigned short x) {
	if (enableScaler == 1) {
		x = scaler(x, 0, 1024, 0, 100);
	}
	unsigned short thousands;
	unsigned short hundreds;
	unsigned short tens;
	unsigned short ones;
	
	thousands = x / 1000;
	LCD_Cursor(pos);
	LCD_WriteData(thousands + '0');
	x = x - (thousands * 1000);

	hundreds = x / 100;
	LCD_Cursor(pos+1);
	LCD_WriteData(hundreds + '0');
	x = x - (hundreds * 100);
	
	tens = x / 10;
	LCD_Cursor(pos+2);
	LCD_WriteData(tens + '0');
	x = x - (tens * 10);
	
	ones = x / 1;
	LCD_Cursor(pos+3);
	LCD_WriteData(ones + '0');
	x = x - (ones * 1);
}

enum {
	WELCOME,
	SETTINGS,
	MAIN1,
	MAIN2,
	MAIN3,
	GLANCE1,
	GLANCE2,
	TAKE_READING,
	CALIB_SUN,
	CALIB_SUN2,
	CALIB_MS,
	CALIB_MS2,
	UPDATE_PROFILE,
	Q1,
	Q2,
	Q3_1,
	Q3_2,
	Q4_1,
	Q4_2,
	CONFIRM,
	WRITE,
	WRITE_MS,
	WRITE_SUN,
	READFROM,
	RETRIEVE,
	SETTING1,
	SETTING2,
	SETTING3,
	SETTING4,
} stater;


int ss(stater) {
	switch(stater) {
		case WELCOME:
			if (DOWN) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case MAIN1:
			if (DOWN && control == 0) {
				LCD_ClearScreen();
				control = 1;
			}
			else if (UP && control == 1) {
				LCD_ClearScreen();
				control = 0;
			}
			else if (DOWN && control == 1) {
				LCD_ClearScreen();
				control = 2;
				stater = MAIN2;
			}
			if (control == 0 && RIGHT) {
				LCD_ClearScreen();
				stater = Q1;
			}
			if (control == 1 && RIGHT) {
				LCD_ClearScreen();
				stater = READFROM;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = SETTINGS;
			}
			break;
			
		case SETTINGS:
			if (RIGHT) {
				LCD_ClearScreen();
				stater = MAIN1;
			}
			else if (UP) {
				LCD_ClearScreen();
				enableScaler = 1;
				break;
			}
			else if (DOWN) {
				LCD_ClearScreen();
				enableScaler = 0;
			}
			break;
			
		case MAIN2:
			if (UP && control == 2) {
				LCD_ClearScreen();
				control = 1;
				stater = MAIN1;
			}
			if (DOWN && control == 2) {
				LCD_ClearScreen();
				control = 3;
			}
			else if (UP && control == 3) {
				LCD_ClearScreen();
				control = 2;
			}
			else if (DOWN && control == 3) {
				LCD_ClearScreen();
				control = 4;
				stater = MAIN3;
			}
			if (control == 2 && RIGHT) {
				LCD_ClearScreen();
				stater = TAKE_READING;
			}
			if (control == 3 && RIGHT) {
				LCD_ClearScreen();
				stater = GLANCE1;
			}
			break;
			
		case MAIN3:
			if (UP && control == 4) {
				LCD_ClearScreen();
				control = 3;
				stater = MAIN2;
			}
			if (UP && control == 5) {
				LCD_ClearScreen();
				control = 4;
			}
			if (control == 4 && RIGHT) {
				LCD_ClearScreen();
				stater = CALIB_MS;
			}
			if (control == 5 && RIGHT) {
				LCD_ClearScreen();
				stater = CALIB_SUN;
			}
			if (DOWN && control == 4) {
				LCD_ClearScreen();
				control = 5;
			}
			break;
			
		case GLANCE1:
			if (UP || DOWN) {
				LCD_ClearScreen();
				stater = GLANCE2;
			}
			else if (LEFT) {
				LCD_ClearScreen();
				stater = MAIN2;
			}
			break;
			
		case GLANCE2:
			if (UP || DOWN) {
				LCD_ClearScreen();
				stater = GLANCE1;
			}
			else if (LEFT) {
				LCD_ClearScreen();
				stater = MAIN2;
			}
			break;
		
		case TAKE_READING:
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = MAIN2;
			}
			break;
			
		case CALIB_SUN:
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = MAIN3;
			}
			if (RIGHT) {
				LCD_ClearScreen();
				stater = CALIB_SUN2;
			}
			break;
			
		case CALIB_SUN2:
			if (RIGHT) {
				LCD_ClearScreen();
				control = 0;
				stater = WRITE_SUN;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = CALIB_SUN;
			}
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case CALIB_MS:
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			if (LEFT) {
				LCD_ClearScreen();
				control = 4;
				stater = MAIN3;
			}
			if (RIGHT) {
				LCD_ClearScreen();
				stater = CALIB_MS2;
			}
			break;
			
		case CALIB_MS2:
			if (RIGHT) {
				LCD_ClearScreen();
				stater = WRITE_MS;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = CALIB_MS;
			}
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case WRITE_MS:
			control = 0;
			stater = MAIN1;
			break;
			
		case WRITE_SUN:
			control = 0;
			stater = MAIN1;
			break;
			
		case UPDATE_PROFILE:
			control = 0;
			stater = MAIN1;
			break;
		
		case READFROM:
			if (RIGHT && gotit == 1) {
				gotit = 0;
				LCD_ClearScreen();
				stater = RETRIEVE;
			}
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			if (LEFT) {
				LCD_ClearScreen();
				control = 1;
				stater = MAIN1;
			}
			break;
			
		case RETRIEVE:
			stater = SETTING1;
			break;
		
		case SETTING1:
			if (UP) {
				LCD_ClearScreen();
				stater = SETTING4;
			}
			if (DOWN) {
				LCD_ClearScreen();
				stater = SETTING2;
			}
			if (LEFT) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case SETTING2:
			if (UP) {
				LCD_ClearScreen();
				stater = SETTING1;
			}
			if (DOWN) {
				LCD_ClearScreen();
				stater = SETTING3;
			}
			if (LEFT) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;

		case SETTING3:
			if (UP) {
				LCD_ClearScreen();
				stater = SETTING2;
			}
			if (DOWN) {
				LCD_ClearScreen();
				stater = SETTING4;
			}
			if (LEFT) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case SETTING4:
			if (UP) {
				LCD_ClearScreen();
				stater = SETTING3;
			}
			if (DOWN) {
				LCD_ClearScreen();
				stater = SETTING1;
			}
			if (LEFT) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case Q1:
			if (RIGHT && gotit == 1) {
				gotit = 0;
				LCD_ClearScreen();
				answer = " ";
				stater = Q2;
			}
			if (LEFT || UP) {
				LCD_ClearScreen();
				answer = " ";
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case Q2:
			if (RIGHT && gotit == 1) {
				gotit = 0;
				LCD_ClearScreen();
				stater = Q3_1;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = Q1;
			}
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
		
		case Q3_1:
			if (RIGHT) {
				LCD_ClearScreen();
				stater = Q3_2;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = Q2;
			}
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;

		case Q3_2:
			if (RIGHT) {
				LCD_ClearScreen();
				stater = Q4_1;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = Q3_1;
			}
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case Q4_1:
			if (RIGHT) {
				LCD_ClearScreen();
				stater = Q4_2;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = Q3_1;
			}
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case Q4_2:
			if (RIGHT) {
				LCD_ClearScreen();
				stater = CONFIRM;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = Q4_1;
			}
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case CONFIRM:
			if (RIGHT && memSlot != 0) {
				LCD_ClearScreen();
				stater = WRITE;
			}
			if (LEFT) {
				LCD_ClearScreen();
				stater = Q4_2;
			}
			if (UP) {
				LCD_ClearScreen();
				control = 0;
				stater = MAIN1;
			}
			break;
			
		case WRITE:
			control = 0;
			stater = MAIN1;
			break;
			
		default:
			control = 0;
			stater = WELCOME;
			break;
	}
	
	switch(stater) {
		case WELCOME:
			LCD_DisplayString(1, "   Welcome To   <Leaf of Faith> ");
			break;
			
		case MAIN1:
			if (control == 0) {
				LCD_DisplayString(1, "> Set  Profile    Load Profile");
			}
			else if (control == 1) {
				LCD_DisplayString(1, "  Set  Profile  > Load Profile");
			}
			break;
			
		case SETTINGS:
			LCD_DisplayString(1, "Enable Scaler?");
			if (enableScaler == 1) {
				LCD_DisplayString(21, "< Yay >");
			}
			else if (enableScaler == 0) {
				LCD_DisplayString(21, "< Nay >");
			}
			break;
						
		case MAIN2:
			if (control == 2) {
				LCD_DisplayString(1, "> Current Data    At A Glance");
			}
			else if (control == 3) {
				LCD_DisplayString(1, "  Current Data  > At A Glance");
			}
			break;

		case MAIN3:
			if (control == 4) {
				LCD_DisplayString(1, "> Calibrate MS    Calibrate Sun");
			}
			else if (control == 5) {
				LCD_DisplayString(1, "  Calibrate MS  > Calibrate Sun");
			}
			break;

		case GLANCE1:
			LCD_DisplayString(1, "Daytime h2O:");
			LCD_DisplayString(17, "Frequency:");
			(plant1.dayTimeWaterOK==1) ? LCD_DisplayString(14, "Yes") : LCD_DisplayString(14, "No");
			convertToDec(28, plant1.waterFrequency);
			break;

		case GLANCE2:
			LCD_DisplayString(1, "MS:");
			LCD_DisplayString(17, "SL:");
			convertToDec(4, plant1.moisture);
			convertToDec(20, plant1.sunLevel);
			break;
			
		case TAKE_READING:
			LCD_DisplayString(1, "Moisture:");
			LCD_DisplayString(17, "Sunlight:");
			convertToDec(11, MS_reading);
			convertToDec(27, SUN_reading);
			break;
			
		case CALIB_SUN:
			LCD_DisplayString(1, "1.Set Photo       Sensor    -->");
			break;
		
		case CALIB_SUN2:
			LCD_DisplayString(1, "2.Reading: ");
			plant1.sunLevel = SUN_reading;
			convertToDec(12, SUN_reading);
			LCD_DisplayString(25, "SAVE -->");
			break;
			
		case CALIB_MS:
			LCD_DisplayString(1, "1.Place Moisture  Sensor    -->");
			break;
			
		case CALIB_MS2:
			LCD_DisplayString(1, "2.Reading: ");
			plant1.moisture = MS_reading;
			convertToDec(12, MS_reading);
			LCD_DisplayString(25, "SAVE -->");
			break;
			
		case READFROM:
			LCD_DisplayString(1, "Source Mem Slot 1,2,3,4?:");
			if (input1234) {
				gotit = 1;
				LCD_Cursor(27);
				memSlot = GetKeypadKey() - '0';
				LCD_WriteData(memSlot + '0');
			}
			break;
			
		case RETRIEVE:
			retrievePlantProfile(memSlot);
			if (memSlot == 1) {
				num = ONE;
			}
			else if (memSlot == 2) {
				num = TWO;
			}
			else if (memSlot == 3) {
				num = THREE;
			}
			else if (memSlot == 4) {
				num = FOUR;
			}
			transmit_data(num);
			break;
			
		case SETTING1:
			LCD_DisplayString(1, "Water During Day");
			if (plant1.dayTimeWaterOK == 1) {
				LCD_DisplayString(17, "Yes");
			}
			else if (plant1.dayTimeWaterOK == 0) {
				LCD_DisplayString(17, "No");
			}
			break;
			
		case SETTING2:
			LCD_DisplayString(1, "Water every");
			LCD_Cursor(13);
			LCD_WriteData(plant1.waterFrequency + '0');
			LCD_DisplayString(17, "days");
			break;
			
		case SETTING3:
			LCD_DisplayString(1, "MS Threshold");
			convertToDec(17, plant1.moisture);
			break;
			
		case SETTING4:
			LCD_DisplayString(1, "Sun Threshold");
			convertToDec(17, plant1.sunLevel);
			break;
			
		case Q1:
			LCD_DisplayString(1, profileQs[0]);
			LCD_DisplayString(22, answer);
			if (GetKeypadKey() == 'A') {
				answer = "Yes";
				plant1.dayTimeWaterOK = 1;
				gotit = 1;
			}
			else if (GetKeypadKey() == 'B') {
				answer = "No ";
				plant1.dayTimeWaterOK = 0;
				gotit = 1;
			}
			else if (GetKeypadKey() == '*') {
				answer = " ";
			}
			break;
			
		case Q2:
			LCD_DisplayString(1, profileQs[1]);
			if (VALID) {
				/* for Demo: special key to set frequency to 1 min, can show reseting/watering */
				if (GetKeypadKey() == '*') {
					plant1.waterFrequency = 99;
					LCD_Cursor(28);
					LCD_WriteData(9 + '0');
					gotit = 1;
				}
				else {
					LCD_Cursor(28);
					LCD_WriteData(GetKeypadKey());
					plant1.waterFrequency = GetKeypadKey() - '0';
					gotit = 1;
				}
			}
			break;
			
		case Q3_1:
			LCD_DisplayString(1, "1.Place Moisture  Sensor    -->");
			break;
		
		case Q3_2:
			LCD_DisplayString(1, "2.Reading: ");
			plant1.moisture = MS_reading;
			convertToDec(12, MS_reading);
			LCD_DisplayString(25, "SAVE -->");
			break;
			
		case Q4_1:
			LCD_DisplayString(1, "3.Set Photo       Sensor    -->");
			break;
		
		case Q4_2:
			LCD_DisplayString(1, "4.Reading: ");
			plant1.sunLevel = SUN_reading;
			convertToDec(12, SUN_reading);
			LCD_DisplayString(25, "SAVE -->");
			break;
			
		case CONFIRM:
			LCD_DisplayString(1, "Select Mem Slot 1,2,3,4?:");
			if (input1234) {
				LCD_Cursor(27);
				memSlot = GetKeypadKey() - '0';
				LCD_WriteData(memSlot + '0');
			}
			break;
		
		case WRITE:
			for (signed char i = 0; i < 50; ++i) {
				LCD_DisplayString(1, "Saving Profile..");
			}
			savePlantProfile(plant1, memSlot);
			if (memSlot == 1) {
				num = ONE;
			}
			else if (memSlot == 2) {
				num = TWO;
			}
			else if (memSlot == 3) {
				num = THREE;
			}
			else if (memSlot == 4) {
				num = FOUR;
			}
			transmit_data(num);
			LCD_ClearScreen();
			break;
			
		case WRITE_MS:
			for (signed char i = 0; i < 50; ++i) {
				LCD_DisplayString(1, "Saving Profile..");
			}
			saveMS(plant1.moisture, memSlot);
			LCD_ClearScreen();
			break;
			
		case WRITE_SUN:
			for (signed char i = 0; i < 50; ++i) {
				LCD_DisplayString(1, "Saving Profile..");
			}
			saveMS(plant1.sunLevel, memSlot);
			LCD_ClearScreen();
			break;
			
	}
	return stater;
}

enum {
	READ,
	UPDATE
} ADC_state;

int reader() {
	switch(ADC_state) {
		case READ:
			//ADC_state = UPDATE;
			break;
		
		default:
			ADC_state = READ;
			break;
	}
	
	switch(ADC_state) {
		case READ:
			readJoystick();
			readMoisture();
			readSun();
			//if (cc % 2 == 0) {
				//PORTD = SetBit(PORTD, 0, 1);
			//}
			//else {
				//PORTD = SetBit(PORTD, 0, 0);
			//}
			//if (cc == 30) {
				//PORTD = SetBit(PORTD, 1, 1);	
			//}
			break;
			
		case UPDATE:
			break;
	}

	return ADC_state;
}

enum {
	TICK,
	WATER_PLANT,
	RESET
} elon_musk;

uchar frequency;
uchar t;
uchar sec;
uchar half;
uchar min;
uchar day;

/* 1 day --> 86,400
   7 days -> 604,800

unsigned long 4 bytes
4,294,967,295 seconds --> 49,710 days
*/

int hourGlass() {
	switch(elon_musk) {
		case TICK:
			if (frequency != plant1.waterFrequency) {
				PORTD = SetBit(PORTD, 1, 0);
				// meaning a plant profile has changed/switched
				elon_musk = RESET;
			}
			//if (frequency == 99 && min == 1) {
			if (frequency == 99 && min == 1 && (plant1.dayTimeWaterOK == 1 || (plant1.dayTimeWaterOK == 0 && SUN_reading < plant1.sunLevel)) && MS_reading < plant1.moisture) {
				elon_musk = WATER_PLANT;
			}
			if (OK_TO_WATER) {
				elon_musk = WATER_PLANT;
			}
			break;

		case WATER_PLANT:
			elon_musk = RESET;
			break;

		case RESET:
			elon_musk = TICK;
			break;

		default:
			t = sec = half = min = day = 0;
			frequency = plant1.waterFrequency;
			elon_musk = TICK;
			break;
	}

	switch(elon_musk) {
		case TICK:
			if (t > 9) { /* a second has gone by*/
				t = 0;
				++sec;
			}
			else if (sec > 50) { /* 60 seconds have gone by */
				sec = 0;
				++min;
			}
			else if (min > 1439) {
				min = 0;
				++day;
			}
			++t;
			break;

		case WATER_PLANT:
			PORTD = SetBit(PORTD, 1, 1);
			break;

		case RESET:
			PORTD = SetBit(PORTD, 0, 1);
			frequency = plant1.waterFrequency;
			t = sec = min = day = 0;
			break;
	}

}


int main(void)
{
	DDRA = 0x0F; PORTA = 0xFF;
	DDRB = 0xF0; PORTB = 0xFF; 	// keypad input
	DDRC = 0xFF; PORTC = 0x00;
	DDRD = 0xFF; PORTD = 0x00;
	
	ADC_init();
	LCD_init();
	
	tasksNum = 3;
	task tsks[tasksNum];
	tasks = tsks;
	
	uchar i = 0;

	tasks[i].state = -1;
	tasks[i].period = 100;
	tasks[i].elapsedTime = tasks[i].period;
	tasks[i].TickFct = &hourGlass;

	++i;

	tasks[i].state = -1;
	tasks[i].period = 100;
	tasks[i].elapsedTime = tasks[i].period;
	tasks[i].TickFct = &reader;
	
	++i;

	tasks[i].state = -1;
	tasks[i].period = 300;
	tasks[i].elapsedTime = tasks[i].period;
	tasks[i].TickFct = &ss;
	
	TimerSet(100); // value set should be GCD of all tasks
	TimerOn();
	
	while(1) {} // task scheduler will be called by the hardware interrupt
}