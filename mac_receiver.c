//////////////////////////////////////////////////////////////////////////////////
/// \file mac_receiver.c
/// \brief Mac receiver thread
/// \author Hanele Bersy
/// \date  2024-04
//////////////////////////////////////////////////////////////////////////////////
#include "stm32f7xx_hal.h"

#include <stdio.h>
#include <string.h>
#include "main.h"


//DEFINITION OF OFFSETS IN DATA FRAME
#define CONTROL 0
#define LENGTH 2
#define DATA 3
#define SOURCE 0
#define DESTINATION 1
#define SAPI_LENGTH 3
#define READ 1
#define ACK 1


// CHECKSUM CALCULATOR
uint8_t calculateChecksum(uint8_t* dataPtr)
{
	uint8_t checksum = 0;
		
	//Get control
	uint16_t control = (uint16_t) *(dataPtr+CONTROL);
	
	//Get length
	uint8_t length = *(dataPtr+LENGTH); 
	
	//Start adding data to checksum
	for(int i = 0; i < length; i++)
	{
		checksum += *(dataPtr+DATA+i);
	}
	
	checksum += control;
	checksum += length;	
}

//////////////////////////////////////////////////////////////////////////////////
// THREAD MAC RECEIVER
//////////////////////////////////////////////////////////////////////////////////
void MacReceiver(void *argument)
{
	// Retrieve Data from MACRec_Q
	struct queueMsg_t queueMsg;							// queue message
	osStatus_t retCode;											// return error code
	
	for (;;)																// loop until doomsday
	{
		//----------------------------------------------------------------------------
		// MACR QUEUE READ								
		//----------------------------------------------------------------------------
		retCode = osMessageQueueGet( 	
			queue_macR_id,
			&queueMsg,
			NULL,
			osWaitForever); 	
		queueMsg.type = FROM_PHY; // msg is from PHY for MAC... not really useful here
    CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);	// print error

		//----------------------------------------------------------------------------
		// UNWARP THE DATA...
		//----------------------------------------------------------------------------		
		
		//Get data frame pointer, slice it into bytes
		uint8_t* dataPtr = (uint8_t*) queueMsg.anyPtr;
		
		//Get destination
		uint8_t destination = dataPtr[CONTROL + DESTINATION];
		destination = (destination >> SAPI_LENGTH);

		//Get nb of bytes of data
		uint8_t length = dataPtr[LENGTH]; //nb of data bytes
		
		//Get checksum of frame
		uint8_t checksum = dataPtr[DATA + length];
		checksum = (checksum >> (READ + ACK));
		
		//New checksum calculated
		uint8_t checksum_temp = 0;
		
		if(destination == MYADDRESS)
		{
			//Check checksum
			checksum_temp = calculateChecksum(dataPtr);
		}
		
	}
}

