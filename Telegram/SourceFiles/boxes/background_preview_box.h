/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/binary_guard.h"
#include "window/themes/window_theme.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/history_view_element.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Checkbox;
} // namespace Ui

class BackgroundPreviewBox
	: public BoxContent
	, private HistoryView::SimpleElementDelegate {
public:
	BackgroundPreviewBox(
		QWidget*,
		not_null<Main::Session*> session,
		const Data::WallPaper &paper);

	static bool Start(
		not_null<Main::Session*> session,
		const QString &slug,
		const QMap<QString, QString> &params);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;

private:
	using Element = HistoryView::Element;
	not_null<HistoryView::ElementDelegate*> delegate();
	HistoryView::Context elementContext() override;

	void apply();
	void share();
	void radialAnimationCallback(crl::time now);
	QRect radialRect() const;

	void checkLoadedDocument();
	bool setScaledFromThumb();
	void setScaledFromImage(QImage &&image, QImage &&blurred);
	void updateServiceBg(std::optional<QColor> background);
	std::optional<QColor> patternBackgroundColor() const;
	void paintImage(Painter &p);
	void paintRadial(Painter &p);
	void paintTexts(Painter &p, crl::time ms);
	void paintDate(Painter &p);
	void createBlurCheckbox();
	int textsTop() const;
	void startFadeInFrom(QPixmap previous);
	void checkBlurAnimationStart();

	const not_null<Main::Session*> _session;
	AdminLog::OwnedItem _text1;
	AdminLog::OwnedItem _text2;
	Data::WallPaper _paper;
	QImage _full;
	QPixmap _scaled, _blurred, _fadeOutThumbnail;
	Ui::Animations::Simple _fadeIn;
	Ui::RadialAnimation _radial;
	base::binary_guard _generating;
	std::optional<QColor> _serviceBg;
	object_ptr<Ui::Checkbox> _blur = { nullptr };

};
