
#define SDA_PIN 4      // PC4
#define SCL_PIN 5      // PC5

// ---- assembly delay: 5 us at 16 MHz = 80 cycles ----
static inline void delay_5us(void) {
  asm volatile(
    "ldi r24, 26   \n\t"   // load loop counter            (1 cycle)
    "1: dec r24    \n\t"   // counter - 1                  (1 cycle)
    "brne 1b       \n\t"   // repeat until zero            (2 cycles if taken, 1 if not)
    "nop           \n\t"   // padding                      (1 cycle)
    "nop           \n\t"   // padding                      (1 cycle)
    :
    :
    : "r24"
  );
}

// ---- gpio layer (open-drain) ----
void sda_high() { DDRC &= ~(1 << SDA_PIN); }
void sda_low()  { PORTC &= ~(1 << SDA_PIN); DDRC |= (1 << SDA_PIN); }
void scl_high() { DDRC &= ~(1 << SCL_PIN); }
void scl_low()  { PORTC &= ~(1 << SCL_PIN); DDRC |= (1 << SCL_PIN); }

// ---- i2c layer ----
void i2c_start() {
  sda_high(); scl_high(); delay_5us();
  sda_low();  delay_5us();
  scl_low();  delay_5us();
}

void i2c_stop() {
  sda_low();  delay_5us();
  scl_high(); delay_5us();
  sda_high(); delay_5us();
}

uint8_t i2c_write(uint8_t data) {          // returns 1 if ACK
  for (uint8_t i = 0; i < 8; i++) {
    if (data & 0x80) sda_high(); else sda_low();
    delay_5us();
    scl_high(); delay_5us();
    scl_low();  delay_5us();
    data <<= 1;
  }
  sda_high();
  delay_5us();
  scl_high(); delay_5us();
  uint8_t ack = !((PINC >> SDA_PIN) & 1);
  scl_low();  delay_5us();
  return ack;
}

uint8_t i2c_read(uint8_t ack) {            // ack=1: want more, ack=0: last byte
  uint8_t data = 0;
  sda_high();
  for (uint8_t i = 0; i < 8; i++) {
    scl_high(); delay_5us();
    data = (data << 1) | ((PINC >> SDA_PIN) & 1);
    scl_low();  delay_5us();
  }
  if (ack) sda_low(); else sda_high();
  delay_5us();
  scl_high(); delay_5us();
  scl_low();  delay_5us();
  sda_high();
  return data;
}

// ---- eeprom layer (24C02) ----
void ee_write(uint8_t addr, uint8_t data) {
  i2c_start();
  i2c_write(0xA0);            // device, write mode
  i2c_write(addr);            // memory address
  i2c_write(data);            // data byte
  i2c_stop();

  for (uint16_t tries = 0; tries < 1000; tries++) {   // ACK polling
    i2c_start();
    uint8_t ack = i2c_write(0xA0);
    i2c_stop();
    if (ack) break;
  }
}

uint8_t ee_read(uint8_t addr) {
  i2c_start();
  i2c_write(0xA0);            // dummy write to set the address pointer
  i2c_write(addr);
  i2c_start();                // repeated START
  i2c_write(0xA1);            // device, read mode
  uint8_t data = i2c_read(0); // NACK: last byte
  i2c_stop();
  return data;
}

// ---- lcd layer (4-bit mode) ----
// RS = PB4, E = PB3, D4 = PD5, D5 = PD4, D6 = PD3, D7 = PD2
void lcd_nibble(uint8_t n) {
  if (n & 0x01) PORTD |= (1 << 5); else PORTD &= ~(1 << 5);   // D4
  if (n & 0x02) PORTD |= (1 << 4); else PORTD &= ~(1 << 4);   // D5
  if (n & 0x04) PORTD |= (1 << 3); else PORTD &= ~(1 << 3);   // D6
  if (n & 0x08) PORTD |= (1 << 2); else PORTD &= ~(1 << 2);   // D7
  PORTB |= (1 << 3);  delayMicroseconds(2);    // E high
  PORTB &= ~(1 << 3); delayMicroseconds(60);   // E low: LCD latches here
}

void lcd_cmd(uint8_t c) {
  PORTB &= ~(1 << 4);          // RS = 0 (command)
  lcd_nibble(c >> 4);
  lcd_nibble(c & 0x0F);
  if (c <= 0x03) delay(2);     // clear/home need about 1.6 ms
}

void lcd_char(char c) {
  PORTB |= (1 << 4);           // RS = 1 (data)
  lcd_nibble(c >> 4);
  lcd_nibble(c & 0x0F);
}

void lcd_print(const char *s) {
  while (*s) lcd_char(*s++);
}

void lcd_hex(uint8_t v) {
  const char digits[] = "0123456789ABCDEF";
  lcd_char(digits[v >> 4]);
  lcd_char(digits[v & 0x0F]);
}

void lcd_goto(uint8_t row, uint8_t col) {
  lcd_cmd(0x80 | (row ? 0x40 : 0x00) | col);
}

void lcd_init() {
  DDRD |= 0b00111100;                    // PD2-PD5 outputs
  DDRB |= (1 << 3) | (1 << 4);           // PB3 (E), PB4 (RS) outputs
  delay(50);                             // LCD power-up time
  PORTB &= ~(1 << 4);                    // RS = 0
  lcd_nibble(0x03); delay(5);            // wake-up sequence
  lcd_nibble(0x03); delayMicroseconds(150);
  lcd_nibble(0x03);
  lcd_nibble(0x02);                      // switch to 4-bit mode
  lcd_cmd(0x28);                         // 4-bit, 2 lines, 5x8 font
  lcd_cmd(0x0C);                         // display on, cursor off
  lcd_cmd(0x06);                         // cursor moves right
  lcd_cmd(0x01);                         // clear display
}

// ---- main ----
void setup() {
  sda_high();
  scl_high();
  DDRB |= (1 << 5);                      // status LED
  lcd_init();

  uint8_t w = 0xA5;
  ee_write(0x10, w);
  uint8_t r = ee_read(0x10);

  lcd_goto(0, 0);
  lcd_print("W:"); lcd_hex(w);
  lcd_print(" R:"); lcd_hex(r);

  lcd_goto(1, 0);
  if (r == w) { lcd_print("EEPROM OK");    PORTB |= (1 << 5); }
  else        { lcd_print("EEPROM ERROR"); PORTB &= ~(1 << 5); }
}

void loop() {
}