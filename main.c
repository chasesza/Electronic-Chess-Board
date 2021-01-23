#include <msp430fr6989.h>

// shift register for led matrix
#define SER BIT2     // SER connected to 9.2
#define SRCLK BIT3   // SRCLK connected to 4.3
#define RCLK BIT2   // RCLK connected to 4.2

// UART offset is used to shift matrix positions into a useful ASCII range
#define UART_OFFSET 33

typedef struct LedPair{
    volatile int coordPairs[2][2]; // first index is pair 0/pair 1, second index is column (0) or row (1)
    unsigned volatile int currentCoord;
}LedPair;

LedPair pair = {0};
volatile int count = 0;
const char converter[] = {'0','1','2','3','4','5','6','7','8','9'};
volatile char txBuff[] = "\0\0\0\0\0\0\0\0";
unsigned volatile int txPos = 0;
volatile int txAvailable = 1;
volatile char coordsToSend[2] = "\0\0";
volatile unsigned int coordRxIndx = 0; // coordRxInx records whether this is the first (0) or second (1) button press
volatile int coordTxIndx = -1;
volatile int transmitting = 0;
volatile int txAck = 0;
volatile int receivingMove = 0;
volatile int ledRxIndx = 0;
volatile int newLedPair = 1;
volatile int ledInput[2] = {UART_OFFSET+4,UART_OFFSET+59};
volatile int turnOff = 0;

// ledSettingColumns is columns, column A is the leftmost bit - QA is column A, QB is column B and so on
void setShiftRegisterLeds(int ledSettingColumns, int ledSettingRows)
{
    // both shift registers wired to the same SRCLK (green) and RCLK (blue) (4.3 and 4.2)
    // the row register SER is connected to 9.2
    // the column register SER is connected to QH' on the row register

    volatile int i;
    P4OUT &= ~SRCLK;

    for(i = 7; i >=0; i--)
    {
        // led is on if column is low and row is high
        if (ledSettingColumns & 1) // if rightmost bit is one, pull column low
        {
            P9OUT &= ~SER;
        }
        else
        {
            P9OUT |= SER;
        }
        ledSettingColumns >>= 1; // bit shift to next led
        P4OUT |= SRCLK; // pulse clock to add to the shift register and update storage register (storage register is NOT behind shift register like it would be with an actual TI SN74HC595)
        P4OUT &= ~SRCLK;
    }

    for(i = 7; i >=0; i--)
    {
        // note that on = serial is pulled low
        if (ledSettingRows & 1) // if rightmost bit is one, pull row high
        {
            P9OUT |= SER;
        }
        else
        {
            P9OUT &= ~SER;
        }
        ledSettingRows >>= 1; // bit shift to next led
        P4OUT |= SRCLK; // pulse clock to add to the shift register and update storage register (storage register is NOT behind shift register like it would be with an actual TI SN74HC595)
        P4OUT &= ~SRCLK;
    }

    // pull RCLK low then high to move from shift reg to storage reg
    P4OUT &= ~RCLK;
    P4OUT |= RCLK;
    P4OUT &= ~RCLK;
}

// ledSetting is rows, row 1 is the leftmost bit
void setLedRows(int ledSetting)
{
    // row 8
    if(ledSetting & 1)
    {
        P9OUT |= BIT6;
    }
    else
    {
        P9OUT &= ~BIT6;
    }
    ledSetting >>= 1;

    // row 7
    if(ledSetting & 1)
    {
        P9OUT |= BIT5;
    }
    else
    {
        P9OUT &= ~BIT5;
    }
    ledSetting >>= 1;

    // row 6
    if(ledSetting & 1)
    {
        P9OUT |= BIT1;
    }
    else
    {
        P9OUT &= ~BIT1;
    }
    ledSetting >>= 1;

    // row 5
    if(ledSetting & 1)
    {
        P9OUT |= BIT0;
    }
    else
    {
        P9OUT &= ~BIT0;
    }
    ledSetting >>= 1;

    // row 4
    if(ledSetting & 1)
    {
        P8OUT |= BIT7;
    }
    else
    {
        P8OUT &= ~BIT7;
    }
    ledSetting >>= 1;

    // row 3
    if(ledSetting & 1)
    {
        P8OUT |= BIT6;
    }
    else
    {
        P8OUT &= ~BIT6;
    }
    ledSetting >>= 1;

    // row 2
    if(ledSetting & 1)
    {
        P8OUT |= BIT5;
    }
    else
    {
        P8OUT &= ~BIT5;
    }
    ledSetting >>= 1;

    // row 1
    if(ledSetting & 1)
    {
        P8OUT |= BIT4;
    }
    else
    {
        P8OUT &= ~BIT4;
    }

    return;
}

// sets as output
// the shift registers controll the LED matrix
void shiftRegisterSetup()
{
    P4DIR |= (SRCLK|RCLK); // set the two to output
    P9DIR |= SER;
    P4OUT &= (~SRCLK & ~RCLK);
    setShiftRegisterLeds(0,0); // start with all lights off


    return;
}

// input a coordinate string and set the LedPair struct
// all other leds are disabled
void setLeds(int *coords)
{
    // (x%8) is equal to (x&7) and (x/8) is equal to (x>>3)
    pair.coordPairs[0][0] = 1<<(7-((*(coords))&7)); // 1<<(column number 0-7)
    pair.coordPairs[0][1] = 1<<(7-((*(coords))>>3)); // row number 0-7
    pair.coordPairs[1][0] = 1<<(7-((*(coords+1))&7)); // 1<<(column number 0-7)
    pair.coordPairs[1][1] = 1<<(7-((*(coords+1))>>3)); // row number 0-7

    //set current LED to pair 0
    pair.currentCoord = 0;
    setShiftRegisterLeds(pair.coordPairs[0][0], pair.coordPairs[0][1]);
}

void switchLeds()
{
    // switch to the other coordinate pair
    if(pair.currentCoord) // if current coordinate pair is coordinate pair 1
    {
        pair.currentCoord = 0;
    }
    else
    {
        pair.currentCoord = 1;
    }
    // set the leds to the new pair
    setShiftRegisterLeds(pair.coordPairs[pair.currentCoord][0], pair.coordPairs[pair.currentCoord][1]);
    return;
}

// sets up timer A0 for 128 interrupts per second
void setupTimerA0()
{
    // disable timer before changing settings
    TA0CTL &= (~BIT5 & ~BIT4);

    // set TA0 to use ACLK
    // note that ACLK is currently configured to 32 kHz
    TA0CTL &= ~BIT9;
    TA0CTL |= BIT8;

    // divide ACLK input by 8
    // new TA0 counts at a frequency of 4096 hz
    TA0CTL |= (BIT6 | BIT7);


    // clear interrupt flag
    TA0CTL &= ~BIT0;

    // disable interrupts for TA0
    TA0CTL &= ~BIT1;

    // timer should trigger interrupt 128 times a second (32)
    TA0CCR0 = 32;

    // TA0 set to count up to TA0CCR0, is started
    TA0CTL |= BIT4;
    TA0CTL &= ~BIT5;
}

// sets up timer A2 for 2 interrupts per second
void setupTimerA2()
{
    // disable timer before changing settings
    TA2CTL &= (~BIT5 & ~BIT4);

    // set TA0 to use ACLK
    // note that ACLK is currently configured to 32 kHz
    TA2CTL &= ~BIT9;
    TA2CTL |= BIT8;

    // divide ACLK input by 8
    // new TA0 counts at a frequency of 4096 hz
    TA2CTL |= (BIT6 | BIT7);


    // clear interrupt flag
    TA2CTL &= ~BIT0;

    // disable interrupts for TA2
    TA2CTL &= ~BIT1;

    // timer should trigger interrupt 2 times a second
    TA2CCR0 = 2048;

    // TA2 stopped
    TA2CTL &= ~BIT4;
    TA2CTL &= ~BIT5;
}

// this function (except for enabling interrupts) is copied from the lab manual for EEL4742C Embedded Systems at UCF
void setupUART()
{
    P3SEL1 &= ~(BIT4|BIT5);
    P3SEL0 |= (BIT4|BIT5);

    UCA1CTLW0 |= UCSWRST; // enable reset mode during config

    UCA1CTLW0 |= UCSSEL_2; // set to SMCLK

    UCA1CTLW0 &= ~UCRXEIE; // erroneous characters are rejected and do not trigger the RX interrupt
    UCA1CTLW0 &= ~UCBRKIE; // break characters do not trigger the RX interrupt


    UCA1CTLW0 &= ~UCSPB; // one stop bit
    UCA1CTLW0 &= ~UCSYNC; // synchronous mode disable
    UCA1CTLW0 |= UCMODE_0; // standard uart mode

    UCA1BRW = 6; // clock prescaler set to 6
    UCA1MCTLW = (UCBRS5 | UCBRS1 | UCBRF3 | UCBRF2 | UCBRF0 | UCOS16); // configure oversampling

    // clear interrupt flags
    UCA1IFG &= ~UCTXIFG;
    UCA1IFG &= ~UCRXIFG;

    UCA1CTLW0 &= ~UCSWRST; // exit reset
    UCA1IE |= UCRXIE; // enable the rx interrupt
    UCA1IE |= UCTXIFG; // enable the tx interrupt

    return;
}

void setupButtons()
{
    /* inputs
     * row 1 = P3.0
     * row 2 = P1.3
     * row 3 = P2.2
     * row 4 = P3.7
     * row 5 = P3.6
     * row 6 = P3.3
     * row 7 = P2.6
     * row 8 = P2.7
     */

    /* outputs
     * column a = P2.1
     * column b = P1.5
     * column c = P9.4
     * column d = P1.6
     * column e = P1.7
     * column f = P2.5
     * column g = P2.4
     * column h = P4.7
     */

    // set rows as input
    P1DIR &= ~BIT3;
    P2DIR &= (~BIT7 & ~BIT6 & ~BIT2);
    P3DIR &= (~BIT7 & ~BIT6 & ~BIT3 & ~BIT0);

    P1DIR &= ~BIT1;
    P1REN |= BIT1;
    P1OUT |= BIT1;
    P1IE |= BIT1;
    P1IES |= BIT1;
    P1IFG &= ~BIT1;

    // enable the internal resistors
    P1REN |= BIT3;
    P2REN |= (BIT7 | BIT6 | BIT2);
    P3REN |= (BIT7 | BIT6 | BIT3 | BIT0);

    // set the resistors to pull-ups
    P1OUT |= BIT3;
    P2OUT |= (BIT7 | BIT6 | BIT2);
    P3OUT |= (BIT7 | BIT6 | BIT3 | BIT0);

    // enable the button interrupts
    P1IE |= BIT3;
    P2IE |= (BIT7 | BIT6 | BIT2);
    P3IE |= (BIT7 | BIT6 | BIT3 | BIT0);

    // interrupt on the falling edge
    P1IES |= BIT3;
    P2IES |= (BIT7 | BIT6 | BIT2);
    P3IES |= (BIT7 | BIT6 | BIT3 | BIT0);

    // clear the interrupt flags
    P1IFG &= ~BIT3;
    P2IFG &= (~BIT7 & ~BIT6 & ~BIT2);
    P3IFG &= (~BIT7 & ~BIT6 & ~BIT3 & ~BIT0);

    // set columns as output
    P1DIR |= (BIT7 | BIT6 | BIT5);
    P2DIR |= (BIT5 | BIT4 | BIT1);
    P4DIR |= BIT7;
    P9DIR |= BIT4;

    // set columns low
    P1OUT &= (~BIT7 & ~BIT6 & ~BIT5);
    P2OUT &= (~BIT5 & ~BIT4 & ~BIT1);
    P4OUT &= ~BIT7;
    P9OUT &= ~BIT4;
}

/**
 * main.c
 */
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;   // stop watchdog timer
    PM5CTL0 &= ~LOCKLPM5; // Disable GPIO power-on default high impedance mode


    shiftRegisterSetup();
    setupUART();
    setupButtons();
    setupTimerA0();
    setupTimerA2();


    _enable_interrupts();
    __low_power_mode_4();

    // Main loop handles LED switching
    volatile int i;
    volatile int j;
    for(j=0;;j++)
    {
        if (newLedPair && !turnOff)
        {
            newLedPair = 0;
            ledInput[0] -= UART_OFFSET;
            ledInput[1] -= UART_OFFSET;
            setLeds(ledInput);
        }
        if((TA0CTL & BIT0) && !turnOff)
        {
            TA0CTL &= ~BIT0;
            switchLeds();
        }
        if (turnOff)
        {
            shiftRegisterSetup();
            __low_power_mode_4();
        }
    }
    return 0;
}

// timer A0 interrupt
// switches currently displayed coordinate pair
// due to the frequency of switching and the relatively long time it takes to run the switchLeds() function, this was moved to the main loop
// timer A0 interrupts are disabled
#pragma vector = TIMER0_A1_VECTOR
__interrupt void TA0_ISR()
{
    switchLeds();

    // clear interrupt flag
    TA0CTL &= ~BIT0;
}

// timer A2 interrupt
// used to re-enable buttons
// buttons are temporarily disabled after being pressed for button debouncing
#pragma vector = TIMER2_A1_VECTOR
__interrupt void TA2_ISR()
{
    // stop TA2
    TA2CTL &= ~BIT4;
    TA2CTL &= ~BIT5;

    // clear TA2 interrupt flag
    TA2CTL &= ~BIT0;

    // disable TA2 interrupts
    TA2CTL &= ~BIT1;

    // clear the interrupt flags
    P1IFG &= ~BIT3;
    P2IFG &= (~BIT7 & ~BIT6 & ~BIT2);
    P3IFG &= (~BIT7 & ~BIT6 & ~BIT3 & ~BIT0);

    // enable the button interrupts
    P1IE |= BIT3;
    P2IE |= (BIT7 | BIT6 | BIT2);
    P3IE |= (BIT7 | BIT6 | BIT3 | BIT0);
}


// UART interrupt
#pragma vector = USCI_A1_VECTOR
__interrupt void UART_ISR()
{

    if(UCA1IFG & UCTXIFG) // if the tx flag is raised
    {
        if(coordTxIndx < 2 && transmitting)
        {
            if(txAck)
            {
                UCA1TXBUF = coordsToSend[coordTxIndx];
                coordTxIndx++;
            }
            else
            {
                UCA1TXBUF = 's'; // s for start
            }
        }
        else if (coordTxIndx > 2)
        {
            transmitting = 0;
            txAck = 0;
        }
        UCA1IFG &= ~UCTXIFG ;
    }
    if(UCA1IFG & UCRXIFG) // if the rx flag is raised
    {
        UCA1IFG &= ~UCRXIFG;
        unsigned char read = UCA1RXBUF;
        read &= ~BIT7;
        if(read == 'r') // r for repeat
        {
            // this is the block of code that will begin to send the currently stored coordinates
            coordTxIndx = 1;
            coordRxIndx = 0;
            transmitting = 1;
            txAck = 1;
            UCA1TXBUF = coordsToSend[0];
        }
        else if (read == 'a') // a for acknowledge
        {
            txAck = 1;
        }
        else if (read == 'm') // m for move, indicates that two LED locations will follow
        {
            turnOff = 0;
            receivingMove = 1;
        }
        else if (read == 'o') // o for off, puts the board into a low power state
        {
            turnOff = 1;
            coordRxIndx = 0;
        }
        else if (read == 'd') // d for draw, slows down LED switching to once per second to indicate that the opponent offers a draw
        {
            TA0CCR0 = 0;
            TA0CCR0 = 4096;
        }
        else if (read == 'i') // i for invalid move, slows down LED switching to four times per second to indicate that the previously entered move was invalid
        {
            TA0CCR0 = 0;
            TA0CCR0 = 1024;
        }
        else if (receivingMove)
        {
            ledInput[ledRxIndx] = read;
            if(ledRxIndx) // if ledRxIndx == 1
            {
                ledRxIndx = 0;
                receivingMove = 0;
                newLedPair = 1;
            }
            else
            {
                ledRxIndx++;
            }
        }
    }
}

// the Portx_ISR functions below handle input from the button matrix
// the corresponding rows are commented at the top of each ISR
#pragma vector = PORT1_VECTOR
__interrupt void Port1_ISR()
{
    /* inputs
     * row 2 = P1.3
     */

    P1IE &= ~BIT3; // disable button interrupt

    P1IFG &= ~BIT3; // clear the interrupt flag
    coordsToSend[coordRxIndx] = 8+UART_OFFSET;

    // check column a
    P2OUT |= BIT1;
    if(P1IN & BIT3)
    {
        coordsToSend[coordRxIndx] += 0;
    }
    P2OUT &= ~BIT1;

    // check column b
    P1OUT |= BIT5;
    if(P1IN & BIT3)
    {
        coordsToSend[coordRxIndx] += 1;
    }
    P1OUT &= ~BIT5;

    // check column c
    P9OUT |= BIT4;
    if(P1IN & BIT3)
    {
        coordsToSend[coordRxIndx] += 2;
    }
    P9OUT &= ~BIT4;

    // check column d
    P1OUT |= BIT6;
    if(P1IN & BIT3)
    {
        coordsToSend[coordRxIndx] += 3;
    }
    P1OUT &= ~BIT6;

    // check column e
    P1OUT |= BIT7;
    if(P1IN & BIT3)
    {
        coordsToSend[coordRxIndx] += 4;
    }
    P1OUT &= ~BIT7;

    // check column f
    P2OUT |= BIT5;
    if(P1IN & BIT3)
    {
        coordsToSend[coordRxIndx] += 5;
    }
    P2OUT &= ~BIT5;

    // check column g
    P2OUT |= BIT4;
    if(P1IN & BIT3)
    {
        coordsToSend[coordRxIndx] += 6;
    }
    P2OUT &= ~BIT4;

    // check column h
    P4OUT |= BIT7;
    if(P1IN & BIT3)
    {
        coordsToSend[coordRxIndx] += 7;
    }
    P4OUT &= ~BIT7;

    if (coordRxIndx)
    {
        // reset the rx index and start the transmission
        coordRxIndx = 0;
        coordTxIndx = 0;
        transmitting = 1;
        txAck = 0;
        UCA1TXBUF = 's';
    }
    else
    {
        coordRxIndx = 1;
    }

    // clear TA2 interrupt flag
    TA2CTL &= ~BIT0;

    // TA2 set to count up to TA0CCR0, is started
    TA2CTL |= BIT4;
    TA2CTL &= ~BIT5;

    // enable TA2 interrupts
    TA2CTL |= BIT1;

    // reset led switch timer
    TA0CCR0 = 0;
    TA0CCR0 = 32;
    turnOff = 0;

    __low_power_mode_off_on_exit();
}

#pragma vector = PORT2_VECTOR
__interrupt void Port2_ISR()
{
    /* inputs
     * row 3 = P2.2
     * row 7 = P2.6
     * row 8 = P2.7
     */

    if(P2IFG & BIT2)
    {
        P2IFG &= ~BIT2; // clear the interrupt flag
        P2IE &= ~BIT2; // disable button interrupt
        coordsToSend[coordRxIndx] = 16+UART_OFFSET;

        // check column a
        P2OUT |= BIT1;
        if(P2IN & BIT2)
        {
            coordsToSend[coordRxIndx] += 0;
        }
        P2OUT &= ~BIT1;

        // check column b
        P1OUT |= BIT5;
        if(P2IN & BIT2)
        {
            coordsToSend[coordRxIndx] += 1;
        }
        P1OUT &= ~BIT5;

        // check column c
        P9OUT |= BIT4;
        if(P2IN & BIT2)
        {
            coordsToSend[coordRxIndx] += 2;
        }
        P9OUT &= ~BIT4;

        // check column d
        P1OUT |= BIT6;
        if(P2IN & BIT2)
        {
            coordsToSend[coordRxIndx] += 3;
        }
        P1OUT &= ~BIT6;

        // check column e
        P1OUT |= BIT7;
        if(P2IN & BIT2)
        {
            coordsToSend[coordRxIndx] += 4;
        }
        P1OUT &= ~BIT7;

        // check column f
        P2OUT |= BIT5;
        if(P2IN & BIT2)
        {
            coordsToSend[coordRxIndx] += 5;
        }
        P2OUT &= ~BIT5;

        // check column g
        P2OUT |= BIT4;
        if(P2IN & BIT2)
        {
            coordsToSend[coordRxIndx] += 6;
        }
        P2OUT &= ~BIT4;

        // check column h
        P4OUT |= BIT7;
        if(P2IN & BIT2)
        {
            coordsToSend[coordRxIndx] += 7;
        }
        P4OUT &= ~BIT7;

        if (coordRxIndx)
        {
            // reset the rx index and start the transmission
            coordRxIndx = 0;
            coordTxIndx = 0;
            transmitting = 1;
            txAck = 0;
            UCA1TXBUF = 's';
        }
        else
        {
            coordRxIndx = 1;
        }
    }
    else if(P2IFG & BIT6)
    {
        P2IFG &= ~BIT6; // clear the interrupt flag
        P2IE &= ~BIT6; // disable button interrupt
        coordsToSend[coordRxIndx] = 48+UART_OFFSET;

        // check column a
        P2OUT |= BIT1;
        if(P2IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 0;
        }
        P2OUT &= ~BIT1;

        // check column b
        P1OUT |= BIT5;
        if(P2IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 1;
        }
        P1OUT &= ~BIT5;

        // check column c
        P9OUT |= BIT4;
        if(P2IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 2;
        }
        P9OUT &= ~BIT4;

        // check column d
        P1OUT |= BIT6;
        if(P2IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 3;
        }
        P1OUT &= ~BIT6;

        // check column e
        P1OUT |= BIT7;
        if(P2IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 4;
        }
        P1OUT &= ~BIT7;

        // check column f
        P2OUT |= BIT5;
        if(P2IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 5;
        }
        P2OUT &= ~BIT5;

        // check column g
        P2OUT |= BIT4;
        if(P2IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 6;
        }
        P2OUT &= ~BIT4;

        // check column h
        P4OUT |= BIT7;
        if(P2IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 7;
        }
        P4OUT &= ~BIT7;

        if (coordRxIndx)
        {
            // reset the rx index and start the transmission
            coordRxIndx = 0;
            coordTxIndx = 0;
            transmitting = 1;
            txAck = 0;
            UCA1TXBUF = 's';
        }
        else
        {
            coordRxIndx = 1;
        }
    }
    else if(P2IFG & BIT7)
    {
        P2IFG &= ~BIT7; // clear the interrupt flag
        P2IE &= ~BIT7; // disable button interrupt
        coordsToSend[coordRxIndx] = 56+UART_OFFSET;

        // check column a
        P2OUT |= BIT1;
        if(P2IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 0;
        }
        P2OUT &= ~BIT1;

        // check column b
        P1OUT |= BIT5;
        if(P2IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 1;
        }
        P1OUT &= ~BIT5;

        // check column c
        P9OUT |= BIT4;
        if(P2IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 2;
        }
        P9OUT &= ~BIT4;

        // check column d
        P1OUT |= BIT6;
        if(P2IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 3;
        }
        P1OUT &= ~BIT6;

        // check column e
        P1OUT |= BIT7;
        if(P2IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 4;
        }
        P1OUT &= ~BIT7;

        // check column f
        P2OUT |= BIT5;
        if(P2IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 5;
        }
        P2OUT &= ~BIT5;

        // check column g
        P2OUT |= BIT4;
        if(P2IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 6;
        }
        P2OUT &= ~BIT4;

        // check column h
        P4OUT |= BIT7;
        if(P2IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 7;
        }
        P4OUT &= ~BIT7;

        if (coordRxIndx)
        {
            // reset the rx index and start the transmission
            coordRxIndx = 0;
            coordTxIndx = 0;
            transmitting = 1;
            txAck = 0;
            UCA1TXBUF = 's';
        }
        else
        {
            coordRxIndx = 1;
        }
    }

    // clear TA2 interrupt flag
    TA2CTL &= ~BIT0;

    // TA2 set to count up to TA0CCR0, is started
    TA2CTL |= BIT4;
    TA2CTL &= ~BIT5;

    // enable TA2 interrupts
    TA2CTL |= BIT1;

    // reset led switch timer
    TA0CCR0 = 0;
    TA0CCR0 = 32;

    turnOff = 0;

    __low_power_mode_off_on_exit();
}

#pragma vector = PORT3_VECTOR
__interrupt void Port3_ISR()
{
    /* inputs
     * row 1 = P3.0
     * row 4 = P3.7
     * row 5 = P3.6
     * row 6 = P3.3
     */

    if(P3IFG & BIT0)
    {
        P3IFG &= ~BIT0; // clear the interrupt flag
        P3IE &= ~BIT0; // disable button interrupt
        coordsToSend[coordRxIndx] = 0+UART_OFFSET;

        // check column a
        P2OUT |= BIT1;
        if(P3IN & BIT0)
        {
            coordsToSend[coordRxIndx] += 0;
        }
        P2OUT &= ~BIT1;

        // check column b
        P1OUT |= BIT5;
        if(P3IN & BIT0)
        {
            coordsToSend[coordRxIndx] += 1;
        }
        P1OUT &= ~BIT5;

        // check column c
        P9OUT |= BIT4;
        if(P3IN & BIT0)
        {
            coordsToSend[coordRxIndx] += 2;
        }
        P9OUT &= ~BIT4;

        // check column d
        P1OUT |= BIT6;
        if(P3IN & BIT0)
        {
            coordsToSend[coordRxIndx] += 3;
        }
        P1OUT &= ~BIT6;

        // check column e
        P1OUT |= BIT7;
        if(P3IN & BIT0)
        {
            coordsToSend[coordRxIndx] += 4;
        }
        P1OUT &= ~BIT7;

        // check column f
        P2OUT |= BIT5;
        if(P3IN & BIT0)
        {
            coordsToSend[coordRxIndx] += 5;
        }
        P2OUT &= ~BIT5;

        // check column g
        P2OUT |= BIT4;
        if(P3IN & BIT0)
        {
            coordsToSend[coordRxIndx] += 6;
        }
        P2OUT &= ~BIT4;

        // check column h
        P4OUT |= BIT7;
        if(P3IN & BIT0)
        {
            coordsToSend[coordRxIndx] += 7;
        }
        P4OUT &= ~BIT7;

        if (coordRxIndx)
        {
            // reset the rx index and start the transmission
            coordRxIndx = 0;
            coordTxIndx = 0;
            transmitting = 1;
            txAck = 0;
            UCA1TXBUF = 's';
        }
        else
        {
            coordRxIndx = 1;
        }
    }
    else if(P3IFG & BIT7)
    {
        P3IFG &= ~BIT7; // clear the interrupt flag
        P3IE &= ~BIT7; // disable button interrupt
        coordsToSend[coordRxIndx] = 24+UART_OFFSET;

        // check column a
        P2OUT |= BIT1;
        if(P3IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 0;
        }
        P2OUT &= ~BIT1;

        // check column b
        P1OUT |= BIT5;
        if(P3IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 1;
        }
        P1OUT &= ~BIT5;

        // check column c
        P9OUT |= BIT4;
        if(P3IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 2;
        }
        P9OUT &= ~BIT4;

        // check column d
        P1OUT |= BIT6;
        if(P3IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 3;
        }
        P1OUT &= ~BIT6;

        // check column e
        P1OUT |= BIT7;
        if(P3IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 4;
        }
        P1OUT &= ~BIT7;

        // check column f
        P2OUT |= BIT5;
        if(P3IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 5;
        }
        P2OUT &= ~BIT5;

        // check column g
        P2OUT |= BIT4;
        if(P3IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 6;
        }
        P2OUT &= ~BIT4;

        // check column h
        P4OUT |= BIT7;
        if(P3IN & BIT7)
        {
            coordsToSend[coordRxIndx] += 7;
        }
        P4OUT &= ~BIT7;

        if (coordRxIndx)
        {
            // reset the rx index and start the transmission
            coordRxIndx = 0;
            coordTxIndx = 0;
            transmitting = 1;
            txAck = 0;
            UCA1TXBUF = 's';
        }
        else
        {
            coordRxIndx = 1;
        }
    }
    else if(P3IFG & BIT6)
    {
        P3IFG &= ~BIT6; // clear the interrupt flag
        P3IE &= ~BIT6; // disable button interrupt
        coordsToSend[coordRxIndx] = 32+UART_OFFSET;

        // check column a
        P2OUT |= BIT1;
        if(P3IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 0;
        }
        P2OUT &= ~BIT1;

        // check column b
        P1OUT |= BIT5;
        if(P3IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 1;
        }
        P1OUT &= ~BIT5;

        // check column c
        P9OUT |= BIT4;
        if(P3IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 2;
        }
        P9OUT &= ~BIT4;

        // check column d
        P1OUT |= BIT6;
        if(P3IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 3;
        }
        P1OUT &= ~BIT6;

        // check column e
        P1OUT |= BIT7;
        if(P3IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 4;
        }
        P1OUT &= ~BIT7;

        // check column f
        P2OUT |= BIT5;
        if(P3IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 5;
        }
        P2OUT &= ~BIT5;

        // check column g
        P2OUT |= BIT4;
        if(P3IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 6;
        }
        P2OUT &= ~BIT4;

        // check column h
        P4OUT |= BIT7;
        if(P3IN & BIT6)
        {
            coordsToSend[coordRxIndx] += 7;
        }
        P4OUT &= ~BIT7;

        if (coordRxIndx)
        {
            // reset the rx index and start the transmission
            coordRxIndx = 0;
            coordTxIndx = 0;
            transmitting = 1;
            txAck = 0;
            UCA1TXBUF = 's';
        }
        else
        {
            coordRxIndx = 1;
        }
    }
    else if(P3IFG & BIT3)
    {
        P3IFG &= ~BIT3; // clear the interrupt flag
        P3IE &= ~BIT3; // disable button interrupt
        coordsToSend[coordRxIndx] = 40+UART_OFFSET;

        // check column a
        P2OUT |= BIT1;
        if(P3IN & BIT3)
        {
            coordsToSend[coordRxIndx] += 0;
        }
        P2OUT &= ~BIT1;

        // check column b
        P1OUT |= BIT5;
        if(P3IN & BIT3)
        {
            coordsToSend[coordRxIndx] += 1;
        }
        P1OUT &= ~BIT5;

        // check column c
        P9OUT |= BIT4;
        if(P3IN & BIT3)
        {
            coordsToSend[coordRxIndx] += 2;
        }
        P9OUT &= ~BIT4;

        // check column d
        P1OUT |= BIT6;
        if(P3IN & BIT3)
        {
            coordsToSend[coordRxIndx] += 3;
        }
        P1OUT &= ~BIT6;

        // check column e
        P1OUT |= BIT7;
        if(P3IN & BIT3)
        {
            coordsToSend[coordRxIndx] += 4;
        }
        P1OUT &= ~BIT7;

        // check column f
        P2OUT |= BIT5;
        if(P3IN & BIT3)
        {
            coordsToSend[coordRxIndx] += 5;
        }
        P2OUT &= ~BIT5;

        // check column g
        P2OUT |= BIT4;
        if(P3IN & BIT3)
        {
            coordsToSend[coordRxIndx] += 6;
        }
        P2OUT &= ~BIT4;

        // check column h
        P4OUT |= BIT7;
        if(P3IN & BIT3)
        {
            coordsToSend[coordRxIndx] += 7;
        }
        P4OUT &= ~BIT7;

        if (coordRxIndx)
        {
            // reset the rx index and start the transmission
            coordRxIndx = 0;
            coordTxIndx = 0;
            transmitting = 1;
            txAck = 0;
            UCA1TXBUF = 's';
        }
        else
        {
            coordRxIndx = 1;
        }
    }

    // clear TA2 interrupt flag
    TA2CTL &= ~BIT0;

    // TA2 set to count up to TA0CCR0, is started
    TA2CTL |= BIT4;
    TA2CTL &= ~BIT5;

    // enable TA2 interrupts
    TA2CTL |= BIT1;

    // reset led switch timer
    TA0CCR0 = 0;
    TA0CCR0 = 32;

    turnOff = 0;

    __low_power_mode_off_on_exit();
}
