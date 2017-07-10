/*
 * loadcell_logger_setup.h
 *
 * Created: 28/06/2017 15:02:00
 *  Author: jan.rune.herheim
 */ 


#ifndef LOADCELL_LOGGER_SETUP_H_
#define LOADCELL_LOGGER_SETUP_H_

//sampling and averaging times
#define SAMPLING_TIME 1
#define AVERAGING_TIME 10

//Data positions
#define POSITION_CURRENT 0
#define POSITION_PREV 1
#define POSITION_AVG 2
#define POSITION_MIN 3
#define POSITION_MAX 4
#define POSITION_TRAN_MAX 5
//other
#define POSITION_ACCU_CNT 9

//Reset values that differs from initial setup
#define RESET_VALUE_MIN 0xffff

#endif /* LOADCELL_LOGGER_SETUP_H_ */