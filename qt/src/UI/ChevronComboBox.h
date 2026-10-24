#pragma once

#include <QComboBox>

// A combo box that paints the Windows 11 chevron itself: a style-sheet combo box draws no arrow without an image file.
class ChevronComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit ChevronComboBox(QWidget* parent = nullptr);
    ~ChevronComboBox();

protected:
    void paintEvent(QPaintEvent* event) override;
};
