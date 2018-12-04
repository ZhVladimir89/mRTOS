#include <avr/io.h>
#include <avr/interrupt.h>
#include "mrtos.h"

#define  PULSE_PORT PORTD
#define  PULSE      PD1
#define  PULSE2     PD2

void task4(void) {
    if(PULSE_PORT & _BV(PULSE))
        PULSE_PORT &= ~_BV(PULSE);
    else
        PULSE_PORT |= _BV(PULSE);
}

void task1(void) {
    while(1) {
        task4();
        mRTOS_TASK_WAIT(100);

    }
}

/*
void task2(void) {
   while(1) {
     if(PULSE_PORT & _BV(PULSE2))
       PULSE_PORT &= ~_BV(PULSE2);
     else
       PULSE_PORT |= _BV(PULSE2);
    mRTOS_TASK_WAIT(50);
   }
}

void task3(void) {
   while(1) {
      asm volatile("nop ::");
      mRTOS_DISPATCH;
   }
}

void task5() {
  static uint8_t counter = 0;
   while(1) {
      counter++;
      mRTOS_DISPATCH;
   }
}

void task6() {
  static uint8_t counter = 0;
   while(1) {
      counter++;
      mRTOS_DISPATCH;
   }
}
*/

int main(void) {

    PORTB = 0x00;
    DDRB = 0x00;

    PORTD = 0x06;
    DDRD = 0x06;

    TCCR0 = 0x00;
    TCNT0 = 0x00;

    // Timer/Counter 1 initialization
    // Clock source: System Clock
    // Clock value: Timer 1 Stopped
    // Mode: Normal top=FFFFh
    // OC1 output: Discon.
    // Noise Canceler: Off
    // Input Capture on Falling Edge
    // Timer 1 Overflow Interrupt: Off
    // Input Capture Interrupt: Off
    // Compare Match Interrupt: Off
    TCCR1A = 0x00;
    TCCR1B = 0x00;
    TCNT1 = 0x00;
    OCR1A = 0x00;
    OCR1B = 0x00;

    // External Interrupt(s) initialization
    // INT0: Off
    // INT1: Off
    GIMSK = 0x00;
    MCUCR = 0x00;

    // Timer(s)/Counter(s) Interrupt(s) initialization
    TIMSK = 0x01;

    // Analog Comparator initialization
    // Analog Comparator: Off
    // Analog Comparator Input Capture by Timer/Counter 1: Off
    ACSR = 0x80;

    // Global enable interrupts
    sei();

    mRTOS_Init();
    mRTOS_CreateTask(task1, 10, ACTIVE);
    //mRTOS_CreateTask(task2, 20, ACTIVE);
    //mRTOS_CreateTask(task3, 20, ACTIVE);
    //mRTOSCreateTask(task5, 50, ACTIVE);
    //mRTOSCreateTask(task6, 50, ACTIVE);
    PULSE_PORT = 0;
    mRTOS_Scheduler();
    return 0;
}
