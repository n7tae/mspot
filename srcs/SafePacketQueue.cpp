/****************************************************************
 *                                                              *
 *             More - An M17-only Repeater/HotSpot              *
 *                                                              *
 *         Copyright (c) 2024 by Thomas A. Early N7TAE          *
 *                                                              *
 * See the LICENSE file for details about the software license. *
 *                                                              *
 ****************************************************************/

 #include <memory>

 #include "SafePacketQueue.h"
 #include "Packet.h"

// global packet FIFO queues

IPFrameFIFO Host2Gate;
IPFrameFIFO Gate2Host;
