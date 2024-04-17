
#include "stm32f7xx_hal.h"

#include "main.h"
#include <stdio.h>
#include <string.h>
#include "rtx_os.h"




	
uint8_t* tokenPtr;
	
//////////////////////////////////////////////////////////////////////////////////
// THREAD MAC SENDER
//////////////////////////////////////////////////////////////////////////////////
void MacSender(void *argument)
{
	// TODO
	struct queueMsg_t queueMsg;				// queue message
	char * stringPtr;									// string to send pointer
	osStatus_t retCode;								// return error code
	
	uint8_t tokenFrame[17];


	
	//----------------------------------------------------------------------------
	// TOKEN GENERATION
	//----------------------------------------------------------------------------
	for(int i = 0; i <=17; i++)
	{
		tokenFrame[i]=0;
	}
/*	token.st_0 = 0b00000000;
	token.st_1 = 0b00000000;
	token.st_2 = 0b00000000;
	token.st_3 = 0b00001010;	//MYADDRESS	SAPI 1 for chat & SAPI 3 for time
	token.st_4 = 0b00000000;
	token.st_5 = 0b00000000;
	token.st_6 = 0b00000000;
	token.st_7 = 0b00000000;
	token.st_8 = 0b00000000;
	token.st_9 = 0b00000000;
	token.st_10 = 0b00000000;
	token.st_11 = 0b00000000;
	token.st_12 = 0b00000000;
	token.st_13 = 0b00000000;
	token.st_14 = 0b00000000;
	token.st_15 = 0b00000000;*/
	
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
//		queueMsg.type = ;
    CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
		
		switch(queueMsg.type){
			case NEW_TOKEN:
				queueMsg.anyPtr = &token;
				queueMsg.type = TO_PHY;				
				retCode = osMessageQueuePut(queue_phyS_id, &queueMsg, osPriorityNormal,
					osWaitForever);
			    CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);

			break;
				
				
		}
		
	}
	
	 


	
}
