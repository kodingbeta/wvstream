/*
* DASHTree.h
*****************************************************************************
* Copyright (C) 2015, liberty_developer
*
* Email: liberty.developer@xmail.net
*
* This source code and its use and distribution, is subject to the terms
* and conditions of the applicable license agreement.
*****************************************************************************/

#pragma once

#include <vector>
#include <string>
#include <inttypes.h>

namespace dash
{
class DASHTree
{
public:
	enum StreamType
	{
		NOTYPE,
		VIDEO,
		AUDIO,
		TEXT,
		STREAM_TYPE_COUNT
	};

	struct SegmentTemplate
    {
      SegmentTemplate() :duration(0), startNumber(1), timescale(0), presentationTimeOffset(0){};
      std::string initialization;
      std::string media;
      unsigned int startNumber;
      unsigned int timescale, duration;
      double presentationTimeOffset;
    };

	// Node definition
	struct Segment
	{
		void SetRange(const char *range);

		uint64_t range_begin_;
		uint64_t range_end_;
		uint64_t startPTS_;
	};

	struct Representation
	{
		Representation() : timescale_(0), duration_(0), bandwidth_(0), samplingRate_(0), width_(0), height_(0),flags_(0), hasInitialization_(false){};
		std::string url_;
		std::string codecs_;
		uint32_t bandwidth_;
		uint32_t samplingRate_;
		uint16_t width_, height_;
		bool hasInitialization_;
		SegmentTemplate segtpl_;
		//Flags
		static const unsigned int BYTERANGE = 0;
		static const unsigned int INDEXRANGEEXACT = 1;
		static const unsigned int TEMPLATE = 2;
		static const unsigned int TIMELINE = 4;
		static const unsigned int INITIALIZATION = 8;
		static const unsigned int TIMETEMPLATE = 16;
		static const unsigned int SEGMENTBASE = 32;
		uint32_t flags_;

		//SegmentList
		uint32_t duration_, timescale_;
		std::vector<Segment> segments_;
		Segment initialization_;
		const Segment *get_initialization() const { return hasInitialization_ ? &segments_[0] : 0; };
		const Segment *get_next_segment(const Segment *seg) const
		{
			uint32_t curpos = static_cast<uint32_t>(seg - &segments_[0] + 1);
			if (curpos < segments_.size())
				return &segments_[0] + curpos;
			return 0;
		};
		const Segment *get_segment(const uint32_t pos) const
		{
			return pos < segments_.size() ? &segments_[pos] : 0;
		};
		const uint32_t get_segment_pos(const Segment *segment) const
		{
			return segment ? segment - &segments_[0] : 0;
		}
	} * current_representation_;

	struct AdaptationSet
	{
		AdaptationSet() : type_(NOTYPE), timescale_(0){};
		~AdaptationSet()
		{
			for (std::vector<Representation *>::const_iterator b(repesentations_.begin()), e(repesentations_.end()); b != e; ++b)
				delete *b;
		};
		StreamType type_;
		uint32_t timescale_;
		std::string language_;
		std::string mimeType_;
		std::vector<Representation *> repesentations_;
		std::vector<uint32_t> segment_durations_;
		SegmentTemplate segtpl_;
	} * current_adaptationset_;

	struct Period
	{
		Period(){};
		~Period()
		{
			for (std::vector<AdaptationSet *>::const_iterator b(adaptationSets_.begin()), e(adaptationSets_.end()); b != e; ++b)
				delete *b;
		};
		std::vector<AdaptationSet *> adaptationSets_;

	} * current_period_;

	std::vector<Period *> periods_;
	std::string base_url_;

	/* XML Parsing*/
	uint32_t currentNode_;
	uint32_t segcount_;
	uint64_t stream_start_, available_time_, publish_time_, base_time_;
	double minPresentationOffset;
	double overallSeconds_;
	double download_speed_;
	std::string pssh_;

	enum
	{
		MPDNODE_MPD = 1 << 0,
		MPDNODE_PERIOD = 1 << 1,
		MPDNODE_ADAPTIONSET = 1 << 2,
		MPDNODE_CONTENTPROTECTION = 1 << 3,
		MPDNODE_REPRESENTATION = 1 << 4,
		MPDNODE_BASEURL = 1 << 5,
		MPDNODE_SEGMENTLIST = 1 << 6,
		MPDNODE_INITIALIZATION = 1 << 7,
		MPDNODE_SEGMENTURL = 1 << 8,
		MPDNODE_SEGMENTDURATIONS = 1 << 9,
		MPDNODE_S = 1 << 11,
		MPDNODE_PSSH = 1 << 12,
		MPDNODE_SEGMENTTEMPLATE = 1 << 13,
		MPDNODE_SEGMENTTIMELINE = 1 << 14
	};
	std::string strXMLText_, adp_pssh_;

	DASHTree();
	~DASHTree();
	bool open(const char *url);
	bool has_type(StreamType t);
	uint32_t estimate_segcount(uint32_t duration, uint32_t timescale);
	double get_download_speed() const { return download_speed_; };
};

} // namespace dash