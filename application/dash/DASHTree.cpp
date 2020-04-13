/*
* DASHTree.cpp
*****************************************************************************
* Copyright (C) 2015, liberty_developer
*
* Email: liberty.developer@xmail.net
*
* This source code and its use and distribution, is subject to the terms
* and conditions of the applicable license agreement.
*****************************************************************************/

#include <string>
#include <cstring>
#include <iostream>

#include "DASHTree.h"
#include "../../libcurl/include/curl/curl.h"
#include "../../expat/include/expat.h"
#include "../oscompat.h"

using namespace dash;

DASHTree::DASHTree()
	: download_speed_(0.0)
{
}

DASHTree::~DASHTree()
{
}

static uint8_t GetChannels(const char **attr)
{
	const char *schemeIdUri(0), *value(0);

	for (; *attr;)
	{
		if (strcmp((const char *)*attr, "schemeIdUri") == 0)
			schemeIdUri = (const char *)*(attr + 1);
		else if (strcmp((const char *)*attr, "value") == 0)
			value = (const char *)*(attr + 1);
		attr += 2;
	}
	if (schemeIdUri && value)
	{
		if (strcmp(schemeIdUri, "urn:mpeg:dash:23003:3:audio_channel_configuration:2011") == 0)
			return atoi(value);
		else if (strcmp(schemeIdUri, "urn:dolby:dash:audio_channel_configuration:2011") == 0)
		{
			if (strcmp(value, "F801") == 0)
				return 6;
			else if (strcmp(value, "FE01") == 0)
				return 8;
		}
	}
	return 0;
}

static void ParseSegmentTemplate(const char **attr, std::string baseURL, DASHTree::SegmentTemplate &tpl, bool adp)
{
	std::cout << "INFO: Parse Template : " << *attr << std::endl;
	uint64_t pto(0);
	for (; *attr;)
	{
		if (strcmp((const char *)*attr, "timescale") == 0)
			tpl.timescale = atoi((const char *)*(attr + 1));
		else if (strcmp((const char *)*attr, "duration") == 0)
			tpl.duration = atoi((const char *)*(attr + 1));
		else if (strcmp((const char *)*attr, "media") == 0)
			tpl.media = (const char *)*(attr + 1);
		else if (strcmp((const char *)*attr, "startNumber") == 0)
			tpl.startNumber = atoi((const char *)*(attr + 1));
		else if (strcmp((const char *)*attr, "initialization") == 0)
			tpl.initialization = (const char *)*(attr + 1);
		else if (strcmp((const char *)*attr, "presentationTimeOffset") == 0)
			pto = atoll((const char *)*(attr + 1));
		attr += 2;
	}
	tpl.presentationTimeOffset = tpl.timescale ? (double)pto / tpl.timescale : 0;
	tpl.media = baseURL + tpl.media;
	std::cout << "INFO: Template Media : " << tpl.media << std::endl;
}

static time_t getTime(const char *timeStr)
{
	int year, mon, day, hour, minu, sec;
	if (sscanf(timeStr, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &minu, &sec) == 6)
	{
		struct tm tmd;

		memset(&tmd, 0, sizeof(tmd));
		tmd.tm_year = year - 1900;
		tmd.tm_mon = mon - 1;
		tmd.tm_mday = day;
		tmd.tm_hour = hour;
		tmd.tm_min = minu;
		tmd.tm_sec = sec;
		return _mkgmtime(&tmd);
	}
	return ~0;
}

/*----------------------------------------------------------------------
|   expat start
+---------------------------------------------------------------------*/
static void XMLCALL
start(void *data, const char *el, const char **attr)
{
	DASHTree *dash(reinterpret_cast<DASHTree *>(data));
	std::cout << "INFO: Current Node : " << dash->currentNode_ << std::endl;
	std::cout << "INFO: Current Base Url : " << dash->base_url_ << std::endl;
	if (dash->currentNode_ & DASHTree::MPDNODE_MPD)
	{
		if (dash->currentNode_ & DASHTree::MPDNODE_PERIOD)
		{
			if (dash->currentNode_ & DASHTree::MPDNODE_ADAPTIONSET)
			{
				if (dash->currentNode_ & DASHTree::MPDNODE_REPRESENTATION)
				{
					if (dash->currentNode_ & DASHTree::MPDNODE_BASEURL)
					{
					}
					else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTEMPLATE)
					{
						if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTIMELINE)
						{
							// <S t="3600" d="900000" r="2398"/>
							unsigned int d(0), r(1);
							static uint64_t t(0);
							for (; *attr;)
							{
								if (strcmp((const char *)*attr, "t") == 0)
									t = atoll((const char *)*(attr + 1));
								else if (strcmp((const char *)*attr, "d") == 0)
									d = atoi((const char *)*(attr + 1));
								if (strcmp((const char *)*attr, "r") == 0)
									r = atoi((const char *)*(attr + 1)) + 1;
								attr += 2;
							}
							if (dash->current_adaptationset_->segment_durations_.data.empty())
								dash->current_adaptationset_->startPTS_ = t - (dash->base_time_) * dash->current_adaptationset_->timescale_;
							if (d && r)
							{
								for (; r; --r)
									dash->current_adaptationset_->segment_durations_.data.push_back(d);
							}
						}
						else if (strcmp(el, "SegmentTimeline") == 0)
						{
							dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTTIMELINE;
						}
					}
					else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTLIST)
					{
						DASHTree::Segment seg;
						if (strcmp(el, "SegmentURL") == 0)
						{
							for (; *attr;)
							{
								if (strcmp((const char *)*attr, "mediaRange") == 0)
								{
									seg.SetRange((const char *)*(attr + 1));
									break;
								}
								attr += 2;
							}
						}
						else if (strcmp(el, "Initialization") == 0)
						{
							for (; *attr;)
							{
								if (strcmp((const char *)*attr, "range") == 0)
								{
									seg.SetRange((const char *)*(attr + 1));
									break;
								}
								attr += 2;
							}
							dash->current_representation_->hasInitialization_ = true;
						}
						else
							return;
						dash->current_representation_->segments_.push_back(seg);
					}
					else if (strcmp(el, "BaseURL") == 0)
					{
						dash->strXMLText_.clear();
						dash->currentNode_ |= DASHTree::MPDNODE_BASEURL;
					}
					else if (strcmp(el, "SegmentList") == 0)
					{

						for (; *attr;)
						{
							if (strcmp((const char *)*attr, "duration") == 0)
								dash->current_representation_->duration_ = atoi((const char *)*(attr + 1));
							else if (strcmp((const char *)*attr, "timescale") == 0)
								dash->current_representation_->timescale_ = atoi((const char *)*(attr + 1));
							attr += 2;
						}
						if (dash->current_representation_->timescale_)
						{
							dash->current_representation_->segments_.reserve(dash->estimate_segcount(
								dash->current_representation_->duration_,
								dash->current_representation_->timescale_));
							dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTLIST;
						}
					}
					else if (strcmp(el, "SegmentTimeline") == 0)
					{
						dash->current_representation_->flags_ |= DASHTree::Representation::TIMELINE;
						dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTTIMELINE;
					}
					else if (strcmp(el, "AudioChannelConfiguration") == 0)
					{
						dash->current_representation_->channelCount_ = GetChannels(attr);
					}
					else if (strcmp(el, "BaseURL") == 0)
					{
						dash->strXMLText_.clear();
						dash->currentNode_ |= DASHTree::MPDNODE_BASEURL;
					}
					else if (strcmp(el, "SegmentList") == 0)
					{

						for (; *attr;)
						{
							if (strcmp((const char *)*attr, "duration") == 0)
								dash->current_representation_->duration_ = atoi((const char *)*(attr + 1));
							else if (strcmp((const char *)*attr, "timescale") == 0)
								dash->current_representation_->timescale_ = atoi((const char *)*(attr + 1));
							attr += 2;
						}
						if (dash->current_representation_->timescale_)
						{
							dash->current_representation_->segments_.reserve(dash->estimate_segcount(
								dash->current_representation_->duration_,
								dash->current_representation_->timescale_));
							dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTLIST;
						}
					}
					else if (strcmp(el, "SegmentBase") == 0)
					{
						//<SegmentBase indexRangeExact = "true" indexRange = "867-1618">
						for (; *attr;)
						{
							if (strcmp((const char *)*attr, "indexRange") == 0)
								sscanf((const char *)*(attr + 1), "%u-%u", &dash->current_representation_->indexRangeMin_, &dash->current_representation_->indexRangeMax_);
							else if (strcmp((const char *)*attr, "indexRangeExact") == 0 && strcmp((const char *)*(attr + 1), "true") == 0)
								dash->current_representation_->flags_ |= DASHTree::Representation::INDEXRANGEEXACT;
							dash->current_representation_->flags_ |= DASHTree::Representation::SEGMENTBASE;
							attr += 2;
						}
						if (dash->current_representation_->indexRangeMax_)
							dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTLIST;
					}
					else if (strcmp(el, "SegmentTemplate") == 0)
					{
						std::cout << "INFO: Current Element : " << el << std::endl;
						dash->current_representation_->segtpl_ = dash->current_adaptationset_->segtpl_;

						ParseSegmentTemplate(attr, dash->current_representation_->url_, dash->current_representation_->segtpl_, false);
						dash->current_representation_->flags_ |= DASHTree::Representation::TEMPLATE;
						if (!dash->current_representation_->segtpl_.initialization.empty())
						{
							dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
							dash->current_representation_->url_ += dash->current_representation_->segtpl_.initialization;
							dash->current_representation_->timescale_ = dash->current_representation_->segtpl_.timescale;
						}
						dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTTEMPLATE;
					}
					else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTEMPLATE)
					{
						if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTIMELINE)
						{
							// <S t="3600" d="900000" r="2398"/>
							unsigned int d(0), r(1);
							static uint64_t t(0);

							for (; *attr;)
							{
								if (strcmp((const char *)*attr, "t") == 0)
									t = atoll((const char *)*(attr + 1));
								else if (strcmp((const char *)*attr, "d") == 0)
									d = atoi((const char *)*(attr + 1));
								if (strcmp((const char *)*attr, "r") == 0)
									r = atoi((const char *)*(attr + 1)) + 1;
								attr += 2;
							}
							if (d && r)
							{
								DASHTree::Segment s;
								if (dash->current_representation_->segments_.empty())
								{
									if (dash->current_representation_->segtpl_.duration && dash->current_representation_->segtpl_.timescale)
										dash->current_representation_->segments_.reserve((unsigned int)(dash->overallSeconds_ / (((double)dash->current_representation_->segtpl_.duration) / dash->current_representation_->segtpl_.timescale)) + 1);

									if (dash->current_representation_->flags_ & DASHTree::Representation::INITIALIZATION)
									{
										s.range_begin_ = s.range_end_ = ~0;
										dash->current_representation_->initialization_ = s;
									}
									s.range_end_ = dash->current_representation_->segtpl_.startNumber;
								}
								else
									s.range_end_ = dash->current_representation_->segments_.back().range_end_ + 1;
								s.range_begin_ = s.startPTS_ = t;
								s.startPTS_ -= (dash->base_time_) * dash->current_representation_->segtpl_.timescale;

								for (; r; --r)
								{
									dash->current_representation_->segments_.push_back(s);
									++s.range_end_;
									s.range_begin_ = (t += d);
									s.startPTS_ += d;
								}
							}
							else //Failure
							{
								dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTTIMELINE;
								dash->current_representation_->timescale_ = 0;
							}
						}
					}
					else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTEMPLATE)
					{
						if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTIMELINE)
						{
							// <S t="3600" d="900000" r="2398"/>
							unsigned int d(0), r(1);
							static uint64_t t(0);
							for (; *attr;)
							{
								if (strcmp((const char *)*attr, "t") == 0)
									t = atoll((const char *)*(attr + 1));
								else if (strcmp((const char *)*attr, "d") == 0)
									d = atoi((const char *)*(attr + 1));
								if (strcmp((const char *)*attr, "r") == 0)
									r = atoi((const char *)*(attr + 1)) + 1;
								attr += 2;
							}
							if (dash->current_adaptationset_->segment_durations_.empty())
								dash->current_adaptationset_->startPTS_ = t - (dash->base_time_) * dash->current_adaptationset_->timescale_;
							if (d && r)
							{
								for (; r; --r)
									dash->current_adaptationset_->segment_durations_.push_back(d);
							}
						}
						else if (strcmp(el, "SegmentTimeline") == 0)
						{
							dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTTIMELINE;
						}
					}
				}
				else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTDURATIONS)
				{
					if (strcmp(el, "S") == 0 && *(const char *)*attr == 'd')
						dash->current_adaptationset_->segment_durations_.push_back(atoi((const char *)*(attr + 1)));
				}
				else if (dash->currentNode_ & DASHTree::MPDNODE_CONTENTPROTECTION)
				{
					if (strcmp(el, "cenc:pssh") == 0)
						dash->currentNode_ |= DASHTree::MPDNODE_PSSH;
				}
				else if (strcmp(el, "SegmentTemplate") == 0)
				{
					ParseSegmentTemplate(attr, dash->current_adaptationset_->base_url_, dash->current_adaptationset_->segtpl_, true);
					dash->current_adaptationset_->timescale_ = dash->current_adaptationset_->segtpl_.timescale;
					dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTTEMPLATE;
				}
				else if (strcmp(el, "Representation") == 0)
				{
					dash->current_representation_ = new DASHTree::Representation();
					dash->current_adaptationset_->repesentations_.push_back(dash->current_representation_);
					for (; *attr;)
					{
						if (strcmp((const char *)*attr, "bandwidth") == 0)
							dash->current_representation_->bandwidth_ = atoi((const char *)*(attr + 1));
						else if (strcmp((const char *)*attr, "codecs") == 0)
							dash->current_representation_->codecs_ = (const char *)*(attr + 1);
						else if (strcmp((const char *)*attr, "width") == 0)
							dash->current_representation_->width_ = static_cast<uint16_t>(atoi((const char *)*(attr + 1)));
						else if (strcmp((const char *)*attr, "height") == 0)
							dash->current_representation_->height_ = static_cast<uint16_t>(atoi((const char *)*(attr + 1)));
						else if (strcmp((const char *)*attr, "audioSamplingRate") == 0)
							dash->current_representation_->samplingRate_ = static_cast<uint32_t>(atoi((const char *)*(attr + 1)));
						attr += 2;
					}
					dash->currentNode_ |= DASHTree::MPDNODE_REPRESENTATION;
				}
				else if (strcmp(el, "SegmentDurations") == 0)
				{
					dash->current_adaptationset_->segment_durations_.reserve(dash->segcount_);
					//<SegmentDurations timescale = "48000">
					dash->currentNode_ |= DASHTree::MPDNODE_SEGMENTDURATIONS;
				}
				else if (strcmp(el, "ContentProtection") == 0)
				{
					dash->strXMLText_.clear();
					bool wvfound(false);
					for (; *attr;)
					{
						if (strcmp((const char *)*attr, "schemeIdUri") == 0)
						{
							wvfound = strcmp((const char *)*(attr + 1), "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED") == 0;
							break;
						}
						attr += 2;
					}
					if (wvfound)
						dash->currentNode_ |= DASHTree::MPDNODE_CONTENTPROTECTION;
				}
			}
			else if (strcmp(el, "AdaptationSet") == 0)
			{
				//<AdaptationSet contentType="video" group="2" lang="en" mimeType="video/mp4" par="16:9" segmentAlignment="true" startWithSAP="1" subsegmentAlignment="true" subsegmentStartsWithSAP="1">
				dash->current_adaptationset_ = new DASHTree::AdaptationSet();
				dash->current_period_->adaptationSets_.push_back(dash->current_adaptationset_);
				dash->current_adaptationset_->base_url_ = dash->current_period_->base_url_;
				dash->adp_pssh_.clear();
				for (; *attr;)
				{
					if (strcmp((const char *)*attr, "contentType") == 0)
						dash->current_adaptationset_->type_ =
							stricmp((const char *)*(attr + 1), "video") == 0 ? DASHTree::VIDEO
																			 : stricmp((const char *)*(attr + 1), "audio") == 0 ? DASHTree::AUDIO
																																: DASHTree::NOTYPE;
					else if (strcmp((const char *)*attr, "lang") == 0)
						dash->current_adaptationset_->language_ = (const char *)*(attr + 1);
					else if (strcmp((const char *)*attr, "mimeType") == 0)
						dash->current_adaptationset_->mimeType_ = (const char *)*(attr + 1);
					attr += 2;
				}
				if (dash->current_adaptationset_->type_ == DASHTree::NOTYPE)
				{
					if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "video", 5) == 0)
						dash->current_adaptationset_->type_ = DASHTree::VIDEO;
					else if (strncmp(dash->current_adaptationset_->mimeType_.c_str(), "audio", 5) == 0)
						dash->current_adaptationset_->type_ = DASHTree::AUDIO;
				}

				dash->segcount_ = 0;
				dash->currentNode_ |= DASHTree::MPDNODE_ADAPTIONSET;
			}
		}
		else if (strcmp(el, "Period") == 0)
		{
			dash->current_period_ = new DASHTree::Period();
			dash->current_period_->base_url_ = dash->base_url_;

			dash->periods_.push_back(dash->current_period_);
			dash->currentNode_ |= DASHTree::MPDNODE_PERIOD;
		}
	}
	else if (strcmp(el, "MPD") == 0)
	{
		const char *mpt(0);
		unsigned int h, m;
		float s;

		dash->overallSeconds_ = 0;
		for (; *attr;)
		{
			if (strcmp((const char *)*attr, "mediaPresentationDuration") == 0)
			{
				mpt = (const char *)*(attr + 1);
				break;
			}
			attr += 2;
		}
		if (mpt && sscanf(mpt, "PT%uH%uM%fS", &h, &m, &s) == 3)
			dash->overallSeconds_ = h * 3600 + m * 60 + s;
		dash->currentNode_ |= DASHTree::MPDNODE_MPD;
	}
}

/*----------------------------------------------------------------------
|   expat text
+---------------------------------------------------------------------*/
static void XMLCALL
text(void *data, const char *s, int len)
{
	DASHTree *dash(reinterpret_cast<DASHTree *>(data));
	if (dash->currentNode_ & (DASHTree::MPDNODE_BASEURL | DASHTree::MPDNODE_PSSH))
		dash->strXMLText_ += std::string(s, len);
}

/*----------------------------------------------------------------------
|   expat end
+---------------------------------------------------------------------*/

static void ReplacePlaceHolders(std::string &rep, const std::string &id, uint32_t bandwidth)
{
	std::string::size_type repPos = rep.find("$RepresentationID$");
	if (repPos != std::string::npos)
		rep.replace(repPos, 18, id);

	repPos = rep.find("$Bandwidth$");
	if (repPos != std::string::npos)
	{
		char bw[32];
		sprintf(bw, "%u", bandwidth);
		rep.replace(repPos, 11, bw);
	}
}
static void XMLCALL
end(void *data, const char *el)
{
	DASHTree *dash(reinterpret_cast<DASHTree *>(data));

	if (dash->currentNode_ & DASHTree::MPDNODE_MPD)
	{
		if (dash->currentNode_ & DASHTree::MPDNODE_MPD)
		{
			if (dash->currentNode_ & DASHTree::MPDNODE_ADAPTIONSET)
			{
				if (dash->currentNode_ & DASHTree::MPDNODE_REPRESENTATION)
				{
					if (dash->currentNode_ & DASHTree::MPDNODE_BASEURL)
					{
						if (strcmp(el, "BaseURL") == 0)
						{
							dash->current_representation_->url_ = dash->base_url_ + dash->strXMLText_;
							dash->current_adaptationset_->base_url_ = dash->current_period_->base_url_ + dash->strXMLText_;
							dash->currentNode_ &= ~DASHTree::MPDNODE_BASEURL;
						}
					}
					else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTLIST)
					{
						if (strcmp(el, "SegmentList") == 0)
						{
							dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTLIST;
							if (!dash->segcount_)
								dash->segcount_ = dash->current_representation_->segments_.size();
						}
					}
					else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTEMPLATE)
					{
						if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTTIMELINE)
						{
							if (strcmp(el, "SegmentTimeline") == 0)
								dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTTIMELINE;
						}
						else if (strcmp(el, "SegmentTemplate") == 0)
						{
							if (dash->current_representation_->segtpl_.presentationTimeOffset < dash->minPresentationOffset)
								dash->minPresentationOffset = dash->current_representation_->segtpl_.presentationTimeOffset;
							dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTTEMPLATE;
						}
					}
					else if (strcmp(el, "Representation") == 0)
						dash->currentNode_ &= ~DASHTree::MPDNODE_REPRESENTATION;
					if (dash->current_representation_->segments_.data.empty())
					{
						bool isSegmentTpl(!dash->current_representation_->segtpl_.media.empty());
						DASHTree::SegmentTemplate &tpl(isSegmentTpl ? dash->current_representation_->segtpl_ : dash->current_adaptationset_->segtpl_);

						if (!tpl.media.empty() && dash->overallSeconds_ > 0 && tpl.timescale > 0 && (tpl.duration > 0 || dash->current_adaptationset_->segment_durations_.data.size()))
						{
							unsigned int countSegs = !dash->current_adaptationset_->segment_durations_.data.empty() ? dash->current_adaptationset_->segment_durations_.data.size() : (unsigned int)(dash->overallSeconds_ / (((double)tpl.duration) / tpl.timescale)) + 1;

							if (countSegs < 65536)
							{
								DASHTree::Segment seg;
								seg.range_begin_ = ~0;

								dash->current_representation_->flags_ |= DASHTree::Representation::TEMPLATE;

								dash->current_representation_->segments_.data.reserve(countSegs);
								if (!tpl.initialization.empty())
								{
									seg.range_end_ = ~0;
									if (!isSegmentTpl)
									{
										dash->current_representation_->url_ += tpl.initialization;
										ReplacePlaceHolders(dash->current_representation_->url_, dash->current_representation_->id, dash->current_representation_->bandwidth_);
										dash->current_representation_->segtpl_.media = tpl.media;
										ReplacePlaceHolders(dash->current_representation_->segtpl_.media, dash->current_representation_->id, dash->current_representation_->bandwidth_);
									}

									dash->current_representation_->initialization_ = seg;
									dash->current_representation_->flags_ |= DASHTree::Representation::INITIALIZATION;
								}

								std::vector<uint32_t>::const_iterator sdb(dash->current_adaptationset_->segment_durations_.begin()),
									sde(dash->current_adaptationset_->segment_durations_.data.end());
								bool timeBased = sdb != sde && tpl.media.find("$Time") != std::string::npos;
								if (timeBased)
									dash->current_representation_->flags_ |= DASHTree::Representation::TIMETEMPLATE;

								seg.range_end_ = timeBased ? dash->current_adaptationset_->startPTS_ : tpl.startNumber;
								seg.startPTS_ = dash->current_adaptationset_->startPTS_;

								if (!timeBased && dash->available_time_ && dash->stream_start_ - dash->available_time_ > dash->overallSeconds_) //we need to adjust the start-segment
									seg.range_end_ += ((dash->stream_start_ - dash->available_time_ - dash->overallSeconds_) * tpl.timescale) / tpl.duration;

								for (; countSegs; --countSegs)
								{
									dash->current_representation_->segments_.data.push_back(seg);
									seg.startPTS_ += (sdb != sde) ? *sdb : tpl.duration;
									seg.range_end_ += timeBased ? *(sdb++) : 1;
								}
								return;
							}
						}
						else if (dash->currentNode_ & DASHTree::MPDNODE_SEGMENTDURATIONS)
						{
							if (strcmp(el, "SegmentDurations") == 0)
								dash->currentNode_ &= ~DASHTree::MPDNODE_SEGMENTDURATIONS;
						}
						else if (dash->currentNode_ & DASHTree::MPDNODE_CONTENTPROTECTION)
						{
							if (dash->currentNode_ & DASHTree::MPDNODE_PSSH)
							{
								if (strcmp(el, "cenc:pssh") == 0)
								{
									dash->adp_pssh_ = dash->strXMLText_;
									dash->currentNode_ &= ~DASHTree::MPDNODE_PSSH;
								}
							}
							else if (strcmp(el, "ContentProtection") == 0)
							{
								if (dash->adp_pssh_.empty())
									dash->adp_pssh_ = "FILE";
								dash->currentNode_ &= ~DASHTree::MPDNODE_CONTENTPROTECTION;
							}
						}
						else if (strcmp(el, "AdaptationSet") == 0)
						{
							if (!dash->pssh_.empty() && dash->adp_pssh_ != dash->pssh_)
								dash->current_period_->adaptationSets_.pop_back();
							else
								dash->pssh_ = dash->adp_pssh_;
							dash->currentNode_ &= ~DASHTree::MPDNODE_ADAPTIONSET;
						}
					}
					else
					{
						ReplacePlaceHolders(dash->current_representation_->url_, dash->current_representation_->id, dash->current_representation_->bandwidth_);
						ReplacePlaceHolders(dash->current_representation_->segtpl_.media, dash->current_representation_->id, dash->current_representation_->bandwidth_);
					}
				}
				else if (strcmp(el, "Period") == 0)
				{
					dash->currentNode_ &= ~DASHTree::MPDNODE_PERIOD;
				}
				else if (strcmp(el, "MPD") == 0)
				{
					dash->currentNode_ &= ~DASHTree::MPDNODE_MPD;
				}
			}
		}

		/*----------------------------------------------------------------------
|   curl callback
+---------------------------------------------------------------------*/

		static size_t curl_fwrite(void *buffer, size_t size, size_t nmemb, void *dest)
		{
			XML_Parser parser(reinterpret_cast<XML_Parser>(dest));

			bool done(false);
			if (XML_Parse(parser, (const char *)buffer, size * nmemb, done) == XML_STATUS_ERROR)
				return 0;
			return size * nmemb;
		}

		/*----------------------------------------------------------------------
|   DASHTree
+---------------------------------------------------------------------*/

		void DASHTree::Segment::SetRange(const char *range)
		{
			const char *delim(strchr(range, '-'));
			if (delim)
			{
				range_begin_ = strtoull(range, 0, 10);
				range_end_ = strtoull(delim + 1, 0, 10);
			}
			else
				range_begin_ = range_end_ = 0;
		}

		bool DASHTree::open(const char *url)
		{
			XML_Parser p = XML_ParserCreate(NULL);
			if (!p)
				return false;
			XML_SetUserData(p, (void *)this);
			XML_SetElementHandler(p, start, end);
			XML_SetCharacterDataHandler(p, text);
			currentNode_ = 0;
			strXMLText_.clear();

			struct curl_slist *headerlist = NULL;

			curl_global_init(CURL_GLOBAL_ALL);

			headerlist = curl_slist_append(headerlist, "Accept: */*");
			headerlist = curl_slist_append(headerlist, "Connection: close");
			headerlist = curl_slist_append(headerlist, "Pragma: no-cache");
			headerlist = curl_slist_append(headerlist, "Cache-Control: no-cache");
			headerlist = curl_slist_append(headerlist, "x-retry-count: 0");
			headerlist = curl_slist_append(headerlist, "x-request-priority: CRITICAL");
			headerlist = curl_slist_append(headerlist, "Accept-Encoding: gzip, deflate");
			headerlist = curl_slist_append(headerlist, "Accept-Language: de-DE,de;q=0.8,en-US;q=0.6,en;q=0.4");
			headerlist = curl_slist_append(headerlist, "EXPECT:");

			CURL *curl = curl_easy_init();
			curl_easy_setopt(curl, CURLOPT_URL, url);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
			/* Define our callback to get called when there's data to be written */
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_fwrite);
			/* Set a pointer to our struct to pass to the callback */
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, p);
			/* Automaticlly decompress gzipped responses */
			curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip");
			CURLcode res = curl_easy_perform(curl);

			download_speed_ = 0.0;
			curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD, &download_speed_);

			curl_easy_cleanup(curl);

			XML_ParserFree(p);

			if (res != CURLE_OK)
				return false;
			return true;
		}

		bool DASHTree::has_type(StreamType t)
		{
			if (periods_.empty())
				return false;

			for (std::vector<AdaptationSet *>::const_iterator b(periods_[0]->adaptationSets_.begin()), e(periods_[0]->adaptationSets_.end()); b != e; ++b)
				if ((*b)->type_ == t)
					return true;
			return false;
		}

		uint32_t DASHTree::estimate_segcount(uint32_t duration, uint32_t timescale)
		{
			double tmp(duration);
			duration /= timescale;
			return static_cast<uint32_t>((overallSeconds_ / duration) * 1.01);
		}
