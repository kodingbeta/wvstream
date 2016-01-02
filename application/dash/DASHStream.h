/*
* DASHStream.h
*****************************************************************************
* Copyright (C) 2015, liberty_developer
*
* Email: liberty.developer@xmail.net
*
* This source code and its use and distribution, is subject to the terms
* and conditions of the applicable license agreement.
*****************************************************************************/

#pragma once

#include "DASHTree.h"

namespace dash
{
	class DASHStream
	{
	public:
		DASHStream(DASHTree &tree,DASHTree::StreamType type);
		~DASHStream();
		bool prepare_stream(uint32_t const offset, const uint32_t width,
			const uint32_t height, const char *lang, uint32_t max_bandwidth);
		void stop(){ stopped_ = true; };
		void clear();
		uint32_t read(void* buffer,
			uint32_t  bytesToRead);
		uint64_t tell(){ return absolute_position_;};
		bool seek(uint64_t const pos);
		DASHTree::Representation const *getRepresentation(){ return current_rep_; };
	private:
		bool download_segment();

		DASHTree &tree_;
		DASHTree::StreamType type_;
		// Active configuration
		const DASHTree::Period *current_period_;
		const DASHTree::AdaptationSet *current_adp_;
		const DASHTree::Representation *current_rep_;
		const DASHTree::Segment *current_seg_;
		//We assume that a single segment can build complete frames
		std::string segment_buffer_;
		std::size_t segment_read_pos_;
		uint64_t absolute_position_;
		
		void *curl_handle_;

		uint16_t width_, height_;
		std::string language_;

		bool stopped_;
	};
};
