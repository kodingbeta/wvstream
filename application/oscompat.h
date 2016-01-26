/*
* oscompat.h
*****************************************************************************
* Copyright (C) 2015, liberty_developer
*
* Email: liberty.developer@xmail.net
*
* This source code and its use and distribution, is subject to the terms
* and conditions of the applicable license agreement.
*****************************************************************************/

#pragma once

bool WSASU();
void WSACU();

#ifdef _WIN32
#define socklen_t int
#else
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define SOCKET_ERROR - 1
#define closesocket close
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
