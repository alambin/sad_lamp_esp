#ifndef INTELHEXPARSER_H_
#define INTELHEXPARSER_H_

#include <Arduino.h>

// This class is borrowed from esp_avr_programmer project
class IntelHexParser
{
public:
    IntelHexParser();
    void     parse_line(uint8_t* hex_line);
    uint8_t* get_memory_page();
    uint8_t* get_load_address();
    bool     is_page_ready();

private:
    int      get_address(uint8_t* hex_line);
    int      get_length(uint8_t* hex_line);
    int      get_record_type(uint8_t* hex_line);
    uint8_t* get_data(uint8_t* hex_line, int len);
    void     get_load_address(uint8_t* hex_line);
    void     end_of_file();

    int     address_{0};
    int     length_{0};
    int     next_address_{0};
    int     mem_idx_{0};
    int     record_type_{0};
    uint8_t memory_page_[128];
    uint8_t load_address_[2];
    bool    page_ready_{false};
    bool    first_run_{true};
};

#endif  // INTELHEXPARSER_H_
