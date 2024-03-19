#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QRegExpValidator>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(QWidget *parent = nullptr);
    void dumpCfgIni(std::string cfg_path);
    ~SettingsDialog();
signals:
    void okButtonDone();
    void cancelButtonClicked();
public slots:
    void slotOkButtonDone();
    void slotCancelButtonClicked();
private:
    QPushButton *okDialogButton;
    QPushButton *cancelDialogButton;
    QLabel *hostLabel;
    QLabel *portLabel;
    QLabel *logLabel;
    QLabel *settingsStatus;
    QLineEdit *hostLineEdit;
    QLineEdit *portLineEdit;
    QComboBox *logComboBox;
    QHBoxLayout *hHostLayout;
    QHBoxLayout *hPortLayout;
    QHBoxLayout *hLogLayout;
    QHBoxLayout *hButtonLayout;
    QHBoxLayout *hSettingsStatusLayout;
    QVBoxLayout *verticalDialogLayout;
    QIntValidator *portValidator;
    QRegExpValidator * reValidator;
};

#endif // SETTINGSDIALOG_H
