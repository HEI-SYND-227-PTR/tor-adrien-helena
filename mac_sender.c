#include "stm32f7xx_hal.h"

#include "main.h"
#include <stdio.h>
#include <string.h>
#include "rtx_os.h"

extern uint8_t calculateChecksum(uint8_t* dataPtr);


const char* ERRORMSG = "MAC Error";
const char* CRCERROR = "CRC Error\n";

osMessageQueueId_t	queue_macS_temp_id;
const osMessageQueueAttr_t mac_snd_temp_attr = {
	.name = "MAC_SENDER_TEMP"
};

//////////////////////////////////////////////////////////////////////////////////
// BUILD FRAME
//////////////////////////////////////////////////////////////////////////////////
uint8_t* buildFrame(const struct queueMsg_t* queueMsg)
{
	// Get some memory space
	uint8_t* framePtr = osMemoryPoolAlloc(memPool,osWaitForever);
	
	// Set source station
	uint8_t source = 0;
	source = ((MYADDRESS << SAPI_LENGTH)| queueMsg->sapi);
	*(framePtr+CONTROL+SOURCE) = source;
	
	//Set destination station
	uint8_t destination = 0;
	destination = ((queueMsg->addr << SAPI_LENGTH)| queueMsg->sapi);
	*(framePtr+CONTROL+DESTINATION) = destination;
	
	//Set length
	uint8_t length = strlen(queueMsg->anyPtr);
	*(framePtr+LENGTH) = length;
	
	//Set user data
	memcpy((framePtr+DATA), queueMsg->anyPtr, length);
	
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
	osStatus_t retCode;								// return error code
	char * tokenPtr;
	
	uint8_t* framePtr; // where the sent frame is saved until ACK = 1 and R = 1
	uint8_t* copiedFramePtr; //always keep an orignal pointer, that won't be destroyed
	
	queue_macS_temp_id = osMessageQueueNew(TEMP_Q_SIZE,sizeof(struct queueMsg_t),&mac_snd_temp_attr);  //Temporary message queue
		
	uint8_t sentCounter = 0; //How many times we tried to send a msg
	
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
		
		//Test what's the type of the frame we just received
		switch(queueMsg.type)	
		{
			//----------------------------------------------------------------------------
			// ASKED FOR A NEW TOKEN
			//----------------------------------------------------------------------------
			case (NEW_TOKEN):	//Creat the token
			{
				//Inject token into ring
				uint8_t* tokenFrame = osMemoryPoolAlloc(memPool, osWaitForever); // where the token will be saved temporarily
				
				//insert data into token frame
				for(int i = 0; i < TOKENSIZE - 2; i++)
				{
					//reset the whole frame first
					*(tokenFrame+i) = 0;
				}
				
				//first byte is always 0xFF
				*(tokenFrame) = 0xFF;
				
				//My station's byte has SAPIs 1 & 3 
				//ATTENTION: BINARY WON'T WORK

				*(tokenFrame + TOKEN_DATA+ MYADDRESS) = (uint8_t) (0x01 << TIME_SAPI);
					
				if(gTokenInterface.connected)
				{
					//Add the CHAT Sapi
					*(tokenFrame + TOKEN_DATA + MYADDRESS) |= (0x01 << CHAT_SAPI);
				}
				
				//Reuse queueMsg, adjust type and dataPtr
				queueMsg.anyPtr = tokenFrame;
				queueMsg.type = TO_PHY;
				
				//Put to PHY_S Queue
				retCode = osMessageQueuePut(queue_phyS_id, &queueMsg, osPriorityNormal,
					osWaitForever);
			  CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				
			}
			break;
			
			//----------------------------------------------------------------------------
			// RECEIVED A TOKEN
			//----------------------------------------------------------------------------
			case (TOKEN):
			{
				//Point to token data
				tokenPtr = queueMsg.anyPtr;
				
				//Update MY station in the token frame anyways !
				*(tokenPtr + TOKEN_DATA + MYADDRESS) = (uint8_t) (0x01 << TIME_SAPI);
					
				if(gTokenInterface.connected)
				{
					*(tokenPtr + TOKEN_DATA + MYADDRESS) |= (0x01 << CHAT_SAPI);
				}
				else
				{
					*(tokenPtr + TOKEN_DATA + MYADDRESS) &= (0x01 << TIME_SAPI);
				}
				
				uint8_t same = 1;
				
				//Check if token list is different then what's already in station_list
				for(int i = 0; i < TOKEN_DATA_SIZE; i++)
				{
					if(*(tokenPtr + TOKEN_DATA + i) != gTokenInterface.station_list[i])
					{
							same = 0;
					}
				}
				
				if(same == 0)
				{
					//Internal station list != token list
					
					//Update global array of active stations
					memcpy(gTokenInterface.station_list,(tokenPtr+TOKEN_DATA), TOKEN_DATA_SIZE);
					
					//Reuse queueMsg, adjust type and dataPtr
					queueMsg.anyPtr = NULL; //no data to be transported for the LCD
					queueMsg.type = TOKEN_LIST;
					
					//Put to LCD_R Queue, it notifies the display
					retCode = osMessageQueuePut(queue_lcd_id, &queueMsg, osPriorityNormal,
						osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				}
				//No need to check else, because it means the list is already up to date
				
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
					framePtr = buildFrame(&queueMsg);
					
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
					
				}
				else
				{
					//No messages to send
					
					//Repoint to token with anyPtr
					queueMsg.anyPtr = tokenPtr;
					
					//Reinject the token
					//We have not modified the received data
					queueMsg.type = TO_PHY;
					osMessageQueuePut(queue_phyS_id, &queueMsg , osPriorityNormal,
					osWaitForever);
					CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				}
			}
			break;
			
			//----------------------------------------------------------------------------
			// RECEIVED DATA BACK FROM OTHER STATION
			//----------------------------------------------------------------------------
			case (DATABACK): 
			{
				//When WE are the sender and get an ACK, NACK or R, NR.. !
							
				//Get destination
				uint8_t* dataPtr = (uint8_t*) queueMsg.anyPtr;		
				uint8_t destination = (dataPtr[CONTROL + DESTINATION] >> SAPI_LENGTH);
				
				//Was it a broadcast?
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
				}
				else
				{
					//It wasn't a broadcast
					
					/*
					 *   R   |   A   |   Result
					 *   0   |   0   |   Destination station not online, so message was not acknowledge too
					 *   0   |   1   |   No sense, station not online, but acknowledge message ??
					 *   1   |   0   |   Send message again, message read, but error on message
					 *   1   |   1   |   All good, message read, and no error
					 */
					
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
						}
						else
						{
							//ack= 0: checksum was wrong
						
							sentCounter++;	//Variable to know how many time we tried to send the message
						
							if(sentCounter == RETRY_SEND)
							{
								//Stop sending... it's not worth it
								
								//Signal LCD of a MAC Error
								char* errorMsg = osMemoryPoolAlloc(memPool,osWaitForever);
								strcpy(errorMsg, CRCERROR);
								
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
								
								//Reset counter
								sentCounter = 0;
							}
							else
							{
								//Resend copy of ORIGINAL frame pointer, not the RECEIVED frame
								
								//Make a copy of build message (and keep this pointer! in case we need to send it again)
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
						
						//Reuse queueMsg, adjust type and dataPtr
						queueMsg.anyPtr = errorMsg; //no data to be transported for the LCD
						queueMsg.type = MAC_ERROR;
						sprintf(errorMsg,"Error :\r\nStation %d doesn't exist !\r\n",(destination+1));
						
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
					}
				}
			}
			break;
			
			//----------------------------------------------------------------------------
			// RECEIVED DATA FROM CHAT OR TIME APPLICATION
			//----------------------------------------------------------------------------
			case (DATA_IND):
			{
				//Put into temp queue
				
				//Temp queue contains only the raw data
				
				//To avoid mempool overload, put wait at 0 !
				retCode = osMessageQueuePut(queue_macS_temp_id, &queueMsg , osPriorityNormal,
					0);
				if(retCode != osOK)
				{
					osMemoryPoolFree(memPool,queueMsg.anyPtr);
				}
				CheckRetCode(retCode,__LINE__,__FILE__,CONTINUE);
				
			}
			break;
			
			//----------------------------------------------------------------------------
			// MODIFIED WHEN WE ARE ONLINE
			//----------------------------------------------------------------------------
			case (START):
			{
					gTokenInterface.connected = true;
			}				
			break;
			
			//----------------------------------------------------------------------------
			// MODIFIED WHEN WE ARE OFFLINE
			//----------------------------------------------------------------------------
			case (STOP):
			{
				gTokenInterface.connected = false;
			}
			break;
			
			default:
				//Do nothing...
			break;
		}
	}
}
