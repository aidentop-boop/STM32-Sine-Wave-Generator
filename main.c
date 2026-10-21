//CPEG222 SINE WAVE GENERATOR
/* * Project Requirements:
* 1. Generate sine waves using DAC, DMA, and TIM6.
* 2. Use a potentiometer to adjust frequency from 100-2000 Hz.
* 3. Display frequency on the four-digit SSD.
* 4. Use TIM5 interrupts to refresh the SSD.
* 5. Use switch S1 to enable/disable the audio amplifier.
* 6. Extra Credit: UP button plays Happy Birthday.
*/

#include "stm32f4xx.h"

#include "ssd.h"

#include <math.h>
#define SINE_SAMPLES 100
#define PI 3.141592653589793

//Stores frequency displayed on SSD
//will be used when implementing to ADC and sine wave gen.
static uint32_t frequency = 100;

//LUT = Look-Up Table
//Stores 100 digital values representing one complete
//sine wave cycle

//uint16_t:
//each sample is an unsigned 16bit int
//the DAC uses 12 bit values (0-4095) which
//fit inside 16 bit int
//2048 = DC offset, moving sinewave above 0
//1000 = amplitude of the sine wave in DAC counts
//smallest value is 2048-1000 = 1048
//Largest is 2048 + 1000 = 3048
//DMA will read these stored samples
// and transfer them into DAC automatically
static uint16_t sine_lut[SINE_SAMPLES];


//stores up buttons state from previous loop
static uint8_t previousUP = 0;

//3 STATES FOR HAPPY BIRTHDAY 
// NORMAL: The potentiometers controls the sine wave freq
//ssd displays it

//PLAYING
// HAPPT BIRTHDAY IS PLAYING
//THE MELODY SELECTS NOTE FREQUENCIES

//ZERO
//MELODY IS OVER SSD SHOWS 0 FOR A SECOND, THE NORMAL
//POTENTIOMETER BEHAVIIOR

typedef enum
{
    NORMAL = 0,
    PLAYING = 1,
    SHOW_ZERO = 2
} PlaybackState;

//stores operating mode
//initially normal
static volatile PlaybackState playbackState = NORMAL;

//HBD MELODY
//DAC uses 100 sample look up table
#define MELODY_LENGTH 25

//NOTE FREQUENCIES
static const uint16_t melodyNotes[MELODY_LENGTH] =
{
    392, 392, 440, 392, 523, 494,
    392, 392, 440, 392, 587, 523,
    392, 392, 784, 659, 523, 494, 440,
    698, 698, 659, 523, 587, 523
};

//NOTE DURATIONS (ms)
static const uint16_t melodyDurations[MELODY_LENGTH] = 
{
    250, 250, 500, 500, 500, 1000,
    250, 250, 500, 500, 500, 1000,
    250, 250, 500, 500, 500, 500, 1000,
    250, 250, 500, 500, 500 ,1000
};

//index of note being played
static uint8_t melodyIndex = 0;

//counts elapsed ms for current note
//timer interrupt will update

static volatile uint32_t noteElapsedTicks = 0;

//counts ms while ssd dispays 0
//after HBD finishes

static volatile uint32_t zeroElapsedTicks = 0;

//calculates the sine samples and stores
//them in sine_lut[]
void SineLUT_Init(void);

//TIM5 Init functions declaration
//TIM5 will generate interrupts to multiplex the SSD

//PSC = prescalar
//ARR = Auto reload register
//Together they determine how frequently 
//TIM5 will generate and update event
void TIM5_Init(void);

//ADC Initialization Function
//How are we converint 12 bit ADC reading into
//Frequency between 100Hz and 2000hz
//ADC = Analog to Digital converter
//The potentiometer produces a variable analog voltage
//The ADC has 12-bit resolution
//2^12 = 4-96 possible digital values (0 through 4095)
//Will later be mapped to 100Hz-2000Hz
//f = 100 + (ADCval x (2000-100))/(4095 - 0)
//will give us value between 100 and 2000
void ADC1_Init(void);

//Reads potentiometer voltage using ADC2
//The ADC converts the analog voltage on PC2
//into a 12 bit digital value between 0 and 4095
//We use it b/c a 12 bit ADC result fits in 16 bits
//Unlike ADC1_Init() this function returns a value
uint16_t ADC1_Read(void);

//DAC initialization
//DAC = digital to analog converter
//The DAC converts LUT samples (0-4095)
//into analog voltage levels
//DAC Channel 1 output through PA4
//TIM6 will trigger DAC updates
//while DMA supplies the next LUT sample
void DAC1_Init(void);

//TIM6 init
//TIM6 control the sample rate of the sine wave
//It generates an update event after ARR+1 counter
//ticks following prescalar division
//Use TRGO signal to trigger DAC
//TIM6 does not need a CPU interrupt for this
//hardware-triggered DAC/DMA design
void TIM6_Init(void);

//TIM6 FREQ UPDATE
//TIM6_UpdateFrequenct() changes the sample rate
//whenever the potentiometer selects a new frequenct
//A 100 sample LUT requires 100 DAC Triggers
//for each complete sine-wave cycle
void TIM6_UpdateFrequency(uint32_t newFrequency);

//DMA Initialization 
//DMA1 Stream 5, channel 7 handles DAC channel 1 requests
//Memory source: sine_lit[]
//Peripheral destination: DAC->DHR12R1

//DMA will automatically transfer 100 samples
//then restart from 0 using circular mode
void DMA1_Init(void);

//The amplifier shutdown pin is connected to PB6
//and is configured as an output
//S1 = 1->PB6 HIGH -> AMPLIFIER EN
//S1 = 0->PB6 LOW -> AMPLIFIER SHUT DOWN
//GPIOC->IDR reads the input switch
//GPIOB->BSRR controls the amplifier output

//configure the S1 input and amplifier shutdown output
void Audio_Init(void);
//Read s1 and update the amplifier shutdown pin
void Audio_Update(void);

void UP_Button_Init(void);



int main(void)
{
    //initialize all 4 SSD digits and their segment pins
    //SSD_init() enables GPIO clocks, configures GPIO outputs
    //and initially turns all SSD segments and digits off
    SSD_Init();

    //convert the starting freq. into display digits
    //E.G. 100hz becomes {blank, 1, 0, 0}
    //this doesn't directly illuminate the display
    //SSD_Refresh() will handle that once TIM5 is configured
    //Inititalize the SSD to display the starting frequency
    SSD_DisplayValue(frequency);

    //STEP 1: Generate the 100 sin wave samples
    //calcuulates the 100 sine wave samples at once
    //Later, DMA will transfer these values to the DAC
    SineLUT_Init();
    
    //STEP 2: Configure DAC Channel 1 on PA4
    //Configures DAC channel 1 to output analog voltage on RA4
    //DAC triggering will be controlled by TIM6
    DAC1_Init();

    //STEP 3: Configuring TIM6 
    //Configure TIM6 to generte DAC trigger rate
    //The counter stays stopped until DMA is ready
    TIM6_Init();

    //STEP 4: Configure and enable DMA1 Stream 5
    //Configure the DMA for LUT-to-DAC transfers
    DMA1_Init();

    //STEP 5: Enable DAC Channel 1 DMA Requests
    //This allows the DAC to request new LUT samples
    //from DMA whenever TIM6 triggers it
    DAC->CR |= DAC_CR_DMAEN1;

    //STEP 6: Statt TIM6
    //CEN = Counter enable 
    //TIM6 now generates periodic triggers for DAC
    TIM6->CR1 |= TIM_CR1_CEN;


    //STEP 7: Initialize TIM5 for SSD refreshing
    //Inititalize the TIM5 to refresh one SSD digit every interrupt
    TIM5_Init();

    //STEP 8: Initialize ADC1 for the potentiometer
    //Initialize ADC1 to read the potentiometer on PC2
    //This configures the ADC
    ADC1_Init();

    Audio_Init();

    UP_Button_Init();

    //main program loop
    while (1)
    {
        //STEP 1: Read the potentiometer
        //Conncerts between 100hz and 2000hz by:
        //ADC1_Read() starts an ADC conversion using SWSTARTA
        //It waits for EOC to become 1
        //Returns value stored in ADC1->DR
        //ADC result ranges from 0 to 4095
        uint16_t adcValue = ADC1_Read();

        //Step 2: Map ADC reading to frequency
        //Mapping formula
        //f = 100 + (ADCval * 1900)/4095
        //4095 = max value of 12 bit adc
        //when adc = 0
        //f = 100Hz
        //When ADC = 4095
        //f = 2000Hz
        //uint32_t is used for the mulitplication bc
        //4095 * 1900 = 7,780,500
        //too large for 16bit integer
        
        uint32_t newFrequency = 
                        100U + ((uint32_t)adcValue * 1900u)/4095U;
        
        //STEP 3 Check whether the frequency changed
        //Smaller ARR = FASTER TRIGGERS = HIGHER FREQ
        //HIGHER ARR = SLOWER TRIGGERS = SLOWER FREQ
        
        // Ignore frequency changes of 5 Hz or less.
        if (adcValue <= 10U)
        {
            newFrequency = 100U;
        }

        // If the potentiometer is near its maximum,
        // force the frequency to exactly 2000 Hz.
        else if (adcValue >= 4085U)
        {
        newFrequency = 2000U;
        }
        if (playbackState == NORMAL) 
        {
            // Update TIM6 if the cHange exceeds 5 Hz,
            // OR if we need to correct an endpoint.
            if ((newFrequency > frequency + 5U) ||
                (newFrequency + 5U < frequency) ||
                (newFrequency == 100U && frequency != 100U) ||
                (newFrequency == 2000U && frequency != 2000U))
            {
                frequency = newFrequency;
                TIM6_UpdateFrequency(frequency);
            }
        }
        //STEP 4: UPDATE SSD WITH FREQUENCY
        //SSD_DisplayValue() separates the frequency
        //into thousdands, hundereds, tens, and ones
        //E.g. 440Hz
        //displayDigits[] = {10, 4, 4, 0}
        //10 is blank digit
        //This does not refresh the SSD
        //TIM5 interrupts independently call SSD_Refresh()
        //400 times per second to multiplex the display
        if (playbackState == NORMAL)
        {
            SSD_DisplayValue(frequency);
        }
        else if (playbackState == PLAYING)
        {
            SSD_DisplayValue(melodyNotes[melodyIndex]);
        }
        else if (playbackState == SHOW_ZERO)
        {
            SSD_DisplayValue(0);
        }

        Audio_Update();

        //STEP 5, READ UP BUTTON ON PF7
        uint8_t currentUP = (GPIOF->IDR & (1U << 7)) ? 0U : 1U;

        //STEP 6. DETECT NEW BUTTON PRESS
        if (currentUP && !previousUP && playbackState == NORMAL)
        {
            //CHANGE OPERATING MODE TO PLAYING
            playbackState = PLAYING;
            melodyIndex = 0;
            noteElapsedTicks = 0;
            TIM6_UpdateFrequency(melodyNotes[melodyIndex]);
        }
        previousUP = currentUP;

        //AUTOMATICALLY ADVANCE MELODY NOTES
        if (playbackState == PLAYING)
        {
            //Convert current notes into duration
            uint32_t requiredTicks = 
                ((uint32_t)melodyDurations[melodyIndex] * 400U) / 1000U;
            
            //check whether current note has finihed
            if (noteElapsedTicks >= requiredTicks)
            {
                //reset counter for next note
                noteElapsedTicks = 0;
                melodyIndex++;

                if (melodyIndex < MELODY_LENGTH)
                {
                    TIM6_UpdateFrequency(melodyNotes[melodyIndex]);
                }
                else
                {
                    TIM6->CR1 &= ~TIM_CR1_CEN;
                    zeroElapsedTicks = 0;
                    playbackState = SHOW_ZERO;
                }
            }
        }
        //ONE SECOND 0 DISPLAY
        if (playbackState == SHOW_ZERO)
        {
            if (zeroElapsedTicks >= 400U)
            {
                frequency = newFrequency;
                TIM6_UpdateFrequency(frequency);

                TIM6->CNT =0;
                TIM6->CR1 |= TIM_CR1_CEN;

                playbackState = NORMAL;

                zeroElapsedTicks = 0;
            }
        }
    }
}

void TIM5_Init(void) 
{
    //STEP 1: Enable TIM5 peripheral clock
    //RCC = RESET CLOCK CONTROL
    //APB1ENR = ABP1 peripheral clock enable register
    //Bit 3 enables TIM5
    //1U << 3 Selects bit 3
    //|= sets that bit without clearing the others 
    RCC->APB1ENR |= (1U << 3);

    //STEP 2: set the prescalar
    //PSC divides the incoming clock timer
    //TIM5 clock = 16MHz
    //PSC = 159 means divide by 159 +1 (160)
    //Counter clock = 16,000,000 / 160
    //Counter clock - 100,000
    TIM5->PSC = 159;
    
    //STEP 3 Set auto reload value
    //ARR determines how many counter ticks occur
    //before the timer produces an update event
    //ARR = 249 means 250 timer ticks
    //Interrupt frequency = 100,000 / 250 
    //Interrupt frequency = 400Hz
    TIM5->ARR = 249;

    //STEP 4`: Load prescalar immediately
    //EGR = event generation register
    //UG = Update generation bit
    //Setting UG generates an update event
    //Which transfers the prescalar setting
    //into the timer's activate prescalar
    TIM5->EGR = TIM_EGR_UG;

    //STEP 5: Clear any pending flag
    //SR = Status register
    //UIF = Update Interrupt Flag
    //Generating an update event can set UIF
    //We clear it so TIM5 doesn't immediatley 
    //Enter the interrupt handler unexpectedly
    TIM5->SR = 0;

    //STEP 6: Enable the TIM5 update interrupts 
    //DIER = DMA/Interrupt enable register
    //UIE = Update Interrupt Enable
    //When UIE = 1, TIM5 may request an interrupt
    //whenever an update event occurs
    TIM5->DIER |= TIM_DIER_UIE;

    //STEP 7: Enable TIM5 in the NVIC
    //NVIC = Nested Vectored Interrupt Controller
    //TIM5_IRQn identifies the TIM5 interrupt
    //This allows the CPU to respond to TIM5's interrupt requests
    NVIC_EnableIRQ(TIM5_IRQn);

    //STEP 8: Start TIM5
    //CR1 = control register 1
    //CEN = Counter enable
    //Setting CEN = 1 allows timer to count
    TIM5->CR1 |= TIM_CR1_CEN;



}

//Everytime TIM5 generates an interrupts flag
//1. Clear update interrupt flag
//2. Refresh one digit of the SSD
//An ISR is a function that runs automaticcaly when
//The corresponding hardware interrupt occurs

    
void TIM5_IRQHandler(void)
{
    //Check the update interrupt flag
    if (TIM5->SR & TIM_SR_UIF) 
    {
        //Clear update interrupt flag
        //Otherwise, interruopt could repeat immediately repeat
        TIM5->SR = ~TIM_SR_UIF;
        //Refresh one digit of the 4 digit SSD
        SSD_Refresh();

        //TRACK TIME FOR MELODY
        if (playbackState == PLAYING)
        {
            noteElapsedTicks++;
        }
        else if (playbackState == SHOW_ZERO)
        { 
            zeroElapsedTicks++;
        }
    }
}   

//ADC = Analog to Digital Converter
//The ADC converts an analog voltage into a digital value
// 0 - 4095

void ADC1_Init(void)
{
    //Step 1: Enable the GPIOC peripheral clock
    //GPIOC is connected to the AHB1 bus
    //Bit 2 in RCC->AHB1ENR enables GPIOC
    //Without the clock enabled we cannot configure PC2 (pot)
    RCC->AHB1ENR |= (1U << 2);

    //STEP 2: Configure PC2 as an analog output
    //PC2 recieves voltage from the potentiometer
    //MODER = 11 selects analog mode
    //Moder is GPIO mode register
    //Each GPIO pin has two mode bits
    //00 = Input
    //01 = General Purpose output
    //10 = Alternate function
    //11 = Analog mode
    //PC2 uses MODER BITS 5:4
    //3U << 4 sets both bits to 1
    //which selects analog mode for PC2
    GPIOC->MODER |= (3U << 4);

    //STEP 3: Disable pull up and pull down resistors
    //PUPDR = Pull-up/Pull-down register
    //00 = No pull up or pull down
    //Since PC2 receives an analog voltage
    //we don't want internal pull resistors
    //affecting the signal
    GPIOC->PUPDR &= ~(3U << 4);

    //STEP 4: Enable the ADC1 peripheral clock
    //ADC1 is conncected to the APB2 bus
    //Bit 8 of RCC->APB2ENR enables ADC1
    RCC->APB2ENR |= (1U << 8);

    //STEP 5: Configure ADC1 for 12 bit resolution
    //CR1 = ADC control regiser 1
    //RES bits [25:24] select ADC resolution
    //00 = 12 bit resolution
    //01 = 10 bit resolution
    //10 = 8 bit resolutiom
    //11 = 6 bit resolution

    //Clearing the two RES bits selects 12 bit mode
    ADC1->CR1 &= ~(3U << 24);

    //STEP 6: Select ADC1 Channel 12
    //SQR3 = Regular Sequence Register 3
    //SQ1 bits [4:0] specify the first ADC channel
    //in the regular conversion sequence
    //select channel 12 because PC2 is ADC1_IN12
    ADC1->SQR3 &= ~(0x1FU);
    ADC1->SQR3 |= 12U;
    
    //STEP 7: Select one conversion in the sequence
    //SQR1 = Regular Sequence Register 1
    //L bits [23:20] determine sequence length
    //L = 0 means one conversion
    ADC1->SQR1 &= ~(0xFU << 20);

    //STEP 8: Configure software trigged single conversion
    //CR2 = ADC control reg 2
    //Cont = continuous conversion bit
    //Clearing cont means one conversion per SWSTART
    ADC1->CR2 &= ~ADC_CR2_CONT;

    //EXTEN = external trigger enable
    //Clearing EXTEN disabled external hardware triggers
    //This allows us to start conversion using SWSTART
    ADC1->CR2 &= ~ADC_CR2_EXTEN;

    //STEP 9: Enable ADC1
    //CR2 = ADC ON
    //Setting ADON enables ADC peripherals
    ADC1->CR2 |= ADC_CR2_ADON;
}

//How to obtain ADC reading thats converted into a freq.
//Between 100-2000Hz
//1: This function starts an ADC conversion
//2: Waits until conversion is complete
//3: Reads the 12 bit result from ADC1->DR
//4: Returns that result to calling function
uint16_t ADC1_Read(void)
{
    //STEP 1: Start a software triggered ADC conversion
    //CR2 = ADC Control register 2
    //SWSTART = Software start bit
    //Setting SWSTART tells ADC1 to begin converting
    //the voltage on its selecting channel
    //|= sets the SWSTART bit without changing
    //other settings in CR2
    ADC1->CR2 |= ADC_CR2_SWSTART;

    //STEP 2: Wait for conversion to finish
    //SR = ADC Status Register
    //EOC = End of conversion flag
    //Hardware sets EOC = 1 when the ADC
    //conversion has finished and result is obtainable
    //ADC1->SR & ADC_SR_EOC
    //Checks whetehr the EOC bit is set
    //The ! operator means not
    //While EOC = 0, the condition is tru
    // CPU STAYS in loop
    //When EOC = 1 the condition is false
    //execution continues
    while (!(ADC1->SR & ADC_SR_EOC))
    {
        //Wait until conversions complete
    }

    //STEP 3: Read and return the conversion result
    //DR = Data register
    //In 12 bit mode, the result is a number
    //Between 0 and 4095
    //The ADC result is right aligned by default
    //Reading DR also clears the EOC flag
    //for this regular conversion
    return (uint16_t)ADC1->DR;
}    

//SineLUT_Init()
//Generates 100 sin wave samples and
//stores them in lookup table
//The samples are calculated once during initialization
//not repeatedlt during sound generation
//Why is LUT centered around 2048?
//What happens if LUT has 10 or 1000 samples

void SineLUT_Init(void)
{
    //Loop through all 100 sample positions
    // i starts at 0 and increases by 1 each time
    // The loop stops when i reaches SINE_SAMPLES (100)
    //Therefore, valid 0 -> 99
    for (uint16_t i = 0; i < SINE_SAMPLES; i++)
    {
        //Calculate the angle correspondign to i
        //one complete sine wave = 2*PI radians
        //Dividing by 100 divides cycles
        //into 100 equally spaces samples positions
        //e.g.
        //i = 0 angle = 0
        //i = 25 angle = PI/2
        //i = 50 angle = PI
        //i = 75 angle = 3PI/2
        double angle = (2.0 * PI * i) / SINE_SAMPLES;
        
        //Calculate DAC samples
        //sin(angle) produces val between [-1,1]
        //Multiplying by 1000 = [-1000,1000]
        //Adding 2048 shifts range to [1048-3048]
        //The cast converts the result to uint16_t
        //because DAC samples must be whole numbers
        sine_lut[i] = (uint16_t)(2048 + 1000.0 * sin(angle) + 0.5); 
    }
}

//DAC1_Init()
//Configures DAC Channel 1 to PA4
//Explain how TIM6, DMA, the LUT, and DAC work together
//1. Enables the GPIOA clock
//2. Configures PA4 in analog mode
//3. Enables the DAC peripheral clock
//4. Selects TIM6 as the DAC trigger source
//5. Enables hardware triggering and DAC channel 1

void DAC1_Init(void)
{
    //STEP 1; Enable the GPIOA peripheral clock
    //RCC = Reset and Clock control
    //AHB1ENR = AHB1 peripheral clock enable register
    //Bit 0 enables GPIOA
    //1U << 0 selects bit 0
    // |= sets bit 0 without clearing enabled clocks
    RCC->AHB1ENR |= (1U << 0);

    //STEP 2: Configuring PA4 in analog mode
    //MODER  GPIO Mode Registers
    //Each GPIO pin uses two MODER bits
    //PA4 uses bits 8:9
    //11 = analog
    GPIOA->MODER |= (3U << 8);

    //STEP 3: Disable inteernal pullup/pulldown reg
    GPIOA->PUPDR &= ~(3U << 8);
    
    //The DAC is connected to APB1 bus
    //Bit 29 is APB1ENR enables DAC clock
    RCC->APB1ENR |= (1U << 29);

    //STEP 5: Select TIM6 as the DAC trigger source
    //CR = DAC Control register
    //TSEL1 = Trigger selection for DAC channel 1
    //TSEL1 bits [5:3] 
    //000 = TIM6 TRGO event
    //Clearing these bits selects TIM6
    //TRGO = trigger output
    //Time 6 will produce periodic TRGO events
    DAC->CR &= ~DAC_CR_TSEL1;

    //STEP 6: Enables DAC Channel 1 hardware triggering
    //TEN1 = Trigger enable for DAC channel 1
    //Setting TEN1 allows DAC conversions to  be 
    //triggered by TIM6 rather than by software
    DAC->CR |= DAC_CR_TEN1;

    //STEP 7 LOad the initial DAC sample 
    //DHR12R1 = 12-BIT right alligned data holding register
    //for DAC channel 10
    //sine_Lut[0] is 2048
    DAC->DHR12R1 = sine_lut[0];

    //STEP 8: Enable DAC channel 1
    //EN1 = DAC Channel 1 enable
    //Setting EN1 enables the channels converter
    DAC->CR |= DAC_CR_EN1;
}

//TIM6 configures TIM6 to trigger DAC samples at a
//frequency required for the currently selected sine wave
//TIM6 ARR is calculted for 440hz output
//fsample = fsine * SINE*SAMPLES
//fsample = f_TIM6/((PSC+1)(ARR+1))
//ARR = 16,000,000 / (frequency * 100) - 1
void TIM6_Init(void)
{
    //Step 1: enable the TIM6 peripheral clock
    //Bit 4 enables TIM6
    //1U << 4 selects bit 4
    RCC->APB1ENR |= (1U << 4);

    //STEP 2: Disable TIM6 before configuration
    //Clearing CEN stops the timer while we configure it
    TIM6->CR1 &= ~TIM_CR1_CEN;
    
    //ARPE = auto reload preload enable
    //apre = 0 means writing to ARR changes the active
    //auto reload value immediately

    //this allows TIM6_UpdateFrequency() to change
    //the timer period withouth generating update event
    TIM6->CR1 &= ~TIM_CR1_ARPE;

    //STEP 3: Configure the prescalar
    //PSC = 0 means divide by 1 
    //With a 16,000,000 Hz clock
    TIM6->PSC = 0;

    //STEP 4: Calculate timer counts per DAC
    //Every complete sine wave has 100 LUT samples
    //If frequency = 440Hz
    //samples per second = 440 * 100 = 44,000
    //ticks per sample = 16,000,000/44,000 = 363.6 OR 364
    uint32_t samplesPerSecond = frequency * SINE_SAMPLES;
    uint32_t ticksPerSample = 
                (16000000UL + samplesPerSecond / 2U) / samplesPerSecond;
    
    //STEP 5: Set ARR
    //Timer counts ARR+1 ticks per update event
    //ARR = ticksPerSample - 1
    //e.g. for 440hz
    //Tcks per sample = 364
    //ARR = 363
    TIM6->ARR = ticksPerSample - 1U;

    //STEP 6: Select update event as TRGO
    //CR2 = Control Register 2
    //MMS = Master Mode Selection
    //MMS bits [6:4] select the timer trigger output
    //010 = Update event used as TRGO
    //First clear the MMS field
    TIM6->CR2 &= ~TIM_CR2_MMS;
    
    //Then set MMS to binary 010
    //This makes TIM6 generate a TRGO on each update
    TIM6->CR2 |= (2U << TIM_CR2_MMS_Pos);

    //STEP 7: Generate an update event to load PSC/ARR
    //EGR = Event Generation Register
    //UG = epdate generation
    //writing UG initialization the timer's prescalar
    //and reloads its counter related settngs
    TIM6->EGR = TIM_EGR_UG;

    //STEP 8 Clear the pending flag
    //SR = Status register
    //UIF = Update interrupt flag
    //Clearing flag first, not enabling
    TIM6->SR = 0; 
}

void DMA1_Init(void)
{
    //STEP 1: Enable the DMA1 peripheral clock
    //BIT 21 of AHB1ENR enables DMA1
    RCC->AHB1ENR |= (1U << 21);

    //STEP 2: Disable DMA1 Stream 5 before configuration
    //CR = Stream configuration register
    //EN = stream enable
    //A DMA stream must be disabled before changing
    //its configuration registers
    DMA1_Stream5->CR &= ~DMA_SxCR_EN;

    //Wait until hardware confirms its disabled
    while (DMA1_Stream5->CR & DMA_SxCR_EN)
    {

    }

    //STEP 3: Clear the existing stream configuration
    //The stream is disabled so we can safely set its
    //configuration from a known state
    DMA1_Stream5->CR = 0;

    //STEP 4: Select DMA channel 7
    //CHSEL = Channel Selection
    //CHSEL bits [27:25] selection the DMA request channel
    //111 binary = 7 decimal
    //Stream 5, channel 7 is connected to DAC channel 1
    DMA1_Stream5->CR |= (7U << DMA_SxCR_CHSEL_Pos);

    //STEP 5: Set transfer direction to memory to peripheral
    //DIR = Data transfer direction 
    //DIR [7:6] 
    //00 = peripheral to memory
    //01 = memory to peripheral
    //10 = memory to memory
    //We want:
    //sine_Lut[] -> DAC holding register
    DMA1_Stream5->CR |= (1u << DMA_SxCR_DIR_Pos);

    //STEP 6: Set memory and peripheral data sizes
    //MSIZE = Memory data size
    //PSIZE = peripheral data size
    //00 = 8 bit
    //01 = 16 bit
    //10 = 32bit
    // sine_Lut[] stores uint16_t samples
    //Configure both sides sides as 16 bit transfers
    DMA1_Stream5->CR |= (1U << DMA_SxCR_MSIZE_Pos);
    DMA1_Stream5->CR |= (1U << DMA_SxCR_PSIZE_Pos);


    //Step 7 Enable memory icrement
    //MINC = Memory Increment Mode
    //After each transfer, DMA advances to the
    //next sample in sine_Lut[]
    DMA1_Stream5->CR |= DMA_SxCR_MINC;

    //Step 8: Enable Circular mode
    //CIRC = Circular mode
    //After 100th transfer DMA automatically
    //returns to beginning of sin_Lut[]
    //This produces a continuously repeating waveform

    //without circular mode, DMA would stop after completing
    //configure transfer count
    DMA1_Stream5->CR |= DMA_SxCR_CIRC;

    //STEP 9: Set the number of data transfers
    //NDTR = Number of data to transfer
    //We have 100 samples in our lookup table
    DMA1_Stream5->NDTR = SINE_SAMPLES;

    //STEP 10: Set the peripheral destination address
    //PAR = peripheral address register
    //DAC->DHR12R1 is the 12-bit right alligned
    //holding register for DAC channel 1
    DMA1_Stream5->PAR = (uint32_t)&DAC->DHR12R1;

    //STEP 11: Set the memory source address
    //M0AR = Memory 0 Address Register
    //sin_Lut without brackets is the address of first 
    //element sin_lut[0]
    //DMA reads the samples starting here
    DMA1_Stream5->M0AR = (uint32_t)sine_lut;

    //CLEAR ALL PENDING INTERRUPTS OR STATUS FLAGS
    //FROM DMA1 STREAM 5
    DMA1->HIFCR = DMA_HIFCR_CTCIF5 |
                  DMA_HIFCR_CHTIF5 |
                  DMA_HIFCR_CTEIF5 |
                  DMA_HIFCR_CDMEIF5 |
                  DMA_HIFCR_CFEIF5;
    //Step 12: Enable DMA1 Stream 5
    //Stream now ready to transfer samples
    DMA1_Stream5->CR |= DMA_SxCR_EN;


}

//TIM6 UPDATE FREQUENCY
//The sine wave ses 100LUT samples per cycle
//DAC sample rate = sine wave frequency * 100
//TIM6 clock is assumed to be 16MHz
//PSC = 0, so the prescalar divides by 1
//ARR = (Timer ticks per sample) - 1
//This function updates ARR without stopping TIM6
void TIM6_UpdateFrequency(uint32_t newFrequency)
{
    //STEP 1: Calculate how many DAC samples are needed
    //every second
    //e.g.
    //newFrequency = 440Hz
    //samplesPerSecond = 440 * 100 = 44,000Hz
    uint32_t samplesPerSecond = newFrequency * SINE_SAMPLES;

    //STEP 2: CALCULATE TIMER CLOCK TICKS PER SAMPLE
    //Assuming the TIM6 clock is 16,000,000 Hz
    //Adding samplesPerSecond/2 rounds the integer
    //e.g.
    //16,000,000 / 44,000 ~ 364
    uint32_t ticksPerSample = 
            (16000000UL + samplesPerSecond/2U)/samplesPerSecond;

    //STEP 3: Update the auto-reload register
    //ARR = Auto-reload register
    //TIM6 counts from 0 to ARR
    //ARR = ticksPerSample - 1
    //At 440Hz
    //ARR = 364 - 1 363
    //With ARR preload disabled
    //this updates the timer period without an
    //explicit software-generate update
    TIM6->ARR = ticksPerSample - 1U;
}

//AUDIO INIT
//CONFIGURES 
//PC8 = SC1 switch input 
//PB6 = Amplifier shutdown output
//Important GPIO concepts 
//RCC MODER PUPDR BSRR
void Audio_Init(void)
{
    //STEP 1: ENABLE GPIOB and GPIOC peripheral clocks
    RCC->AHB1ENR |= (1U << 1) | (1U << 2);

    //STEP 2 CONFIGURE PC8 as an input
    GPIOC->MODER &= ~(3U << 16);

    //STEP 3: DISABLE PUPDR
    GPIOC->PUPDR &= ~(3U << 16);

    //STEP 4 CONFIGURE PB6 AS AN OUTPUT
    GPIOB->MODER &= ~(3U << 12);
    GPIOB->MODER |= (1U << 12);

    //Step 5 INITIALLY DISABLE THE AMPLIFIER
    GPIOB->BSRR = (1U << (6+16));

}

//AUDIO UPDATE
//READS SWITCH 1 ON PC8
//SW1 = HIGH , PB6 IS HIGH (AMP ENABLED)
//SW1 IS LOW, PB6 IS LOW (AMP DISABLED)

void Audio_Update(void)
{
    //STEP 1 READ THE S1 SWITCH
    if (GPIOC->IDR & (1U << 8))
    {
        //STEP 2: S1 IS HIGH (ENABLE AMP)
        GPIOB->BSRR = (1U << 6);
    }   
    else {
        //STEP 2B S1 IS LOW
        GPIOB->BSRR = (1U << (6 + 16));
    }
}
//up is PF7
void UP_Button_Init(void)
{
    RCC->AHB1ENR |= (1U << 5);
    GPIOF->MODER &= ~(3U << 14);
    GPIOF->PUPDR &= ~(3U << 14);
    GPIOF->PUPDR |= (1U << 14);
}