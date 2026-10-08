// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Adapted 2026-10-06 from the reviewed equalizer working snapshot.
// See reuse/upstream-review.json for source HEAD, hash and retained original.
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyleOptionSpinBox>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
namespace soundcurrent::daw::widgets {
// Adapted from the 2026-10-08 reviewed upstream input. Some Qt numeric
// formatters emit presentation marks their spin-box parser cannot consume.
inline void normalizeNumericDirectionMarks(QString &text, int *cursor = nullptr) {
    for (qsizetype i = text.size(); i-- > 0;) {
        const auto code = text.at(i).unicode();
        if (code == 0x061c || code == 0x200e || code == 0x200f) {
            text.remove(i, 1);
            if (cursor && i < *cursor) --*cursor;
        }
    }
}
// The step-rate curve integrates a half Gaussian: acceleration decays smoothly
// from its peak, and repeat speed asymptotically reaches 16 normal steps.
inline double heldSpinMultiplier(double seconds) {
    const double t = std::max(0.0, seconds - .35);
    return 1.0 + 15.0 * std::erf(t / (1.2 * std::sqrt(2.0)));
}
template <class Base> class AcceleratingSpin : public Base {
  public:
    explicit AcceleratingSpin(QWidget *parent = nullptr) : Base(parent) {
        this->setAccelerated(false);
    }

  protected:
    using NumericValue = decltype(std::declval<Base>().value());
    QValidator::State validate(QString &text, int &position) const override {
        normalizeNumericDirectionMarks(text, &position);
        return Base::validate(text, position);
    }
    NumericValue valueFromText(const QString &text) const override {
        auto normalized = text;
        normalizeNumericDirectionMarks(normalized);
        return Base::valueFromText(normalized);
    }
    void stepBy(int steps) override {
        const int multiplier =
            held_ ? std::clamp(
                        int(std::lround(heldSpinMultiplier(double(clock_.elapsed()) / 1000.0))), 1,
                        16)
                  : 1;
        const auto scaled = std::int64_t(steps) * multiplier;
        Base::stepBy(
            static_cast<int>(std::clamp(scaled, std::int64_t(std::numeric_limits<int>::min()),
                                        std::int64_t(std::numeric_limits<int>::max()))));
    }
    void mousePressEvent(QMouseEvent *event) override {
        QStyleOptionSpinBox option;
        this->initStyleOption(&option);
        const auto hit = this->style()->hitTestComplexControl(QStyle::CC_SpinBox, &option,
                                                              event->position().toPoint(), this);
        if (event->button() == Qt::LeftButton &&
            (hit == QStyle::SC_SpinBoxUp || hit == QStyle::SC_SpinBoxDown))
            begin();
        else
            held_ = false;
        Base::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        held_ = false;
        Base::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
            if (!event->isAutoRepeat())
                begin();
        } else
            held_ = false;
        Base::keyPressEvent(event);
    }
    void keyReleaseEvent(QKeyEvent *event) override {
        if (!event->isAutoRepeat())
            held_ = false;
        Base::keyReleaseEvent(event);
    }
    void wheelEvent(QWheelEvent *event) override {
        held_ = false;
        Base::wheelEvent(event);
    }
    bool event(QEvent *event) override {
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate ||
            event->type() == QEvent::Hide || event->type() == QEvent::EnabledChange)
            held_ = false;
        return Base::event(event);
    }

  private:
    void begin() {
        held_ = true;
        clock_.start();
    }
    bool held_ = false;
    QElapsedTimer clock_;
};
using AcceleratingSpinBox = AcceleratingSpin<QSpinBox>;
using AcceleratingDoubleSpinBox = AcceleratingSpin<QDoubleSpinBox>;
} // namespace soundcurrent::daw::widgets
