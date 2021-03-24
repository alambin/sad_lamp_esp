#include "IntelHexParser.h"

IntelHexParser::IntelHexParser()
{
    load_address_[0] = 0x00;
    load_address_[1] = 0x00;
}

void
IntelHexParser::parse_line(byte* hex_line)
{
    record_type_ = get_record_type(hex_line);

    if (record_type_ == 0) {
        address_ = get_address(hex_line);
        length_  = get_length(hex_line);


        get_data(hex_line, length_);

        if (mem_idx_ == 128) {
            if (!page_ready_) {
                load_address_[1] += 0x40;
                if (load_address_[1] == 0) {
                    load_address_[0] += 0x1;
                }
            }
            page_ready_  = false;
            page_ready_ = true;
            mem_idx_    = 0;
        }

        next_address_ = address_ + length_;
    }

    if (record_type_ == 1) {
        end_of_file();
        page_ready_ = true;
    }
}

bool
IntelHexParser::is_page_ready()
{
    return page_ready_;
}


byte*
IntelHexParser::get_memory_page()
{
    page_ready_ = false;
    return memory_page_;
}

byte*
IntelHexParser::get_load_address()
{
    return load_address_;
}

void
IntelHexParser::get_load_address(byte* hex_line)
{
    char buff[3];
    buff[2] = '\0';

    buff[0]         = hex_line[3];
    buff[1]         = hex_line[4];
    load_address_[0] = strtol(buff, 0, 16);
    buff[0]         = hex_line[5];
    buff[1]         = hex_line[6];
    load_address_[1] = strtol(buff, 0, 16);
}

byte*
IntelHexParser::get_data(byte* hex_line, int len)
{
    int  start = 9;
    int  end   = (len * 2) + start;
    char buff[3];
    buff[2] = '\0';

    for (int x = start; x < end; x = x + 2) {
        buff[0]              = hex_line[x];
        buff[1]              = hex_line[x + 1];
        memory_page_[mem_idx_] = strtol(buff, 0, 16);
        mem_idx_++;
    }
}

void
IntelHexParser::end_of_file()
{
    load_address_[1] += 0x40;
    if (load_address_[1] == 0) {
        load_address_[0] += 0x1;
    }

    while (mem_idx_ < 128) {
        memory_page_[mem_idx_] = 0xFF;
        mem_idx_++;
    }
}

int
IntelHexParser::get_address(byte* hex_line)
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
IntelHexParser::get_length(byte* hex_line)
{
    char buff[3];
    buff[0] = hex_line[1];
    buff[1] = hex_line[2];
    buff[2] = '\0';

    return strtol(buff, 0, 16);
}

int
IntelHexParser::get_record_type(byte* hex_line)
{
    char buff[3];
    buff[0] = hex_line[7];
    buff[1] = hex_line[8];
    buff[2] = '\0';

    return strtol(buff, 0, 16);
}
