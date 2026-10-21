#include "ssd.h"

//Segment pins (A-G decimal point)
// A = PG9
//B = PF12
//C = PF13
//D = PG14
//E = PE8
//F = PF15
//G = PB4
//DP = PF14

//Common anode pins for digit selection
//CA1 = PE10
//CA2 == PE7
//CA3 == PB5
//CA4 == PB3

static const unsigned char digitSegments[] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111, // 9
    0b00000000  // 10 = blank (all segs off)
};

void SSD_Init(void)
{
    //Enable clocks for the GPIO ports B, E, F, and G
    RCC->AHB1ENR |= (1U << 1); //GPIOB
    RCC->AHB1ENR |= (1U << 4); //GPIOE
    RCC->AHB1ENR |= (1U << 5); //GPIOF
    RCC->AHB1ENR |= (1U << 6); //GPIOG

    //Configure segment pins as outputs
    //A = PG9, D = PG14
    GPIOG->MODER &= ~((3U << 18) | (3U << 28));
    GPIOG->MODER |= ((1U << 18) | (1U << 28));

    //B = PF12, C = PF13, DP = PF14, F = PF15
    GPIOF->MODER &= ~((3U << 24) | (3U << 26) |
                     (3U << 28) | (3U << 30));
    GPIOF->MODER |= ((1U << 24) | (1U << 26) |
                     (1U << 28) | (1U << 30));
    
    //E = PE8, CA1 = PE10, CA2 = PE7
    GPIOE->MODER &= ~((3U << 16) | (3U << 20) |
                     (3U << 14));
    GPIOE->MODER |= ((1U << 16) | (1U << 20) |
                    (1U << 14));

    //G = PB4, CA3 = PB5, CA4 = PB3
    GPIOB->MODER &= ~((3U << 8) | (3U << 10) |
                     (3U << 6));
    GPIOB->MODER |= ((1U << 8) | (1U << 10) |
                    (1U << 6));

    //Turn all segments OFF (HIGH = OFF)
    GPIOG->BSRR = (1U << 9) | (1U << 14); //A, D
    GPIOF->BSRR = (1U << 12) | (1U << 13) |
                  (1U << 14) | (1U << 15); //B, C, DP, F
    GPIOE->BSRR = (1U << 8); //E
    GPIOB->BSRR = (1U << 4); //G

    //Turn all 4 digits off
    GPIOE->BSRR = (1U << 10) | (1U << 7); //CA1, CA2
    GPIOB->BSRR = (1U << 5) | (1U << 3); //CA3, CA4
}

//Stores 4 digits currently shown on the SSD
//Index 0 = thousands, 1 = hundereds, 2 = tens, 3 = ones
//digitSegments[]: 0-9 numerical digits, 10= blank digit (all segs off)
//e.g. frequency = 440hz, displayDigits[]={10,4,4,0}
//SSD Shows "440"

static unsigned char displayDigits[4] = {10,1,0,0};

//Converts a numerical frequency into 4 individual digits
//integer division removes remainder, 
//modulo (%) extracts remainder
//e.g. 1234hz
//1234/1000 = 1
//(1234/100) % 10 = 2
//(1234/10) % 10 = 3
//1234 % 10 = 4
//This function updates display digits
//TIM5 interrupts call SSDRefresh() repeatedly

void SSD_DisplayValue(int value)
{
    //prevent values below 0
    if (value < 0) {
        value = 0;
    }
    //9999 is max, prevent over that
    if (value > 9999) {
        value = 9999;
    }
    // Extract the remaining digits.
    displayDigits[1] = (value / 100) % 10;
    displayDigits[2] = (value / 10) % 10;
    displayDigits[3] = value % 10;
    //separate value into indiviudal digits

    if (value < 1000) {
        displayDigits[0] = 10;
    }
    else{
        displayDigits[0] = value /1000;
    }

    if (value < 100) {
        displayDigits[1] = 10;
    }

    if (value < 10) {
        displayDigits[2] = 10;
    }


}


//Keeps track of which of the 4 ssd digits is being refreshed
//Static means the variable keeps its value between function calls
//0 = far left, 3 = far right
static unsigned char currentDigit = 0;

void SSD_Refresh(void)
{
    //Turn all four digits off before changing segments
    //This prevents unwanted segments lighting up
    //on another digit while the GPIO outputs are changing
    //HIGH = OFF for the common anode control pins
    GPIOE->BSRR = (1U << 10) | (1U << 7); //CA1, CA2 OFF
    GPIOB->BSRR = (1U << 5) | (1U << 3); //CA3, CA4 OFF

    //Look up the segment pattern for the current digit
    //displayDigits[] stores an index 0 to 10
    //0-9 correspond to numbers, and 10 is blank
    //digitSegments[] converts that index to an A-G pattern
    // Index 10 produces blank digit
    unsigned char pattern = digitSegments[displayDigits[currentDigit]]; // digits tell us which digit to show, second tells us which leds

    //Turn all segments and decimal point off before displaying new digit
    //Other wise segments from previous digit could remain on
    //Writing BSRR bits 0-15 drives pins high
    GPIOG->BSRR = (1U << 9) | (1U << 14); //A,D OFF
    GPIOF->BSRR = (1U << 12) | (1U << 13) |
                  (1U << 14) | (1U << 15); //B,C,DP,F OFF
    GPIOE->BSRR = (1U << 8); //E OFF
    GPIOB->BSRR = (1U << 4); //G OFF

    //bit 0 controls segment A (PG9)
    //if bit 0 is 1, drive PG9 low to turn segment A on
    if (pattern & (1U << 0)) {
        GPIOG->BSRR = (1U << (9 + 16));
    }
    //bit 1 controls segment B (PF12)
    if (pattern & (1U << 1)) {
        GPIOF->BSRR = (1U << (12 + 16));
    }
    //bit 2 controls segment C (PF13)
    if (pattern & (1U << 2)) {
        GPIOF->BSRR = (1U << (13 + 16));
    }
    //bit 3 controls segment D (PG14)
    if (pattern & (1U << 3)) {
        GPIOG->BSRR = (1U << (14 + 16));
    }
    //bit 4 controls segment E (PE8)
    if (pattern & (1U << 4)) {
        GPIOE->BSRR = (1U << (8 + 16));
    }
    //bit 5 controls segment F (PF15)
    if (pattern & (1U << 5)) {
        GPIOF->BSRR = (1U << (15 + 16));
    }
    //bit 6 controls segment G (PB4)
    if (pattern & (1U << 6)) {
        GPIOB->BSRR = (1U<< (4 + 16));
    }
    
 

    //Activate only the current digit
    //Common anode digit controls are active LOW
    //Writing to upper BSRR bits drives the GPIO LOW
    if (currentDigit == 0) {
        GPIOE->BSRR = (1U << (10 + 16)); //CA1 ON, first dig
    }
    else if (currentDigit == 1) {
        GPIOE->BSRR = (1U << (7 + 16)); //CA2 ON, second digit
    }
    else if (currentDigit == 2) {
        GPIOB->BSRR = (1U << (5 + 16)); //CA3 ON third digit
    }
    else {
        GPIOB->BSRR = (1U << (3 + 16)); //CA4 ON, fourth digit
    }

    //Move to the next digit for the next TIM5 interrupt
    //% 4 makes the index wrap from 3 back to 0
    //Sequence 0 -> 1 -> 2 -> 3 -> 0
    currentDigit = (currentDigit + 1) % 4;

}