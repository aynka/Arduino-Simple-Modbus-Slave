#ifndef _ESPHW_
#define _ESPHW_
//IOT Aggrigator Hardware Rev1 2020/07
//if auto boot fails, add a 10k pullup to EN pin
#define LED2  2 //BLUE

#define CFG_CLR 35 //Button

#define ISO_OUTPUT_PIN1 26
#define ISO_INPUT_PIN1  25

#define INPUT_HIGH  0
#define INPUT_LOW   (!INPUT_HIGH)

#define OUTPUT_ON  1
#define OUTPUT_OFF  (!OUTPUT_ON)



//ESP <-> RS485 (ADM2483)
#define RX2         16
#define TX2         17
#define POWER_VALID 32
#define MODBUS_DE   33
#endif 
