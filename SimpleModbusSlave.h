/*
 * Copyright © 2011-2012 Stéphane Raimbault <stephane.raimbault@gmail.com>
 * Copyright © 2015 Alexandr Kolodkin <alexandr.kolodkin@gmail.com>
 *
 * License ISC, see LICENSE for more details.
 *
 * This library implements the Modbus protocol.
 * http://libmodbus.org/
 *
 */

#ifndef SimpleModbusSlave_h
#define SimpleModbusSlave_h

#include <inttypes.h>
#if defined(ARDUINO) && ARDUINO >= 100
  #include "Arduino.h"
#else
  #include "WProgram.h"
  #include <pins_arduino.h>
#endif

#define MODBUS_BROADCAST_ADDRESS 0

/* Protocol exceptions */
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION     1
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 2
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE   3

class SimpleModbusSlave {
public:
    SimpleModbusSlave(uint8_t slave);
    void setup(long baud);
    int loop(uint16_t *tab_reg, uint16_t nb_reg);
private:
    int _slave;
};

#endif /* SimpleModbusSlave_h */
