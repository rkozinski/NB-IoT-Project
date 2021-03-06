/*
 * NB_IOT_MEGA324PA.c
 *
 * Created: 09/07/2017 19:57:45
 * Author : jan.rune.herheim
 */ 

#include <avr/io.h>
/**
 /*
 */
//#include "main.h"
/*
 * main.h
 *
 * Created: 28/06/2017 17:09:19
 *  Author: jan.rune.herheim
 */ 

#include "main.h"

/*
	TODO:
	- HW data flow
	*/


/*
Data shared between the ISR and your main program must be both volatile and global in scope in the C language. 
Without the volatile keyword, the compiler may optimize out accesses to a variable you update in an ISR,
as the C language itself has no concept of different execution threads. Take the following example:
*/

//MOVE!!!!
#define USART_SERIAL_SIM900				&USART0 //
#define USART_SERIAL_EXAMPLE			&USART0 //
#define  USART_EXT_DATA					&USART0


//DFEINITIOINS OF CONTROLLER STATES. THESE WILL MATCH WITH VISIO FLOW CHART.
typedef enum {
	READ_EXT_DATA,
	MEASURE,
	CALC,
	STORE_EXT_MEM,
	RF_POWER_ON,
	RF_CONNECT,
	GENERATE_PACKAGE,
	TX_DATA,
	RX_DATA,
	RF_DISCONNECT,
	RF_POWER_OFF,
	RESET_REGISTERS
} controller_states_t;

//Initial states.
controller_states_t controller_state = READ_EXT_DATA; //CHECK IN FINAL.
controller_states_t controller_next_state = READ_EXT_DATA; //
//////////////////////////////////////////////////////////////////////////


/*DECLARATION OF WAKEUP AND TRANSFER RATES IN SECONDS
Default settings are 5s wakeup rate and 600s (10 minutes) transfer rate.
*/
#define WAKEUP_RATE SAMPLING_TIME //5 //Use the value defined for the load cell in this case.
#define TRANSMIT_RATE AVERAGING_TIME //600 //use the value defined for the load cell in this case.
//NEED A DEDICATED COUNTER ON MEGA TO MAKE RTC TRHOUGH THE WDT
volatile uint16_t wdt_counter = 1; //minimum at 1 second
//////////////////////////////////////////////////////////////////////////




//DEFINE DATA POSITIONS
#define POSITION_ANA0 POSITION_CURRENT //0
#define POSITION_ANA1 POSITION_PREV //1
#define POSITION_ANA2 POSITION_AVG //2
#define POSITION_ANA3 POSITION_MIN //3
#define POSITION_ANA4 POSITION_MAX //4
#define POSITION_ANA5 POSITION_TRAN_MAX //5
#define POSITION_TEMP 6
#define POSITION_VDD 7
#define POSITION_DIO 8
#define POSITION_TIME POSITION_ACCU_CNT //9
#define POSITION_YEAR 10
#define POSITION_MONTH 11
#define POSITION_DAY 12
#define POSITION_HOUR 13
#define POSITION_MINUTE 14
#define POSITION_SECOND 15
#define POSITION_STATUS 16
#define TX_DATA_SIZE 17 //sum of the above

//DEFINE DATA RESET VALUES
#define RESET_VALUE_ANA0 0
#define RESET_VALUE_ANA1 0
#define RESET_VALUE_ANA2 0
#define RESET_VALUE_ANA3 RESET_VALUE_MIN //0
#define RESET_VALUE_ANA4 0
#define RESET_VALUE_ANA5 0
#define RESET_VALUE_TEMP 0
#define RESET_VALUE_VDD 0
#define RESET_VALUE_DIO 0x0000 //all bits cleared, i.e. all ok.
#define RESET_VALUE_TIME 0
#define RESET_VALUE_YEAR 0
#define RESET_VALUE_MONTH 0
#define RESET_VALUE_DAY 0
#define RESET_VALUE_HOUR 0
#define RESET_VALUE_MINUTE 0
#define RESET_VALUE_SECOND 0
#define RESET_VALUE_STATUS 0x00 //all bits cleared, i.e. all ok.

//DEFINE STATUS BITS
#define STATUS_BIT_RF_POWER_ON 0
#define STATUS_BIT_RF_CONNECT 1
#define STATUS_BIT_TX 2

volatile uint16_t tx_data[TX_DATA_SIZE]; //declare the array for internal accumulation and storage.
volatile static uint16_t tx_data_reset_values[TX_DATA_SIZE] = {RESET_VALUE_ANA0, RESET_VALUE_ANA1, RESET_VALUE_ANA2, RESET_VALUE_ANA3,
	RESET_VALUE_ANA4, RESET_VALUE_ANA5, RESET_VALUE_TEMP, RESET_VALUE_VDD, RESET_VALUE_DIO, RESET_VALUE_TIME,
RESET_VALUE_YEAR, RESET_VALUE_MONTH, RESET_VALUE_DAY, RESET_VALUE_HOUR, RESET_VALUE_MINUTE, RESET_VALUE_SECOND, RESET_VALUE_STATUS}; //declare the reset register.
//////////////////////////////////////////////////////////////////////////

//RADIO RESPONSE ARRAY AND SIZE DEFINITIONS
#define RESPONSE_SIZE 100
volatile char response[RESPONSE_SIZE];
//////////////////////////////////////////////////////////////////////////

//DEFINITIONS OF THE EXTERNAL DATA PINS
#define REQUEST_DATA_PORT PORTD
#define REQUEST_DATA_PIN 7
//////////////////////////////////////////////////////////////////////////

//SPECIALIZED PARAMETERS FOR THE LOAD CELL APPLICATION
//local data declarations
uint32_t accu_data = 0; //allocating internal accumulation storage.
//////////////////////////////////////////////////////////////////////////

//DEFINITIONS OF STATUS AND MODES PARAMETERS
volatile uint8_t RTC_ISR_ACTIVE = 0;
//////////////////////////////////////////////////////////////////////////

//DEFINITIONS OF PINS CONNECTED TO THE RADIO
#define PWRKEY_PORT PORTD //PORTE
#define PWRKEY_PIN 2 //6
#define STATUS_PORT PIND //PORTE
#define STATUS_PIN 4 //7
//#define NETLIGHT_PORT PORTR
//#define NETLIGHT_PIN 0
//////////////////////////////////////////////////////////////////////////

//ADC definitions
#define ADC_NUM_AVG 9 //number of averages
//////////////////////////////////////////////////////////////////////////

//DEFINITIONS OF DATA FORMATS AND DATA SIZES TO BE TRANSMITTED THROUGH THE RADIO
#define TRANSFER_DATA_BASE 10 //DATA FORMAT THE DATA IS TRANSFERRED WITH. 10 = Base10 (decimal), 16 = Base16 (hex), 32 = Base32
#define TX_DATA_DIGITS 4 //NUMBER OF DATA DIGITS TO TRANSMIT: 2 or 4
#define TX_DATE_DIGITS 2 //Number of date digits to transmit: 1 or 2
#define TX_ASCII 1 //0 will transfer hex bytes, 1 will transfer ascii coded bytes: 1 or 2

#define TRANSFER_DATA_SIZE 128 //ASSUMING 128 bytes are enough.....
volatile char tx_data_bytes[TRANSFER_DATA_SIZE] = ""; //Final data to be transmitted (and stored?)
#define TRANSFER_DATA_SIZE_PACKAGE (TRANSFER_DATA_SIZE*2) //ASSUMING 2 TIMES THE DATA SIZE IS THE TOTAL PACKAGE OVERHEAD.
volatile char tx_data_package[TRANSFER_DATA_SIZE_PACKAGE] = ""; //Final data package generated from the tx_data_bytes array.
int transfer_data_length_package = 0; //ACTUAL PACKAGE SIZE TO BE TRANSMITTED. CALCULATED IN PROGRAM. KEEP AS LOW AS POSSIBLE!!!!!!
////////////////////////////////////////////////////////////////////////////////////


//Functions related to the radio communication
void usart_tx_at(USART_t *usart, uint8_t *cmd) {
	
	//send the command
	while(*cmd) {
		usart_putchar(usart, *cmd++);
	}
	
}

uint8_t usart_rx_at(USART_t *usart, uint16_t timeout, uint8_t *timeout_status)
{
	uint32_t timeout2 = timeout*1000; //300ms
	
	while ((usart_rx_is_complete(usart) == false) & (timeout2 > 0)) {
		timeout2--;
	}
	
	if (timeout2 == 0)
	{
		*timeout_status = 1;
	}

	//return ((uint8_t)(usart)->DATA); //XMEGA
	return ((uint8_t)(usart)->UDR); //MEGA
}


uint8_t at_response(USART_t *usart, uint16_t timeout, char *array_pointer) {
	
	uint8_t i = 0;
	uint8_t status = 0;
	
	while ( (status == 0) & (i < RESPONSE_SIZE) )
	{
		*(array_pointer+i) = usart_rx_at(usart, timeout, &status);
		i++;
	}
	
	return status;
}

void usart_tx_char(USART_t *usart, char *cmd) {
	
	//send the command
	while(*cmd) {
		usart_putchar(usart, *cmd++);
	}
	
}

void radio_pins_init(void) {
	
	//PWRKEY and startup sequence.
	//PWRKEY_PORT.DIR |= (1<<PWRKEY_PIN); //reset pin //XMEGA
	DDRD |= (1<<PWRKEY_PIN);
	
	
	//STATUS, NOT NEEDED AS NETLIGHT IS REQUIRED BEFORE SENDING COMMANDS.
	//STATUS_PORT.DIR &= ~(1<<STATUS_PIN); //input //XMEGA
	DDRD &= ~(1<<STATUS_PIN);
	//portctrl_setup(STATUS_PORT, STATUS_PIN); //should not be needed.
	
	//NETLIGHT. NOT AVAILABLE ON DEVELOPMENT BOARD, HAVE TO USE SW CALL TO CHECK FOR CONNECTION STATUS.
	//NETLIGHT_PORT.DIR &= ~(1<<NETLIGHT_PIN); //input
	
}

uint8_t radio_power_down(void) {
	uint8_t status = 0;
	uint8_t cnt_pwrdwn = 0;
	
	//power down
	PWRKEY_PORT &= ~(1<<PWRKEY_PIN);
	delay_s(1);
	PWRKEY_PORT |= (1<<PWRKEY_PIN); 
	delay_s(1);
	PWRKEY_PORT &= ~(1<<PWRKEY_PIN);
	
	while ( (STATUS_PORT & (1<<STATUS_PIN)) & (cnt_pwrdwn < AT_REPEAT_LONG) )
	{
		delay_ms(AT_REPEAT_DELAY);
		cnt_pwrdwn++;
	}
	
	
	if (cnt_pwrdwn == AT_REPEAT_LONG)
	{
		status = 1;
		/*
		reset_char_array(&response, RESPONSE_SIZE);
		usart_tx_at(USART_SERIAL_SIM900, AT_QPOWD); //return OK
		at_response(USART_SERIAL_SIM900, RESPONSE_TIME_300M, &response);
		usart_tx_at(USART_SERIAL_EXAMPLE, response);
		*/
	}
	
	return status;
}

uint8_t reset_tx_data(uint16_t *array, uint16_t *reset_array, uint8_t len_array) {
	uint8_t status = 0;
	uint8_t i = 0;
	
	while (i < len_array)
	{
		*(array+i) = *(reset_array+i);
		i++;
	}
	return status;
}

void reset_char_array(char *array_pointer , uint8_t size) {
	uint8_t i = 0;
	while (i < size)
	{
		*(array_pointer+i) = 0x00;
		i++;
	}
}


uint8_t reset_all_data() {
	//reset data and date
	reset_tx_data(&tx_data, &tx_data_reset_values, TX_DATA_SIZE);
	reset_char_array(&tx_data_bytes, TRANSFER_DATA_SIZE);
	reset_char_array(&tx_data_package, TRANSFER_DATA_SIZE_PACKAGE);
}

void rtc_init_period(uint16_t period)
{
	//USE WDT ON MEGA
	
	MCUSR = 0x00; //RESET STATUS REGISTER
	WDTCSR = (1<<WDCE) | (1<<WDE);
	WDTCSR = (1<<WDIE) | (1<<WDP2) | (1<<WDP1); //1 second wakeup
	
	
}

#ifdef DEBUG
void led_blink(uint16_t on_time) {
	
	PORTB |= (1<<5);
	delay_ms(on_time);
	PORTB &= ~(1<<5);
	delay_ms(on_time);
}
#endif // DEBUG

static void adc_initialization(void)
{
	PRR &= ~(1<<PRADC); //enable ADC clock
	adc_init(ADC_PRESCALER_DIV128);
	
}

uint16_t adc_result_average (uint8_t adc_ch, uint8_t num_avg) {
	
	uint8_t i = 0;
	uint32_t res = 0;
	uint16_t res_median[num_avg];
	
	adc_initialization();
	
	while (i<num_avg)
	{
		res_median[i] = adc_read_10bit(adc_ch, ADC_VREF_AVCC);
		res = res + res_median[i];
		
		if (res_median[i] > 300)
		{
			led_blink(1000);
		}
		
		i++;
		
		
	}
	
	res = res/num_avg;
	
	
	//return res_median[(num_avg-1)/2];
	return res;
}

uint8_t radio_power_on(void) {
	uint8_t status = 0;
	uint8_t cnt_pwron = 0;
	
	PWRKEY_PORT &= ~(1<<PWRKEY_PIN); //reset
	delay_ms(1); //wait for battery voltage to settle.
	PWRKEY_PORT |= (1<<PWRKEY_PIN); //reset of radio
	delay_ms(100); //boot time, 100ms recommended for m95
	PWRKEY_PORT &= ~(1<<PWRKEY_PIN);
	delay_ms(800); //time before m95 is running. There exist a status bit that might be useful to monitor.
	delay_ms(400);
	PWRKEY_PORT |= (1<<PWRKEY_PIN); //normal level for this pin
	
	//wait for status
	while ( (!(STATUS_PORT & (1<<STATUS_PIN))) & (cnt_pwron < AT_REPEAT_LONG) )
	{
		delay_ms(AT_REPEAT_DELAY);
		cnt_pwron++;
	}
	
	if (cnt_pwron == AT_REPEAT_LONG)
	{
		status = 1;
	}
	
	return status;
}


uint16_t controller_calc_avg(uint32_t data, uint16_t cnt) {
	uint16_t avg = 0;
	
	avg = data/cnt;
	return avg;
}

void at_get_radio_network_time(){
	
	int j = 0;
	int k = AT_QLTS_START;
	char temp[3] = "";
	
	while (j < 6)
	{
		int i = 0;
		while (i < 2)
		{
			temp[i] = *(response+k+i);
			i++;
		}
		tx_data[POSITION_YEAR+j] = atoi(temp);
		k = k+3;
		j++;
	}
	
}

uint8_t tx_at_response(uint8_t *cmd, char *compare, uint8_t response_time, uint8_t repeat) {
	
	uint8_t status = 0; //tx status, 0 = alles ok.
	uint8_t tx_at_cnt = 0; //nr of AT command sent
	char *ret; //response pointer
	
	ret = 0;
	while (tx_at_cnt < repeat) //9*300ms ~ 3s
	{
		reset_char_array(&response, RESPONSE_SIZE); //reset response buffer
		usart_tx_at(USART_SERIAL_SIM900, cmd); //send AT command to radio
		at_response(USART_SERIAL_SIM900, response_time, &response); //read the response from the radio
		ret = strstr(response, compare); //DO THE COMPARISON AND BREAK THE LOOP TO SAVE TIME => AVOID THE DELAY ROUTINE.
		
		if (ret != 0) //correct response received
		{
			status = 0;
			break;
			} else {
			status = 1;
		}
		
		delay_ms(AT_REPEAT_DELAY);
		tx_at_cnt++;
	}
	
	#ifdef DEBUG
	usart_tx_at(USART_SERIAL_EXAMPLE, response); //DEBUG
	#endif // DEBUG
	

	return status;
}


uint8_t at_rf_connect(void) {
	/*
	status = 0 => all AT commands was executed sucesessfully
	status = 1 => one of the AT commands was not executed sucessfully.
	status = 32 => QLTS, i.e. the network time didn't execute sucessfully.
	
	WILL MATCH FLOWCHART IN VISIO
	*/
	uint8_t status = 0;
		
	if (tx_at_response(AT_QNSTATUS, AT_QNSTATUS_COMPARE, RESPONSE_TIME_300M, AT_REPEAT_LONG)) {goto END;}
		//if (tx_at_response(AT_QNSTATUS, LOCAL_IP, 1, AT_REPEAT) == 1) {goto END;} //DEBUG
	if (tx_at_response(AT_QIFGCNT, RESPONSE_OK, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;}
	if (tx_at_response(AT_QICSGP, RESPONSE_OK, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;}
	
	if (tx_at_response(AT_QIMUX, RESPONSE_OK, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;}
	if (tx_at_response(AT_QIMODE, RESPONSE_OK, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;}
	if (tx_at_response(AT_QIDNSIP, RESPONSE_OK, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;}
	if (tx_at_response(AT_QIREGAPP, RESPONSE_OK, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;}
	if (tx_at_response(AT_QISTAT, AT_QISTAT_COMPARE_IP_START, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;} //CHECK IP STATUS
	if (tx_at_response(AT_QIACT, AT_QIACT_COMPARE, RESPONSE_TIME_20S, AT_REPEAT)) {goto END;}
	if (tx_at_response(AT_QILOCIP, LOCAL_IP, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;} //Need local IP
	if (tx_at_response(AT_QISTAT, AT_QISTAT_COMPARE_CONNECT_OK || 
								AT_QISTAT_COMPARE_IP_INITIAL ||
								AT_QISTAT_COMPARE_IP_STATUS ||
								AT_QISTAT_COMPARE_IP_CLOSE, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;} //CHECK IP STATUS
	if (tx_at_response(AT_QIOPEN, RESPONSE_OK, RESPONSE_TIME_20S, AT_REPEAT)) {status = 1; goto END;}
	if (tx_at_response(AT_QISRVC, RESPONSE_OK, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;}
	if (tx_at_response(AT_QISTAT, AT_QISTAT_COMPARE_CONNECT_OK, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;} //CHECK IP STATUS
	if (tx_at_response(AT_QLTS, AT_QLTS_COMPARE, RESPONSE_TIME_300M, AT_REPEAT)) {status = 32; goto END;} else {at_get_radio_network_time();} //get network's time
	
	END: return status;
}

uint8_t at_rf_disconnect(void) {
	/*
	status = 0 => all AT commands was executed sucesessfully
	status = 1 => one of the AT commands was not executed sucessfully.
	
	WILL MATCH FLOWCHART IN VISIO
	*/
	uint8_t status = 0;
	
	if (tx_at_response(AT_QICLOSE, RESPONSE_OK, RESPONSE_TIME_300M, AT_REPEAT)) {goto END;}
	if (tx_at_response(AT_QIDEACT, RESPONSE_OK, RESPONSE_TIME_20S, AT_REPEAT)) {goto END;}
	
	END: return status;
}

uint8_t tx(char *data, int len) {
	/*
	status = 0 => all AT commands was executed sucesessfully
	status > 0 => one of the AT commands was not executed sucessfully.
	*/
	uint8_t status = 0;
	int i = 0;
	
	if (tx_at_response(AT_QISEND, AT_QISEND_COMPARE, RESPONSE_TIME_300M, AT_REPEAT)) {status = 1; goto END;}
	while (i < len)
	{
		//usart_putchar(USART_SERIAL_SIM900, *(data+i));
		#ifdef DEBUG
		usart_putchar(USART_SERIAL_EXAMPLE, *(data+i)); //DEBUG
		#endif // DEBUG
		
		i++;
	}
	
	if (tx_at_response(CTRL_Z, NULL, RESPONSE_TIME_300M, AT_REPEAT)) {status = 2; goto END;}
	
	END: return status;
}

uint8_t data_to_char(uint16_t *array_data, uint8_t array_data_len, char *array_ascii, int base) {
	uint8_t status = 0;
	uint8_t i = 0;
	uint8_t j = 0;
	char temp[5] = ""; //MAX 4 VALUES + NULL TERMINATION
	
	//CONVERT ALL 2 BYTES NUMBERS
	while (i <= POSITION_TIME)
	{
		j=1;
		while (TX_DATA_DIGITS-j > 0)
		{
			if ((*(array_data+i) < pow(base,TX_DATA_DIGITS-j))) //CHECK IF NUMBER IS LESS THAN LIMITS
			{
				strcat(array_ascii, "0"); //ADD LEADING ZEROS
			}
			
			j++;
		}
		
		if (TX_ASCII)
		{
			itoa(*(array_data+i), temp, base); //CONVERT NUMBER TO ASCII
			} else {
			temp[0] = (*(array_data+i) >> 8) & 0xff; //JUST GRAB THE BYTES
			temp[1] = *(array_data+i) & 0xff;
		}
		
		strcat(array_ascii, temp); //APPEND NUMBER
		strcat(array_ascii, ","); //DEBUG
		reset_char_array(&temp, sizeof(temp));
		i++;
	}
	//////////////////////////////////////////////////////////////////////////
	
	//CONVERT ALL 1 BYTES NUMBERS
	i = POSITION_YEAR;
	while (i <= POSITION_STATUS)
	{
		j=1;
		while (TX_DATE_DIGITS-j > 0)
		{
			if ((*(array_data+i) < pow(base,TX_DATE_DIGITS-j))) //CHECK IF NUMBER IS LESS THAN LIMITS
			{
				strcat(array_ascii, "0"); //ADD LEADING ZEROS
			}
			j++;
		}
		
		if (TX_ASCII)
		{
			itoa(*(array_data+i), temp, base); //CONVERT NUMBER TO ASCII
			} else {
			temp[0] = *(array_data+i) & 0xff; //JUST GRAB THE BYTES
		}
		
		strcat(array_ascii, temp); //APPEND NUMBER
		strcat(array_ascii, ","); //DEBUG
		reset_char_array(&temp, sizeof(temp));
		i++;
	}
	//////////////////////////////////////////////////////////////////////////
	return status;
}

uint8_t loadcell_min_max_tran(uint16_t current_value, uint16_t *data_array) {
	//USING THE POSITIONS DEFINED IN THE LOADCELL HEADER FILE.
	
	//find tran
	signed int tran = 0; //could go positive and negative, and could store a 15 bits number, hence enough for our 12 bits results.
	uint16_t tran_abs = 0;
	
	tran = current_value - *(data_array + POSITION_PREV); //tran = current - previous
	if ((abs(tran) > abs(*(data_array+POSITION_TRAN_MAX))) & (*(data_array+POSITION_ACCU_CNT) > 0)) //first step is not valid due to only one value.
	{
		if (tran < 0)
		{
			tran_abs = abs(tran); //check if >2047, if yes this is the max limit that could be transferred.
			if (tran_abs >= 0x7ff) //if yes the set to max value
			{
				tran_abs = 0x7ff;
			}
			tran_abs |= (1<<11); //flip the MSB of the 12 bits word to set the negative sign.
			tran = tran_abs;
		}
		
		*(data_array+POSITION_TRAN_MAX) = tran; //store new tran max.
	}
	
	//find min and max
	if (current_value < *(data_array+POSITION_MIN))
	{
		*(data_array+POSITION_MIN) = current_value; //store new min value.
	}
	if (current_value > *(data_array+POSITION_MAX))
	{
		*(data_array+POSITION_MAX) = current_value; //Store new max.
	}
}


ISR(WDT_vect)
{
	
	cli();
	#ifdef DEBUG
		led_blink(50); //DEBUG
	#endif // DEBUG
	
	//MEGA SPECIFIC LONG TIME RTC FUNCTION
	wdt_counter++; //increment counter 
	if (wdt_counter < SAMPLING_TIME)
	{
		goto END; //BREAK ISR IF TRANSMIT 
	} else {wdt_counter = 0;}
	//////////////////////////////////////////////////////////////////////////
	
	RTC_ISR_ACTIVE = 1;
	while (RTC_ISR_ACTIVE == 1)
	{
		
		switch(controller_state) {
			
			case READ_EXT_DATA:
				reset_char_array(&response, RESPONSE_SIZE); //reset response buffer
				REQUEST_DATA_PORT |= (1<<REQUEST_DATA_PIN); //set signal high
				at_response(USART_EXT_DATA, RESPONSE_TIME_300M, &response); //read the response from the radio
				REQUEST_DATA_PORT &= ~(1<<REQUEST_DATA_PIN); //set signal low
				#ifdef DEBUG
				usart_tx_at(USART_SERIAL_EXAMPLE, response);
				#endif // _DEBUG
				uint16_t ext_data = (response[0] << 8) | response[1]; //convert response to bytes and store in data registers
				//This position needs to be specified for each use case dependent on available registers.
				tx_data[POSITION_DIO] = ext_data;
				//////////////////////////////////////////////////////////////////////////
				
				controller_next_state = MEASURE;
				//controller_next_state = RESET_REGISTERS; //DEBUG
				break;
			
			case MEASURE:
				//GENERAL MEASUREMENTS
				tx_data[POSITION_ANA0] = adc_result_average(ADC_MUX_ADC0, ADC_NUM_AVG); //NEED TO FIX ADC CONVERSINS!!!!!!!!
				tx_data[POSITION_ANA1] = adc_result_average(ADC_MUX_ADC1, ADC_NUM_AVG); //
				tx_data[POSITION_ANA2] = adc_result_average(ADC_MUX_ADC2, ADC_NUM_AVG); //
				tx_data[POSITION_ANA3] = adc_result_average(ADC_MUX_ADC3, ADC_NUM_AVG); //
				tx_data[POSITION_ANA4] = adc_result_average(ADC_MUX_ADC4, ADC_NUM_AVG); //
				tx_data[POSITION_ANA5] = adc_result_average(ADC_MUX_ADC5, ADC_NUM_AVG); //
				tx_data[POSITION_TEMP] = adc_result_average(ADC_MUX_TEMPSENSE, 1); //PIN CHANGE HAVE NO EFFECT ON ADCB
				tx_data[POSITION_VDD] = adc_result_average(ADC_MUX_1V1, 1); //PIN CHANGE HAVE NO EFFECT ON ADCB
			
				//SPECIAL MEASUREMENTS REQUIRED BY THE LOADCELL////////////////////////////////////////////////////////////////////////
				accu_data += tx_data[POSITION_CURRENT]; //controller_measure(9, &tx_data); //measure with averaging, and accumulate.
				loadcell_min_max_tran(tx_data[POSITION_CURRENT], &tx_data); //check if new value should be stored in min, max and tran.
 				tx_data[POSITION_PREV] = tx_data[POSITION_CURRENT]; //store adc value for next measurement.
				///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
								
				tx_data[POSITION_TIME]++; //increase timestamp counter.
			
				if (tx_data[POSITION_TIME] >= (TRANSMIT_RATE/WAKEUP_RATE)) //if accumulation limit is reached.
				{
					controller_next_state = CALC; //limit reached, go to next
					} else {
					controller_next_state = READ_EXT_DATA; //Start from top again
					RTC_ISR_ACTIVE = 0; //Break loop and go to sleep again
				}
				break;
			
			case CALC:
				led_blink(3000); //DEBUG
				
				//SPECIAL MEASUREMENTS REQUIRED BY THE LOADCELL////////////////////////////////////////////////////////////////////////
				tx_data[POSITION_AVG] = controller_calc_avg(accu_data, tx_data[POSITION_TIME]); //calc and store average.
				accu_data = 0; //reset parameters
				///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				controller_next_state = STORE_EXT_MEM;
				break;
			
			case STORE_EXT_MEM:
				tx_data[POSITION_TIME] = 0; //reset accumulation counter if something???????????????
				controller_next_state = RF_POWER_ON;
				break;
			
			case RF_POWER_ON:
				
				if (radio_power_on() == 1) //power on and check if it fails
				{
					tx_data[POSITION_STATUS] |= (1<<STATUS_BIT_RF_POWER_ON); //set failure status
					controller_next_state = RF_POWER_OFF; //if failure go to power off
					break;
				}
				
				controller_next_state = RF_CONNECT;
				break;
			
			case RF_CONNECT:
				if (at_rf_connect() != 0) //Connect to network. MAKE STATUS REPORT FROM THIS!!!!!!!
				{
					tx_data[POSITION_STATUS] |= (1<<STATUS_BIT_RF_CONNECT); //set failure status
					controller_next_state = RF_DISCONNECT; //if failure go to disconnect
					break;
				}
				controller_next_state = GENERATE_PACKAGE;
				break;
			
			case GENERATE_PACKAGE:
				data_to_char(&tx_data, TX_DATA_SIZE, &tx_data_bytes, TRANSFER_DATA_BASE);
				transfer_data_length_package = mqtt_packet(&tx_data_bytes, &tx_data_package, TRANSFER_DATA_SIZE_PACKAGE); //convert ascii data to MQTT package.
				#ifdef DEBUG //output package size
				char package_lenght[5] = "";
				char mystring[5] = "";
				itoa(transfer_data_length_package, package_lenght, 10);
				strcpy(mystring, package_lenght);
				usart_tx_at(USART_SERIAL_EXAMPLE, mystring);
				#endif // DEBUG
				controller_next_state = TX_DATA;
				break;
			
			case TX_DATA:
				tx(&tx_data_package, transfer_data_length_package); //transmit package. GENERATE STATUS FROM THIS.
				controller_next_state = RX_DATA;
				break;
			
			case RX_DATA:
				controller_next_state = RF_DISCONNECT;
				break;
			
			case RF_DISCONNECT:
				at_rf_disconnect(); //Disconnect
				controller_next_state = RF_POWER_OFF;
				break;
			
			case RF_POWER_OFF:
				radio_power_down(); //radio power down
				controller_next_state = RESET_REGISTERS;
				break;
			
			case RESET_REGISTERS:
				reset_all_data(); //reset all arrays
				controller_next_state = READ_EXT_DATA;
				RTC_ISR_ACTIVE = 0; //FINISHED, break LOOP!
				break;
			
			default:
				controller_next_state = RESET_REGISTERS; //TRY RESET.
				break;
			
		}
		
		controller_state = controller_next_state; //NEXT STATE => STATE
		
	}

	
	//WDTCSR |= (1<<WDIE);
	
	END:
	sei();
}

/*! \brief Main function.
 */
int main(void)
{
	//disables interrupts
	cli();
		
	/* Initialize the board.
	 * The board-specific conf_board.h file contains the configuration of
	 * the board initialization.
	 */
	board_init();
	//pmic_init(); //XMEGA
	sysclk_init(); //disables all peripheral clocks
	
	//select system clock
	//CLK.CTRL = 0x01; //2M
	
	//ADC setup
	
	
			
	//PUT IN OWN FILE???
	#define USART_BAUDRATE    19200
	#define USART_CHAR_LENGTH         USART_CHSIZE_8BIT_gc
	#define USART_PARITY              USART_PMODE_DISABLED_gc
	#define USART_STOP_BIT            true

	// USART for debug (COM port)
	static usart_rs232_options_t USART_OPTIONS = {
		.baudrate = USART_BAUDRATE,
		.charlength = USART_CHAR_LENGTH,
		.paritytype = USART_PARITY,
		.stopbits = USART_STOP_BIT
	};
	//////////////////////////////////////////////////////////////////////////
	
	usart_init_rs232(USART_SERIAL_SIM900, &USART_OPTIONS); //Radio UART
	sysclk_enable_module(POWER_RED_REG0, PRUSART0_bm);
	
	//initialize radio pins
	radio_pins_init();
	delay_s(1);
	
	//Shut down radio if already awake
	//check if radio is off, and turn of if it's on
	if (STATUS_PORT & (1<<STATUS_PIN))
	{
		radio_power_down();
	}
	////////////////////////////////////////////////////////
	
	//reset all tx data and date
	reset_all_data();
	
	//RTC setup.
	//PR.PRGEN &= ~(1<<2); //enable the RTC clock
	//sleepmgr_init();
	//rtc_init_period(WAKEUP_RATE); //using RTC as sampler timer.
	rtc_init_period(1); //using RTC as sampler timer.
	
	sei(); //enable interrupts
	
	
	
	#ifdef DEBUG
		DDRB |= (1<<5);
	#endif // DEBUG
	
	
	//go to sleep and let interrupts do the work...zzz....zzzz
	while (1)
	{
		/*sleepmgr_enter_sleep();*/
		
		
	}
		
}


