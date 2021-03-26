#include "Stk500Protocol.h"

#include <Arduino.h>

#include "logger.h"

Stk500Protocol::Stk500Protocol(Stream* serial, int res_pin)
  : reset_pin_(res_pin)
  , serial_(serial)
{
    pinMode(reset_pin_, OUTPUT);
    digitalWrite(reset_pin_, HIGH);
}

bool
Stk500Protocol::setup_device()
{
    reset_mcu();

    int s = get_sync();
    DEBUG_PRINTF(PSTR("avrflash: sync=d%d/0x%x\n"), s, s);
    if (!s)
        return false;

    s = set_prog_params();
    DEBUG_PRINTF(PSTR("avrflash: setparam=d%d/0x%x\n"), s, s);
    if (!s)
        return false;

    s = set_ext_prog_params();
    DEBUG_PRINTF(PSTR("avrflash: setext=d%d/0x%x\n"), s, s);
    if (!s)
        return false;

    s = enter_prog_mode();
    DEBUG_PRINTF(PSTR("avrflash: progmode=d%d/0x%x\n"), s, s);
    if (!s)
        return false;

    return true;
}

bool
Stk500Protocol::flash_page(uint8_t* load_addr, uint8_t* data)
{
    uint8_t header[] = {0x64, 0x00, 0x80, 0x46};
    // WTF?!?!? addr_hi should be load_addr[0]. But the same in another project.
    // Looks like this is mistype, which migrated from one project to another. Look at http://www.amelek.gda.pl/avr/uisp/doc2525.pdf
    // page 8 - firtst should go LOW, then HIGH address. This is how it is done here, despite of mixed up names.
    // TODO: rename hi and lo properly
    int     s        = load_address(load_addr[0], load_addr[1]);
    DEBUG_PRINTF(PSTR("avrflash: loadAddr(%d,%d)=%d\n"), load_addr[1], load_addr[0], s);

    serial_->write(header, 4);
    for (int i = 0; i < 128; i++)
        serial_->write(data[i]);
    serial_->write(0x20);

    s = wait_for_serial_data(2, 1000);
    if (s == 0) {
        DEBUG_PRINTF(PSTR("avrflash: flashpage: ack: error\n"));
        return false;
    }
    s     = serial_->read();
    int t = serial_->read();
    DEBUG_PRINTF(PSTR("avrflash: flashpage: ack: d%d/d%d - 0x%x/0x%x\n"), s, t, s, t);

    return true;
}

void
Stk500Protocol::reset_mcu()
{
    digitalWrite(reset_pin_, LOW);
    delay(1);
    digitalWrite(reset_pin_, HIGH);
    delay(200);
}

int
Stk500Protocol::get_sync()
{
    return exec_cmd(0x30);
}

int
Stk500Protocol::enter_prog_mode()
{
    return exec_cmd(0x50);
}

int
Stk500Protocol::exit_prog_mode()
{
    return exec_cmd(0x51);
}

int
Stk500Protocol::set_ext_prog_params()
{
    uint8_t params[] = {0x05, 0x04, 0xd7, 0xc2, 0x00};
    return exec_param(0x45, params, sizeof(params));
}

int
Stk500Protocol::set_prog_params()
{
    uint8_t params[] = {0x86, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x03, 0xff, 0xff,
                        0xff, 0xff, 0x00, 0x80, 0x04, 0x00, 0x00, 0x00, 0x80, 0x00};
    return exec_param(0x42, params, sizeof(params));
}

int
Stk500Protocol::load_address(uint8_t adr_hi, uint8_t adr_lo)
{
    uint8_t params[] = {adr_lo, adr_hi};
    return exec_param(0x55, params, sizeof(params));
}

uint8_t
Stk500Protocol::exec_cmd(uint8_t cmd)
{
    uint8_t bytes[] = {cmd, 0x20};
    return send_bytes(bytes, 2);
}

uint8_t
Stk500Protocol::exec_param(uint8_t cmd, uint8_t* params, int count)
{
    uint8_t bytes[count + 2];
    bytes[0] = cmd;

    int i = 0;
    while (i < count) {
        bytes[i + 1] = params[i];
        i++;
    }

    bytes[i + 1] = 0x20;

    return send_bytes(bytes, i + 2);
}

uint8_t
Stk500Protocol::send_bytes(uint8_t* bytes, int count)
{
    serial_->write(bytes, count);
    wait_for_serial_data(2, 1000);

    uint8_t sync = serial_->read();
    uint8_t ok   = serial_->read();
    if (sync == 0x14 && ok == 0x10) {
        return 1;
    }

    return 0;
}

int
Stk500Protocol::wait_for_serial_data(int dataCount, int timeout)
{
    int timer = 0;

    while (timer < timeout) {
        if (serial_->available() >= dataCount) {
            return 1;
        }
        delay(1);
        timer++;
    }

    return 0;
}
