#include <main.h>
#include <string.h>

#fuses NOWDT, MCLR, NOLVP, HS
#use delay(clock=20000000)

#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7, stream=ESP8266_UART, errors)

#define wps_blink_delay 500
#define VREF 5.0

// ESP8266-01 Default AT responses
char target_wps_str[] = "OK";
char error_wps_str[]  = "ERROR";    

// WPS Related Global Variables
volatile int1 wps_mode = 0;

// RDA variables
char rx_buffer[32]; 
volatile int8 rx_index = 0;

// Measurement variables
volatile int8 overflow_count = 0;
volatile int1 trigger_send = 0;
volatile double temperature = 0.0;
char payload[16];
int len = 0;

void setup(){
   // Timer0 configuration
   setup_timer_0(T0_INTERNAL | T0_DIV_256);
   set_timer0(63583);

   // Interrupt setup
   enable_interrupts(INT_TIMER0);
   enable_interrupts(INT_EXT1);
   enable_interrupts(INT_RDA);
   enable_interrupts(GLOBAL);
   
   ext_int_edge(1, L_TO_H);
   clear_interrupt(INT_EXT1);
   
   // ADC configuration
   setup_adc_ports(AN0); 
   setup_adc(ADC_CLOCK_INTERNAL); 
   set_adc_channel(0); 
}

void main()
{
   setup();
   
   while(TRUE){
      while(wps_mode){
         output_toggle(PIN_D0);
         delay_ms(wps_blink_delay);
      }
      
      if (trigger_send) {
         trigger_send = 0; // Clear flag

         // 1. Acquire ADC Reading
         delay_us(20); // Acquisition time
         temperature = (read_adc() * (VREF / 1023.0)) * 100.0;
         
         // Format as float string
         len = sprintf(payload, "%.1f\r\n", temperature);

         // 2. Open TCP Connection
         fprintf(ESP8266_UART, "AT+CIPSTART=\"TCP\",\"192.168.1.101\",8080\r\n");
         delay_ms(150); 

         // 3. Declare length and transmit
         fprintf(ESP8266_UART, "AT+CIPSEND=%d\r\n", len);
         delay_ms(50); 
         fprintf(ESP8266_UART, "%s", payload);
      }
   }
}

#INT_EXT1
void ext1_isr(){
   wps_mode = 1;
   fprintf(ESP8266_UART, "AT+CWMODE=1\r\n");
   delay_ms(50);
   fprintf(ESP8266_UART, "AT+WPS=1\r\n");
   clear_interrupt(INT_EXT1);
}

#INT_TIMER0
void timer0_isr(void) {
   set_timer0(63583); // Always reload for consistent 100ms ticks
   
   overflow_count++;
   // 30 overflows * 100ms = 3.0 seconds
   if (overflow_count >= 30) {
      overflow_count = 0;
      trigger_send = 1; 
      output_high(PIN_D1);   
   } else {
      output_low(PIN_D1);
   }
}

#INT_RDA
void serial_isr() {
   char c = fgetc(ESP8266_UART);
   
   if (c == '\r' || c == '\n') {
      rx_buffer[rx_index] = '\0';
      
      if(wps_mode){
         if (strstr(rx_buffer, target_wps_str) != NULL) {
            output_high(PIN_D0);
            wps_mode = 0;
         } else if (strstr(rx_buffer, error_wps_str) != NULL) {
            output_low(PIN_D0);
            wps_mode = 0;
         }
      }
      rx_index = 0;
   } else if (rx_index < 31){
      rx_buffer[rx_index++] = c;
   }
}
