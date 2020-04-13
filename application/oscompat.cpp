/*
* oscompat.cpp
*****************************************************************************
* Copyright (C) 2015, liberty_developer
*
* Email: liberty.developer@xmail.net
*
* This source code and its use and distribution, is subject to the terms
* and conditions of the applicable license agreement.
*****************************************************************************/

#include "oscompat.h"
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
bool WSASU()
{
	WSADATA wsaData;
	// Initialize Winsock version 2.2
	return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}
void WSACU(){ WSACleanup(); };
const char *widevinedll = "widevinecdm.dll";
#define socklen_t int
#else
bool WSASU(){return true;}
void WSACU(){};
const char *widevinedll = "./libwidevinecdm.so";
time_t _mkgmtime(struct tm *tm)
{
  time_t ret;
  char *tz;

  tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzset();
  ret = mktime(tm);
  if (tz)
    setenv("TZ", tz, 1);
  else
    unsetenv("TZ");
  tzset();
  return ret;
}
#endif