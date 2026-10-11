#include <main-proteus.h>
#include <string.h>
#fuses NOWDT, MCLR, NOLVP, HS
#use delay(clock=20000000)

// -------------------------------------------------------------------------
// Device Static Parameters
// -------------------------------------------------------------------------
#define wps_blink_delay 500 // Delay in milliseconds for WPS mode LED blinking
#define VREF 5.0            // Reference voltage for ADC conversion

//  -------------------------------------------------------------------------
//  WiFi module ESP8266 communication parameters and AT commands/responses
//  -------------------------------------------------------------------------
#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7, stream=ESP8266_UART, errors)
char target_wps_str[] = "OK";       // ESP8266-01 WPS success response
char error_wps_str[]  = "ERROR";    // ESP8266-01 WPS error response
char rx_buffer[32];                 // Buffer to store received data from ESP8266_UART
volatile int8 rx_index = 0;         // Index for the rx_buffer

// -------------------------------------------------------------------------
// Device Global Variables
// -------------------------------------------------------------------------
volatile int1 wps_mode = 0;         // If true, the device is in WPS mode and will blink the LED on PIN_D0
volatile int1 wps_success = 0;      // Flag to indicate if WPS was successful

// -------------------------------------------------------------------------
// Device Measurement Variables
// -------------------------------------------------------------------------
volatile int8 overflow_count = 0;   // Counts the number of Timer0 overflows to trigger ADC measurement
volatile int1 trigger_send = 0;     // Flag to indicate that it's time to send the measurement data
volatile double temperature = 0.0;  // Variable to store the temperature reading from the ADC
char payload[16];                   // Buffer to store the formatted temperature string
int len = 0;

// -------------------------------------------------------------------------
// Function Declarations
// -------------------------------------------------------------------------
void measure_and_send(); // Function to perform ADC measurement and send data over TCP

// -------------------------------------------------------------------------
// Setup Routine and Main Loop
// -------------------------------------------------------------------------

void setup(){
    // Timer0 setup for measurement triggering
    setup_timer_0(T0_INTERNAL | T0_DIV_256);
    set_timer0(63583);
    
    //Interrupt setup
    enable_interrupts(INT_TIMER0);
    enable_interrupts(INT_EXT1);
    ext_int_edge(1, L_TO_H);
    clear_interrupt(INT_EXT1);
    enable_interrupts(INT_RDA);
    
    //Enable global interrupts
    enable_interrupts(GLOBAL);

    // ADC configuration
    setup_adc_ports(AN0); 
    setup_adc(ADC_CLOCK_INTERNAL); 
    set_adc_channel(0); 

    // Test transmission to ESP8266
    fprintf(ESP8266_UART, "Terminal connected successfully!\r\n");
    
    // Set PORTD as output for LED
    set_tris_d(0x00); 
}

void main(){
    setup();
    //TODO: Add adc measurements
    while(TRUE){
        while(wps_mode){
            output_toggle(PIN_D0);
            delay_ms(wps_blink_delay);
        }

        if (trigger_send) {
            trigger_send = 0; // Clear flag
            measure_and_send(); // Perform measurement and send data
      }
    }
}

// -------------------------------------------------------------------------
// Interrupt Service Routines
// -------------------------------------------------------------------------
#INT_EXT1
void ext1_isr(){
    wps_mode = 1;                               //WPS mode is triggered
    fprintf(ESP8266_UART, "AT+CWMODE=1\r\n");   //Set ESP8266 to Station mode    
    fprintf(ESP8266_UART, "AT+WPS=1\r\n");      //Trigger WPS mode on ESP8266                 
    clear_interrupt(INT_EXT1);                  //Clear the interrupt flag for INT_EXT1
}

#INT_TIMER0
void timer0_isr(void) {
   set_timer0(63583); // Always reload for consistent 100ms ticks
   
   overflow_count++;
   // 30 overflows * 100ms = 3.0 seconds
   if (overflow_count >= 30) {
      overflow_count = 0;
      if(wps_success && !wps_mode) {
         trigger_send = 1; // Set flag to send measurement data
         output_high(PIN_D1);
      }         
   } else {
      output_low(PIN_D1);
   }
}

#INT_RDA
void serial_isr() {
    char c = fgetc(ESP8266_UART);

    if(c == '\r' || c == '\n') {
        rx_buffer[rx_index] = '\0'; // Null-terminate the string

        if(wps_mode) {
            if (strstr(rx_buffer, target_wps_str) != NULL) {
                output_high(PIN_D0); // WPS success, turn on LED
                wps_mode = 0;         // Exit WPS mode
                wps_success = 1;      // Mark WPS as successful
            } else if (strstr(rx_buffer, error_wps_str) != NULL) {
                output_low(PIN_D0);  // WPS error, turn off LED
                wps_success = 0;     // Mark WPS as failed   
                wps_mode = 0;        // Exit WPS mode
            }
        }
        rx_index = 0; // Reset index for next message
    } else if (rx_index < sizeof(rx_buffer) - 1) {
        rx_buffer[rx_index++] = c; // Store character in buffer
    }
}

// -------------------------------------------------------------------------
// Utility Functions (Measurements, etc.)
// -------------------------------------------------------------------------
void measure_and_send() {
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