/** Sensorino library.
 * This library abstracts the nRF24L01.
 * Decisions taken:
 * - pipe 0 is used as broadcast pipe, with shared address and no acks
 * - pipe 1 is used as private address
 * - nodes send their address
 * - addresses are 4 bytes long
 * - CRC is 2 bytes
 * - 2Mbps, 750us ack time, 3 retries
 *
 * Author: Dario Salvi (dariosalvi78 at gmail dot com)
 *
 * Licensed under the GPL license http://www.gnu.org/copyleft/gpl.html
 */

#ifndef SENSORINO_h
#define SENSORINO_h

#include "NRF24.h"

//The pipe used for broadcast messages
#define BROADCAST_PIPE 0

//Default broadcast address
#define BROADCAST_ADDR 0,255,0,255

//The pipe used for private messages
#define PRIVATE_PIPE 1

//Default adrress of the base station
#define BASE_ADDR 255,0,255,0

//Default radio channel
#define RF_CHANNEL 10

/** Sensorino, library that builds on top of the nRF24L01 chip.
 *
 *
 *
 */
class Sensorino : public NRF24
{
    friend class Base;

    public:

        /** The broadcast address. Default is 0 255 0 255
         */
        static byte broadCastAddress[4];

        /** The address of the base station.
         */
        static byte baseAddress[4];

        /** The address of this node
         */
        static byte thisAddress[4];

        /** Configures Sensorino.
         * init() must be called to initialise the interface and the radio module
         * @param chipEnablePin the Arduino pin to use to enable the chip for transmit/receive
         * @param chipSelectPin the Arduino pin number of the output to use to select the NRF24 before
         * @param myAddress the address of this node
         */
        static void configure(byte chipEnablePin, byte chipSelectPin, byte myAddress[]);

        /** Initialises this instance and the radio module connected to it.
         * Initializes the SPI
         * - Set the radio to powerDown
         */
        static void init();

        /** Sends a packet to the base.
         * @param service the service id
         * @param data data associated
         * @param len the length of the data
         * @return true on success
         */
        static boolean sendToBase(unsigned int service, byte* data, int len);

        /** Sends a packet to broadcast.
         * @param service the service id
         * @param data data associated
         * @param len the length of the data
         * @return true on success
         */
        static boolean sendToBroadcast(unsigned int service, byte* data, int len);

        /** Waits to receive a packet on a pipe, if received it extracts sender and service.
         * @param timeout a timeout in millis
         * @param pipe the pipe number will be written here
         * @param sender a buffer or 4 bytes where the sender address will be stored
         * @param service here the service number will be stored
         * @param data here the data will be stored
         * @param len the length of the data
         * @return true if data has been received
         */
        static boolean receive(unsigned int timeout, byte* pipe, byte* sender, unsigned int* service, byte* data, int* len);

    protected:

    private:

        /** Composes the base packet with sender information.
         */
        static void composeBasePacket(byte* buffer, unsigned int service, byte* data, int len);

        /** Decomposes the base packet.
         * @param packet the raw packet
         * @param totlen the length of the raw packet
         * @param sender a buffer where the sender address will be written, must be of size 4
         * @param service the service number
         * @param data a buffer where the data will be written. Must be of size 26.
         * @param len the length of the data without base packet
         */
        static void decomposeBasePacket(byte* packet, int totlen, byte* sender, unsigned int* service, byte* data, int* len);

};

/** THE instance, Arduino style
 */
extern Sensorino sensorino;

#endif // SENSORINO_h
