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

#include "dash/DASHTree.h"
#include "dash/DASHStream.h"
#include "cdm/media/cdm/cdm_adapter.h"
#include "../libbento4/Core/Ap4.h"
#include "helpers.h"
#include "../libcurl/include/curl/curl.h"

std::string lastError_;
std::string wvlicenseURL_;
std::vector<std::string> wvHeaders_;


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
		return bytesRead > 0 ? AP4_SUCCESS : AP4_ERROR_EOS;
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

dash::DASHTree dashtree;
dash::DASHStream video(dashtree, dash::DASHTree::VIDEO), audio(dashtree, dash::DASHTree::AUDIO);

media::CdmAdapter *wvadapter(NULL);

AP4_ByteStream *video_input(NULL), *audio_input(NULL);
#ifdef SEPARATE_STREAMS
	AP4_ByteStream *video_output(NULL);
	AP4_ByteStream *audio_output(NULL);
#else
	AP4_ByteStream *muxed_output(NULL);
#endif

// Forward declaration
void stop();

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

bool initialize(const char *mpdURL, const char* wvlicenseURL, const char **wvHeaders, const uint32_t numHeaders,
	const uint16_t width, const uint16_t height, const char *language, uint32_t max_bandwidth)
{
	stop();
	
	const char* delim(strrchr(mpdURL, '/'));
	if (!delim)
	{
		lastError_ = "Invalid mpdURL: / expected";
		return false;
	}
	dashtree.base_url_ = std::string(mpdURL, (delim - mpdURL)+1);
	
	if (!dashtree.open(mpdURL))
	{
		lastError_ = "Could not open / parse mpdURL";
		return false;
	}
	if (!video.prepare_stream(0, width, height, 0, max_bandwidth))
	{
		lastError_ = "Could not prepare video stream";
		return false;
	}
	video_input = new AP4_DASHStream(&video);
	//video_input can be used to fetch initdata for license handling now.....
	
	if (!audio.prepare_stream(0, 0, 0, language, max_bandwidth))
	{
		lastError_ = "Could not prepare audio stream";
		return false;
	}
	audio_input = new AP4_DASHStream(&audio);

#ifdef SEPARATE_STREAMS
	AP4_FileByteStream::Create("C:\\Temp\\video.mp4", AP4_FileByteStream::STREAM_MODE_WRITE, video_output);
	AP4_FileByteStream::Create("C:\\Temp\\audio.mp4", AP4_FileByteStream::STREAM_MODE_WRITE, audio_output);
#else
	AP4_FileByteStream::Create("C:\\Temp\\muxed.mp4", AP4_FileByteStream::STREAM_MODE_WRITE, muxed_output);
#endif

	// Also we have read initalization data from both streams, we 
	// currently use initialization data from mpd file.
	// Maybe this has to fixed later (pass initdata to Bento)

	wvlicenseURL_ = wvlicenseURL;
	wvHeaders_.resize(numHeaders);
	for (uint32_t i(0); i < numHeaders; ++i)
		wvHeaders_[i] = wvHeaders[i];

	wvadapter = new media::CdmAdapter("com.widevine.alpha", "widevinecdm.dll", media::CdmConfig());
	if (!wvadapter->valid())
	{
		lastError_ = "Could not load widevine shared library";
		return false;
	}

	// This will request a new session and initializes session_id and message members in cdm_adapter.
	// message will be used to create a license request in the step after CreateSession call.
	// Initialization data is the widevine cdm pssh code in google proto style found in mpd schemeIdUri
	uint8_t init_data[1024] = { 0x00, 0x00, 0x00, 0x63, 0x70, 0x73, 0x73, 0x68, 0x00, 0x00, 0x00, 0x00, 0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed, 0x00, 0x00, 0x00, 0x43};
	
	unsigned int init_data_size(992);
	b64_decode(dashtree.pssh_.data(), dashtree.pssh_.size(), &init_data[32], init_data_size);
	wvadapter->CreateSessionAndGenerateRequest(0, cdm::SessionType::kTemporary, cdm::InitDataType::kCenc, init_data, init_data_size+32);

	if (wvadapter->SessionValid())
	{
#if 1
		std::string license, challenge(b64_encode(wvadapter->GetMessage(), wvadapter->GetMessageSize(), true));
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
		curl_easy_setopt(curl, CURLOPT_URL, wvlicenseURL_.c_str());
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
			lastError_ = "curl - license request failed";
			return false;
		}

		std::string::size_type licStartPos(license.find("\"license\":\""));
		if (licStartPos == std::string::npos)
		{
			stop();
			lastError_ = "license start position not found";
			return false;

		}
		licStartPos += 11;
		std::string::size_type licEndPos(license.find("\",", licStartPos));
		if (licEndPos == std::string::npos)
		{
			stop();
			lastError_ = "license end position not found";
			return false;
		}

		init_data_size = 1024;
		b64_decode(license.c_str() + licStartPos, licEndPos - licStartPos, init_data, init_data_size);
		wvadapter->UpdateSession(init_data, init_data_size);

		if (!wvadapter->KeyIdValid())
		{
			stop();
			lastError_ = "License update not successful";
			return false;
		}
#endif
	}
	return true;
}

bool play()
{
	// create a key map object to hold keys
	AP4_ProtectionKeyMap key_map;
	key_map.SetKey(1, reinterpret_cast<const AP4_UI08 *>("WIDEVINE"), 8); //Add a dummy key for track 0
	key_map.SetKey(2, reinterpret_cast<const AP4_UI08 *>("WIDEVINE"), 8); //Add a dummy key for track 1

	AP4_Processor *processor(new AP4_CencDecryptingProcessor(&key_map, NULL, new WV_CencSingleSampleDecrypter(wvadapter)));
	
	AP4_Result result;
#ifdef SEPARATE_STREAMS
	result = processor->Process(*audio_input, *audio_output, NULL);
	result = processor->Process(*video_input, *video_output, NULL);
#else
	result = processor->Mux(*audio_input, *video_input, *muxed_output, NULL);
#endif
	return result == AP4_SUCCESS;
}

void stop()
{
	video.stop();
	audio.stop();

	delete video_input;
	video_input = 0;

	delete audio_input;
	audio_input = 0;

#ifdef SEPARATE_STREAMS
	delete audio_output;
	audio_output = 0;

	delete video_output;
	video_output = 0;
#else
	delete muxed_output;
	muxed_output = 0;
#endif

	delete wvadapter;
	wvadapter = 0;
}

int main()
{
	const char *licString = "";
	const char *mpdURL = "";

	initialize(mpdURL, licString, NULL, 0, 1280, 720, "de", 0);
	play();
	stop();

	//TODO: Start the worker thread

	return 0;
}
