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


//OFFSETS IN DATA FRAME
#define CONTROL 0
#define LENGTH 2
#define DATA 3
#define SOURCE 0
#define DESTINATION 1
#define SAPI_LENGTH 3
#define READ 1
#define ACK 1

#define BYTE 8

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
	
	// only x LSB !
	checksum = checksum >> (READ + ACK);
	
	return checksum;
}

// Because my sapi is less than a BYTE
uint8_t getSapi(uint8_t sapi)
{
	sapi = (sapi << (BYTE - SAPI_LENGTH));
	sapi = (sapi >> (BYTE -SAPI_LENGTH));
	
	return sapi;
}

//////////////////////////////////////////////////////////////////////////////////
// THREAD MAC RECEIVER
//////////////////////////////////////////////////////////////////////////////////
void MacReceiver(void *argument)
{
	// Retrieve Data from MACRec_Q
	struct queueMsg_t queueMsg;							// queue message
	osStatus_t retCode;											// return error code
	
	// Put data into APP_Queues
	for (;;)																// loop until doomsday
	{
		//----------------------------------------------------------------------------
		// MAC_R QUEUE READ								
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
				
		if(destination == MYADDRESS)
		{
			// Message is for me
			if(calculateChecksum(dataPtr) == checksum)
			{
				//Checksum is correct
				
				//Get destination sapi
				uint8_t destSapi = getSapi(dataPtr[CONTROL + DESTINATION]);
				
				//Allocate new memory for the user data
				uint8_t * userData = osMemoryPoolAlloc(memPool, osWaitForever);
				
				//for loop to transit the data from the frame
				for(int i = 0; i < length; i++)
				{
					*(userData+i) = *(dataPtr+DATA+i);
				}
				
				osMessageQueueId_t msgQ_id_temp;
				
				//Which SAPI ?
				switch (destSapi)
				{
					//Select app queue 
					//Update queueMsg type
					case CHAT_SAPI:
						msgQ_id_temp = queue_chatR_id;
						queueMsg.type = DATA_IND;
						queueMsg.anyPtr = userData;
					break;
					case TIME_SAPI:
						msgQ_id_temp = queue_timeR_id;
						queueMsg.type = DATA_IND;
						queueMsg.anyPtr = userData;
					break;
					default:
						// SAPI not recognized...
						//PUT TO MAC SENDER QUEUE ?
					break;
				}
				
				// Put to right queue
				osMessageQueuePut(msgQ_id_temp, &queueMsg , osPriorityNormal,
					osWaitForever);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
			}
			else
			{
				//Data corrupted
			}
		}
		else
		{
			//Message is not for me
			
			//Get source
			uint8_t source = dataPtr[CONTROL + SOURCE];
			source = (source >> SAPI_LENGTH);
			
			if(source == MYADDRESS)
			{
				//Message is from me
				
				//This is either an ack, nack or exactly same message
			}
			else
			{
				//Message is for another station
				queueMsg.type = TO_PHY;
				
				// Put into PHY SENDER queue
				retCode = osMessageQueuePut(
				queue_phyS_id,
				&queueMsg,
				osPriorityNormal,
				0);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				
			}
		}
	}
}

