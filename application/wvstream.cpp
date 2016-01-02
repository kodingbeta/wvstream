/*
* libwvstream.cpp
*****************************************************************************
* Copyright (C) 2015, liberty_developer
*
* Email: liberty.developer@xmail.net
*
* This source code and its use and distribution, is subject to the terms
* and conditions of the applicable license agreement.
*****************************************************************************/

// libwvstream.cpp : Defines the exported functions for the DLL application.
//

#include <iostream>
#include <thread>
#include <string.h>

#include "dash/DASHTree.h"
#include "dash/DASHStream.h"
#include "cdm/media/cdm/cdm_adapter.h"
#include "../libbento4/Core/Ap4.h"
#include "helpers.h"
#include "../libcurl/include/curl/curl.h"

/*******************************************************
						CDM
********************************************************/

/*----------------------------------------------------------------------
|   CdmDecryptedBlock implementation
+---------------------------------------------------------------------*/

class CdmDecryptedBlock : public cdm::DecryptedBlock {
public:
	CdmDecryptedBlock() :buffer_(0), timestamp_(0) {};
	virtual ~CdmDecryptedBlock() {};

	virtual void SetDecryptedBuffer(cdm::Buffer* buffer) override { buffer_ = buffer; };
	virtual cdm::Buffer* DecryptedBuffer() override { return buffer_; };

	virtual void SetTimestamp(int64_t timestamp) override { timestamp_ = timestamp; };
	virtual int64_t Timestamp() const override { return timestamp_; };
private:
	cdm::Buffer *buffer_;
	int64_t timestamp_;
};

/*----------------------------------------------------------------------
|   CdmDecryptedBlock implementation
+---------------------------------------------------------------------*/
class CdmBuffer : public cdm::Buffer {
public:
	CdmBuffer(AP4_DataBuffer *buffer) :buffer_(buffer) {};
	virtual ~CdmBuffer() {};

	virtual void Destroy() override {};

	virtual uint32_t Capacity() const override
	{
		return buffer_->GetBufferSize();
	};
	virtual uint8_t* Data() override
	{
		return (uint8_t*)buffer_->GetData();
	};
	virtual void SetSize(uint32_t size) override
	{
		buffer_->SetDataSize(size);
	};
	virtual uint32_t Size() const override
	{
		return buffer_->GetDataSize();
	};
private:
	AP4_DataBuffer *buffer_;
};

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter
+---------------------------------------------------------------------*/
class WV_CencSingleSampleDecrypter : public AP4_CencSingleSampleDecrypter
{
public:
	// methods
	WV_CencSingleSampleDecrypter(media::CdmAdapter *adapter) :
		AP4_CencSingleSampleDecrypter(0),
		wv_adapter(adapter),
		max_subsample_count_(0),
		subsample_buffer_(0) {};
	virtual AP4_Result DecryptSampleData(AP4_DataBuffer& data_in,
		AP4_DataBuffer& data_out,

		// always 16 bytes
		const AP4_UI08* iv,

		// pass 0 for full decryption
		unsigned int    subsample_count,

		// array of <subsample_count> integers. NULL if subsample_count is 0
		const AP4_UI16* bytes_of_cleartext_data,

		// array of <subsample_count> integers. NULL if subsample_count is 0
		const AP4_UI32* bytes_of_encrypted_data);

private:
	media::CdmAdapter *wv_adapter;
	unsigned int max_subsample_count_;
	cdm::SubsampleEntry *subsample_buffer_;
};

/*----------------------------------------------------------------------
|   WV_CencSingleSampleDecrypter::DecryptSampleData
+---------------------------------------------------------------------*/
AP4_Result WV_CencSingleSampleDecrypter::DecryptSampleData(
	AP4_DataBuffer& data_in,
	AP4_DataBuffer& data_out,
	const AP4_UI08* iv,
	unsigned int    subsample_count,
	const AP4_UI16* bytes_of_cleartext_data,
	const AP4_UI32* bytes_of_encrypted_data)
{
	// the output has the same size as the input
	data_out.SetDataSize(data_in.GetDataSize());

	// check input parameters
	if (iv == NULL) return AP4_ERROR_INVALID_PARAMETERS;
	if (subsample_count) {
		if (bytes_of_cleartext_data == NULL || bytes_of_encrypted_data == NULL) {
			return AP4_ERROR_INVALID_PARAMETERS;
		}
	}

	// transform ap4 format into cmd format
	cdm::InputBuffer cdm_in;
	if (subsample_count > max_subsample_count_)
	{
		subsample_buffer_ = (cdm::SubsampleEntry*)realloc(subsample_buffer_, subsample_count*sizeof(cdm::SubsampleEntry));
		max_subsample_count_ = subsample_count;
	}
	for (cdm::SubsampleEntry *b(subsample_buffer_), *e(subsample_buffer_ + subsample_count); b != e; ++b, ++bytes_of_cleartext_data, ++bytes_of_encrypted_data)
	{
		b->clear_bytes = *bytes_of_cleartext_data;
		b->cipher_bytes = *bytes_of_encrypted_data;
	}
	cdm_in.data = data_in.GetData();
	cdm_in.data_size = data_in.GetDataSize();
	cdm_in.iv = iv;
	cdm_in.iv_size = 16; //Always 16, see AP4_CencSingleSampleDecrypter declaration.
	cdm_in.key_id = wv_adapter->GetKeyId();
	cdm_in.key_id_size = wv_adapter->GetKeyIdSize();
	cdm_in.num_subsamples = subsample_count;
	cdm_in.subsamples = subsample_buffer_;

	CdmBuffer buf(&data_out);
	CdmDecryptedBlock cdm_out;
	cdm_out.SetDecryptedBuffer(&buf);

	cdm::Status ret = wv_adapter->Decrypt(cdm_in, &cdm_out);

	return AP4_SUCCESS;
}

/*******************************************************
					Bento4 Streams
********************************************************/

class AP4_DASHStream : public AP4_ByteStream
{
public:
	// Constructor
	AP4_DASHStream(dash::DASHStream *dashStream) :dash_stream_(dashStream){};

	// AP4_ByteStream methods
	AP4_Result ReadPartial(void*    buffer,
		AP4_Size  bytesToRead,
		AP4_Size& bytesRead) override
	{
		bytesRead = dash_stream_->read(buffer, bytesToRead);
		return bytesRead > 0 ? AP4_SUCCESS : AP4_ERROR_READ_FAILED;
	};
	AP4_Result WritePartial(const void* buffer,
		AP4_Size    bytesToWrite,
		AP4_Size&   bytesWritten) override
	{
		/* unimplemented */
		return AP4_ERROR_NOT_SUPPORTED;
	};
	AP4_Result Seek(AP4_Position position) override
	{
		return dash_stream_->seek(position)?AP4_SUCCESS:AP4_ERROR_NOT_SUPPORTED;
	};
	AP4_Result Tell(AP4_Position& position) override
	{
		position = dash_stream_->tell();
		return AP4_SUCCESS;
	};
	AP4_Result GetSize(AP4_LargeSize& size) override
	{
		/* unimplemented */
		return AP4_ERROR_NOT_SUPPORTED;
	};
	// AP4_Referenceable methods
	void AddReference() override {};
	void Release()override      {};
protected:
	// members
	dash::DASHStream *dash_stream_;
};

class AP4_SockStream : public AP4_ByteStream
{
public:
	// Constructor
	AP4_SockStream(int socket, AP4_Position offset) :socket_(socket), send_offset_(offset), position_(0), buffer_start_(~0){};

	// AP4_ByteStream methods
	AP4_Result ReadPartial(void*    buffer,
		AP4_Size  bytesToRead,
		AP4_Size& bytesRead) override
	{
		/* unimplemented */
		return AP4_ERROR_NOT_SUPPORTED;
	};
	AP4_Result WritePartial(const void* buffer,
		AP4_Size    bytesToWrite,
		AP4_Size&   bytesWritten) override
	{
		if (~buffer_start_)
		{
			AP4_Position rel = position_ - buffer_start_;
			if (rel + bytesToWrite > buffer_.size())
				buffer_.resize(static_cast<size_t>(rel + bytesToWrite));
			memcpy(&buffer_[static_cast<size_t>(rel)], buffer, bytesToWrite);
			bytesWritten = bytesToWrite;
			position_ += static_cast<AP4_Size>(bytesWritten);
			return AP4_SUCCESS;
		}
		int nWritten(0);
		bytesWritten = 0;
		const char *b(static_cast<const char *>(buffer));
		if (position_ < send_offset_)
		{
			if (position_ + bytesToWrite < send_offset_)
				return AP4_SUCCESS;
			bytesWritten = static_cast<AP4_Size>(send_offset_ - position_);
			bytesToWrite -= bytesWritten;
			b += bytesWritten;
		}
		while (bytesToWrite && (nWritten = send(socket_, b, bytesToWrite, 0)) != SOCKET_ERROR)
		{
			bytesToWrite -= nWritten;
			b += nWritten;
			bytesWritten += nWritten;
		}
		position_ += static_cast<AP4_Size>(bytesWritten);

		return (nWritten != SOCKET_ERROR) ? AP4_SUCCESS : AP4_ERROR_WRITE_FAILED;
	};
	AP4_Result Seek(AP4_Position position) override
	{
		if (~buffer_start_ && position >= buffer_start_)
		{
			if (position > buffer_start_ + buffer_.size())
				buffer_.resize(static_cast<std::string::size_type>(position - buffer_start_));
			position_ = position;
			return AP4_SUCCESS;
		}
		return AP4_ERROR_WRITE_FAILED;
	};
	AP4_Result Tell(AP4_Position& position) override
	{
		position = position_;
		return AP4_SUCCESS;
	};
	AP4_Result GetSize(AP4_LargeSize& size) override
	{
		/* unimplemented */
		return AP4_ERROR_NOT_SUPPORTED;
	};
	AP4_Result Buffer() 
	{ 
		buffer_start_ = position_;
		buffer_.clear();
		return AP4_SUCCESS;
	}
	AP4_Result Flush()
	{
		buffer_start_ = ~0;
		position_ -= buffer_.size();
		return Write(&buffer_[0], buffer_.size());
	}
	// AP4_Referenceable methods
	void AddReference() override {};
	void Release()override      {};
protected:
	// members
	int socket_;
	AP4_Position position_;
	AP4_Position buffer_start_;
	AP4_Position send_offset_;
	std::vector<uint8_t> buffer_;
};

/*******************************************************
Main class Session
********************************************************/
class Session
{
public:
	Session(uint32_t session_id, const char *mpdURL, const char *licenseURL);
	Session(uint32_t session_id, const char *encoded);
	~Session();
	uint32_t GetSessionId(){ return session_id_; };
	bool initialize();
	void SetStreamProperties(uint16_t width, uint16_t height, const char* language, uint32_t maxBitPS, bool allow_ec_3);
	bool play(int socket_desc = 0, AP4_Position byteOffset=0);
	void stop();

private:
	void thread_play(AP4_Position byteOffset);

	uint32_t session_id_;
	uint64_t session_expiation_;
	
	std::string wvLicenseURL_;
	std::string mpdFileURL_;

	dash::DASHTree dashtree_;
	dash::DASHStream video_, audio_;

	media::CdmAdapter *wvadapter_;

	AP4_ByteStream *video_input_, *audio_input_;

	uint16_t width_, height_;
	std::string language_;
	uint32_t max_bandwidth_;

	AP4_ProtectionKeyMap key_map_;
	AP4_Processor *processor_;

#ifdef SEPARATE_STREAMS
	AP4_ByteStream *video_output_;
	AP4_ByteStream *audio_output_;
#elif MUXED_STREAM
	AP4_ByteStream *muxed_output_;
#else
	std::thread *thread_;
	int socket_desc_;
#endif
};

Session::Session(uint32_t session_id, const char *mpdURL, const char *licenseURL)
	: session_id_(session_id)
	, session_expiation_(media::gtc() + 600000)
	, wvLicenseURL_(licenseURL)
	, mpdFileURL_(mpdURL)
	, width_(1280)
	, height_(720)
	, language_("de")
	, max_bandwidth_(0)
	, video_(dashtree_, dash::DASHTree::VIDEO)
	, audio_(dashtree_, dash::DASHTree::AUDIO)
	, wvadapter_(NULL)
	, video_input_(NULL)
	, audio_input_(NULL)
	, processor_(NULL)
#ifdef SEPARATE_STREAMS
	, video_output(NULL)
	, audio_output(NULL)
#elif MUXED_STREAM
	, muxed_output(NULL)
#else
	, thread_(NULL)
	, socket_desc_(0)
#endif
{
	// create a key map object to hold keys
	key_map_.SetKey(1, reinterpret_cast<const AP4_UI08 *>("WIDEVINE"), 8); //Add a dummy key for track 1 (audio)
	key_map_.SetKey(2, reinterpret_cast<const AP4_UI08 *>("WIDEVINE"), 8); //Add a dummy key for track 2 (video)
}

Session::Session(uint32_t session_id, const char *encoded)
	:Session(session_id, "", "")
{
	const char *strGETEnd(strstr(encoded, " HTTP"));
	if (!strGETEnd)
		return;

	const char *strF(strstr(encoded, "mpdurl=")), *strE(strF?strchr(strF, '&'):0);
	uint8_t buffer[4096];

	if (!strE) strE = strGETEnd;
	if (strF && strE)
	{
		unsigned int newLen(4096);
		b64_decode(strF + 7, strE - strF - 7, buffer, newLen);
		mpdFileURL_ = std::string((const char*)buffer, newLen);
	}
	strF = strstr(encoded, "licurl=");
	strE = strF?strchr(strF, '&'):0;
	if (!strE) strE = strGETEnd;
	if (strF && strE)
	{
		unsigned int newLen(4096);
		b64_decode(strF + 7, strE - strF - 7, buffer, newLen);
		wvLicenseURL_ = std::string((const char*)buffer, newLen);
	}
}

Session::~Session()
{
	stop();

	delete wvadapter_;
	wvadapter_ = 0;
}

/*----------------------------------------------------------------------
|   curl callback
+---------------------------------------------------------------------*/

static size_t write_license(void *buffer, size_t size, size_t nmemb, void *dest)
{
	std::string *str(reinterpret_cast<std::string*>(dest));
	*str += std::string(reinterpret_cast<char*>(buffer), size*nmemb);
	return size*nmemb;
}

/*----------------------------------------------------------------------
|   initialize
+---------------------------------------------------------------------*/

bool Session::initialize()
{
	stop();
	
	const char* delim(strrchr(mpdFileURL_.c_str(), '/'));
	if (!delim)
	{
		std::cout << "ERROR: Invalid mpdURL: / expected (" << mpdFileURL_ << ")" << std::endl;
		return false;
	}
	dashtree_.base_url_ = std::string(mpdFileURL_.c_str(), (delim - mpdFileURL_.c_str()) + 1);
	
	if (!dashtree_.open(mpdFileURL_.c_str()))
	{
		std::cout << "ERROR: Could not open / parse mpdURL (" << mpdFileURL_ << ")" << std::endl;
		return false;
	}
	std::cout << "Info: Successfully aded .mpd file, bandwidth: " << dashtree_.download_speed_ << "Bytes/s" << std::endl;


	// Currently use initialization data from mpd file.
	// Maybe this has to fixed later (pass initdata of the lowest stream to Bento)

	wvadapter_ = new media::CdmAdapter("com.widevine.alpha", "widevinecdm.dll", media::CdmConfig());
	if (!wvadapter_->valid())
	{
		std::cout << "ERROR: Unable to load widevine shared library" << std::endl;
		return false;
	}

	// This will request a new session and initializes session_id and message members in cdm_adapter.
	// message will be used to create a license request in the step after CreateSession call.
	// Initialization data is the widevine cdm pssh code in google proto style found in mpd schemeIdUri
	uint8_t init_data[1024] = { 0x00, 0x00, 0x00, 0x63, 0x70, 0x73, 0x73, 0x68, 0x00, 0x00, 0x00, 0x00, 0xed, 0xef, 0x8b, 0xa9,
		0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed, 0x00, 0x00, 0x00, 0x43};
	
	unsigned int init_data_size(992);
	b64_decode(dashtree_.pssh_.data(), dashtree_.pssh_.size(), &init_data[32], init_data_size);

	wvadapter_->CreateSessionAndGenerateRequest(0, cdm::SessionType::kTemporary, cdm::InitDataType::kCenc, init_data, init_data_size+32);

	if (wvadapter_->SessionValid())
	{
#if 1
		std::string license, challenge(b64_encode(wvadapter_->GetMessage(), wvadapter_->GetMessageSize(), true));
		challenge = "widevine2Challenge=" + challenge;
		challenge += "&includeHdcpTestKeyInLicense=false";

		static const unsigned int numDefaultHeaders(6);
		static const char* licHeaders[numDefaultHeaders] = {
			"Accept: application/json",
			"Pragma: no-cache",
			"Cache-Control: no-cache",
			"Content-Type: application/x-www-form-urlencoded",
			"Accept-Encoding: gzip, deflate",
			"EXPECT:"
		};

		// Get the license
		curl_global_init(CURL_GLOBAL_ALL);

		struct curl_slist *headerlist = NULL;
		for (unsigned int i(0); i < numDefaultHeaders;++i)
			headerlist = curl_slist_append(headerlist, licHeaders[i]);

		CURL *curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_URL, wvLicenseURL_.c_str());
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
		/* Set the form info */
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, challenge.c_str());
		/* Define our callback to get called when there's data to be written */
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_license);
		/* Set a pointer to our struct to pass to the callback */
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &license);
		/* Disable security checks here */
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		/* Automaticlly decompress gzipped responses */
		curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip");
		CURLcode res = curl_easy_perform(curl);

		if (res != CURLE_OK)
		{
			stop();
			std::cout << "ERROR: curl - license request failed (" << license << ")" << std::endl;
			return false;
		}

		std::string::size_type licStartPos(license.find("\"license\":\""));
		if (licStartPos == std::string::npos)
		{
			stop();
			std::cout << "ERROR: license start position not found (" << license << ")" << std::endl;
			return false;

		}
		licStartPos += 11;
		std::string::size_type licEndPos(license.find("\",", licStartPos));
		if (licEndPos == std::string::npos)
		{
			stop();
			std::cout << "ERROR: license end position not found (" << license << ")" << std::endl;
			return false;
		}

		init_data_size = 1024;
		b64_decode(license.c_str() + licStartPos, licEndPos - licStartPos, init_data, init_data_size);
		wvadapter_->UpdateSession(init_data, init_data_size);

		if (!wvadapter_->KeyIdValid())
		{
			stop();
			std::cout << "INFO: License update not successful" << std::endl;
			return false;
		}
#endif
	}
	return true;
}

void Session::thread_play(AP4_Position byteOffset)
{
	std::cout << "INFO: thread for session-id: " << session_id_ << " started" << std::endl;
	AP4_SockStream sock_output(socket_desc_, byteOffset);
	std::string ret("HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n\r\n");
	send(socket_desc_, ret.c_str(), ret.size(), 0);
	processor_->Mux(*audio_input_, *video_input_, sock_output, NULL);
	closesocket(socket_desc_);
	socket_desc_ = 0;
	std::cout << "INFO: thread for session-id: " << session_id_ << " terminated" << std::endl;
}


bool Session::play(int socket_desc, AP4_Position byteOffset)
{
	stop();

	socket_desc_ = socket_desc;

	if (!video_.prepare_stream(0, width_, height_, 0, max_bandwidth_))
	{
		std::cout << "ERROR: Could not prepare video stream" << std::endl;
		return false;
	}
	std::cout << "Info: video representation: " << video_.getRepresentation()->url_ << "bandwidth: " << video_.getRepresentation()->bandwidth_ << std::endl;

	video_input_ = new AP4_DASHStream(&video_);
	//video_input can be used to fetch initdata for license handling now.....

	if (!audio_.prepare_stream(0, 0, 0, language_.c_str(), max_bandwidth_))
	{
		std::cout << "ERROR: Could not prepare audio stream" << std::endl;
		return false;
	}
	std::cout << "Info: audio representation: " << audio_.getRepresentation()->url_ << "bandwidth: " << audio_.getRepresentation()->bandwidth_  << std::endl;

	audio_input_ = new AP4_DASHStream(&audio_);

#ifdef SEPARATE_STREAMS
	AP4_FileByteStream::Create("C:\\Temp\\video.mp4", AP4_FileByteStream::STREAM_MODE_WRITE, video_output_);
	AP4_FileByteStream::Create("C:\\Temp\\audio.mp4", AP4_FileByteStream::STREAM_MODE_WRITE, audio_output_);
#elif MUXED_STREAM
	AP4_FileByteStream::Create("C:\\Temp\\muxed.mp4", AP4_FileByteStream::STREAM_MODE_WRITE, muxed_output_);
#endif

	processor_ = new AP4_CencDecryptingProcessor(&key_map_, NULL, new WV_CencSingleSampleDecrypter(wvadapter_));
	
	AP4_Result result;
#ifdef SEPARATE_STREAMS
	result = processor_->Process(*audio_input_, *audio_output_, NULL);
	result = processor_->Process(*video_input_, *video_output_, NULL);
#elif MUXED_STREAM
	result = processor_->Mux(*audio_input_, *video_input_, *muxed_output_, NULL);
#else
	thread_ = new std::thread(&Session::thread_play, this, byteOffset);
	return true;
#endif
	return result == AP4_SUCCESS;
}

void Session::stop()
{
	video_.stop();
	audio_.stop();

#if !defined SEPARATE_STREAMS && !defined(MUXED_STREAM)
	shutdown(socket_desc_, 2);
	if (thread_) {
		if (thread_->joinable())
			thread_->join();
		delete thread_;
		thread_ = NULL;
	}
#endif

	delete video_input_;
	video_input_ = 0;

	delete audio_input_;
	audio_input_ = 0;

#ifdef SEPARATE_STREAMS
	delete audio_output_;
	audio_output_ = 0;

	delete video_output_;
	video_output_ = 0;
#elif MUXED_STREAM
	delete muxed_output_;
	muxed_output_ = 0;
#endif
}


#if !defined SEPARATE_STREAMS && !defined MUXED_STREAM 

int main(int argc, char *argv[])
{
	int socket_desc, client_sock, c;
	struct sockaddr_in server, client;
	std::string strMessage;
	uint32_t sessionId(0);
	std::vector<Session*> session_stack;

	WSADATA wsaData;
	// Initialize Winsock version 2.2
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		std::cout << "FATAL: WSAStartup failed with error:" << WSAGetLastError() << std::endl;
		return -1;
	}
	
	//Create socket
	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_desc == -1)
	{
		std::cout << "FATAL: Could not create socket" << std::endl;
		return -1;
	}

	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(8888);

	//Bind
	if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
	{
		//print the error message
		std::cout << "FATAL: bind failed.Error" << std::endl;
		return -1;
	}

	//Listen
	listen(socket_desc, 3);

	//Accept and incoming connection
	std::cout << "INFO: Waiting for incoming connections..." << std::endl;
	c = sizeof(struct sockaddr_in);

	while ((client_sock = accept(socket_desc, (struct sockaddr *)&client, &c)))
	{
		//Receive a message from client
		char read_buf[1024];
		int read_size;
		strMessage.clear();
		while ((read_size = recv(client_sock, read_buf, 1024, 0)) > 0)
		{
			strMessage += std::string(read_buf, read_size);
			std::string::size_type endGet;
			
			if ((endGet = strMessage.find("\r\n\r\n")) != std::string::npos)
			{
				strMessage.resize(endGet);
				break;
			}
		}
		if(read_size >= 0)
		{
			if(strncmp(strMessage.c_str(),"GET /initialize?",16)==0)
			{
				Session *session = new Session(++sessionId, strMessage.c_str()+16);
				if (session->initialize())
				{
					std::cout << "INFO: new session created: " << sessionId << std::endl;
					session_stack.push_back(session);
				}
				else {
					delete session;
					std::cout << "ERROR: Unable to create / initialize session!" << std::endl;
				}
			}
			else if (strncmp(strMessage.c_str(), "GET /play?", 10) == 0)
			{
				uint32_t sid = atoi(strMessage.c_str() + 10);
				std::vector<Session*>::iterator b(session_stack.begin()), e(session_stack.end());
				for (; b != e; ++b)
					if ((*b)->GetSessionId() == sid)
						break;
				if (b != e)
				{
					if (!(*b)->play(client_sock, 0))
						std::cout << "ERROR: Unable to play stream with session_id: " << sid << std::endl;
					else
					{
						std::cout << "INFO: Start playing stream with session_id: " << sid << std::endl;
						goto success;
					}
				}
				else
					std::cout << "ERROR: Unable to find stream with session_id: " << sid << std::endl;
			}
			else if (strncmp(strMessage.c_str(), "HEAD", 4) != 0)
			{
				std::cout << "ERROR: Unknown command: " << strMessage << std::endl;
			}
			else
			{
				strMessage = "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n\r\n";
				send(client_sock, strMessage.c_str(), strMessage.size(), 0);
				closesocket(client_sock);
				goto success;
			}
			strMessage = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
			send(client_sock, strMessage.c_str(), strMessage.size(), 0);
		}
		closesocket(client_sock);
success:;
	}

	for (std::vector<Session*>::iterator b(session_stack.begin()), e(session_stack.end()); b != e; ++b)
		delete *b;
	
	if (client_sock < 0)
	{
		std::cout << "FATAL: accept failed." << std::endl;
		return -1;
	}
	WSACleanup();
	return 0;
}
#else
int main()
{
	const char *lic = "FILL IT FOR TESTING";
	const char *mpd = "FILL IT FOR TESTING";

	Session s(0, mpd, lic);
	//s.SetStreamProperties(,,,,,,); //use defaults
	if (s.initialize())
		s.play();



	return 0;
}
#endif