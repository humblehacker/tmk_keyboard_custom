/*
Copyright 2014 Kai Ryu <kai1103@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define KIMERA_C

#include <stdbool.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include "i2cmaster.h"
#include "kimera.h"
#include "debug.h"

uint8_t row_mapping[PX_COUNT] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED,
    UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED,
    UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED
};
uint8_t col_mapping[PX_COUNT] = {
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED, UNCONFIGURED
};
uint8_t row_count = 8;
uint8_t col_count = 24;
uint8_t data[EXP_COUNT][EXP_PORT_COUNT];

void kimera_init(void)
{
    /* read config */
    write_matrix_mapping(); /* debug */
    if (read_matrix_mapping()) {
        write_matrix_mapping();
    }

    /* init i2c */
    i2c_init();
    
    /* init i/o expander */
    expander_init();
}

uint8_t read_matrix_mapping(void)
{
    uint8_t error = 0;

    /* read number of rows and cols */
    row_count = eeprom_read_byte(EECONFIG_ROW_COUNT);
    col_count = eeprom_read_byte(EECONFIG_COL_COUNT);
    if (row_count == 0) error++;
    if (row_count == UNCONFIGURED) error++;
    if (col_count == 0) error++;
    if (col_count == UNCONFIGURED) error++;
    if (row_count + col_count > PX_COUNT) error++;

    /* read row mapping */
    uint8_t *mapping = EECONFIG_ROW_COL_MAPPING;
    for (uint8_t i = 0; i < PX_COUNT; i++) {
        if (i < row_count) {
            row_mapping[i] = eeprom_read_byte(mapping++);
            if (row_mapping[i] >= PX_COUNT) error++;
        }
        else {
            row_mapping[i] = UNCONFIGURED;
        }
    }
    /* read col mapping*/
    for (uint8_t i = 0; i < PX_COUNT; i++) {
        if (i < col_count) {
            col_mapping[i] = eeprom_read_byte(mapping++);
            if (col_mapping[i] >= PX_COUNT) error++;
        }
        else {
            col_mapping[i] = UNCONFIGURED;
        }
    }

    return error;
}

void write_matrix_mapping(void)
{
    /* write number of rows and cols */
    eeprom_write_byte(EECONFIG_ROW_COUNT, row_count);
    eeprom_write_byte(EECONFIG_COL_COUNT, col_count);

    /* write row mapping */
    uint8_t *mapping = EECONFIG_ROW_COL_MAPPING;
    for (uint8_t row = 0; row < row_count; row++) {
        eeprom_write_byte(mapping++, row_mapping[row]);
    }
    /* write col mapping */
    for (uint8_t col = 0; col < col_count; col++) {
        eeprom_write_byte(mapping++, col_mapping[col]);
    }
}

matrix_row_t read_cols(void)
{
    init_data(0x00);

    /* read all input registers */
    for (uint8_t exp = 0; exp < EXP_COUNT; exp++) {
        expander_read_input(exp, data[exp]);
    }

    /* make cols */
    matrix_row_t cols = 0;
    for (uint8_t col = 0; col < col_count; col++) {
        uint8_t px = col_mapping[col];
        if (px != UNCONFIGURED) {
            if (data[PX_TO_EXP(px)][PX_TO_PORT(px)] & (1 << PX_TO_PIN(px))) {
                cols |= (1UL << col);
            }
        }
    }
    return cols;
}

void unselect_rows(void)
{
    /* set all output registers to 0xFF */
    init_data(0xFF);
    for (uint8_t exp = 0; exp < EXP_COUNT; exp++) {
        expander_write_output(exp, data[exp]);
    }
}

void select_row(uint8_t row)
{
    /* set selected row to low */
    init_data(0xFF);
    uint8_t px = row_mapping[row];
    if (px != UNCONFIGURED) {
        uint8_t exp = PX_TO_EXP(px);
        data[exp][PX_TO_PORT(px)] &= ~(1 << PX_TO_PIN(px));
        expander_write_output(exp, data[exp]);
    }
}

void expander_init(void)
{
    init_data(0xFF);

    /* write inversion register */
    for (uint8_t exp = 0; exp < EXP_COUNT; exp++) {
        expander_write_inversion(exp, data[exp]);  
    }                                              

    /* set output bit */
    for (uint8_t row = 0; row < row_count; row++) {
        uint8_t px = row_mapping[row];
        if (px != UNCONFIGURED) {
            data[PX_TO_EXP(px)][PX_TO_PORT(px)] &= ~(1 << PX_TO_PIN(px));
        }
    }

    /* write config registers */
    for (uint8_t exp = 0; exp < EXP_COUNT; exp++) {
        expander_write_config(exp, data[exp]);
    }
}

uint8_t expander_write(uint8_t exp, uint8_t command, uint8_t *data)
{
    uint8_t addr = EXP_ADDR(exp);
    uint8_t ret;
    ret = i2c_start(addr | I2C_WRITE);
    if (ret) goto stop;
    ret = i2c_write(command);
    if (ret) goto stop;
    ret = i2c_write(*data++);
    if (ret) goto stop;
    ret = i2c_write(*data);
stop:
    i2c_stop();
    return ret;
}

uint8_t expander_read(uint8_t exp, uint8_t command, uint8_t *data)
{
    uint8_t addr = EXP_ADDR(exp);
    uint8_t ret;
    ret = i2c_start(addr | I2C_WRITE);
    if (ret) goto stop;
    ret = i2c_write(command);
    if (ret) goto stop;
    ret = i2c_start(addr | I2C_READ);
    if (ret) goto stop;
    *data++ = i2c_readAck();
    *data = i2c_readNak();
stop:
    i2c_stop();
    return ret;
}

inline
uint8_t expander_write_output(uint8_t exp, uint8_t *data)
{
    return expander_write(exp, EXP_COMM_OUTPUT_0, data);
}

inline
uint8_t expander_write_inversion(uint8_t exp, uint8_t *data)
{
    return expander_write(exp, EXP_COMM_INVERSION_0, data);
}

inline
uint8_t expander_write_config(uint8_t exp, uint8_t *data)
{
    return expander_write(exp, EXP_COMM_CONFIG_0, data);
}
inline
uint8_t expander_read_input(uint8_t exp, uint8_t *data)
{
    return expander_read(exp, EXP_COMM_INPUT_0, data);
}

void init_data(uint8_t value)
{
    for (uint8_t exp = 0; exp < EXP_COUNT; exp++) {
        for (uint8_t port = 0; port < EXP_PORT_COUNT; port++) {
            data[exp][port] = value;
        }
    }
}