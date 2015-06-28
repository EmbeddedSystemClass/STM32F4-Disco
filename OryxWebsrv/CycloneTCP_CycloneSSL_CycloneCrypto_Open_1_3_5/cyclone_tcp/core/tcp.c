/**
 * @file tcp.c
 * @brief TCP (Transmission Control Protocol)
 *
 * @section License
 *
 * Copyright (C) 2010-2013 Oryx Embedded. All rights reserved.
 *
 * This file is part of CycloneTCP Open.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * @author Oryx Embedded (www.oryx-embedded.com)
 * @version 1.3.5
 **/

//Switch to the appropriate trace level
#define TRACE_LEVEL TCP_TRACE_LEVEL

//Dependencies
#include <stdlib.h>
#include <string.h>
#include "tcp_ip_stack.h"
#include "socket.h"
#include "tcp.h"
#include "tcp_misc.h"
#include "debug.h"

//Check TCP/IP stack configuration
#if (TCP_SUPPORT == ENABLED)


/**
 * @brief Establish a TCP connection
 * @param[in] socket Handle to an unconnected socket
 * @return Error code
 **/

error_t tcpConnect(Socket *socket)
{
   error_t error;
   uint_t event;

   //Socket already connected?
   if(socket->state != TCP_STATE_CLOSED)
      return ERROR_ALREADY_CONNECTED;

   //The user owns the socket
   socket->ownedFlag = TRUE;

   //Number of chunks that comprise the TX and the RX buffers
   socket->txBuffer.maxChunkCount = arraysize(socket->txBuffer.chunk);
   socket->rxBuffer.maxChunkCount = arraysize(socket->rxBuffer.chunk);

   //Allocate transmit buffer
   error = chunkedBufferSetLength((ChunkedBuffer *) &socket->txBuffer, socket->txBufferSize);
   //Allocate receive buffer
   if(!error)
      error = chunkedBufferSetLength((ChunkedBuffer *) &socket->rxBuffer, socket->rxBufferSize);

   //Failed to allocate memory?
   if(error)
   {
      //Free any previously allocated memory
      tcpDeleteControlBlock(socket);
      //Report an error to the caller
      return error;
   }

   //Default MSS value
   socket->mss = min(TCP_DEFAULT_MSS, TCP_MAX_MSS);
   //An initial send sequence number is selected
   socket->iss = rand();
   //Initialize TCP control block
   socket->sndUna = socket->iss;
   socket->sndNxt = socket->iss + 1;
   socket->rcvUser = 0;
   socket->rcvWnd = socket->rxBufferSize;
   //Default retransmission timeout
   socket->rto = TCP_INITIAL_RTO;

   //Send a SYN segment
   error = tcpSendSegment(socket, TCP_FLAG_SYN, socket->iss, 0, 0, TRUE);
   //Failed to send TCP segment?
   if(error) return error;

   //Switch to the SYN-SENT state
   tcpChangeState(socket, TCP_STATE_SYN_SENT);
   //Wait for the connection to be established
   event = tcpWaitForEvents(socket, SOCKET_EVENT_CONNECTED |
      SOCKET_EVENT_CLOSED, socket->timeout);

   //Connection successfully established?
   if(event == SOCKET_EVENT_CONNECTED)
      return NO_ERROR;
   //Failed to establish connection?
   else if(event == SOCKET_EVENT_CLOSED)
      return ERROR_CONNECTION_FAILED;
   //Timeout exception?
   else
      return ERROR_TIMEOUT;
}


/**
 * @brief Place a socket in the listening state
 *
 * Place a socket in a state in which it is listening for an incoming connection
 *
 * @param[in] socket Socket to place in the listening state
 * @return Error code
 **/

error_t tcpListen(Socket *socket)
{
   //Socket already connected?
   if(socket->state != TCP_STATE_CLOSED)
      return ERROR_ALREADY_CONNECTED;

   //Place the socket in the listening state
   tcpChangeState(socket, TCP_STATE_LISTEN);

   //Successful processing
   return NO_ERROR;
}


/**
 * @brief Permit an incoming connection attempt on a TCP socket
 * @param[in] socket Handle to a socket previously placed in a listening state
 * @param[out] clientIpAddr IP address of the client
 * @param[out] clientPort Port number used by the client
 * @return Handle to the socket in which the actual connection is made
 **/

Socket *tcpAccept(Socket *socket, IpAddr *clientIpAddr, uint16_t *clientPort)
{
   error_t error;
   TcpSynQueueItem *queueItem;
   Socket *newSocket;

   //Ensure the socket was previously placed in the listening state
   if(tcpGetState(socket) != TCP_STATE_LISTEN)
      return NULL;

   //Enter critical section
   osMutexAcquire(socketMutex);

   //Wait for an connection attempt
   while(1)
   {
      //The SYN queue is empty?
      if(!socket->synQueue)
      {
         //Set the events the application is interested in
         socket->eventMask = SOCKET_EVENT_RX_READY;
         //Reset the event object
         osEventReset(socket->event);
         //Leave critical section
         osMutexRelease(socketMutex);
         //Wait until a SYN message is received from a client
         osEventWait(socket->event, socket->timeout);
         //Enter critical section
         osMutexAcquire(socketMutex);
      }

      //Check whether the queue is still empty
      if(!socket->synQueue)
      {
         //Leave critical section
         osMutexRelease(socketMutex);
         //Timeout error
         return NULL;
      }

      //Point to the first item in the receive queue
      queueItem = socket->synQueue;
      //Return the client IP address and port number
      if(clientIpAddr)
         *clientIpAddr = queueItem->srcAddr;
      if(clientPort)
         *clientPort = queueItem->srcPort;

      //Leave critical section
      osMutexRelease(socketMutex);

      //Create a new socket to handle the incoming connection request
      newSocket = socketOpen(SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP);
      //Failed to open socket?
      if(!newSocket)
      {
         //Debug message
         TRACE_WARNING("Cannot accept TCP connection!\r\n");

         //Remove the item from the SYN queue
         socket->synQueue = queueItem->next;
         //Deallocate memory buffer
         memPoolFree(queueItem);
         //Wait for the next connection attempt
         continue;
      }

      //Enter critical section
      osMutexAcquire(socketMutex);

      //The user owns the socket
      newSocket->ownedFlag = TRUE;

      //Number of chunks that comprise the TX and the RX buffers
      newSocket->txBuffer.maxChunkCount = arraysize(newSocket->txBuffer.chunk);
      newSocket->rxBuffer.maxChunkCount = arraysize(newSocket->rxBuffer.chunk);

      //Allocate transmit buffer
      error = chunkedBufferSetLength((ChunkedBuffer *) &newSocket->txBuffer, newSocket->txBufferSize);
      //Allocate receive buffer
      if(!error)
         error = chunkedBufferSetLength((ChunkedBuffer *) &newSocket->rxBuffer, newSocket->rxBufferSize);

      //Failed to allocate memory?
      if(error)
      {
         //Debug message
         TRACE_WARNING("Cannot accept TCP connection!\r\n");

         //Properly close the socket
         tcpAbort(newSocket);
         //Remove the item from the SYN queue
         socket->synQueue = queueItem->next;
         //Deallocate memory buffer
         memPoolFree(queueItem);
         //Wait for the next connection attempt
         continue;
      }

      //Bind the newly created socket to the appropriate interface
      newSocket->interface = queueItem->interface;
      //Bind the socket to the specified address
      newSocket->localIpAddr = queueItem->destAddr;
      newSocket->localPort = socket->localPort;
      //Save the port number and the IP address of the remote host
      newSocket->remoteIpAddr = queueItem->srcAddr;
      newSocket->remotePort = queueItem->srcPort;
      //Save the maximum segment size
      newSocket->mss = queueItem->mss;

      //Initialize TCP control block
      newSocket->iss = rand();
      newSocket->irs = queueItem->isn;
      newSocket->sndUna = newSocket->iss;
      newSocket->sndNxt = newSocket->iss + 1;
      newSocket->rcvNxt = newSocket->irs + 1;
      newSocket->rcvUser = 0;
      newSocket->rcvWnd = newSocket->rxBufferSize;

      //Default retransmission timeout
      newSocket->rto = TCP_INITIAL_RTO;
      //Initial congestion window
      newSocket->cwnd = min(TCP_INITIAL_WINDOW * newSocket->mss, newSocket->txBufferSize);
      //Slow start threshold should be set arbitrarily high
      newSocket->ssthresh = UINT16_MAX;

      //Send a SYN ACK control segment
      error = tcpSendSegment(newSocket, TCP_FLAG_SYN | TCP_FLAG_ACK,
         newSocket->iss, newSocket->rcvNxt, 0, TRUE);
      //Failed to send TCP segment?
      if(error)
      {
         //Debug message
         TRACE_WARNING("Cannot accept TCP connection!\r\n");

         //Close previously created socket
         tcpAbort(newSocket);
         //Remove the item from the SYN queue
         socket->synQueue = queueItem->next;
         //Deallocate memory buffer
         memPoolFree(queueItem);
         //Wait for the next connection attempt
         continue;
      }

      //Remove the item from the SYN queue
      socket->synQueue = queueItem->next;
      //Deallocate memory buffer
      memPoolFree(queueItem);
      //Update the state of events
      tcpUpdateEvents(socket);

      //The connection state should be changed to SYN-RECEIVED
      tcpChangeState(newSocket, TCP_STATE_SYN_RECEIVED);

      //Leave critical section
      osMutexRelease(socketMutex);
      //Return a handle to the newly created socket
      return newSocket;
   }
}


/**
 * @brief Send data to a connected socket
 * @param[in] socket Handle that identifies a connected socket
 * @param[in] data Pointer to a buffer containing the data to be transmitted
 * @param[in] length Number of bytes to be transmitted
 * @param[out] written Actual number of bytes written (optional parameter)
 * @param[in] flags Set of flags that influences the behavior of this function
 * @return Error code
 **/

error_t tcpSend(Socket *socket, const uint8_t *data,
   size_t length, size_t *written, uint_t flags)
{
   uint_t n;
   uint_t totalLength;
   uint_t event;

   //Check whether the socket is in the listening state
   if(socket->state == TCP_STATE_LISTEN)
      return ERROR_NOT_CONNECTED;

   //Send as much data as possible
   for(totalLength = 0; totalLength < length; )
   {
      //Wait until there is more room in the send buffer
      event = tcpWaitForEvents(socket, SOCKET_EVENT_TX_READY, socket->timeout);

      //A timeout exception occurred?
      if(event != SOCKET_EVENT_TX_READY)
         return ERROR_TIMEOUT;

      //Check current TCP state
      switch(socket->state)
      {
      //ESTABLISHED or CLOSE-WAIT state?
      case TCP_STATE_ESTABLISHED:
      case TCP_STATE_CLOSE_WAIT:
         //The send buffer is now available for writing
         break;

      //LAST-ACK, FIN-WAIT-1, FIN-WAIT-2, CLOSING or TIME-WAIT state?
      case TCP_STATE_LAST_ACK:
      case TCP_STATE_FIN_WAIT_1:
      case TCP_STATE_FIN_WAIT_2:
      case TCP_STATE_CLOSING:
      case TCP_STATE_TIME_WAIT:
         //The connection is being closed
         return ERROR_CONNECTION_CLOSING;

      //CLOSED state?
      default:
         //The connection was reset by remote side?
         return (socket->resetFlag) ? ERROR_CONNECTION_RESET : ERROR_NOT_CONNECTED;
      }

      //Determine the actual number of bytes in the send buffer
      n = socket->sndUser + socket->sndNxt - socket->sndUna;
      //Exit immediately if the transmission buffer is full (sanity check)
      if(n >= socket->txBufferSize)
         return ERROR_FAILURE;

      //Number of bytes available for writing
      n = socket->txBufferSize - n;
      //Calculate the number of bytes to copy at a time
      n = min(n, length - totalLength);

      //Copy user data to send buffer
      tcpWriteTxBuffer(socket, socket->sndNxt + socket->sndUser, data, n);

      //Update the number of data buffered but not yet sent
      socket->sndUser += n;
      //Advance data pointer
      data += n;
      //Update byte counter
      totalLength += n;
      //Total number of data that have been written
      if(written) *written = totalLength;

      //Update TX events
      tcpUpdateEvents(socket);

      //To avoid a deadlock, it is necessary to have a timeout to force
      //transmission of data, overriding the SWS avoidance algorithm. In
      //practice, this timeout should seldom occur (see RFC 1122 4.2.3.4)
      if(socket->sndUser == n)
         osTimerStart(&socket->overrideTimer, TCP_OVERRIDE_TIMEOUT);

      //The Nagle algorithm should be implemented to coalesce
      //short segments (refer to RFC 1122 4.2.3.4)
      tcpNagleAlgo(socket);
   }

   //The SOCKET_FLAG_WAIT_ACK flag causes the function to
   //wait for acknowledgement from the remote side
   if(flags & SOCKET_FLAG_WAIT_ACK)
   {
      //Wait for the data to be acknowledged
      event = tcpWaitForEvents(socket, SOCKET_EVENT_TX_COMPLETE, socket->timeout);

      //A timeout exception occurred?
      if(event != SOCKET_EVENT_TX_COMPLETE)
         return ERROR_TIMEOUT;

      //The connection was closed before an acknowledgement was received?
      if(socket->state != TCP_STATE_ESTABLISHED && socket->state != TCP_STATE_CLOSE_WAIT)
         return ERROR_NOT_CONNECTED;
   }

   //Successful write operation
   return NO_ERROR;
}


/**
 * @brief Receive data from a connected socket
 * @param[in] socket Handle that identifies a connected socket
 * @param[out] data Buffer where to store the incoming data
 * @param[in] size Maximum number of bytes that can be received
 * @param[out] received Number of bytes that have been received
 * @param[in] flags Set of flags that influences the behavior of this function
 * @return Error code
 **/

error_t tcpReceive(Socket *socket, uint8_t *data,
   size_t size, size_t *received, uint_t flags)
{
   uint_t i;
   uint_t n;
   uint_t event;
   uint32_t seqNum;

   //Retrieve the break character code
   char_t c = LSB(flags);
   //No data has been read yet
   *received = 0;

   //Check whether the socket is in the listening state
   if(socket->state == TCP_STATE_LISTEN)
      return ERROR_NOT_CONNECTED;

   //Read as much data as possible
   while(*received < size)
   {
      //Wait for data to be available for reading
      event = tcpWaitForEvents(socket, SOCKET_EVENT_RX_READY, socket->timeout);

      //A timeout exception occurred?
      if(event != SOCKET_EVENT_RX_READY)
         return ERROR_TIMEOUT;

      //Check current TCP state
      switch(socket->state)
      {
      //ESTABLISHED, FIN-WAIT-1 or FIN-WAIT-2 state?
      case TCP_STATE_ESTABLISHED:
      case TCP_STATE_FIN_WAIT_1:
      case TCP_STATE_FIN_WAIT_2:
         //Sequence number of the first byte to read
         seqNum = socket->rcvNxt - socket->rcvUser;
         //Data is available in the receive buffer
         break;

      //CLOSE-WAIT, LAST-ACK, CLOSING or TIME-WAIT state?
      case TCP_STATE_CLOSE_WAIT:
      case TCP_STATE_LAST_ACK:
      case TCP_STATE_CLOSING:
      case TCP_STATE_TIME_WAIT:
         //The user must be satisfied with data already on hand
         if(!socket->rcvUser)
         {
            if(*received > 0)
               return NO_ERROR;
            else
               return ERROR_END_OF_STREAM;
         }

         //Sequence number of the first byte to read
         seqNum = (socket->rcvNxt - 1) - socket->rcvUser;
         //Data is available in the receive buffer
         break;

      //CLOSED state?
      default:
         //The connection was reset by remote side?
         if(socket->resetFlag)
            return ERROR_CONNECTION_RESET;
         //The connection has not yet been established?
         if(!socket->closedFlag)
            return ERROR_NOT_CONNECTED;

         //The user must be satisfied with data already on hand
         if(!socket->rcvUser)
         {
            if(*received > 0)
               return NO_ERROR;
            else
               return ERROR_END_OF_STREAM;
         }

         //Sequence number of the first byte to read
         seqNum = (socket->rcvNxt - 1) - socket->rcvUser;
         //Data is available in the receive buffer
         break;
      }

      //Sanity check
      if(!socket->rcvUser)
         return ERROR_FAILURE;

      //Calculate the number of bytes to read at a time
      n = min(socket->rcvUser, size - *received);
      //Copy data from circular buffer
      tcpReadRxBuffer(socket, seqNum, data, n);

      //Read data until a break character is encountered?
      if(flags & SOCKET_FLAG_BREAK_CHAR)
      {
         //Search for the specified break character
         for(i = 0; i < n && data[i] != c; i++);
         //Adjust the number of data to read
         n = min(n, i + 1);
      }

      //Total number of data that have been read
      *received += n;
      //Remaining data still available in the receive buffer
      socket->rcvUser -= n;

      //Update the receive window
      tcpUpdateReceiveWindow(socket);
      //Update RX event state
      tcpUpdateEvents(socket);

      //The SOCKET_FLAG_BREAK_CHAR flag causes the function to stop reading
      //data as soon as the specified break character is encountered
      if(flags & SOCKET_FLAG_BREAK_CHAR)
      {
         //Check whether a break character has been found
         if(data[n - 1] == c) break;
      }
      //The SOCKET_FLAG_WAIT_ALL flag causes the function to return
      //only when the requested number of bytes have been read
      else if(!(flags & SOCKET_FLAG_WAIT_ALL))
      {
         break;
      }

      //Advance data pointer
      data += n;
   }

   //Successful read operation
   return NO_ERROR;
}


/**
 * @brief Shutdown gracefully reception, transmission, or both
 *
 * Note that socketShutdown() does not close the socket, and resources attached
 * to the socket will not be freed until socketClose() is invoked
 *
 * @param[in] socket Handle to a socket
 * @param[in] how Flag that describes what types of operation will no longer be allowed
 * @return Error code
 **/

error_t tcpShutdown(Socket *socket, uint_t how)
{
   error_t error;
   uint_t event;

   //Disable transmission?
   if(how == SOCKET_SD_SEND || how == SOCKET_SD_BOTH)
   {
      //Check current state
      switch(socket->state)
      {
      //CLOSED or LISTEN state?
      case TCP_STATE_CLOSED:
      case TCP_STATE_LISTEN:
         //Connection does not exist
         return ERROR_NOT_CONNECTED;

      //SYN-RECEIVED or ESTABLISHED state?
      case TCP_STATE_SYN_RECEIVED:
      case TCP_STATE_ESTABLISHED:
         //Make sure all the data has been sent out
         event = tcpWaitForEvents(socket, SOCKET_EVENT_TX_COMPLETE, socket->timeout);
         //Timeout error?
         if(event != SOCKET_EVENT_TX_COMPLETE)
            return ERROR_TIMEOUT;

         //Send a FIN segment
         error = tcpSendSegment(socket, TCP_FLAG_FIN | TCP_FLAG_ACK,
            socket->sndNxt, socket->rcvNxt, 0, TRUE);
         //Failed to send FIN segment?
         if(error) return error;

         //Sequence number expected to be received
         socket->sndNxt++;
         //Switch to the FIN-WAIT1 state
         tcpChangeState(socket, TCP_STATE_FIN_WAIT_1);

         //Wait for the FIN to be acknowledged
         event = tcpWaitForEvents(socket, SOCKET_EVENT_TX_SHUTDOWN, socket->timeout);
         //Timeout interval elapsed?
         if(event != SOCKET_EVENT_TX_SHUTDOWN)
            return ERROR_TIMEOUT;

         //Continue processing...
         break;

      //CLOSE-WAIT state?
      case TCP_STATE_CLOSE_WAIT:
         //Make sure all the data has been sent out
         event = tcpWaitForEvents(socket, SOCKET_EVENT_TX_COMPLETE, socket->timeout);
         //Timeout error?
         if(event != SOCKET_EVENT_TX_COMPLETE)
            return ERROR_TIMEOUT;

         //Send a FIN segment
         error = tcpSendSegment(socket, TCP_FLAG_FIN | TCP_FLAG_ACK,
            socket->sndNxt, socket->rcvNxt, 0, TRUE);
         //Failed to send FIN segment?
         if(error) return error;

         //Sequence number expected to be received
         socket->sndNxt++;
         //Switch to the LAST-ACK state
         tcpChangeState(socket, TCP_STATE_LAST_ACK);

         //Wait for the FIN to be acknowledged
         event = tcpWaitForEvents(socket, SOCKET_EVENT_TX_SHUTDOWN, socket->timeout);
         //Timeout interval elapsed?
         if(event != SOCKET_EVENT_TX_SHUTDOWN)
            return ERROR_TIMEOUT;

         //Continue processing...
         break;

      //SYN-SENT, FIN-WAIT-1, FIN-WAIT-2, CLOSING,
      //TIME-WAIT or LAST-ACK state?
      default:
         //Continue processing...
         break;
      }
   }

   //Disable reception?
   if(how == SOCKET_SD_RECEIVE || how == SOCKET_SD_BOTH)
   {
      //Check current state
      switch(socket->state)
      {
      //CLOSED or LISTEN state?
      case TCP_STATE_CLOSED:
      case TCP_STATE_LISTEN:
         //Connection does not exist
         return ERROR_NOT_CONNECTED;

      //SYN-SENT, SYN-RECEIVED, ESTABLISHED, FIN-WAIT-1 or FIN-WAIT-2 state?
      case TCP_STATE_SYN_SENT:
      case TCP_STATE_SYN_RECEIVED:
      case TCP_STATE_ESTABLISHED:
      case TCP_STATE_FIN_WAIT_1:
      case TCP_STATE_FIN_WAIT_2:
         //Wait for a FIN to be received
         event = tcpWaitForEvents(socket, SOCKET_EVENT_RX_SHUTDOWN, socket->timeout);
         //Timeout interval elapsed?
         if(event != SOCKET_EVENT_RX_SHUTDOWN)
            return ERROR_TIMEOUT;
         //A FIN segment has been received
         break;

      //CLOSING, TIME-WAIT, CLOSE-WAIT or LAST-ACK state?
      default:
         //A FIN segment has already been received
         break;
      }
   }

   //Successful operation
   return NO_ERROR;
}


/**
 * @brief Abort an existing TCP connection
 * @param[in] socket Handle identifying the socket to close
 * @return Error code
 **/

error_t tcpAbort(Socket *socket)
{
   error_t error;

   //Check current state
   switch(socket->state)
   {
   //SYN-RECEIVED, ESTABLISHED, FIN-WAIT-1
   //FIN-WAIT-2 or CLOSE-WAIT state?
   case TCP_STATE_SYN_RECEIVED:
   case TCP_STATE_ESTABLISHED:
   case TCP_STATE_FIN_WAIT_1:
   case TCP_STATE_FIN_WAIT_2:
   case TCP_STATE_CLOSE_WAIT:
      //Send a reset segment
      error = tcpSendSegment(socket, TCP_FLAG_RST, socket->sndNxt, 0, 0, FALSE);
      //Enter CLOSED state
      tcpChangeState(socket, TCP_STATE_CLOSED);
      //Delete TCB
      tcpDeleteControlBlock(socket);
      //Mark the socket as closed
      socket->type = SOCKET_TYPE_UNUSED;
      //Return status code
      return error;

   //TIME-WAIT state?
   case TCP_STATE_TIME_WAIT:
      //The user doe not own the socket anymore...
      socket->ownedFlag = FALSE;
      //TCB will be deleted and socket will be closed
      //when the 2MSL timer will elapse
      return NO_ERROR;

   //Any other state?
   default:
      //Enter CLOSED state
      tcpChangeState(socket, TCP_STATE_CLOSED);
      //Delete TCB
      tcpDeleteControlBlock(socket);
      //Mark the socket as closed
      socket->type = SOCKET_TYPE_UNUSED;
      //No error to report
      return NO_ERROR;
   }
}


/**
 * @brief Get the current state of the TCP FSM
 * @param[in] socket Handle identifying the socket
 * @return TCP FSM state
 **/

TcpState tcpGetState(Socket *socket)
{
   TcpState state;

   //Enter critical section
   osMutexAcquire(socketMutex);
   //Get TCP FSM current state
   state = socket->state;
   //Leave critical section
   osMutexRelease(socketMutex);

   //Return current state
   return state;
}

#endif
