/*
 * Autoři:
 * 	Mark Menzynski (xmenzy00) - Zbytek souboru - 96,663 %
 * 	Michal Bidlo (bidlom) - Inspiroval jsem se funkcemi MCUInit a LPTMR0Init z FITkit3-demo - 3,337 %
 * Datum provedení poslední změny:
 * 	22. prosince 2019
 */
#include "MK60D10.h"
#include <math.h> // for pow()

/* Macros for bit-level registers manipulation */
#define DISP_NUM_1 0x200 // PTD9
#define DISP_NUM_2 0x1000 // PTD12
#define DISP_NUM_3 0x2000 // PTD13
#define DISP_NUM_4 0x100 // PTD8

#define DISP_SEG_A 0x800 // PTA11
#define DISP_SEG_B 0x200 // PTA9
#define DISP_SEG_C 0x4000 // PTD14
#define DISP_SEG_D 0x400 // PTA10
#define DISP_SEG_E 0x40 // PTA6
#define DISP_SEG_F 0x80 // PTA7
#define DISP_SEG_G 0x8000 // PTD15
#define DISP_SEG_DOT 0x100 // PTA8

#define SONAR_TRIG 0x8000000 // PTA27
#define SONAR_ECHO 0x4000000 // PTA26

#define BTN_SW4 0x8000000 // PTE27
#define BTN_SW6 0x800 // PTE11

#define GPIOA_DIGITS_MASK 0xFC0; // All digits on port A
#define GPIOD_DIGITS_MASK 0xC000; // All digits on port D

/* Other macros */
#define DISPLAY_DELAY 10000
#define DISP_DRAW_REPEATS 35
#define MAX_DISTANCE 9999
#define DIGIT_UNDEFINED -1
#define TRIG_LENGTH 250 // ~ 10 microseconds
#define SPEED_OF_SOUND 340 // meters per second
#define TIMEOUT_LENGTH 250 // 250 milliseconds

struct Digit {
	int value;
	int dot;
	int hide;
};

int timeout_flag = 0;

/* Initialize the MCU - basic clock settings, turning the watchdog off */
void MCUInit(void)
{
    MCG_C4 |= ( MCG_C4_DMX32_MASK | MCG_C4_DRST_DRS(0x01) );
    SIM_CLKDIV1 |= SIM_CLKDIV1_OUTDIV1(0x00);
    WDOG_STCTRLH &= ~WDOG_STCTRLH_WDOGEN_MASK;
}

/* Initialize ports */
void PortsInit(void)
{
    /* Turn on port clocks */
    SIM->SCGC5 = SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTD_MASK | SIM_SCGC5_PORTE_MASK;

    /* Set pins used for display segment selection to GPIO */
    PORTA->PCR[6] = PORT_PCR_MUX(0x01); // PTA6
    PORTA->PCR[7] = PORT_PCR_MUX(0x01); // PTA7
    PORTA->PCR[8] = PORT_PCR_MUX(0x01); // PTA8
    PORTA->PCR[9] = PORT_PCR_MUX(0x01); // PTA9
    PORTA->PCR[10] = PORT_PCR_MUX(0x01); // PTA10
    PORTA->PCR[11] = PORT_PCR_MUX(0x01); // PTA11

    PORTD->PCR[14] = PORT_PCR_MUX(0x01); // PTD14/SPI2_MISO
	PORTD->PCR[15] = PORT_PCR_MUX(0x01); // PTD15/SPI2_CS1

    /* Set pins used for display number selection as GPIO */
    PORTD->PCR[8] = PORT_PCR_MUX(0x01); // PTD8/I2C0_SCL
    PORTD->PCR[9] = PORT_PCR_MUX(0x01); // PTD9/I2C0_SDA
    PORTD->PCR[12] = PORT_PCR_MUX(0x01); // PTD12/SPI2_CLK
    PORTD->PCR[13] = PORT_PCR_MUX(0x01); // PTD13/SPI2_MOSI

    /* Set pin used for button to GPIO */
    PORTE->PCR[27] = PORT_PCR_MUX(0x01); // PTE27 SW4
    PORTE->PCR[11] = PORT_PCR_MUX(0x01); // PTE11 SW6

    /* Set pins used for ultrasonic sensors to GPIO */
    PORTA->PCR[26] = PORT_PCR_MUX(0x01); // PTA26
    PORTA->PCR[27] = PORT_PCR_MUX(0x01); // PTA27

    /* Set corresponding port pins as OUT */
    GPIOA->PDDR = GPIO_PDDR_PDD(0x8000FC0); // All PTA except 27
    GPIOD->PDDR = GPIO_PDDR_PDD(0xF300); // All PTD

    /* Disable number columns by putting voltage into it,
     * restricting it from completing circuit and lighting the diodes*/
    GPIOD->PDOR |= DISP_NUM_1 | DISP_NUM_2 | DISP_NUM_3 | DISP_NUM_4;
}

/* Timer initialization */
void LPTMR0Init()
{
    SIM_SCGC5 |= SIM_SCGC5_LPTIMER_MASK; // Enable clock to LPTMR
    LPTMR0_CSR &= ~LPTMR_CSR_TEN_MASK;   // Turn OFF LPTMR to perform setup
    LPTMR0_PSR = ( LPTMR_PSR_PBYP_MASK   // LPO feeds directly to LPT
                 | LPTMR_PSR_PCS(1)) ;   // use the choice of clock
    LPTMR0_CMR = TIMEOUT_LENGTH;         // Set compare value
    LPTMR0_CSR =(  LPTMR_CSR_TCF_MASK    // Clear any pending interrupt (now)
                 | LPTMR_CSR_TIE_MASK    // LPT interrupt enabled
                );
    NVIC_EnableIRQ(LPTMR0_IRQn);         // enable interrupts from LPTMR0
}

/* Interrupt handler */
void LPTMR0_IRQHandler(void)
{
    LPTMR0_CSR |=  LPTMR_CSR_TCF_MASK;   // writing 1 to TCF to clear the flag and reset the counter
    timeout_flag = 1;
}

/* If button is pressed */
int btn_pressed(int mask)
{
	return !(GPIOE->PDIR & mask);
}

/* A delay function */
void wait(long long bound)
{
	long long i;
	for(i = 0; i < bound; i++);
}

/* Get a digit from certain position, indexed from zero from the right to the left
 * for example, in number 5932 the digit 3 is in position 1 */
void get_digit(int number, int pos, struct Digit *digit)
{
	int divisor;
	digit->hide = 0;

	/* The value is in millimeters, to get centimeters, simply put a decimal dot on second position */
		digit->dot = (pos == 2) ? 1 : 0;

	/* If it is pos 0 there is no need to do other calculations
	 * and we can simply get the digit with a modulo */
	if (pos == 1) {
		digit->value = number % 10;
		return;
	}

	/* Calculate the digit */
	divisor = pow(10, pos-1);
	digit->value = (number / divisor) % 10;

	/* If digit on pos 4 is zero, there is no need to show it */
	if (pos == 4 && digit->value == 0) {
		digit->hide = 1;
	}
	/* If digit on pos 3 is zero and also pos 4 is zero (number is smaller then 1000) there is no need to show it */
	if (pos == 3 && digit->value == 0 && number < 1000) {
		digit->hide = 1;
	}

	return;
}

/* Enable whole digit on desired column */
void display_column_toggle(int col)
{
	switch(col) {
		case 1:
			GPIOD->PDOR ^= DISP_NUM_1;
			break;
		case 2:
			GPIOD->PDOR ^= DISP_NUM_2;
			break;
		case 3:
			GPIOD->PDOR ^= DISP_NUM_3;
			break;
		case 4:
			GPIOD->PDOR ^= DISP_NUM_4;
			break;
	}
}

/* Enable corresponding segments for showing desired digit */
void display_digit(struct Digit digit, int pos)
{
	if (digit.hide) {
		return;
	}

	display_column_toggle(pos);

	if(digit.dot) {
		GPIOA->PDOR |= DISP_SEG_DOT;
	}

	switch(digit.value) {
		case 0:
			GPIOD->PDOR |= DISP_SEG_C;
			GPIOA->PDOR |= DISP_SEG_A | DISP_SEG_B | DISP_SEG_D | DISP_SEG_E | DISP_SEG_F;
			break;
		case 1:
			GPIOD->PDOR |= DISP_SEG_C;
			GPIOA->PDOR |= DISP_SEG_B;
			break;
		case 2:
			GPIOD->PDOR |= DISP_SEG_G;
			GPIOA->PDOR |= DISP_SEG_A | DISP_SEG_B | DISP_SEG_D | DISP_SEG_E;
			break;
		case 3:
			GPIOD->PDOR |= DISP_SEG_C | DISP_SEG_G;
			GPIOA->PDOR |= DISP_SEG_A | DISP_SEG_B | DISP_SEG_D | DISP_SEG_G;
			break;
		case 4:
			GPIOD->PDOR |= DISP_SEG_C | DISP_SEG_G;
			GPIOA->PDOR |= DISP_SEG_B | DISP_SEG_F;
			break;
		case 5:
			GPIOD->PDOR |= DISP_SEG_C | DISP_SEG_G;
			GPIOA->PDOR |= DISP_SEG_A | DISP_SEG_D | DISP_SEG_F;
			break;
		case 6:
			GPIOD->PDOR |= DISP_SEG_C | DISP_SEG_G;
			GPIOA->PDOR |= DISP_SEG_A | DISP_SEG_D | DISP_SEG_E | DISP_SEG_F;
			break;
		case 7:
			GPIOD->PDOR |= DISP_SEG_C;
			GPIOA->PDOR |= DISP_SEG_A | DISP_SEG_B;
			break;
		case 8:
			GPIOD->PDOR |= DISP_SEG_C | DISP_SEG_G;
			GPIOA->PDOR |= DISP_SEG_A | DISP_SEG_B | DISP_SEG_D | DISP_SEG_E | DISP_SEG_F;
			break;
		case 9:
			GPIOD->PDOR |= DISP_SEG_C | DISP_SEG_G;
			GPIOA->PDOR |= DISP_SEG_A | DISP_SEG_B | DISP_SEG_F;
			break;
		default:
			GPIOD->PDOR |= DISP_SEG_G;
	}
	wait(DISPLAY_DELAY);

	display_column_toggle(pos);
	GPIOA->PDOR &= ~GPIOA_DIGITS_MASK;
	GPIOD->PDOR &= ~GPIOD_DIGITS_MASK;
}

/* Draw a number by repeatedly drawing each digit fast enough on each column so it looks like one number */
void disp_draw_number(int *number)
{
	int cycle = 0;
	int pause = 0;
	int already_pressed = 0;
	struct Digit digit;

	/* initialize structure */
	digit.value = DIGIT_UNDEFINED;
	digit.dot = 0;
	digit.hide = 0;

	while ((cycle < DISP_DRAW_REPEATS) || pause) // Repeat cycle if 'pause' is true
	{
		/* Implements a "toggle button". When button is already_pressed, pauses the measurements and waits
		 * for unpressing it. After that when already_pressed again, the pause is lifted and measuring continues */
		if (pause) {
			if (already_pressed && !btn_pressed(BTN_SW6)) { // If button was already presssed and now it's not
				already_pressed = 0;
			}
			if (!already_pressed && btn_pressed(BTN_SW6)) { // If button wasn't already_pressed before and now it is
				pause = 0;
				/* Do nothing until button is unpressed this is needed because otherwise it would get immediately paused again */
				while(btn_pressed(BTN_SW6));
			}
		}
		else {
			if (!already_pressed && btn_pressed(BTN_SW6)) { // If button wasn't already_pressed before and now it is
				already_pressed = 1;
				pause = 1;
			}
		}

		int prev_value = 0;
		for (int col = 4; col > 0; col--)
		{
			if (number) {
				get_digit(*number, col, &digit); // Rewrite default digit
			}
			display_digit(digit, col);
		}
		cycle++;
	}
}

/* Send trigger signal to a sensor */
void sonar_trigger()
{
	GPIOA->PDOR |= SONAR_TRIG;
	wait(TRIG_LENGTH);
	GPIOA->PDOR &= ~SONAR_TRIG;
}

/* Measure the delay of echo signal */
long sonar_read_echo() {
	long delay = 0;

    LPTMR0_CSR |= LPTMR_CSR_TEN_MASK;    // Start the timeout by turning on LPTMR0 and starting counting

	while (!(GPIOA_PDIR & SONAR_ECHO) && !timeout_flag); // Wait until SENS_ECHO is HIGH or timeout
	while ((GPIOA_PDIR & SONAR_ECHO) && !timeout_flag) { // Count until SENS_ECHO is LOW or timeout
		delay++;
	}

	LPTMR0_CSR &= ~LPTMR_CSR_TEN_MASK;   // Turn OFF LPTMR0
    LPTMR0_CSR |=  LPTMR_CSR_TCF_MASK;   // writing 1 to TCF to reset the counter

	if (timeout_flag) {
		delay = 0;
		timeout_flag = 0;
	}

	return delay;
}

int main(void)
{
    int distance;
    float delay = 0;

	MCUInit();
    PortsInit();
    LPTMR0Init();

    while (1) {
    	sonar_trigger(); // Send a trigger pulse to the sensor

    	/* Get a impulse length from the sensor which is the actual sound delay, divide by 25 to get microseconds,
    	 * then divide by 2, to get delay only of one way instead of both ways */
        delay = sonar_read_echo() / 25.0 / 2.0;

        /* Divide the delay by speed of sound, result is in meters, multiply by 1000 to get millimeters */
    	distance = delay / SPEED_OF_SOUND * 1000;

    	if (0 < distance && distance <= MAX_DISTANCE) {
    		disp_draw_number(&distance); // Display calculated number
    	}
    	else {
    		disp_draw_number(0); // Display undefined number
    	}
    }

    return 0;
}
