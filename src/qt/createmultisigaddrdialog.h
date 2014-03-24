#ifndef CREATEMULTISIGADDRESSDIALOG_H
#define CREATEMULTISIGADDRESSDIALOG_H

#include <QDialog>

namespace Ui {
    class CreateMultiSigAddrDialog;
}

/** Dialog for editing an address and associated information.
 */
class CreateMultiSigAddrDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateMultiSigAddrDialog(QWidget *parent = 0);
    ~CreateMultiSigAddrDialog();

public slots:
    void create();
    void cancel();
    void handleSelectionChanged(int idx);

private:
    Ui::CreateMultiSigAddrDialog *ui;
    int currentPubkeyNum;
};

#endif // CREATEMULTISIGADDRESSDIALOG_H
