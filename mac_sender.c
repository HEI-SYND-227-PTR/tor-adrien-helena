#include "stm32f7xx_hal.h"

#include "main.h"
#include <stdio.h>
#include <string.h>
#include "rtx_os.h"

extern uint8_t calculateChecksum(uint8_t* dataPtr);


const char* ERRORMSG = "MAC Error";

osMessageQueueId_t	queue_macS_temp_id;
const osMessageQueueAttr_t mac_snd_temp_attr = {
	.name = "MAC_SENDER_TEMP"
};


//TO DO: modifiy parameter to either a queueMsg ptr or just give the needed values
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
	
	//Check if it's for a broadcast
	if(destination != BROADCAST_ADDRESS)
	{
		//Not a broadcast msg
		status = (checksum << (ACK + READ));
		*(framePtr+DATA+length) = status;
	}
	else
	{
		//A broadcast msg
		status = (checksum << (ACK + READ));
		status |= (READ_SET + ACK_SET); //Concat POSITIVE R,ACK
		*(framePtr+DATA+length) = status;
	}
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
	char * tokenPtr;
	
//	uint8_t* tokenFrame = osMemoryPoolAlloc(memPool, osWaitForever); // where the token will be saved temporarily
	uint8_t* framePtr; // where the sent frame is saved until ACK = 1 and R = 1
	uint8_t* copiedFramePtr; //always keep an orignal pointer, that won't be destroyed
	
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
			{
				//Inject token into ring
				uint8_t* tokenFrame = osMemoryPoolAlloc(memPool, osWaitForever); // where the token will be saved temporarily
				
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
			}
			break;
			case (TOKEN):
			{
				// Got the token
				gotToken = true;
				
				//point to token data
				tokenPtr = queueMsg.anyPtr;
				
				//Update global array of active stations
				memcpy(gTokenInterface.station_list,(tokenPtr+TOKEN_DATA), TOKEN_DATA_SIZE);
				
				//Reuse queueMsg, adjust type and dataPtr
				queueMsg.anyPtr = NULL; //no data to be transported for the LCD
				queueMsg.type = TOKEN_LIST;
				
				//Put to LCD_R Queue, it notifies the display
				retCode = osMessageQueuePut(queue_lcd_id, &queueMsg, osPriorityNormal,
					osWaitForever);
			  CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				
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
					
					//Build new frame in new mem block, keep this one to make copies!
					framePtr = buildFrame(queueMsg);
					
					//Free old mem block
					osMemoryPoolFree(memPool,queueMsg.anyPtr);
					
					//Make a copy of build message
					copiedFramePtr = osMemoryPoolAlloc(memPool, osWaitForever);
					memcpy(copiedFramePtr, framePtr, (*(framePtr + LENGTH) + DATA + STATUS_LEN));
					
					//Send the copy
					queueMsg.anyPtr = copiedFramePtr;
					
					//Put into PHY_S queue
					queueMsg.type = TO_PHY;
					osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					
					//Wait for response
					waitDataback = true;
					
				}
				else
				{
					//No messages to send
					
					//Repoint to token with anyPtr
					queueMsg.anyPtr = tokenPtr;
					
					//Update sapi list
					uint8_t* mySapis = (uint8_t*) queueMsg.anyPtr;
					mySapis += MYADDRESS;
					
					if(gTokenInterface.connected)
					{
						//CHAT Sapi is available
							*mySapis = CHAT_TIME_ACTIVE; 
					}
					else
					{
						//CHAT Sapi is not available
						*mySapis = TIME_ACTIVE;
					}
					
					//Reinject the token
					//We have not modified the received data
					queueMsg.type = TO_PHY;
					osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					
					//Reset
					waitDataback = false;
					gotToken= false;
				}
			}
			break;
			
			case (DATABACK): //When WE are the sender and get an ACK, NACK or R, NR.. !
			{
							
				//Was it a broadcast?
				//Get destination
				uint8_t* dataPtr = (uint8_t*) queueMsg.anyPtr;		
				uint8_t destination = (dataPtr[CONTROL + DESTINATION] >> SAPI_LENGTH);
				
				if(destination == BROADCAST_ADDRESS)
				{
					//Stop sending... the broadcast has gone trough the ring
					
					//Free the ORIGINAL frame pointer
					if(framePtr != NULL)
					{
						osMemoryPoolFree(memPool, framePtr);
					}
					
					//Free RECEIVED frame from mem pool
					osMemoryPoolFree(memPool, queueMsg.anyPtr);
					
					//Reinject the token
					queueMsg.anyPtr = tokenPtr;
					queueMsg.type = TO_PHY;
					osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					gotToken = false;
				}
				else
				{
					//It wasn't a broadcast
					
					//ACK ? READ ? 
					uint8_t* status = (uint8_t*) queueMsg.anyPtr;
				
					//Get length
					uint8_t length = *(status+LENGTH);
					
					//Point to status byte
					status += (DATA + length);
					
					//Get Read, Ack
					uint8_t read = *(status)&(READ_SET);
					uint8_t ack = *(status)&(ACK_SET);
					
					//Free frame from mem pool
					osMemoryPoolFree(memPool, queueMsg.anyPtr);
					
					if(read == READ_SET)
					{
						// R = 1 : message was read
						
						if(ack == ACK_SET)
						{
							// ACK = 1
							// Data wasn't corrupted
							
							//Free the ORIGINAL frame pointer
							if(framePtr != NULL)
							{
								osMemoryPoolFree(memPool, framePtr);
							}
							
							//Reinject the token
							queueMsg.anyPtr = tokenPtr;
							queueMsg.type = TO_PHY;
							osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
							osWaitForever);
							CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
							
							waitDataback = false;
							gotToken= false;
					}
					else
					{
						//ack= 0: checksum was wrong
						
						sentCounter++;
						
						if(sentCounter == RETRY_SEND)
						{
							//Stop sending... it's not worth it
							
							
							//Signal LCD of a MAC Error
							char* errorMsg = osMemoryPoolAlloc(memPool,osWaitForever);
//							memcpy(errorMsg, ERRORMSG, strlen(ERRORMSG));
								strcpy(errorMsg, ERRORMSG);
							//Reuse queueMsg, adjust type and dataPtr
							queueMsg.anyPtr = errorMsg; //no data to be transported for the LCD
							queueMsg.type = MAC_ERROR;
				      queueMsg.addr = framePtr[SOURCE] >> SAPI_LENGTH;
							//Put to LCD_R Queue, it notifies the display of an error
							retCode = osMessageQueuePut(queue_lcd_id, &queueMsg, osPriorityNormal,
									osWaitForever);
							CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);

							//Free the ORIGINAL frame pointer
							if(framePtr != NULL)
							{
								osMemoryPoolFree(memPool, framePtr);
							}
							
							//Reinject the token
							queueMsg.anyPtr = tokenPtr;
							queueMsg.type = TO_PHY;
							osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
							osWaitForever);
							CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
							gotToken = false;
							
							//Reset counter
							sentCounter = 0;
						}
						else
						{
							//Resend copy of ORIGINAL frame pointer, not the RECEIVED frame
							//Make a copy of build message (and keep this pointer! in case of needed resend)
							copiedFramePtr = osMemoryPoolAlloc(memPool, osWaitForever);
							memcpy(copiedFramePtr, framePtr, *(framePtr + LENGTH) + DATA + STATUS_LEN);
							
							//Send COPY frame !
							queueMsg.anyPtr = copiedFramePtr;

							//Put into PHY_S queue
							queueMsg.type = TO_PHY;
							osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
							osWaitForever);
							CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
						}
					}
				}
				else
				{
					// R = 0: message wasn't read
					
					//Stop sending... it's not worth it
							
					
					//Signal LCD of a MAC Error
					char* errorMsg = osMemoryPoolAlloc(memPool,osWaitForever);
//							memcpy(errorMsg, ERRORMSG, strlen(ERRORMSG));
//								strcpy(errorMsg, ERRORMSG);
							//Reuse queueMsg, adjust type and dataPtr
							queueMsg.anyPtr = errorMsg; //no data to be transported for the LCD
							queueMsg.type = MAC_ERROR;
				      uint8_t source = framePtr[SOURCE] >> SAPI_LENGTH;
					sprintf(errorMsg,"Error :\r\nStation %d doesn't exist !\r\n",source);
					//Reuse queueMsg, adjust type and dataPtr
					queueMsg.anyPtr = errorMsg; //no data to be transported for the LCD
					queueMsg.type = MAC_ERROR;
				
					//Put to LCD_R Queue, it notifies the display of an error
					retCode = osMessageQueuePut(queue_lcd_id, &queueMsg, osPriorityNormal,
							osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					//Free the ORIGINAL frame pointer
					if(framePtr != NULL)
					{
						osMemoryPoolFree(memPool, framePtr);
					}
					
					//Reinject the token
					queueMsg.anyPtr = tokenPtr;
					queueMsg.type = TO_PHY;
					osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
					gotToken = false;
				}
			}
			}
			break;
			
			case (DATA_IND):
			{
				//Put into temp queue
				//Temp queue is BIGGER
				//Temp queue contains only the raw data
				
				retCode = osMessageQueuePut(queue_macS_temp_id, &queueMsg , osPriorityNormal,
					0);
				if(retCode != osOK)
				{
					osMemoryPoolFree(memPool,queueMsg.anyPtr);
				}
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				
			}
			break;
			
			case (START):
			{
					gTokenInterface.connected = true;
			}				
			break;
			case (STOP):
			{
				gTokenInterface.connected = false;
			}
			break;
			
			/*
			default:
			break;
			*/
		}
	}
}
