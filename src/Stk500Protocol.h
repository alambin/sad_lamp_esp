#ifndef STK500PROTOCOL_H_
#define STK500PROTOCOL_H_

#include <Stream.h>

// This class implements STK500 protocol to upload firmware to Arduino via hardware serial port using standard Arduino
// bootloader
// This class is borrowed from esp_avr_programmer project
class Stk500Protocol
{
public:
    Stk500Protocol(Stream* serial, int res_pin);
    bool setup_device();
    bool flash_page(uint8_t* load_addr, uint8_t* data);
    int  exit_prog_mode();

private:
    void reset_mcu();
    int  get_sync();
    int  enter_prog_mode();
    int  load_address(uint8_t adr_hi, uint8_t adr_lo);
    int  set_prog_params();
    int  set_ext_prog_params();

    uint8_t exec_cmd(uint8_t cmd);
    uint8_t exec_param(uint8_t cmd, uint8_t* params, int count);
    uint8_t send_bytes(uint8_t* bytes, int count);
    int     wait_for_serial_data(int data_count, int timeout);
    // int     get_flash_page_count(uint8_t flashData[][131]);

    int     reset_pin_;
    Stream* serial_;
};

#endif  // STK500PROTOCOL_H_
