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

//Struct with all the fields of the frame
typedef struct DataFrame_
{
	//Source
	uint8_t sourceStation;
	uint8_t sourceSapi;
	
	//Destination
	uint8_t destStation;
	uint8_t destSapi;
	
	//Length
	uint8_t length;
	
	//Data
	uint8_t* userData;
	
	//Status
	uint8_t checksum;
	uint8_t read;
	uint8_t ack;
	
} DataFrame;

//////////////////////////////////////////////////////////////////////////////////
//CHECKSUM CALCULATOR
//////////////////////////////////////////////////////////////////////////////////
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

//////////////////////////////////////////////////////////////////////////////////
//SAPI getter
//////////////////////////////////////////////////////////////////////////////////
uint8_t getSapi(uint8_t controlByte)
{
	uint8_t sapi = controlByte & SAPI_MASK;
	
	return sapi;
}

//////////////////////////////////////////////////////////////////////////////////
//Set DataFrame
//////////////////////////////////////////////////////////////////////////////////
void setDataFrame(DataFrame* dataFrame, uint8_t* anyPtr)
{
	//Set Destination
	dataFrame->destStation = (anyPtr[CONTROL + DESTINATION] & STATION_MASK) >> SAPI_LENGTH;
	dataFrame->destSapi = anyPtr[CONTROL + DESTINATION] & SAPI_MASK;
	
	//Set Source
	dataFrame->sourceStation = (anyPtr[CONTROL + SOURCE] & STATION_MASK) >> SAPI_LENGTH;
	dataFrame->sourceSapi = anyPtr[CONTROL + SOURCE] & SAPI_MASK;

	//Set Length 
	dataFrame->length = anyPtr[LENGTH];

	//Set Data
	dataFrame->userData = anyPtr + DATA + dataFrame->length;

	//Set Status
	dataFrame->checksum = (anyPtr[DATA + dataFrame->length] & CHECKSUM_MASK) >> (ACK + READ);
	dataFrame->read = (anyPtr[DATA + dataFrame->length] & READ_SET) >> READ;
	dataFrame->ack = anyPtr[DATA + dataFrame->length] & ACK_SET;	
}

//////////////////////////////////////////////////////////////////////////////////
// THREAD MAC RECEIVER
//////////////////////////////////////////////////////////////////////////////////
void MacReceiver(void *argument)
{
	// Retrieve Data from MACRec_Q
	struct queueMsg_t queueMsg;							// queue message
	osStatus_t retCode;											// return error code
	
	DataFrame dataFrame;										//Contains the data frame information
	
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
    CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);	// print error if any

		//----------------------------------------------------------------------------
		// UNWRAP THE DATA...
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
		uint8_t length = dataPtr[LENGTH];
		
		//Get checksum of frame
		uint8_t checksum = dataPtr[DATA + length];
		checksum = (checksum >> (READ + ACK));
		
		setDataFrame(&dataFrame, (uint8_t*) queueMsg.anyPtr);
		
		if((dataFrame.destStation == MYADDRESS) || (dataFrame.destStation == BROADCAST_ADDRESS))
		{
			// Message is for me (message directly for me, or broadcast)
			
			if((dataFrame.destStation == BROADCAST_ADDRESS) || (calculateChecksum(dataPtr) == dataFrame.checksum))
			{
				//Checksum is correct or not needed because it's a broadcast
				
				//Get destination sapi
				uint8_t destSapi = getSapi(dataPtr[CONTROL + DESTINATION]);
				
				//Prepare new memory pointer for the user data
				uint8_t * userData;
				
				//Temporary msg queue ID, will be selected in the switch case
				osMessageQueueId_t msgQ_id_temp;
				
				//Which SAPI ?
				switch (dataFrame.destSapi)
				{

				//----------------------------------------------------------------------------
				// CHAT SAPI								
				//----------------------------------------------------------------------------
					//Select app queue
					//Update queueMsg type
					case CHAT_SAPI:
					{
						if(gTokenInterface.connected) //If we are online
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
							queueMsg.addr= source;
						}
						else	//If we are offline
						{
							//CHAT Sapi is not active
							//Leave R and ACK at 0
							
							//Send directly to PHY_S queue
							//Don't modify the frame's ACK, READ
							msgQ_id_temp = NULL;
							
						}
					}
					break;
					
				//----------------------------------------------------------------------------
				// TIME SAPI								
				//----------------------------------------------------------------------------
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
					
				//----------------------------------------------------------------------------
				// DEFAULT
				//----------------------------------------------------------------------------
					default:
						// SAPI not recognized...
						//Put directly to PHY_S
						msgQ_id_temp = NULL;
						
					break;
				}
				
				// Distribute to APP layer, if Sapi is really active
				if(msgQ_id_temp != NULL)
				{
					retCode = osMessageQueuePut(msgQ_id_temp, &queueMsg , osPriorityNormal,
																				osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				}
				
				if(dataFrame.sourceStation != MYADDRESS)
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
					
					//If it was a broadcast, force R,A = 1
					
					retCode = osMessageQueuePut(queue_macS_id, &queueMsg , osPriorityNormal,
							osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				}			
			}
			else	//It was not a Broadcast, or there was an error on the checksum
			{
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
		else	//Message was not for me, nor a broadcast
		{
			//Maybe it was a token
			if(dataPtr[CONTROL] == TOKEN_TAG)	
			{
				//It's the token
				
				//Give the token to MAC_Sender
				queueMsg.type = TOKEN;
				osMessageQueuePut(queue_macS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				
			}
			//It is not a token, maybe I sent it
			else if(dataFrame.sourceStation == MYADDRESS)
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

