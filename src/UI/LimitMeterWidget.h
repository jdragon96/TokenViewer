#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class LimitMeterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LimitMeterWidget(const QString& name, QWidget* parent = nullptr);
    ~LimitMeterWidget();

public:
    void SetValue(const QString& valueText, double percent, const QString& subText, const QColor& fillColor, bool dimmed);
    QSize sizeHint() const override;

public:
    QString GetValueText() const;
    QString GetSubText() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_strName;
    QString m_strValueText;
    double m_dPercent;
    QString m_strSubText;
    QColor m_clrFill;
    bool m_bDimmed;
};
