/*
* helpers.h
*****************************************************************************
* Copyright (C) 2015, liberty_developer
*
* Email: liberty.developer@xmail.net
*
* This source code and its use and distribution, is subject to the terms
* and conditions of the applicable license agreement.
*****************************************************************************/

#pragma once

#include <string>
#include <stdint.h>

bool b64_decode(const char *in, unsigned int in_len, uint8_t *out, unsigned int &out_len);

std::string b64_encode(unsigned char const* in, unsigned int in_len, bool urlEncode);

bool getInitDataFromStream(uint8_t *initData, unsigned int initdata_size, uint8_t *out, unsigned int &out_len);

void findreplace(std::string& str, const char *f, const char *r);
