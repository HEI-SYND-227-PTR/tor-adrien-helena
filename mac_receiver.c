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
		
		//Get source
		uint8_t source = dataPtr[CONTROL + SOURCE];
		source = (source >> SAPI_LENGTH);
		
		//Get nb of bytes of data
		uint8_t length = dataPtr[LENGTH]; //nb of data bytes
		
		//Get checksum of frame
		uint8_t checksum = dataPtr[DATA + length];
		checksum = (checksum >> (READ + ACK));
				
		//TO DO:
		//Differentiate if I am the source of the Broadcast
		// or if someone else is.
		if((destination == MYADDRESS) || (destination == BROADCAST_ADDRESS))
		{
			// Message is for me
			
			if((destination == BROADCAST_ADDRESS) || (calculateChecksum(dataPtr) == checksum))
			{
				//Checksum is correct or not needed because it's a broadcast
				
				//Get destination sapi
				uint8_t destSapi = getSapi(dataPtr[CONTROL + DESTINATION]);
				
				//Prepare new memory pointer for the user data
				uint8_t * userData;
				
				//Temporary msg queue ID, will be selected in the switch case
				osMessageQueueId_t msgQ_id_temp;
				
				//Which SAPI ?
				switch (destSapi)
				{
					//Select app queue
					//Update queueMsg type
					case CHAT_SAPI:
					{
						if(gTokenInterface.connected)
						{
							//Set received frame's ACK, READ
							*(dataPtr + DATA + length) |= (READ_SET + ACK_SET);
							
							//Allocate new memory for the user data
							userData = osMemoryPoolAlloc(memPool, osWaitForever);
				
							//Transit the raw data into new mem block
							memcpy(userData, dataPtr+DATA, length);
							//Don't forget terminating char
							*(userData+length) = 0;
							
							msgQ_id_temp = queue_chatR_id;
							queueMsg.type = DATA_IND;
							queueMsg.anyPtr = userData;
						}
						else
						{
							//CHAT Sapi is not active
							//Leave R and ACK at 0
							
							//Send directly to PHY_S queue
							//Don't modify the frame's ACK, READ
							msgQ_id_temp = NULL;
							
						}
					}
					break;
					case TIME_SAPI:
					{
						//Modify received frame's ACK, READ
						*(dataPtr + DATA + length) |= (READ_SET + ACK_SET);

						//Allocate new memory for the user data
						userData = osMemoryPoolAlloc(memPool, osWaitForever);
				
						//Transit the raw data into new mem block
						memcpy(userData, dataPtr+DATA, length);
						//Don't forget terminating char
						*(userData+length) = 0;
						
						msgQ_id_temp = queue_timeR_id;
						queueMsg.type = DATA_IND;
						queueMsg.anyPtr = userData;
					}
					break;
					default:
						// SAPI not recognized...
						//Put directly to PHY_S
						msgQ_id_temp = NULL;
						
					break;
				}
				
				// Distribute to APP, if Sapi is really active
				if(msgQ_id_temp != NULL)
				{
				
					retCode = osMessageQueuePut(msgQ_id_temp, &queueMsg , osPriorityNormal,
						osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				}
				
				if(source != MYADDRESS)
				{
					//Message was not from me 
					
					//Send same frame back to PHY_S, so the source gets DATABACK
					queueMsg.type = TO_PHY;
					queueMsg.anyPtr = dataPtr; //With R and A modified...
				
					retCode = osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
							osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				}
				else
				{
					//Message was from me
					
					//Send same frame back to MAC_S, myself gets DATABACK
					queueMsg.type = DATABACK;
					queueMsg.anyPtr = dataPtr; //With R and A modified...
				
					retCode = osMessageQueuePut(queue_macS_id, &queueMsg , osPriorityNormal,
							osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				}			
			}
			else
			{
				//Data corrupted
				// ACK = 0, R = 1 -> put into PHY_S Queue
				
				//Modify received frame's READ, leave ACK at 0
				queueMsg.type = TO_PHY;
				*(dataPtr + DATA + length) |= (READ_SET);
				
				//Send frame directly back to source
				retCode = osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
						osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
			}
		}
		else
		{
			//Message is not "really" for me
					
			if(dataPtr[CONTROL] == TOKEN_TAG)
			{
				//It's the token
				
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

