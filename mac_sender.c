#include "stm32f7xx_hal.h"

#include "main.h"
#include <stdio.h>
#include <string.h>
#include "rtx_os.h"

extern uint8_t calculateChecksum(uint8_t* dataPtr);

// Temporary message queue
#define TEMP_Q_SIZE 8

//Resend of a frame
#define RETRY_SEND 3

//Read, ack masks
const uint8_t READ_MASK = 0x02;
const uint8_t ACK_MASK = 0x01;

osMessageQueueId_t	queue_macS_temp_id;
const osMessageQueueAttr_t mac_snd_temp_attr = {
	.name = "MAC_SENDER_TEMP"
};


uint8_t* buildFrame(const struct queueMsg_t queueMsg)
{
	// Get some memory space
	uint8_t* framePtr = osMemoryPoolAlloc(memPool,osWaitForever);
	
	// Set source station
	uint8_t source = 0;
	source = ((MYADDRESS << SAPI_LENGTH)| queueMsg.sapi);
	*(framePtr+CONTROL+SOURCE) = source;
	
	//Set destination station
	uint8_t destination = 0;
	destination = ((queueMsg.addr << SAPI_LENGTH)| queueMsg.sapi);
	*(framePtr+CONTROL+DESTINATION) = destination;
	
	//Set length
	uint8_t length = strlen(queueMsg.anyPtr);
	*(framePtr+LENGTH) = length;
	
	//Set user data
	memcpy((framePtr+DATA), queueMsg.anyPtr, length);
	
	//Set status
	uint8_t status = 0;
	uint8_t checksum = calculateChecksum(framePtr);
	status = (checksum << (ACK + READ));//TO CHECK: Maybe ACK and READ are not at zero ?
	*(framePtr+DATA+length) = status;
	
	return framePtr;
}

//////////////////////////////////////////////////////////////////////////////////
// THREAD MAC SENDER
//////////////////////////////////////////////////////////////////////////////////
void MacSender(void *argument)
{
	struct queueMsg_t queueMsg;				// queue message
	char * stringPtr;									// string to send pointer
	osStatus_t retCode;								// return error code
	
	uint8_t* tokenFrame = osMemoryPoolAlloc(memPool, osWaitForever); // where the token will be saved temporarily
	uint8_t* framePtr;
	
	queue_macS_temp_id = osMessageQueueNew(TEMP_Q_SIZE,sizeof(struct queueMsg_t),&mac_snd_temp_attr);  //Temporary message queue
	
	bool gotToken = false;
	bool waitDataback = false; //when WE are the source and wait for a response
	uint8_t sentCounter = 0; //How many times we tried to sent a msg
	
	//------------------------------------------------------------------------------
	for (;;)														// loop until doomsday
	{
		//----------------------------------------------------------------------------
		// QUEUE READ
		//----------------------------------------------------------------------------
		retCode = osMessageQueueGet( 	
			queue_macS_id,
			&queueMsg,
			NULL,
			osWaitForever); 	
    CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
		
		switch(queueMsg.type)
		{
			case (NEW_TOKEN):
				//Inject token into ring
				
				gotToken = true;
				//insert data into token frame
				for(int i = 0; i < TOKENSIZE - 2; i++)
				{
					//reset the whole frame first
					*(tokenFrame+i) = 0;
				}
				
				//first byte is always 0xFF
				*(tokenFrame) = 0xFF;
				
				//My station's byte has SAPIs 1 & 3 ready
				//ATTENTION: BINARY WON'T WORK
				*(tokenFrame + MYADDRESS) = 0x0A;
				
				//Reuse queueMsg, adjust type and dataPtr
				queueMsg.anyPtr = tokenFrame;
				queueMsg.type = TO_PHY;
				
				//Put to PHY_S Queue
				retCode = osMessageQueuePut(queue_phyS_id, &queueMsg, osPriorityNormal,
					osWaitForever);
			  CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				
				gotToken = false;
			break;
			case (TOKEN):
				// Got the token
				gotToken = true;
				
				//copy token data into token memory block
				memcpy(tokenFrame, queueMsg.anyPtr, TOKENSIZE-2); 
				
				// Are there any messages in temp queue ? 
				if(osMessageQueueGetCount(queue_macS_temp_id) > 0)
				{
					//Yes, there are messages in temp queue
					retCode = osMessageQueueGet( 	
					queue_macS_temp_id,
					&queueMsg,
					NULL,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					
					//Build new frame in new mem block
					framePtr = buildFrame(queueMsg);
					
					//Free old mem block
					osMemoryPoolFree(memPool,queueMsg.anyPtr);
					
					//Link queueMsg to framePtr
					queueMsg.anyPtr = framePtr;

					//Put into PHY_S queue
					queueMsg.type = TO_PHY;
					osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					
					//Wait for response
					waitDataback = true;
					
				}
				/* DONT DO THIS
				else if(osMessageQueueGetCount(queue_macS_id) > 0)
				{
					//No messages in temp queue, but messages in MAC_S queue
					retCode = osMessageQueueGet( 	
					queue_macS_id,
					&queueMsg,
					NULL,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					
					//Build new frame in new mem block
					framePtr = buildFrame(queueMsg);
					
					//Free old mem block
					osMemoryPoolFree(memPool,queueMsg.anyPtr);
					
					//Link queueMsg to framePtr
					queueMsg.anyPtr = framePtr;
					
					//Put into PHY_S queue
					queueMsg.type = TO_PHY;
					osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					
					//Wait for response
					waitDataback = true;
				}*/
				else
				{
					//No messages to send
					
					//Update sapi list
					uint8_t* mySapis = (uint8_t*) queueMsg.anyPtr; //TO CHECK IF + MYADDRESS IS CORRECT 
					mySapis += MYADDRESS;
					*mySapis = 0x0A; // sapi 1 and 3 are actif !
					
					// Reinject the token
					queueMsg.type = TO_PHY;
					osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					
					//Reset
					waitDataback = false;
					gotToken= false;
				}
				
				/* FOR TESTING
				// Put the same token right back into the PHYS_queue
				osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				*/
			break;
			
			case (DATABACK): //When WE are the sender and get an ACK, NACK or R, NR.. !
			{
				//ACK ? READ ? 
				uint8_t* status = (uint8_t*) queueMsg.anyPtr;
			
				//Get length
				uint8_t length = *(status+LENGTH);
				
				//Point to status byte
				status += (DATA + length);
				
				//Get Read, Ack
				uint8_t read = *(status)&(READ_MASK);
				uint8_t ack = *(status)&(ACK_MASK);
				
				if(read == READ_MASK)
				{
					// R = 1 : message was read
					
					if(ack == ACK_MASK)
					{
						// Data wasn't corrupted
						
						//Free frame from mem pool
						osMemoryPoolFree(memPool, framePtr);
						
						//Reinject the token
						queueMsg.anyPtr = tokenFrame;
						queueMsg.type = TO_PHY;
						osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
						osWaitForever);
						CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
						
						waitDataback = false;
						gotToken= false;
					}
				}
				else
				{
					
					
				}
			}
			break;
			
				
			case (DATA_IND):
			{
				//Put into temp queue
				//Temp queue is BIGGER
				//Temp queue contains only the raw data
				
				osMessageQueuePut(queue_macS_temp_id, &queueMsg , osPriorityNormal,
					osWaitForever);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				
			}
				
			break;
			/*
			case (START):
			break;
			case (STOP):
			break;

			default:
			break;
			*/
		}
	}
}
