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

#define LSB_MASK 0x3F

// CHECKSUM CALCULATOR
extern uint8_t calculateChecksum(uint8_t* dataPtr)
{
	uint8_t checksum = 0;

	//Add all bytes together
	for(int i = 0; i < dataPtr[LENGTH] + DATA; i++)
	{
		checksum += (dataPtr[i]);
	}
	
	//Mask the 6 LSB
	return checksum & LSB_MASK;
	
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
				// ACK = 0, R = 1 -> put into MAC_S Queue
			}
		}
		else
		{
			//Message is not for me
				
			//Get source
			uint8_t source = dataPtr[CONTROL + SOURCE];
			source = (source >> SAPI_LENGTH);
			
			if(dataPtr[CONTROL] == 0xFF)
			{
				//It's the token
				
				/* FOR TESTING 
					Put the same token right back into the PHYS_queue
				osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				*/
				
				//Give the token to MAC_Sender
				queueMsg.type = TOKEN;
				osMessageQueuePut(queue_macS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				
			}
			
			else if(source == MYADDRESS)
			{
				//Message is from me: it's a DATABACK
				//Don't modify the data!
				//Don't check the checksum...
				
				//Give the token to MAC_Sender
				queueMsg.type = DATABACK;
				osMessageQueuePut(queue_macS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
			}
			else
			{
				//Message is for another station
				
				// Put into PHY SENDER queue
				queueMsg.type = TO_PHY;
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

