/*
 * Copyright (c) 2010  by Radek Pazdera <radek.pazdera@gmail.com>
 *
 * This file is part of nfgen.
 *
 * nfgen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nfgen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with nfgen.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/stat.h>

#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "nfgen.h"

#define MIN_FLOW_DURATION 1
#define MAX_FLOW_DURATION 60

#define SRC_PORT 10000
#define DEST_PORT 2055

#define PRG_SEED 5

#define NETFLOW_HEADER_SIZE 24
#define NETFLOW_RECORD_SIZE 48

#define NUMBER_OF_ADDRESSES 4
const char *addresses[NUMBER_OF_ADDRESSES] =
{
  "127.0.0.1",
  "192.168.1.100",
  "192.168.1.101",
  "192.168.1.102"
//   0x7F000001, /* 127.0.0.1 */
//   0xC0A80164, /* 192.168.1.100 */
//   0xC0A80165, /* 192.168.1.101 */
//   0xC0A80166  /* 192.168.1.102 */
};



in_addr_t convertAddress(const char *addressInDotNotation)
{
  in_addr_t conversionResult;
  if (inet_pton(AF_INET, addressInDotNotation, (void *) &conversionResult) != 1)
  {
    perror("Address conversion failed.");
  }

  return conversionResult;
}

in_addr_t generateRandomAddress()
{
  /* This could be more sophisticated */
  return convertAddress(addresses[(1 + rand()) % NUMBER_OF_ADDRESSES]);
}

in_port_t generateRandomPortNumber()
{
  return rand() % 65536;
}

char generateRandomTCPFlags()
{
  return rand() % 255;
}

size_t makeNetflowPacket(char *buffer, int numberOfFlows, time_t systemStartTime)
{
  time_t currentTime = time(0);

  struct netflowRecord record;
  struct netflowHeader header;
  
  for (int flow = 0;flow < numberOfFlows; flow++)
  {
    // Addresses are already in network byte order
    record.srcAddr = generateRandomAddress();
    record.dstAddr = generateRandomAddress();

    record.nextHop = 0;
    record.input = 0;
    record.output = 0;

    // Some random flow lengths
    record.dPkts = rand() % 100000;
    record.dOctets = record.dPkts * (rand() % 300);
    record.dPkts = htonl(record.dPkts);
    record.dOctets = htonl(record.dOctets);

    record.first = (currentTime - systemStartTime - (MIN_FLOW_DURATION + rand()) % MAX_FLOW_DURATION)*1000;
    record.last = record.first + (rand() % MAX_FLOW_DURATION)*1000;
    record.first = htonl(record.first);
    record.last = htonl(record.last);

    record.srcPort = htons(generateRandomPortNumber());
    record.dstPort = htons(generateRandomPortNumber());
    
    record.pad = 0;
    
    record.prot = rand() % 2 ? IPPROTO_TCP : IPPROTO_UDP;
    record.tcpFlags = record.prot == IPPROTO_TCP ? generateRandomTCPFlags() : 0;
    record.tos = 0;
    record.srcAs = 0;
    record.dstAs = 0;
    record.srcMask = 0;
    record.dstMask = 0;
    
    record.drops = 0;

    memcpy(buffer + NETFLOW_HEADER_SIZE + flow*NETFLOW_RECORD_SIZE, &record, NETFLOW_RECORD_SIZE);
  }

  /* Setup header */
  header.version      = 5;
  header.count        = numberOfFlows;
  
  header.sysUpTime    = htonl((currentTime - systemStartTime) * 1000); // Time since the program was run is used
  header.unixSecs     = htonl(currentTime);

  // Random amount of residual nanoseconds is generated for testing purposes
  header.unixNsecs    = htonl(rand() % (1000000000 - 1));

  // NIY TODO
  header.flowSequence = 0;

  memcpy(buffer, &header, sizeof(struct netflowHeader));

  // returns size of generated pdu
  return sizeof(struct netflowHeader) + numberOfFlows*sizeof(struct netflowRecord);
}

int udpSend(in_addr_t addr, in_port_t port, void *buff, size_t nbytes)
{
  int udtSocketFileDescriptor = socket(AF_INET, SOCK_DGRAM, 0);

  if (udtSocketFileDescriptor <= 0)
  {
    perror("Unable to create socket.");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in socketAddress;
  memset(&socketAddress, 0, sizeof(socketAddress));
  socketAddress.sin_family = AF_INET;
  socketAddress.sin_addr.s_addr = htonl(0);
  socketAddress.sin_port = htons(SRC_PORT);

  if (bind(udtSocketFileDescriptor, (const struct sockaddr *) &socketAddress, sizeof(socketAddress)) == -1)
  {
    perror("Unable to bind.");
    exit(EXIT_FAILURE);
  }

  // Now we're ready to go.
  struct stat info;
  if (fstat(udtSocketFileDescriptor, &info) != 0)
  {
    perror("Descriptor is invalid.");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(addr);
  sa.sin_port = htons(port);
  ssize_t nsend = sendto(udtSocketFileDescriptor, buff, nbytes, 0, (const struct sockaddr *) &sa, sizeof(sa));

  close(udtSocketFileDescriptor);

  return nsend;
}

int main(int argc, char **argv)
{
  in_addr_t remoteAddress = convertAddress("147.229.176.19"); //0x7f000001; // 127.0.0.1
  in_port_t remotePort = DEST_PORT;

  time_t systemStartTime = time(0);
  unsigned int flowsGenerated = 0;

  int seed = PRG_SEED;
  srand(seed);

  char buffer[1480];
  memset(buffer, 0, 1480);
  size_t pduSize = 0;

  while(1)
  {
    flowsGenerated = (1 + rand()) % 30;
    pduSize = makeNetflowPacket(buffer, flowsGenerated, systemStartTime);

    fprintf(stderr, "%i\n", udpSend(remoteAddress, remotePort, buffer, pduSize));

    sleep(rand() % 3);
  }

  return 0;
}