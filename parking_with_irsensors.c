/* Smart Parking - single slot + parking timer on 7-segment (no LEDs)
 * Board: ALS-SDA-ARMCTXM3-01 (LPC1768)
 *
 * ===== Connections =====
 * CND -> CNAD (FRC)   : LCD   D4-D7 = P0.23-P0.26, RS = P0.27, EN = P0.28
 * CNA -> CNA2 (FRC)   : 7-seg segment data a..g,dp = P0.4-P0.11
 * CNB -> CNB2 (FRC)   : 7-seg digit select (74LS145 BCD input) = P1.23-P1.26
 * Sensor OUT          : CNC pin 1 = P0.15 (jumper wire)
 * Sensor VCC / GND    : TP2 (3.3 V) / TP3 (GND)
 * Jumpers             : JP16 and JP5 shorted, POT3 = LCD contrast
 */
#include <LPC17xx.h>

/* LCD pins (Port 0) */
#define RS_PIN    (1u << 27)
#define EN_PIN    (1u << 28)
#define LCD_SHIFT 23
#define LCD_DATA  (0xFu << LCD_SHIFT)       /* D4-D7 on P0.23-P0.26 */

/* Sensor pin (Port 0) */
#define SENSOR_1  (1u << 15)                /* P0.15, active LOW = vehicle present */

/* ---------- NEW: 7-segment pins (Port 0) ---------- */
#define SEG_SHIFT    4
#define SEG_DATA     (0xFFu << SEG_SHIFT)   /* P0.4-P0.11 via CNA -> CNA2 */
#define SEL_SHIFT    23
#define SEL_MASK     (0xFu << SEL_SHIFT)    /* P1.23-P1.26 via CNB -> CNB2 (Port 1) */
#define SEG_REVERSED 1                      /* set to 0 if digits appear in reverse order */

/* ---------- NEW: 7-segment data ---------- */
/* bit order: dp g f e d c b a  (same table as Lab 8) */
const unsigned char seg_digit[10] =
    { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
#define SEG_DASH  0x40
#define SEG_DP    0x80

volatile unsigned char seg_buf[4] = { SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH };
volatile unsigned int  ms_ticks = 0;        /* counts milliseconds since boot */

/* Parking fee settings: Rs. 10 per started minute.
 * Example: 1-60 seconds = Rs. 10, 61-120 seconds = Rs. 20.
 * Change this value if your project uses a different rate. */
#define FEE_PER_MINUTE 10u
#define FEE_DISPLAY_MS 5000u

void delay_ms(unsigned int ms)
{
    volatile unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 20000; j++);        /* approx. 1 ms at 100 MHz */
}

/* ---------- NEW: Timer0 interrupt, every 1 ms ----------
 * Lights one digit per interrupt, so each of the 4 digits refreshes
 * at 250 Hz (no flicker). Also counts milliseconds for the timer.
 * It keeps running even while main() is inside delay_ms().          */
void TIMER0_IRQHandler(void)
{
    static unsigned int digit = 0;
    unsigned int sel;

    LPC_TIM0->IR = 1;                       /* clear the MR0 interrupt flag */
    ms_ticks++;

    LPC_GPIO0->FIOCLR = SEG_DATA;           /* blank first, avoids ghosting */
    sel = SEG_REVERSED ? (3u - digit) : digit;
    LPC_GPIO1->FIOCLR = SEL_MASK;
    LPC_GPIO1->FIOSET = sel << SEL_SHIFT;   /* BCD 0-3 -> decoder enables one digit */
    LPC_GPIO0->FIOSET = (unsigned int)seg_buf[digit] << SEG_SHIFT;

    digit = (digit + 1) & 3;
}

void seg_init(void)
{
    LPC_PINCON->PINSEL0 &= 0xFF0000FF;      /* P0.4-P0.11 as GPIO  */
    LPC_PINCON->PINSEL3 &= ~(0xFFu << 14);  /* P1.23-P1.26 as GPIO */
    LPC_GPIO0->FIODIR   |= SEG_DATA;
    LPC_GPIO0->FIOCLR    = SEG_DATA;
    LPC_GPIO1->FIODIR   |= SEL_MASK;
    LPC_GPIO1->FIOCLR    = SEL_MASK;

    LPC_TIM0->TCR = 0x02;                   /* reset timer */
    LPC_TIM0->PR  = 0;
    LPC_TIM0->MR0 = (SystemCoreClock / 4) / 1000 - 1;   /* PCLK = CCLK/4 -> 1 ms */
    LPC_TIM0->MCR = 0x03;                   /* interrupt + reset on MR0 match */
    LPC_TIM0->TCR = 0x01;                   /* start timer */
    NVIC_EnableIRQ(TIMER0_IRQn);
}

/* Show elapsed seconds as MM.SS (caps at 99.59) */
void seg_show_time(unsigned int seconds)
{
    unsigned int mm = seconds / 60, ss = seconds % 60;
    if (mm > 99) { mm = 99; ss = 59; }
    seg_buf[0] = seg_digit[mm / 10];
    seg_buf[1] = seg_digit[mm % 10] | SEG_DP;   /* decimal point = separator */
    seg_buf[2] = seg_digit[ss / 10];
    seg_buf[3] = seg_digit[ss % 10];
}

void seg_show_dashes(void)
{
    seg_buf[0] = seg_buf[1] = seg_buf[2] = seg_buf[3] = SEG_DASH;
}

/* ---------- LCD (unchanged) ---------- */
void lcd_latch(void)
{
    LPC_GPIO0->FIOSET = EN_PIN;
    delay_ms(1);
    LPC_GPIO0->FIOCLR = EN_PIN;
    delay_ms(1);
}

void lcd_write_4bit(unsigned char nibble)
{
    LPC_GPIO0->FIOCLR = LCD_DATA;
    LPC_GPIO0->FIOSET = ((unsigned int)(nibble & 0x0F) << LCD_SHIFT);
    lcd_latch();
}

void lcd_cmd(unsigned char cmd)
{
    LPC_GPIO0->FIOCLR = RS_PIN;
    lcd_write_4bit(cmd >> 4);
    lcd_write_4bit(cmd & 0x0F);
    if (cmd < 0x04) delay_ms(2);
}

void lcd_data(unsigned char data)
{
    LPC_GPIO0->FIOSET = RS_PIN;
    lcd_write_4bit(data >> 4);
    lcd_write_4bit(data & 0x0F);
}

void lcd_print(const char *str)
{
    while (*str) lcd_data((unsigned char)*str++);
}

void lcd_init(void)
{
    LPC_PINCON->PINSEL1 &= ~(0xFFFu << 14); /* P0.23-P0.28 as GPIO */
    LPC_GPIO0->FIODIR   |= (RS_PIN | EN_PIN | LCD_DATA);
    LPC_GPIO0->FIOCLR    = (RS_PIN | EN_PIN | LCD_DATA);

    delay_ms(20);
    lcd_write_4bit(0x03);  delay_ms(5);
    lcd_write_4bit(0x03);  delay_ms(1);
    lcd_write_4bit(0x03);
    lcd_write_4bit(0x02);

    lcd_cmd(0x28);
    lcd_cmd(0x0C);
    lcd_cmd(0x06);
    lcd_cmd(0x01);
}

int slot_occupied(void)
{
    int a, b;
    do {
        a = ((LPC_GPIO0->FIOPIN & SENSOR_1) == 0);
        delay_ms(20);
        b = ((LPC_GPIO0->FIOPIN & SENSOR_1) == 0);
    } while (a != b);
    return a;
}

int main(void)
{
    int occupied, last_state = -1;
    unsigned int park_start = 0;
    unsigned int parked_seconds = 0;
    unsigned int parked_minutes = 0;
    unsigned int parking_fee = 0;
    unsigned int fee_until = 0;
    int show_fee = 0;

    SystemInit();
    SystemCoreClockUpdate();

    LPC_PINCON->PINSEL0 &= ~(3u << 30);     /* P0.15 sensor as GPIO input */
    LPC_GPIO0->FIODIR   &= ~SENSOR_1;

    seg_init();                             /* Existing seven-segment + Timer0 */
    lcd_init();

    /* Start in the normal empty-slot state; don't charge on power-up. */
    last_state = 0;
    lcd_print("SLOTS AVAILABLE");
    lcd_cmd(0xC0);
    lcd_print("SLOTS: 1/1 FREE");
    seg_show_dashes();

    while (1) {
        occupied = slot_occupied();

        if (occupied != last_state) {
            last_state = occupied;

            if (occupied) {
                /* A new parking session starts; cancel any old fee screen. */
                show_fee = 0;
                park_start = ms_ticks;

                lcd_cmd(0x01);
                lcd_print("PARKING FULL");
                lcd_cmd(0xC0);
                lcd_print("SLOTS: 0/1 FREE");
            } else {
                /* Object has just left: calculate the charge from elapsed time. */
                parked_seconds = (ms_ticks - park_start) / 1000u;
                parked_minutes = (parked_seconds + 59u) / 60u; /* Round up */
                parking_fee = parked_minutes * FEE_PER_MINUTE;

                seg_show_dashes();

                /* Show fee for 5 seconds, using the existing 1 ms timer tick. */
                show_fee = 1;
                fee_until = ms_ticks + FEE_DISPLAY_MS;

                lcd_cmd(0x01);
                lcd_print("PARKING CHARGE");
                lcd_cmd(0xC0);
                lcd_print("Fee: Rs ");
                {
                    char digits[10];
                    unsigned int value = parking_fee;
                    int i = 0;
                    if (value == 0) {
                        lcd_data('0');
                    } else {
                        while (value > 0 && i < 10) {
                            digits[i++] = (char)('0' + (value % 10u));
                            value /= 10u;
                        }
                        while (i > 0) lcd_data((unsigned char)digits[--i]);
                    }
                }
            }
        }

        if (occupied) {
            /* Continue showing elapsed parking time on the seven-segment display. */
            seg_show_time((ms_ticks - park_start) / 1000u);
        } else if (show_fee) {
            /* Signed difference handles the millisecond counter wrapping around. */
            if ((int)(ms_ticks - fee_until) >= 0) {
                show_fee = 0;
                lcd_cmd(0x01);
                lcd_print("SLOTS AVAILABLE");
                lcd_cmd(0xC0);
                lcd_print("SLOTS: 1/1 FREE");
            }
        } else {
            /* Normal idle screen after the 5-second fee display. */
            /* It is already shown; no repeated LCD writes are needed. */
        }

        delay_ms(100);
    }
}
