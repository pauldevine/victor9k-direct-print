#include <stdio.h>
#include <stdlib.h> 
#include <i86.h>
#define PHASE2_DEVICE_SEGMENT 0xE800
#define CONTRAST_BRIGHTNESS_REG_OFFSET 0x0040
#define CRTC_ADDR_REG_OFFSET 0x0  // CRT-chip address register target
#define CRTC_DATA_REG_OFFSET 0x1  // CRT-chip address register value
#define HIRES_REG_OFFSET 0x0
#define FONT_REG_OFFSET 0x1
#define SCREEN_BUFFER_SEGMENT 0xF000
#define SCREEN_BUFFER_BOTTOM 0x0
#define SCREEN_BUFFER_TOP 0xFFF
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 24
#define SCREEN_BUFFER_SIZE 2000  // Total characters in the screen buffer
#define SCREEN_MASK 0x999;   //Screen wrap around mask
#define FONT_GLYFF_SEGMENT 0xC00
#define FONT_GLYFF_OFFSET 0x0000
#define ESCAPE_CHAR 0x1B
#define VT52_CLEAR_SCREEN 'E'
#ifndef uint8_t
typedef unsigned char uint8_t;
#endif

#ifndef uint16_t
typedef unsigned short uint16_t;
#endif

typedef int bool;
#define true 1
#define false 0


typedef struct {
    char character;
    char attribute;
} screen_char;
   
static volatile uint8_t far *crtc_addr_reg = MK_FP(PHASE2_DEVICE_SEGMENT, 
                                                   CRTC_ADDR_REG_OFFSET);

static volatile uint8_t far *crtc_data_reg = MK_FP(PHASE2_DEVICE_SEGMENT, 
                                                   CRTC_DATA_REG_OFFSET); 

static volatile uint8_t far *crtc_contrast_reg = MK_FP(PHASE2_DEVICE_SEGMENT, 
                                                       CONTRAST_BRIGHTNESS_REG_OFFSET);

static volatile uint8_t far *screen_buffer = MK_FP(SCREEN_BUFFER_SEGMENT, 
                                                   SCREEN_BUFFER_BOTTOM);

void tostring(char str[], int num)
{
    int i, rem, len = 0, n;
 
    n = num;
    while (n != 0)
    {
        len++;
        n /= 10;
    }
    for (i = 0; i < len; i++)
    {
        rem = num % 10;
        num = num / 10;
        str[len - (i + 1)] = rem + '0';
    }
    str[len] = '\0';
}

// static volatile uint8_t far *font_table_base = MK_FP(FONT_GLYFF_SEGMENT,
//                                                      FONT_GLYFF_OFFSET);

/* contrast is three bits 
 * for a 0-7 scale. 0=low 7=max
 * located at PB5, PB6, and PB7 of the 6522 at E8040
 */
void set_contrast(uint8_t value) {
    if (value > 7) {
        value = 7;
    }
    uint8_t mask = 0xE0; // 0b11100000
    value = ((value <<5) & mask);
    *crtc_contrast_reg = (*crtc_contrast_reg & ~mask) | value;
}

/* brightness is three bits 
 * for a 0-7 scale. 0=low 7=max
 * located at PB2, PB3, and PB4 of the 6522 at E8040
 */
void set_brightness(uint8_t value) {
    if (value > 7) {
        value = 7;
    }
    uint8_t mask = 0x1C; // 0b00011100
    value = ((value <<2) & mask);
    *crtc_contrast_reg = (*crtc_contrast_reg & ~mask) | value;
}

void set_crtc_reg(uint8_t reg, uint8_t value) {
    *crtc_addr_reg = reg;   //which register to set
    *crtc_data_reg = value; //what value to set it to
}

uint8_t get_crtc_reg(uint8_t reg) {
    uint8_t value = 0;
    *crtc_addr_reg = reg;   //which register to set
    value = *crtc_data_reg; //what value to set it to
    return value;
}

void set_screen_start(long start_addr) {
    set_crtc_reg(0x0C, (start_addr >> 8) & 0xFF); // R12
    set_crtc_reg(0x0D, start_addr & 0xFF);        // R13
}

void set_cursor_position(long position) {
    set_crtc_reg(0x0E, (position >> 8) & 0xFF); // R14
    set_crtc_reg(0x0F, position & 0xFF);        // R15
}

uint16_t get_cursor_position() {
    uint16_t position = 0;
    uint8_t high_byte = get_crtc_reg(0x0E); // R14
    uint8_t low_byte = get_crtc_reg(0x0F); // R15
    position = (high_byte << 8) | low_byte;
    return position;
}

uint16_t find_character_table() {
    putchar(ESCAPE_CHAR);
    putchar(VT52_CLEAR_SCREEN);
    uint16_t top_left_cell = *screen_buffer;  // Get the top-left cell content
    uint16_t glyph_offset = top_left_cell & 0x07FF;  // Mask off the attributes
    glyph_offset = glyph_offset << 5; //shift left 5 bits as the CRT sets those bits for each row of the glpyh
    uint16_t char_table = glyph_offset - 0x400;  // Adjust the offset at 32 bytes/char, space = 0x20 so 0x400
    //printf("top_left_cell: %X, glyph_offset: %X, char_table: %X\n", top_left_cell, glyph_offset, char_table);
    return char_table;  // Return the calculated character table address
}

uint16_t convert_line_column_to_offset(uint16_t line, uint16_t column) {
    uint16_t lineoffset[25];

    //build up an array we can use to calculate the screen offset
    for (int i = 0; i < 25; ++i) {
        lineoffset[i] = SCREEN_WIDTH * i;
    }

    uint16_t screen_offset = lineoffset[line];  // Get line starting address from lineoffset array
    screen_offset = (screen_offset << 1);  // Double the offset to convert from bytes to words
    
    uint16_t col_offset = column << 1;  // Double the column number (2 bytes per screen cell)
    
    screen_offset += col_offset;  // Add column offset to screen_offset

    uint16_t absoluteAddress = screen_offset + (uint16_t) screen_buffer;  //add the screen buffer base offset
    
    return absoluteAddress;  // Return the calculated screen offset
}

void set_cursor(uint8_t line, uint8_t column) {
    // Step 1: Calculate cursor address
    uint16_t cur_address = convert_line_column_to_offset(line, column);
    
    set_cursor_position(cur_address);
}

//function to calculate the start location of an ASCII character's font cell
uint16_t calculate_font_cell_start(char ch, uint16_t char_table_offset) {
    //each glyph is 16 consecutive words and each word is 2 bytes = 32 bytes
    uint16_t glyph_size = 32;
    uint16_t glyph_offset= ch * glyph_size;
    uint16_t glyph_pointer = glyph_offset + char_table_offset;
    //shift right 5 bits as the CRT controller sets those bits for each row of the glpyh
    glyph_pointer = glyph_pointer >> 5;
    return glyph_pointer;
}


void putchardirect(char c, uint16_t char_table_offset) {
    uint16_t cursor_address = get_cursor_position();
    uint16_t volatile far *absolute_cursor_address = MK_FP(SCREEN_BUFFER_SEGMENT, cursor_address);

    //glyph_pointer = display_attributes | ASCII + character_table_offset
    uint16_t glyph_pointer = calculate_font_cell_start(c, char_table_offset); //locate the glyph for the char in the font area of RAM
    uint16_t word = ((0x08 << 8) | glyph_pointer);  // convert the char to the format used in the screen buffer (char + attribute byte)
    *absolute_cursor_address = word;
    
    //increment cursor location
    cursor_address +=2;
    set_cursor_position(cursor_address);
    return;
}

void newline(uint16_t char_table_offset) {
    uint16_t cursor_address = get_cursor_position();
    uint8_t screen_words = SCREEN_WIDTH * 2;   //each char is ASCII value + styling

    // Calculate remaining words to fill in current line
    uint16_t words_to_end = screen_words - (cursor_address % screen_words);
    for (int i = 0; i < words_to_end; i += 2) {
        putchardirect(' ', char_table_offset);
    }
}


void print(const char* str, uint16_t char_table_offset) {
    uint16_t cursor_pos = get_cursor_position();
    for (const char* ch = str; *ch; ++ch, cursor_pos += 2) {
        // handle newline character
        if (*ch == '\n') {
            newline(char_table_offset);
            continue;
        }
        putchardirect(*ch, char_table_offset);
    }
    return;
}

void println(const char* str, uint16_t char_table_offset) {
    print(str, char_table_offset);
    newline(char_table_offset);
    return;
}


int main() {
    uint8_t before, after;
    uint16_t cursor_pos, char_table;

    set_screen_start(*screen_buffer);
    set_cursor_position(convert_line_column_to_offset(0,0));

    char_table = find_character_table();
    print("char_table: ", char_table);
    char str_table[30];
    tostring(str_table, char_table);
    println(str_table, char_table);
 
    before = *crtc_contrast_reg;
    set_brightness(7);
    set_contrast(2);

    //read current cursor position
    cursor_pos = get_cursor_position();

    print("Helloworld\n", char_table);
    println("HelloWorld!\n", char_table);
    print("HelloWorld", char_table);
    println("--HelloWorld--", char_table);
    //set cursor display mode
    set_crtc_reg(0x0A, 0x66); // R10 visible + start row
    set_crtc_reg(0x0B, 0x8);  // R11 end row

    putchardirect('!', char_table);
    putchardirect('0', char_table);
    putchardirect('A', char_table);
    putchardirect('Z', char_table);
    putchardirect('a', char_table);
    putchardirect('z', char_table);
    putchardirect('A', char_table);
    putchardirect(0x2A, char_table);
    putchardirect(0x7B, char_table);

    println("about to exit", char_table);
    return(0);
}
