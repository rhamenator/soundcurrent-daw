// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/session.hpp>
#include <QDialog>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QLocale>
#include <QWheelEvent>
#include <algorithm>
namespace soundcurrent::daw::ui {
template <class Base> class MasterFocusControl : public Base {
  public:
    using Base::Base;
    void wheelEvent(QWheelEvent *event) override {
        if (this->hasFocus())
            Base::wheelEvent(event);
        else
            event->ignore();
    }
};
// Stages an immutable snapshot. Apply is one semantic project edit; Cancel has no effect.
class MasterDialog : public QDialog {
    Session session_;
    MasterBus staged_;
    QComboBox *kind_;
    QSpinBox *channels_;
    QTableWidget *rows_;
    QLabel *error_;
    std::optional<MasterBus> result_;
    static QString tr(const char *s) {
        return QCoreApplication::translate("MasterDialog", s);
    }
    void add(const Id &track, std::uint32_t source, std::uint32_t destination, double gain) {
        if (rows_->rowCount() >= 4096) {
            error_->setText(tr("The desktop editor supports at most 4096 entries. The project API "
                               "supports 65536."));
            return;
        }
        const int row = rows_->rowCount();
        rows_->insertRow(row);
        auto *t = new MasterFocusControl<QComboBox>;
        t->setFocusPolicy(Qt::StrongFocus);
        for (const auto &v : session_.tracks)
            t->addItem(QString::fromUtf8(v.name), QString::fromStdString(v.id.str()));
        t->setCurrentIndex(t->findData(QString::fromStdString(track.str())));
        auto *s = new MasterFocusControl<QSpinBox>;
        auto *d = new MasterFocusControl<QSpinBox>;
        s->setRange(1, 256);
        d->setRange(1, 256);
        s->setValue(int(source + 1));
        d->setValue(int(destination + 1));
        auto *g = new QLineEdit(QLocale().toString(gain, 'g', 17));
        t->setAccessibleName(tr("Source track"));
        s->setAccessibleName(tr("Source channel"));
        d->setAccessibleName(tr("Destination channel"));
        g->setAccessibleName(tr("Linear gain"));
        rows_->setCellWidget(row, 0, t);
        rows_->setCellWidget(row, 1, s);
        rows_->setCellWidget(row, 2, d);
        rows_->setCellWidget(row, 3, g);
    }
    void apply() {
        try {
            auto m = staged_;
            m.plan.output = {static_cast<LayoutKind>(kind_->currentData().toInt()),
                             std::uint32_t(channels_->value())};
            if (m.plan.output.channels != staged_.plan.output.channels)
                m.output = {};
            m.plan.tracks.clear();
            for (int row = 0; row < rows_->rowCount(); ++row) {
                const auto id = Id(static_cast<QComboBox *>(rows_->cellWidget(row, 0))
                                       ->currentData()
                                       .toString()
                                       .toStdString());
                const auto source =
                    std::uint32_t(static_cast<QSpinBox *>(rows_->cellWidget(row, 1))->value() - 1);
                const auto dest =
                    std::uint32_t(static_cast<QSpinBox *>(rows_->cellWidget(row, 2))->value() - 1);
                bool ok = false;
                const auto gain = QLocale().toDouble(
                    static_cast<QLineEdit *>(rows_->cellWidget(row, 3))->text(), &ok);
                if (!ok)
                    throw ProjectError(ErrorCode::InvalidParameter, "Invalid matrix gain");
                auto t = std::find_if(m.plan.tracks.begin(), m.plan.tracks.end(),
                                      [&](const auto &v) { return v.track == id; });
                if (t == m.plan.tracks.end()) {
                    m.plan.tracks.push_back({id, {}});
                    t = m.plan.tracks.end() - 1;
                }
                t->channels.push_back({source, dest, gain});
            }
            auto candidate = session_;
            candidate.master = m;
            validate(candidate);
            result_ = std::move(m);
            accept();
        } catch (const std::exception &e) {
            error_->setText(tr("Master was not applied: %1").arg(QString::fromUtf8(e.what())));
        }
    }

  public:
    explicit MasterDialog(const Session &s, QWidget *parent = nullptr)
        : QDialog(parent), session_(s) {
        setObjectName("masterDialog");
        setWindowTitle(tr("Master layout and channel matrix"));
        resize(780, 560);
        if (s.master)
            staged_ = *s.master;
        else {
            staged_.plan.output = s.tracks.empty() ? ChannelLayout{} : s.tracks.front().layout;
            for (const auto &t : s.tracks)
                if (t.layout == staged_.plan.output) {
                    TrackMix lane{t.id, {}};
                    for (std::uint32_t c = 0; c < t.layout.channels; ++c)
                        lane.channels.push_back({c, c, 1});
                    staged_.plan.tracks.push_back(std::move(lane));
                }
        }
        auto *v = new QVBoxLayout(this);
        auto *hint =
            new QLabel(tr("Each row maps one source channel to a master channel. Gains are linear, "
                          "including zero or negative polarity. Unlisted tracks are excluded. "
                          "Changing the output channel count clears saved output assignments."));
        hint->setWordWrap(true);
        v->addWidget(hint);
        auto *line = new QHBoxLayout;
        kind_ = new MasterFocusControl<QComboBox>;
        kind_->setFocusPolicy(Qt::StrongFocus);
        kind_->setObjectName("masterLayout");
        kind_->addItem(tr("Mono"), int(LayoutKind::Mono));
        kind_->addItem(tr("Stereo"), int(LayoutKind::Stereo));
        kind_->addItem(tr("Discrete"), int(LayoutKind::Discrete));
        kind_->setCurrentIndex(kind_->findData(int(staged_.plan.output.kind)));
        channels_ = new MasterFocusControl<QSpinBox>;
        channels_->setObjectName("masterChannels");
        channels_->setRange(1, 256);
        channels_->setValue(int(staged_.plan.output.channels));
        kind_->setAccessibleName(tr("Master layout"));
        channels_->setAccessibleName(tr("Master channels"));
        line->addWidget(kind_);
        line->addWidget(channels_);
        v->addLayout(line);
        connect(kind_, &QComboBox::currentIndexChanged, this, [&] {
            const auto k = static_cast<LayoutKind>(kind_->currentData().toInt());
            if (k != LayoutKind::Discrete)
                channels_->setValue(k == LayoutKind::Mono ? 1 : 2);
            channels_->setEnabled(k == LayoutKind::Discrete);
        });
        channels_->setEnabled(staged_.plan.output.kind == LayoutKind::Discrete);
        rows_ = new QTableWidget(0, 4);
        rows_->setObjectName("masterMatrix");
        rows_->setHorizontalHeaderLabels(
            {tr("Track"), tr("Source channel"), tr("Master channel"), tr("Linear gain")});
        rows_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        rows_->setSelectionBehavior(QAbstractItemView::SelectRows);
        v->addWidget(rows_);
        error_ = new QLabel;
        error_->setObjectName("masterError");
        error_->setWordWrap(true);
        v->addWidget(error_);
        std::size_t size = 0;
        for (const auto &t : staged_.plan.tracks)
            size += t.channels.size();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
        v->addWidget(buttons);
        if (size > 4096) {
            error_->setText(tr("This matrix exceeds the 4096-entry desktop editor limit; its saved "
                               "state is preserved. Use the project API."));
            buttons->button(QDialogButtonBox::Apply)->setEnabled(false);
        } else
            for (const auto &t : staged_.plan.tracks)
                for (const auto &c : t.channels)
                    add(t.track, c.source, c.destination, c.gain);
        auto *addRow = new QPushButton(tr("Add route"));
        addRow->setObjectName("masterAddRoute");
        auto *remove = new QPushButton(tr("Remove selected routes"));
        auto *ops = new QHBoxLayout;
        ops->addWidget(addRow);
        ops->addWidget(remove);
        v->insertLayout(v->count() - 1, ops);
        addRow->setEnabled(!s.tracks.empty() && size <= 4096);
        remove->setEnabled(size <= 4096);
        connect(addRow, &QPushButton::clicked, this,
                [this] { add(session_.tracks.front().id, 0, 0, 1); });
        connect(remove, &QPushButton::clicked, this, [this] {
            auto selected = rows_->selectionModel()->selectedRows();
            std::sort(selected.begin(), selected.end(),
                      [](auto a, auto b) { return a.row() > b.row(); });
            for (auto i : selected)
                rows_->removeRow(i.row());
        });
        connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
                [this] { apply(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
    const std::optional<MasterBus> &selection() const {
        return result_;
    }
};
} // namespace soundcurrent::daw::ui
