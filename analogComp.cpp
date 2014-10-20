/* This file is part of the analogComp library.
   Please check the README file and the notes
   inside the analogComp.h file
*/

//include required libraries
#include "analogComp.h"

#if !defined (__MK20DX256__) && !defined (__MK20DX128__)

//setting and switching on the analog comparator
uint8_t analogComp::setOn(uint8_t tempAIN0, uint8_t tempAIN1) {
    if (_initialized) { //already running
        return 1;
    }

    //initialize the analog comparator (AC)
    ACSR &= ~(1<<ACIE); //disable interrupts on AC
    ACSR &= ~(1<<ACD); //switch on the AC

    //choose the input for non-inverting input
    if (tempAIN0 == INTERNAL_REFERENCE) {
        ACSR |= (1<<ACBG); //set Internal Voltage Reference (1V1)
    } else {
        ACSR &= ~(1<<ACBG); //set pin AIN0
    }

//for Atmega32U4, only ADMUX is allowed as input for AIN-
#ifdef ATMEGAxU
	if (tempAIN1 == AIN1) {
		tempAIN1 = 0; //choose ADC0
	}
#endif

//AtTiny2313/4313 don't have ADC, so inputs are always AIN0 and AIN1
#ifndef ATTINYx313
    // allow for channel or pin numbers
#if defined (ATMEGAx0)
    if (tempAIN1 >= 54) tempAIN1 -= 54;
#elif defined (ATMEGAxU)
	if (tempAIN1 >= 18) tempAIN1 -= 18;
#elif defined (ATMEGAx4)
	if (tempAIN1 >= 24) tempAIN1 -= 24;
#elif defined (CORE_ANALOG_FIRST) && (defined (ATTINYx5) || defined (ATTINYx4))
    if (tempAIN1 >= CORE_ANALOG_FIRST) {
        tempAIN1 -= CORE_ANALOG_FIRST;
    }
#else
	if (tempAIN1 >= 14) tempAIN1 -= 14;
#endif
    //choose the input for inverting input
    oldADCSRA = ADCSRA;
    if ((tempAIN1 >= 0) && (tempAIN1 < NUM_ANALOG_INPUTS)) { //set the AC Multiplexed Input using an analog input pin
        ADCSRA &= ~(1<<ADEN);
        ADMUX &= ~31; //reset the first 5 bits
        ADMUX |= tempAIN1; //choose the ADC channel (0..NUM_ANALOG_INPUTS-1)
        AC_REGISTER |= (1<<ACME);
    } else {
        AC_REGISTER &= ~(1<<ACME); //set pin AIN1
    }
#endif

//disable digital buffer on pins AIN0 && AIN1 to reduce current consumption
#if defined(ATTINYx5)
	DIDR0 &= ~((1<<AIN1D) | (1<<AIN0D));
#elif defined(ATTINYx4)
	DIDR0 &= ~((1<<ADC2D) | (1<<ADC1D));
#elif defined (ATMEGAx4)
	DIDR1 &= ~(1<<AIN0D);
#elif defined (ATTINYx313)
	DIDR &= ~((1<<AIN1D) | (1<<AIN0D));
#elif defined (ATMEGAx8) || defined(ATMEGAx4) || defined(ATMEGAx0)
    DIDR1 &= ~((1<<AIN1D) | (1<<AIN0D));
#endif
    _initialized = 1;
    return 0; //OK
}


//enable the interrupt on comparations
void analogComp::enableInterrupt(void (*tempUserFunction)(void), uint8_t tempMode) {
    if (_interruptEnabled) { //disable interrupts
		SREG &= ~(1<<SREG_I);
        ACSR &= ~(1<<ACIE);
    }

    if (!_initialized) {
        setOn(AIN0, AIN1);
    }

    //set the interrupt mode
    userFunction = tempUserFunction;
    if (tempMode == CHANGE) {
        ACSR &= ~((1<<ACIS1) | (1<<ACIS0)); //interrupt on toggle event
    } else if (tempMode == FALLING) {
        ACSR &= ~(1<<ACIS0);
        ACSR |= (1<<ACIS1);
    } else { //default is RISING
        ACSR |= ((1<<ACIS1) | (1<<ACIS0));

    }
    //enable interrupts
    ACSR |= (1<<ACIE);
    SREG |= (1<<SREG_I);
    _interruptEnabled = 1;
}


//disable the interrupt on comparations
void analogComp::disableInterrupt(void) {
    if ((!_initialized) || (!_interruptEnabled)) {
        return;
    }
    ACSR &= ~(1<<ACIE); //disable interrupt
    _interruptEnabled = 0;
}


//switch off the analog comparator
void analogComp::setOff() {
    if (_initialized) {
		if (_interruptEnabled) {
			ACSR &= ~(1<<ACIE); //disable interrupts on AC events
			_interruptEnabled = 0;
		}
        ACSR |= (1<<ACD); //switch off the AC

        //reenable digital buffer on pins AIN0 && AIN1
#if defined(ATTINYx5)
		DIDR0 |= ((1<<AIN1D) | (1<<AIN0D));
#elif defined(ATTINYx4)
		DIDR0 |= ((1<<ADC2D) | (1<<ADC1D));
#elif defined (ATMEGAx4)
		DIDR1 |= (1<<AIN0D);
#elif defined (ATTINYx313)
		DIDR |= ((1<<AIN1D) | (1<<AIN0D));
#elif defined (ATMEGAx8) || defined(ATMEGAx4) || defined(ATMEGAx0)
		DIDR1 |= ((1<<AIN1D) | (1<<AIN0D));
#endif

#ifndef ATTINYx313
        //if ((AC_REGISTER & (1<<ACME)) == 1) { //we must reset the ADC
        if (oldADCSRA & (1<<ADEN)) { //ADC has to be powered up
            AC_REGISTER |= (1<<ADEN); //ACDSRA = oldADCSRA;
        }
#endif
		_initialized = 0;
    }
}


//wait for a comparation until the function goes in timeout
uint8_t analogComp::waitComp(unsigned long _timeOut) {
	//exit if the interrupt is on
	if (_interruptEnabled) {
		return 0; //error
	}

	//no timeOut?
	if (_timeOut == 0) {
		_timeOut = 5000; //5 secs
	}

	//set up the analog comparator if it isn't
	if (!_initialized) {
		setOn(AIN0, AIN1);
		_initialized = 0;
	}

	//wait for the comparation
	unsigned long _tempMillis = millis() + _timeOut;
	do {
		if ((ACSR && (1<<ACO)) == 1) { //event raised
			return 1;
		}
	} while ((long)(millis() - _tempMillis) < 0);

	//switch off the analog comparator if it was off
	if (!_initialized) {
		setOff();
	}
	return 0;
}


//ISR (Interrupt Service Routine) called by the analog comparator when
//the user choose the raise of an interrupt
#if defined(ATMEGAx8) || defined(ATMEGAx0) || defined(ATMEGAx4)
ISR(ANALOG_COMP_vect) {
#else
ISR(ANA_COMP_vect) {
#endif
    analogComparator.userFunction(); //call the user function
}

#else

#define CMP_SCR_DMAEN   ((uint8_t)0x40) // DMA Enable Control
#define CMP_SCR_IER     ((uint8_t)0x10) // Comparator Interrupt Enable Rising
#define CMP_SCR_IEF     ((uint8_t)0x08) // Comparator Interrupt Enable Falling
#define CMP_SCR_CFR     ((uint8_t)0x04) // Analog Comparator Flag Rising
#define CMP_SCR_CFF     ((uint8_t)0x02) // Analog Comparator Flag Falling
#define CMP_SCR_COUT    ((uint8_t)0x01) // Analog Comparator Output
#define CMPx_CR0        0 // CMP Control Register 0
#define CMPx_CR1        1 // CMP Control Register 1
#define CMPx_FPR        2 // CMP Filter Period Register
#define CMPx_SCR        3 // CMP Status and Control Register
#define CMPx_DACCR      4 // DAC Control Register
#define CMPx_MUXCR      5 // MUX Control Register

#ifndef CMP2_CR0
// it is better to add following lines to
// Arduino.app/Contents/Resources/Java/hardware/teensy/cores/teensy3/kinetis.h
#define CMP2_CR0                (*(volatile uint8_t  *)0x40073010) // CMP Control Register 0
#define CMP2_CR1                (*(volatile uint8_t  *)0x40073011) // CMP Control Register 1
#define CMP2_FPR                (*(volatile uint8_t  *)0x40073012) // CMP Filter Period Register
#define CMP2_SCR                (*(volatile uint8_t  *)0x40073013) // CMP Status and Control Register
#define CMP2_DACCR              (*(volatile uint8_t  *)0x40073014) // DAC Control Register
#define CMP2_MUXCR              (*(volatile uint8_t  *)0x40073015) // MUX Control Register
#endif

analogComp::analogComp(volatile uint8_t *base, void(*seton_cb)(uint8_t, uint8_t)) :
 _initialized(0), _interruptEnabled(0), CMP_BASE_ADDR(base), _setonCb(seton_cb) {
} 

//setting and switching on the analog comparator
uint8_t analogComp::setOn(uint8_t tempAIN0, uint8_t tempAIN1) {
    if (_initialized) { //already running
        return 1;
    }

    // clock gate: comparator clock set on. it is off by default for conserving power.
    SIM_SCGC4 |= SIM_SCGC4_CMP;
    _setonCb(tempAIN0, tempAIN1);
    CMP_BASE_ADDR[CMPx_CR1] = 0; // set CMPx_CR1 to a known state
    CMP_BASE_ADDR[CMPx_CR0] = 0; // set CMPx_CR0 to a known state
    CMP_BASE_ADDR[CMPx_CR1] = B00000001; // enable comparator
    CMP_BASE_ADDR[CMPx_MUXCR] = tempAIN0 << 3 | tempAIN1; // set comparator input PSEL=tempAIN0 / MSEL=tempAIN1

    _initialized = 1;
    return 0; //OK
}


//enable the interrupt on comparations
void analogComp::enableInterrupt(void (*tempUserFunction)(void), uint8_t tempMode) {
    if (_interruptEnabled) { //disable interrupts
        CMP_BASE_ADDR[CMPx_SCR] = 0;
    }

    if (!_initialized) {
        setOn(0, 1);
    }

    //set the interrupt mode
    userFunction = tempUserFunction;
    if (tempMode == CHANGE) {
        CMP_BASE_ADDR[CMPx_SCR] = CMP_SCR_IER | CMP_SCR_IEF;
    } else if (tempMode == FALLING) {
        CMP_BASE_ADDR[CMPx_SCR] = CMP_SCR_IEF;
    } else { //default is RISING
        CMP_BASE_ADDR[CMPx_SCR] = CMP_SCR_IER;
    }
    _interruptEnabled = 1;
}


//disable the interrupt on comparations
void analogComp::disableInterrupt(void) {
    if ((!_initialized) || (!_interruptEnabled)) {
        return;
    }
    CMP_BASE_ADDR[CMPx_SCR] = 0; //disable interrupt
    _interruptEnabled = 0;
}


//switch off the analog comparator
void analogComp::setOff() {
    if (_initialized) {
        if (_interruptEnabled) {
            CMP_BASE_ADDR[CMPx_SCR] = 0; //disable interrupts on AC events
            _interruptEnabled = 0;
        }
        CMP_BASE_ADDR[CMPx_CR1] = 0; //switch off the AC
        _initialized = 0;
    }
}


//wait for a comparation until the function goes in timeout
uint8_t analogComp::waitComp(unsigned long _timeOut) {
    //exit if the interrupt is on
    if (_interruptEnabled) {
        return 0; //error
    }

    //no timeOut?
    if (_timeOut == 0) {
        _timeOut = 5000; //5 secs
    }

    //set up the analog comparator if it isn't
    if (!_initialized) {
        setOn(0, 1);
        _initialized = 0;
    }

    //wait for the comparation
    unsigned long _tempMillis = millis() + _timeOut;
    do {
        if ((CMP_BASE_ADDR[CMPx_SCR] & CMP_SCR_COUT)) { //event raised
            return 1;
        }
    } while ((long)(millis() - _tempMillis) < 0);

    //switch off the analog comparator if it was off
    if (!_initialized) {
        setOff();
    }
    return 0;
}

//ISR (Interrupt Service Routine) called by the analog comparator when
//the user choose the raise of an interrupt
void cmp0_isr() {
    CMP0_SCR |= CMP_SCR_CFR | CMP_SCR_CFF; // clear CFR and CFF
    analogComparator.userFunction(); //call the user function
}

static void CMP0_Init(uint8_t inp, uint8_t inm) {
    NVIC_SET_PRIORITY(IRQ_CMP0, 64); // 0 = highest priority, 255 = lowest
    NVIC_ENABLE_IRQ(IRQ_CMP0); // handler is now cmp0_isr()
    if (inp == 0 || inm == 0) { // CMP0_IN0 (51 = pin11)
        CORE_PIN11_CONFIG = PORT_PCR_MUX(0); // set pin11 function to ALT0
    }
    if (inp == 1 || inm == 1) { // CMP0_IN1 (52 = pin12)
        CORE_PIN12_CONFIG = PORT_PCR_MUX(0); // set pin12 function to ALT0
    }
    if (inp == 2 || inm == 2) { // CMP0_IN2 (53 = pin28)
        CORE_PIN28_CONFIG = PORT_PCR_MUX(0); // set pin28 function to ALT0
    }
    if (inp == 3 || inm == 3) { // CMP0_IN3 (54 = pin27)
        CORE_PIN27_CONFIG = PORT_PCR_MUX(0); // set pin27 function to ALT0
    }
    if (inp == 4 || inm == 4) { // CMP0_IN4 (55 = pin29)
        CORE_PIN29_CONFIG = PORT_PCR_MUX(0); // set pin29 function to ALT0
    }
    if (inp == 5 || inm == 5) { // VREF Output/CMP0_IN5 (17)
        ; // do nothing
    }
    if (inp == 6 || inm == 6) { // Bandgap
        ; // do nothing
    }
    if (inp == 7 || inm == 7) { // 6b DAC0 Reference
        ; // do nothing
    }
}

void cmp1_isr() {
    CMP1_SCR |= CMP_SCR_CFR | CMP_SCR_CFF; // clear CFR and CFF
    analogComparator1.userFunction(); //call the user function
}

static void CMP1_Init(uint8_t inp, uint8_t inm) {
    NVIC_SET_PRIORITY(IRQ_CMP1, 64); // 0 = highest priority, 255 = lowest
    NVIC_ENABLE_IRQ(IRQ_CMP1); // handler is now cmp1_isr()
    if (inp == 0 || inm == 0) { // CMP1_IN0 (45 = pin23)
        CORE_PIN23_CONFIG = PORT_PCR_MUX(0); // set pin23 function to ALT0
    }
    if (inp == 1 || inm == 1) { // CMP1_IN1 (46 = pin9)
        CORE_PIN9_CONFIG = PORT_PCR_MUX(0); // set pin9 function to ALT0
    }
    if (inp == 3 || inm == 3) { // 12-bit DAC0_OUT/CMP1_IN3 (18)
        ; // do nothing
    }
    if (inp == 5 || inm == 5) { // VREF Output/CMP1_IN5 (17) 
        ; // do nothing
    }
    if (inp == 6 || inm == 6) { // Bandgap
        ; // do nothing
    }
    if (inp == 7 || inm == 7) { // 6b DAC1 Reference
        ; // do nothing
    }
}

#if CMP_INTERFACES_COUNT > 2
void cmp2_isr() {
    CMP2_SCR |= CMP_SCR_CFR | CMP_SCR_CFF; // clear CFR and CFF
    analogComparator2.userFunction(); //call the user function
}

static void CMP2_Init(uint8_t inp, uint8_t inm) {
    NVIC_SET_PRIORITY(IRQ_CMP2, 64); // 0 = highest priority, 255 = lowest
    NVIC_ENABLE_IRQ(IRQ_CMP2); // handler is now cmp2_isr()
    if (inp == 0 || inm == 0) { // CMP2_IN0 (28 = pin3)
        CORE_PIN3_CONFIG = PORT_PCR_MUX(0); // set pin3 function to ALT0
    }
    if (inp == 1 || inm == 1) { // CMP2_IN1 (29 = pin4)
        CORE_PIN4_CONFIG = PORT_PCR_MUX(0); // set pin4 function to ALT0
    }
//  if (inp == 3 || inm == 3) { // CMP2_IN3
// this setting exists at the CMP section of the manual but
// non-existent in "K20 Signal Multiplexing and Pin Assignments"
//  }
    if (inp == 6 || inm == 6) { // Bandgap
        ; // do nothing
    }
    if (inp == 7 || inm == 7) { // 6b DAC2 Reference
        ; // do nothing
    }
}
#endif

#endif

#ifndef CMP_INTERFACES_COUNT
analogComp analogComparator;
#else
analogComp analogComparator(&CMP0_CR0, CMP0_Init);
#if CMP_INTERFACES_COUNT > 1
analogComp analogComparator1(&CMP1_CR0, CMP1_Init);
#endif
#if CMP_INTERFACES_COUNT > 2
analogComp analogComparator2(&CMP2_CR0, CMP2_Init);
#endif
#endif
