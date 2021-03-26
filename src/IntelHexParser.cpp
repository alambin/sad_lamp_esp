#include "IntelHexParser.h"

IntelHexParser::IntelHexParser()
{
    load_address_[0] = 0x00;
    load_address_[1] = 0x00;
}

void
IntelHexParser::parse_line(unsigned char* hex_line)
{
    auto record_type = get_record_type(hex_line);

    if (record_type == 0) {
        // Looks like in all similar HEX-parsers it is assumed that lines of HEX file represent one single piece of
        // memory without gaps. Because of that writing in Flash uses blocks of 128 bytes, not writing libe-by-line.
        // In esp_avr_programmer project they did the same. So, reading of address from HEX line doesn't make sense.
        // address_ = get_address(hex_line);

        auto line_length = get_length(hex_line);
        byte line_data[line_length];
        read_line_data(hex_line, line_data, line_length);

        // Some lines can be less than 16 bytes. It will lead to exceeding page size when reading last line on page.
        auto read_bytes    = mem_idx_ + line_length;
        auto bytes_to_copy = (read_bytes <= page_size_) ? line_length : page_size_ - mem_idx_;
        memcpy(memory_page_ + mem_idx_, line_data, bytes_to_copy);
        mem_idx_ += bytes_to_copy;

        // If part of line didn't fit page, save it and put on page when it has enough space.
        remaining_data_size_ = line_length - bytes_to_copy;
        if (remaining_data_size_) {
            memcpy(remaining_data_, line_data + bytes_to_copy, remaining_data_size_);
        }

        if (mem_idx_ == page_size_) {
            if (!first_run_) {
                // Increase load address
                load_address_[1] += 0x40;
                if (load_address_[1] == 0) {
                    load_address_[0] += 0x1;
                }
            }
            first_run_  = false;
            page_ready_ = true;
            mem_idx_    = 0;
        }
    }

    if (record_type == 1) {
        end_of_file();
        page_ready_ = true;
    }
}

bool
IntelHexParser::is_page_ready()
{
    return page_ready_;
}

void
IntelHexParser::get_memory_page(byte* page)
{
    page_ready_ = false;
    memcpy(page, memory_page_, page_size_);

    // Handle remain data from previous line read, if any
    if (remaining_data_size_) {
        memcpy(memory_page_ + mem_idx_, remaining_data_, remaining_data_size_);
        mem_idx_ += remaining_data_size_;
        remaining_data_size_ = 0;
    }
}

byte*
IntelHexParser::get_load_address()
{
    return load_address_;
}

void
IntelHexParser::read_line_data(unsigned char* hex_line, byte* dst, int line_lenth)
{
    int  start = 9;
    int  end   = (line_lenth * 2) + start;
    char buff[3];
    buff[2] = '\0';

    for (int x = start, i = 0; x < end; x = x + 2, ++i) {
        buff[0] = hex_line[x];
        buff[1] = hex_line[x + 1];
        dst[i]  = strtol(buff, 0, 16);
    }
}

void
IntelHexParser::end_of_file()
{
    // Increase load address
    load_address_[1] += 0x40;
    if (load_address_[1] == 0) {
        load_address_[0] += 0x1;
    }

    while (mem_idx_ < page_size_) {
        memory_page_[mem_idx_] = 0xFF;
        mem_idx_++;
    }
}

int
IntelHexParser::get_address(unsigned char* hex_line)
{
    char buff[5];
    buff[0] = hex_line[3];
    buff[1] = hex_line[4];
    buff[2] = hex_line[5];
    buff[3] = hex_line[6];
    buff[4] = '\0';

    return strtol(buff, 0, 16);
}

int
IntelHexParser::get_length(unsigned char* hex_line)
{
    char buff[3];
    buff[0] = hex_line[1];
    buff[1] = hex_line[2];
    buff[2] = '\0';

    return strtol(buff, 0, 16);
}

int
IntelHexParser::get_record_type(unsigned char* hex_line)
{
    char buff[3];
    buff[0] = hex_line[7];
    buff[1] = hex_line[8];
    buff[2] = '\0';

    return strtol(buff, 0, 16);
}
