/*
 * Copyright © 2011-2019 Stéphane Raimbault <stephane.raimbault@gmail.com>
 * Copyright © 2015-2019 Alexandr Kolodkin <alexandr.kolodkin@gmail.com>
 *
 * License ISC, see LICENSE for more details.
 *
 * This library implements the Modbus protocol.
 * http://libmodbus.org/
 *
 */

#include <inttypes.h>

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#include <pins_arduino.h>
#endif

#include "SimpleModbusSlave.h"

#define _MODBUS_RTU_SLAVE                0
#define _MODBUS_RTU_FUNCTION             1
#define _MODBUS_RTU_PRESET_REQ_LENGTH    6
#define _MODBUS_RTU_PRESET_RSP_LENGTH    2

#define _MODBUS_RTU_CHECKSUM_LENGTH      2

// As reported in https://github.com/stephane/modbusino/issues/6, the code could segfault for longer ADU
#define _MODBUSINO_RTU_MAX_ADU_LENGTH 256

// Supported function codes
#define _FC_READ_HOLDING_REGISTERS    0x03
#define _FC_WRITE_MULTIPLE_REGISTERS  0x10

enum {
	_STEP_FUNCTION = 0x01,
	_STEP_META,
	_STEP_DATA
};

SimpleModbusSlave::SimpleModbusSlave(uint8_t slave) {
	if (slave >= 0 & slave <= 247) {
		_slave = slave;
	}
}

void SimpleModbusSlave::setup(long baud, int RS485DE_Pin) {
	Serial2.begin(baud);
	_pin_DE = RS485DE_Pin;
}

// Check CRC of msg
static int check_integrity(uint8_t *msg, uint8_t msg_length) {
	if ((msg_length >= 2) && crc16(msg, msg_length) == 0) {
		return msg_length;
	} else {
		return -1;
	}
}

static int build_response_basis(uint8_t slave, uint8_t function, uint8_t* rsp) {
	rsp[0] = slave;
	rsp[1] = function;
	return _MODBUS_RTU_PRESET_RSP_LENGTH;
}

static void send_msg(uint8_t *msg, uint8_t msg_length) {
	add_crc16(msg, msg_length);
	digitalWrite(_pin_DE, 1);
	Serial2.write(msg, msg_length + 2);
	digitalWrite(_pin_DE, 0);
}

static uint8_t response_exception(uint8_t slave, uint8_t function, uint8_t exception_code, uint8_t *rsp) {
	uint8_t rsp_length = build_response_basis(slave, function + 0x80, rsp);

	// Positive exception code
	rsp[rsp_length++] = exception_code;

	return rsp_length;
}

static void flush(void) {
	uint8_t i = 0;

	// Wait a moment to receive the remaining garbage but avoid getting stuck
	// because the line is saturated
	while (Serial2.available() && i++ < 10) {
		Serial2.flush();
		delay(3);
	}
}

static int receive(uint8_t *req, uint8_t _slave) {
	uint8_t i;
	uint8_t length_to_read;
	uint8_t req_index;
	uint8_t step;
	uint8_t function;

	// We need to analyse the message step by step.  At the first step, we want
	// to reach the function code because all packets contain this
	// information.
	step = _STEP_FUNCTION;
	length_to_read = _MODBUS_RTU_FUNCTION + 1;

	req_index = 0;
	while (length_to_read != 0) {

		// The timeout is defined to ~10 ms between each bytes.  Precision is
		// not that important so I rather to avoid millis() to apply the KISS
		// principle (millis overflows after 50 days, etc) */
		if (!Serial2.available()) {
			i = 0;
			while (!Serial2.available()) {
				if (++i == 10) return -1 - MODBUS_INFORMATIVE_RX_TIMEOUT; // Too late, bye
				delay(1);
			}
		}

		req[req_index] = Serial2.read();

		// Moves the pointer to receive other data 
		req_index++;

		// Computes remaining bytes
		length_to_read--;

		if (length_to_read == 0) {

			if (req[_MODBUS_RTU_SLAVE] != _slave && req[_MODBUS_RTU_SLAVE != MODBUS_BROADCAST_ADDRESS]) {
				flush();
				return -1 - MODBUS_INFORMATIVE_NOT_FOR_US;
			}

			switch (step) {
			case _STEP_FUNCTION:
				// Function code position
				function = req[_MODBUS_RTU_FUNCTION];
				if (function == _FC_READ_HOLDING_REGISTERS) {
					length_to_read = 4;
				} else if (function == _FC_WRITE_MULTIPLE_REGISTERS) {
					length_to_read = 5;
				} else {
					// Wait a moment to receive the remaining garbage
					flush();
					if (req[_MODBUS_RTU_SLAVE] == _slave || req[_MODBUS_RTU_SLAVE] == MODBUS_BROADCAST_ADDRESS) {
						// It's for me so send an exception (reuse req)
						uint8_t rsp_length = response_exception(_slave, function, MODBUS_EXCEPTION_ILLEGAL_FUNCTION, req);
						send_msg(req, rsp_length);
						return - 1 - MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
					}

					return -1;
				}
			step = _STEP_META;
			break;

			case _STEP_META:
				length_to_read = _MODBUS_RTU_CHECKSUM_LENGTH;

				if (function == _FC_WRITE_MULTIPLE_REGISTERS) {
					length_to_read += req[_MODBUS_RTU_FUNCTION + 5];
				}

				if ((req_index + length_to_read) > _MODBUSINO_RTU_MAX_ADU_LENGTH) {
					flush();
					if (req[_MODBUS_RTU_SLAVE] == _slave || req[_MODBUS_RTU_SLAVE] == MODBUS_BROADCAST_ADDRESS) {
						// It's for me so send an exception (reuse req)
						uint8_t rsp_length = response_exception(_slave, function, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, req);
						send_msg(req, rsp_length);
						return - 1 - MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
					}
					return -1;
				}
				step = _STEP_DATA;
			}
		}
	}

	return check_integrity(req, req_index);
}

static void reply(uint16_t *tab_reg, uint16_t nb_reg, uint8_t *req, uint8_t req_length, uint8_t _slave) {
	uint8_t  slave    = req[_MODBUS_RTU_SLAVE];
	uint8_t  function = req[_MODBUS_RTU_FUNCTION];
	uint16_t address  = (req[_MODBUS_RTU_FUNCTION + 1] << 8) + req[_MODBUS_RTU_FUNCTION + 2];
	uint16_t nb       = (req[_MODBUS_RTU_FUNCTION + 3] << 8) + req[_MODBUS_RTU_FUNCTION + 4];
	uint8_t  rsp[_MODBUSINO_RTU_MAX_ADU_LENGTH];
	uint8_t  rsp_length = 0;

	if (slave != _slave && slave != MODBUS_BROADCAST_ADDRESS) return;

	if ((address + nb) > nb_reg) {
		rsp_length = response_exception(slave, function, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, rsp);
	} else {
		req_length -= _MODBUS_RTU_CHECKSUM_LENGTH;

		if (function == _FC_READ_HOLDING_REGISTERS) {
			uint16_t i;

			rsp_length = build_response_basis(slave, function, rsp);
			rsp[rsp_length++] = nb << 1;
			for (i = address; i < address + nb; i++) {
			    rsp[rsp_length++] = tab_reg[i] >> 8;
				rsp[rsp_length++] = tab_reg[i] & 0xFF;
			}
		} else {
			uint16_t i, j;

			for (i = address, j = 6; i < address + nb; i++, j += 2) {
				/* 6 and 7 = first value */
				tab_reg[i] = (req[_MODBUS_RTU_FUNCTION + j] << 8) + req[_MODBUS_RTU_FUNCTION + j + 1];
			}

			rsp_length = build_response_basis(slave, function, rsp);
			/* 4 to copy the address (2) and the no. of registers */
			memcpy(rsp + rsp_length, req + rsp_length, 4);
			rsp_length += 4;
		}
	}

	send_msg(rsp, rsp_length);
}

int SimpleModbusSlave::loop(uint16_t* tab_reg, uint16_t nb_reg) {
	int rc = 0;
	uint8_t req[_MODBUSINO_RTU_MAX_ADU_LENGTH];

	if (Serial2.available()) {
		rc = receive(req, _slave);
		if (rc > 0) {
			reply(tab_reg, nb_reg, req, rc, _slave);
		}
	}

	// Returns a positive value if successful,
	//  0 if a slave filtering has occured,
	// -1 if an undefined error has occured,
	// -2 for MODBUS_EXCEPTION_ILLEGAL_FUNCTION
	// -3 for MODBUS_EXCEPTION_ILLEGAL_FUNCTION
	// -4 for MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS
	// -5 for MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE
	// -6 for MODBUS_INFORMATIVE_NOT_FOR_US
	// -7 for MODBUS_INFORMATIVE_RX_TIMEOUT
	return rc;
}
