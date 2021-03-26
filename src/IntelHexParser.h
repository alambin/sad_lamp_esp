#ifndef INTELHEXPARSER_H_
#define INTELHEXPARSER_H_

#include <Arduino.h>

// This class is borrowed from esp_avr_programmer project
class IntelHexParser
{
public:
    IntelHexParser();
    void     parse_line(unsigned char* hex_line);
    void     get_memory_page(byte* page);
    uint8_t* get_load_address();
    bool     is_page_ready();  // Page is "ready" in case it is full or in case it is last page of file.

private:
    int  get_address(unsigned char* hex_line);
    int  get_length(unsigned char* hex_line);
    int  get_record_type(unsigned char* hex_line);
    void read_line_data(unsigned char* hex_line, byte* dst, int line_lenth);
    void end_of_file();

    int                       mem_idx_{0};
    static constexpr uint16_t page_size_{128};
    uint8_t                   memory_page_[page_size_];
    uint8_t                   load_address_[2];
    bool                      page_ready_{false};
    bool                      first_run_{true};

    // Store data from previous line read, if that line didn't fit page.
    byte remaining_data_[64];
    byte remaining_data_size_{0};
};

#endif  // INTELHEXPARSER_H_
