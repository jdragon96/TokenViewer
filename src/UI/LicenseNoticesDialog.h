#pragma once

#include <QDialog>

class LicenseNoticesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LicenseNoticesDialog(QWidget* parent = nullptr);
    ~LicenseNoticesDialog();
};
