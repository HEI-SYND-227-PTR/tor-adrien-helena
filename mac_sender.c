#include "stm32f7xx_hal.h"

#include "main.h"
#include <stdio.h>
#include <string.h>
#include "rtx_os.h"



// Temporary message queue
#define TEMP_Q_SIZE 8

osMessageQueueId_t	queue_macS_temp_id;
const osMessageQueueAttr_t mac_snd_temp_attr = {
	.name = "MAC_SENDER_TEMP"
};

//////////////////////////////////////////////////////////////////////////////////
// THREAD MAC SENDER
//////////////////////////////////////////////////////////////////////////////////
void MacSender(void *argument)
{
	struct queueMsg_t queueMsg;				// queue message
	char * stringPtr;									// string to send pointer
	osStatus_t retCode;								// return error code
	
	uint8_t* tokenFrame;							// Points to the token frame
	queue_macS_temp_id = osMessageQueueNew(TEMP_Q_SIZE,sizeof(struct queueMsg_t),&mac_snd_temp_attr);  //Temporary message queue
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
				//Create new token
				tokenFrame = osMemoryPoolAlloc(memPool, osWaitForever); //get some memory in the pool
				
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
			break;
			case (TOKEN):
				// Got the token
				
				// FOR TESTING
				// Put the same token right back into the PHYS_queue
				osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);

			break;
			/*
			case (START):
			break;
			case (STOP):
			break;
			case (DATA_IND):
			break;

			case (DATABACK): //When WE are the sender and get an ACK, NACK or R, NR.. !
			break;
			default:
			break;
			*/
		}
	}
}
