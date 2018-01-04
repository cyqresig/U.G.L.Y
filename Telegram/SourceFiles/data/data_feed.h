/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ChannelData;

namespace Data {

class Feed {
public:
	Feed(FeedId id);

	void registerOne(not_null<ChannelData*> channel);
	void unregisterOne(not_null<ChannelData*> channel);

	void setUnreadCounts(int unreadCount, int unreadMutedCount);

private:
	FeedId _id = 0;
	base::flat_set<not_null<ChannelData*>> _channels;
	int _unreadCount = 0;
	int _unreadMutedCount = 0;
	bool _complete = false;

};

} // namespace Data
