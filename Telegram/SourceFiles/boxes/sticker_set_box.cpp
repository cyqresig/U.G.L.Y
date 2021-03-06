/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/sticker_set_box.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "chat_helpers/stickers.h"
#include "boxes/confirm_box.h"
#include "core/application.h"
#include "mtproto/sender.h"
#include "storage/localstorage.h"
#include "dialogs/dialogs_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/image/image.h"
#include "ui/text/text_utilities.h"
#include "ui/emoji_config.h"
#include "lottie/lottie_multi_player.h"
#include "lottie/lottie_animation.h"
#include "window/window_session_controller.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace {

constexpr auto kStickersPanelPerRow = 5;

} // namespace

class StickerSetBox::Inner : public Ui::RpWidget, private base::Subscriber {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const MTPInputStickerSet &set);

	bool loaded() const;
	bool notInstalled() const;
	bool official() const;
	rpl::producer<TextWithEntities> title() const;
	QString shortName() const;

	void install();
	rpl::producer<uint64> setInstalled() const;
	rpl::producer<> updateControls() const;

	~Inner();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Element {
		not_null<DocumentData*> document;
		Lottie::Animation *animated = nullptr;
		Ui::Animations::Simple overAnimation;
	};

	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

	QSize boundingBoxSize() const;

	void paintSticker(Painter &p, int index, QPoint position) const;
	void setupLottie(int index);

	void updateSelected();
	void setSelected(int selected);
	void startOverAnimation(int index, float64 from, float64 to);
	int stickerFromGlobalPos(const QPoint &p) const;

	void gotSet(const MTPmessages_StickerSet &set);
	void installDone(const MTPmessages_StickerSetInstallResult &result);

	bool isMasksSet() const {
		return (_setFlags & MTPDstickerSet::Flag::f_masks);
	}

	not_null<Lottie::MultiPlayer*> getLottiePlayer();

	void showPreview();

	not_null<Window::SessionController*> _controller;
	MTP::Sender _mtp;
	std::vector<Element> _elements;
	std::unique_ptr<Lottie::MultiPlayer> _lottiePlayer;
	Stickers::Pack _pack;
	Stickers::ByEmojiMap _emoji;
	bool _loaded = false;
	uint64 _setId = 0;
	uint64 _setAccess = 0;
	QString _setTitle, _setShortName;
	int _setCount = 0;
	int32 _setHash = 0;
	MTPDstickerSet::Flags _setFlags = 0;
	TimeId _setInstallDate = TimeId(0);
	ImagePtr _setThumbnail;

	MTPInputStickerSet _input;

	mtpRequestId _installRequest = 0;

	int _selected = -1;

	base::Timer _previewTimer;
	int _previewShown = -1;

	rpl::event_stream<uint64> _setInstalled;
	rpl::event_stream<> _updateControls;

};

StickerSetBox::StickerSetBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	const MTPInputStickerSet &set)
: _controller(controller)
, _set(set) {
}

void StickerSetBox::Show(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document) {
	if (const auto sticker = document->sticker()) {
		if (sticker->set.type() != mtpc_inputStickerSetEmpty) {
			Ui::show(
				Box<StickerSetBox>(controller, sticker->set),
				LayerOption::KeepOther);
		}
	}
}

void StickerSetBox::prepare() {
	setTitle(tr::lng_contacts_loading());

	_inner = setInnerWidget(
		object_ptr<Inner>(this, _controller, _set),
		st::stickersScroll);
	_controller->session().data().stickersUpdated(
	) | rpl::start_with_next([=] {
		updateButtons();
	}, lifetime());

	setDimensions(st::boxWideWidth, st::stickersMaxHeight);

	updateTitleAndButtons();

	_inner->updateControls(
	) | rpl::start_with_next([=] {
		updateTitleAndButtons();
	}, lifetime());

	_inner->setInstalled(
	) | rpl::start_with_next([=](uint64 setId) {
		_controller->session().api().stickerSetInstalled(setId);
		closeBox();
	}, lifetime());
}

void StickerSetBox::addStickers() {
	_inner->install();
}

void StickerSetBox::shareStickers() {
	auto url = Core::App().createInternalLinkFull(qsl("addstickers/") + _inner->shortName());
	QGuiApplication::clipboard()->setText(url);
	Ui::show(Box<InformBox>(tr::lng_stickers_copied(tr::now)));
}

void StickerSetBox::updateTitleAndButtons() {
	setTitle(_inner->title());
	updateButtons();
}

void StickerSetBox::updateButtons() {
	clearButtons();
	if (_inner->loaded()) {
		if (_inner->notInstalled()) {
			addButton(tr::lng_stickers_add_pack(), [=] { addStickers(); });
			addButton(tr::lng_cancel(), [=] { closeBox(); });
		} else if (_inner->official()) {
			addButton(tr::lng_about_done(), [=] { closeBox(); });
		} else {
			addButton(tr::lng_stickers_share_pack(), [=] { shareStickers(); });
			addButton(tr::lng_cancel(), [=] { closeBox(); });
		}
	} else {
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	}
	update();
}

void StickerSetBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_inner->resize(width(), _inner->height());
}

StickerSetBox::Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	const MTPInputStickerSet &set)
: RpWidget(parent)
, _controller(controller)
, _input(set)
, _previewTimer([=] { showPreview(); }) {
	set.match([&](const MTPDinputStickerSetID &data) {
		_setId = data.vid().v;
		_setAccess = data.vaccess_hash().v;
	}, [&](const MTPDinputStickerSetShortName &data) {
		_setShortName = qs(data.vshort_name());
	}, [&](const MTPDinputStickerSetEmpty &) {
	}, [&](const MTPDinputStickerSetAnimatedEmoji &) {
	});

	_mtp.request(MTPmessages_GetStickerSet(
		_input
	)).done([=](const MTPmessages_StickerSet &result) {
		gotSet(result);
	}).fail([=](const RPCError &error) {
		_loaded = true;
		Ui::show(Box<InformBox>(tr::lng_stickers_not_found(tr::now)));
	}).send();

	_controller->session().api().updateStickers();

	subscribe(_controller->session().downloaderTaskFinished(), [this] { update(); });

	setMouseTracking(true);
}

void StickerSetBox::Inner::gotSet(const MTPmessages_StickerSet &set) {
	_pack.clear();
	_emoji.clear();
	_elements.clear();
	_selected = -1;
	setCursor(style::cur_default);
	set.match([&](const MTPDmessages_stickerSet &data) {
		const auto &v = data.vdocuments().v;
		_pack.reserve(v.size());
		_elements.reserve(v.size());
		for (const auto &item : v) {
			const auto document = _controller->session().data().processDocument(item);
			const auto sticker = document->sticker();
			if (!sticker) {
				continue;
			}
			_pack.push_back(document);
			_elements.push_back({ document });
		}
		for (const auto &pack : data.vpacks().v) {
			pack.match([&](const MTPDstickerPack &pack) {
				if (const auto emoji = Ui::Emoji::Find(qs(pack.vemoticon()))) {
					const auto original = emoji->original();
					auto &stickers = pack.vdocuments().v;

					auto p = Stickers::Pack();
					p.reserve(stickers.size());
					for (auto j = 0, c = stickers.size(); j != c; ++j) {
						auto doc = _controller->session().data().document(stickers[j].v);
						if (!doc || !doc->sticker()) continue;

						p.push_back(doc);
					}
					_emoji.insert(original, p);
				}
			});
		}
		data.vset().match([&](const MTPDstickerSet &set) {
			_setTitle = Stickers::GetSetTitle(set);
			_setShortName = qs(set.vshort_name());
			_setId = set.vid().v;
			_setAccess = set.vaccess_hash().v;
			_setCount = set.vcount().v;
			_setHash = set.vhash().v;
			_setFlags = set.vflags().v;
			_setInstallDate = set.vinstalled_date().value_or(0);
			if (const auto thumb = set.vthumb()) {
				_setThumbnail = Images::Create(set, *thumb);
			} else {
				_setThumbnail = ImagePtr();
			}
			auto &sets = _controller->session().data().stickerSetsRef();
			const auto it = sets.find(_setId);
			if (it != sets.cend()) {
				using ClientFlag = MTPDstickerSet_ClientFlag;
				const auto clientFlags = it->flags
					& (ClientFlag::f_featured
						| ClientFlag::f_not_loaded
						| ClientFlag::f_unread
						| ClientFlag::f_special);
				_setFlags |= clientFlags;
				it->flags = _setFlags;
				it->installDate = _setInstallDate;
				it->stickers = _pack;
				it->emoji = _emoji;
				it->thumbnail = _setThumbnail;
			}
		});
	});

	if (_pack.isEmpty()) {
		Ui::show(Box<InformBox>(tr::lng_stickers_not_found(tr::now)));
		return;
	} else {
		int32 rows = _pack.size() / kStickersPanelPerRow + ((_pack.size() % kStickersPanelPerRow) ? 1 : 0);
		resize(st::stickersPadding.left() + kStickersPanelPerRow * st::stickersSize.width(), st::stickersPadding.top() + rows * st::stickersSize.height() + st::stickersPadding.bottom());
	}
	_loaded = true;

	updateSelected();
	_updateControls.fire({});
}

rpl::producer<uint64> StickerSetBox::Inner::setInstalled() const {
	return _setInstalled.events();
}

rpl::producer<> StickerSetBox::Inner::updateControls() const {
	return _updateControls.events();
}

void StickerSetBox::Inner::installDone(
		const MTPmessages_StickerSetInstallResult &result) {
	auto &sets = _controller->session().data().stickerSetsRef();

	bool wasArchived = (_setFlags & MTPDstickerSet::Flag::f_archived);
	if (wasArchived) {
		auto index = _controller->session().data().archivedStickerSetsOrderRef().indexOf(_setId);
		if (index >= 0) {
			_controller->session().data().archivedStickerSetsOrderRef().removeAt(index);
		}
	}
	_setInstallDate = base::unixtime::now();
	_setFlags &= ~MTPDstickerSet::Flag::f_archived;
	_setFlags |= MTPDstickerSet::Flag::f_installed_date;
	auto it = sets.find(_setId);
	if (it == sets.cend()) {
		it = sets.insert(
			_setId,
			Stickers::Set(
				_setId,
				_setAccess,
				_setTitle,
				_setShortName,
				_setCount,
				_setHash,
				_setFlags,
				_setInstallDate,
				_setThumbnail));
	} else {
		it->flags = _setFlags;
		it->installDate = _setInstallDate;
	}
	it->stickers = _pack;
	it->emoji = _emoji;

	auto &order = _controller->session().data().stickerSetsOrderRef();
	int insertAtIndex = 0, currentIndex = order.indexOf(_setId);
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, _setId);
	}

	auto custom = sets.find(Stickers::CustomSetId);
	if (custom != sets.cend()) {
		for_const (auto sticker, _pack) {
			int removeIndex = custom->stickers.indexOf(sticker);
			if (removeIndex >= 0) custom->stickers.removeAt(removeIndex);
		}
		if (custom->stickers.isEmpty()) {
			sets.erase(custom);
		}
	}

	if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
		Stickers::ApplyArchivedResult(result.c_messages_stickerSetInstallResultArchive());
	} else {
		if (wasArchived) {
			Local::writeArchivedStickers();
		}
		Local::writeInstalledStickers();
		_controller->session().data().notifyStickersUpdated();
	}
	_setInstalled.fire_copy(_setId);
}

void StickerSetBox::Inner::mousePressEvent(QMouseEvent *e) {
	int index = stickerFromGlobalPos(e->globalPos());
	if (index >= 0 && index < _pack.size()) {
		_previewTimer.callOnce(QApplication::startDragTime());
	}
}

void StickerSetBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateSelected();
	if (_previewShown >= 0) {
		int index = stickerFromGlobalPos(e->globalPos());
		if (index >= 0 && index < _pack.size() && index != _previewShown) {
			_previewShown = index;
			if (const auto w = App::wnd()) {
				w->showMediaPreview(
					Data::FileOriginStickerSet(_setId, _setAccess),
					_pack[_previewShown]);
			}
		}
	}
}

void StickerSetBox::Inner::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void StickerSetBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	if (_previewShown >= 0) {
		_previewShown = -1;
		return;
	}
	if (_previewTimer.isActive()) {
		_previewTimer.cancel();
		const auto index = stickerFromGlobalPos(e->globalPos());
		if (index >= 0 && index < _pack.size() && !isMasksSet()) {
			const auto sticker = _pack[index];
			Core::App().postponeCall(crl::guard(App::main(), [=] {
				if (App::main()->onSendSticker(sticker)) {
					Ui::hideSettingsAndLayer();
				}
			}));
		}
	}
}

void StickerSetBox::Inner::updateSelected() {
	auto selected = stickerFromGlobalPos(QCursor::pos());
	setSelected(isMasksSet() ? -1 : selected);
}

void StickerSetBox::Inner::setSelected(int selected) {
	if (_selected != selected) {
		startOverAnimation(_selected, 1., 0.);
		_selected = selected;
		startOverAnimation(_selected, 0., 1.);
		setCursor(_selected >= 0 ? style::cur_pointer : style::cur_default);
	}
}

void StickerSetBox::Inner::startOverAnimation(int index, float64 from, float64 to) {
	if (index < 0 || index >= _elements.size()) {
		return;
	}
	_elements[index].overAnimation.start([=] {
		const auto row = index / kStickersPanelPerRow;
		const auto column = index % kStickersPanelPerRow;
		const auto left = st::stickersPadding.left() + column * st::stickersSize.width();
		const auto top = st::stickersPadding.top() + row * st::stickersSize.height();
		rtlupdate(left, top, st::stickersSize.width(), st::stickersSize.height());
	}, from, to, st::emojiPanDuration);
}

void StickerSetBox::Inner::showPreview() {
	int index = stickerFromGlobalPos(QCursor::pos());
	if (index >= 0 && index < _pack.size()) {
		_previewShown = index;
		if (const auto w = App::wnd()) {
			w->showMediaPreview(
				Data::FileOriginStickerSet(_setId, _setAccess),
				_pack[_previewShown]);
		}
	}
}

not_null<Lottie::MultiPlayer*> StickerSetBox::Inner::getLottiePlayer() {
	if (!_lottiePlayer) {
		_lottiePlayer = std::make_unique<Lottie::MultiPlayer>(
			Lottie::Quality::Default,
			Lottie::MakeFrameRenderer());
		_lottiePlayer->updates(
		) | rpl::start_with_next([=] {
			update();
		}, lifetime());
	}
	return _lottiePlayer.get();
}

int32 StickerSetBox::Inner::stickerFromGlobalPos(const QPoint &p) const {
	QPoint l(mapFromGlobal(p));
	if (rtl()) l.setX(width() - l.x());
	int32 row = (l.y() >= st::stickersPadding.top()) ? qFloor((l.y() - st::stickersPadding.top()) / st::stickersSize.height()) : -1;
	int32 col = (l.x() >= st::stickersPadding.left()) ? qFloor((l.x() - st::stickersPadding.left()) / st::stickersSize.width()) : -1;
	if (row >= 0 && col >= 0 && col < kStickersPanelPerRow) {
		int32 result = row * kStickersPanelPerRow + col;
		return (result < _pack.size()) ? result : -1;
	}
	return -1;
}

void StickerSetBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	if (_elements.empty()) {
		return;
	}

	int32 rows = (_elements.size() / kStickersPanelPerRow)
		+ ((_elements.size() % kStickersPanelPerRow) ? 1 : 0);
	int32 from = qFloor(e->rect().top() / st::stickersSize.height()), to = qFloor(e->rect().bottom() / st::stickersSize.height()) + 1;

	for (int32 i = from; i < to; ++i) {
		for (int32 j = 0; j < kStickersPanelPerRow; ++j) {
			int32 index = i * kStickersPanelPerRow + j;
			if (index >= _elements.size()) {
				break;
			}
			const auto pos = QPoint(st::stickersPadding.left() + j * st::stickersSize.width(), st::stickersPadding.top() + i * st::stickersSize.height());
			paintSticker(p, index, pos);
		}
	}

	if (_lottiePlayer) {
		const auto paused = _controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
		if (!paused) {
			_lottiePlayer->markFrameShown();
		}
	}
}

QSize StickerSetBox::Inner::boundingBoxSize() const {
	return QSize(
		st::stickersSize.width() - st::buttonRadius * 2,
		st::stickersSize.height() - st::buttonRadius * 2);
}

void StickerSetBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	const auto pauseInRows = [&](int fromRow, int tillRow) {
		Expects(fromRow <= tillRow);

		for (auto i = fromRow; i != tillRow; ++i) {
			for (auto j = 0; j != kStickersPanelPerRow; ++j) {
				const auto index = i * kStickersPanelPerRow + j;
				if (index >= _elements.size()) {
					break;
				}
				if (const auto animated = _elements[index].animated) {
					_lottiePlayer->pause(animated);
				}
			}
		}
	};
	const auto count = int(_elements.size());
	const auto rowsCount = (count / kStickersPanelPerRow)
		+ ((count % kStickersPanelPerRow) ? 1 : 0);
	const auto rowsTop = st::stickersPadding.top();
	const auto singleHeight = st::stickersSize.height();
	const auto rowsBottom = rowsTop + rowsCount * singleHeight;
	if (visibleTop >= rowsTop + singleHeight && visibleTop < rowsBottom) {
		const auto pauseHeight = (visibleTop - rowsTop);
		const auto pauseRows = std::min(
			pauseHeight / singleHeight,
			rowsCount);
		pauseInRows(0, pauseRows);
	}
	if (visibleBottom > rowsTop
		&& visibleBottom + singleHeight <= rowsBottom) {
		const auto pauseHeight = (rowsBottom - visibleBottom);
		const auto pauseRows = std::min(
			pauseHeight / singleHeight,
			rowsCount);
		pauseInRows(rowsCount - pauseRows, rowsCount);
	}
}

void StickerSetBox::Inner::setupLottie(int index) {
	auto &element = _elements[index];
	const auto document = element.document;

	element.animated = Stickers::LottieAnimationFromDocument(
		getLottiePlayer(),
		document,
		Stickers::LottieSize::StickerSet,
		boundingBoxSize() * cIntRetinaFactor());
}

void StickerSetBox::Inner::paintSticker(
		Painter &p,
		int index,
		QPoint position) const {
	if (const auto over = _elements[index].overAnimation.value((index == _selected) ? 1. : 0.)) {
		p.setOpacity(over);
		auto tl = position;
		if (rtl()) tl.setX(width() - tl.x() - st::stickersSize.width());
		App::roundRect(p, QRect(tl, st::stickersSize), st::emojiPanHover, StickerHoverCorners);
		p.setOpacity(1);
	}

	const auto &element = _elements[index];
	const auto document = element.document;
	document->checkStickerSmall();

	if (document->sticker()->animated
		&& !element.animated
		&& document->loaded()) {
		const_cast<Inner*>(this)->setupLottie(index);
	}

	auto w = 1;
	auto h = 1;
	if (element.animated && !document->dimensions.isEmpty()) {
		const auto request = Lottie::FrameRequest{ boundingBoxSize() * cIntRetinaFactor() };
		const auto size = request.size(document->dimensions) / cIntRetinaFactor();
		w = std::max(size.width(), 1);
		h = std::max(size.height(), 1);
	} else {
		auto coef = qMin((st::stickersSize.width() - st::buttonRadius * 2) / float64(document->dimensions.width()), (st::stickersSize.height() - st::buttonRadius * 2) / float64(document->dimensions.height()));
		if (coef > 1) coef = 1;
		w = std::max(qRound(coef * document->dimensions.width()), 1);
		h = std::max(qRound(coef * document->dimensions.height()), 1);
	}
	QPoint ppos = position + QPoint((st::stickersSize.width() - w) / 2, (st::stickersSize.height() - h) / 2);
	if (element.animated && element.animated->ready()) {
		const auto frame = element.animated->frame();
		p.drawImage(
			QRect(ppos, frame.size() / cIntRetinaFactor()),
			frame);

		_lottiePlayer->unpause(element.animated);
	} else if (const auto image = document->getStickerSmall()) {
		p.drawPixmapLeft(
			ppos,
			width(),
			image->pix(document->stickerSetOrigin(), w, h));
	}
}

bool StickerSetBox::Inner::loaded() const {
	return _loaded && !_pack.isEmpty();
}

bool StickerSetBox::Inner::notInstalled() const {
	if (!_loaded) {
		return false;
	}
	const auto it = _controller->session().data().stickerSets().constFind(_setId);
	if ((it == _controller->session().data().stickerSets().cend())
		|| !(it->flags & MTPDstickerSet::Flag::f_installed_date)
		|| (it->flags & MTPDstickerSet::Flag::f_archived)) {
		return !_pack.empty();
	}
	return false;
}

bool StickerSetBox::Inner::official() const {
	return _loaded && _setShortName.isEmpty();
}

rpl::producer<TextWithEntities> StickerSetBox::Inner::title() const {
	if (!_loaded) {
		return tr::lng_contacts_loading() | Ui::Text::ToWithEntities();
	} else if (_pack.isEmpty()) {
		return tr::lng_attach_failed() | Ui::Text::ToWithEntities();
	}
	auto text = TextWithEntities{ _setTitle };
	TextUtilities::ParseEntities(text, TextParseMentions);
	return rpl::single(text);
}

QString StickerSetBox::Inner::shortName() const {
	return _setShortName;
}

void StickerSetBox::Inner::install() {
	if (isMasksSet()) {
		Ui::show(
			Box<InformBox>(tr::lng_stickers_masks_pack(tr::now)),
			LayerOption::KeepOther);
		return;
	} else if (_installRequest) {
		return;
	}
	_installRequest = _mtp.request(MTPmessages_InstallStickerSet(
		_input,
		MTP_bool(false)
	)).done([=](const MTPmessages_StickerSetInstallResult &result) {
		installDone(result);
	}).fail([=](const RPCError &error) {
		Ui::show(Box<InformBox>(tr::lng_stickers_not_found(tr::now)));
	}).send();
}

StickerSetBox::Inner::~Inner() = default;
