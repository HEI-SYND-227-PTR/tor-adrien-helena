#include "stm32f7xx_hal.h"

#include "main.h"
#include <stdio.h>
#include <string.h>
#include "rtx_os.h"

extern uint8_t calculateChecksum(uint8_t* dataPtr);

// Temporary message queue
#define TEMP_Q_SIZE 8

osMessageQueueId_t	queue_macS_temp_id;
const osMessageQueueAttr_t mac_snd_temp_attr = {
	.name = "MAC_SENDER_TEMP"
};


uint8_t* buildFrame(struct queueMsg_t queueMsg)
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
	
	queue_macS_temp_id = osMessageQueueNew(TEMP_Q_SIZE,sizeof(struct queueMsg_t),&mac_snd_temp_attr);  //Temporary message queue
	bool gotToken = false;					
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
				
				memcpy(tokenFrame, queueMsg.anyPtr, TOKENSIZE-2); //copy token data into new memorypool
				
		
				// Are there any messages? 
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
					uint8_t* framePtr = buildFrame(queueMsg);
					
					//Free old mem block
					osMemoryPoolFree(memPool,queueMsg.anyPtr);
					
					//Link queueMsg to framePtr
					queueMsg.anyPtr = framePtr;
				}
				else if(osMessageQueueGetCount(queue_macS_id) > 0)
				{
					//No messages in temp queue, but messages in MAC_S queue
					retCode = osMessageQueueGet( 	
					queue_macS_id,
					&queueMsg,
					NULL,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				}
				
				// Reinject the token
				/* FOR TESTING
				// Put the same token right back into the PHYS_queue
				osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				*/
				
				//TO DO: update token before reinjecting it !!
			break;
			
			case (DATA_IND):
				
			break;
			/*
			case (START):
			break;
			case (STOP):
			break;
			case (DATABACK): //When WE are the sender and get an ACK, NACK or R, NR.. !
			break;
			default:
			break;
			*/
		}
	}
}
