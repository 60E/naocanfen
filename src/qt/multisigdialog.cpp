#include "multisigdialog.h"
#include "ui_multisigdialog.h"

#include "wallet.h"
#include "walletmodel.h"
#include "bitcoinunits.h"
#include "addressbookpage.h"
#include "optionsmodel.h"
#include "sendcoinsentry.h"
#include "guiutil.h"
#include "askpassphrasedialog.h"
#include "base58.h"
#include "init.h"
#include "coincontrol.h"
#include "createmultisigaddrdialog.h"

#include "bitcoinrpc.h"
using namespace json_spirit;

#include <QMessageBox>
#include <QTextDocument>
#include <QScrollBar>
#include <QFile>
#include <QTextStream>

CCoinControl* MultiSigDialog::coinControl = new CCoinControl();
CTransaction* MultiSigDialog::rawTx = new CTransaction();

MultiSigDialog::MultiSigDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MultiSigDialog),
    model(0)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->addButton->setIcon(QIcon());
    ui->clearButton->setIcon(QIcon());
    ui->sendButton->setIcon(QIcon());
    ui->btnExportDraft->setIcon(QIcon());
    ui->btnImportDraft->setIcon(QIcon());
    ui->btnCreateAddr->setIcon(QIcon());
#endif

    addEntry();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(ui->btnExportDraft, SIGNAL(clicked()), this, SLOT(exportDraft()));
    connect(ui->btnImportDraft, SIGNAL(clicked()), this, SLOT(importDraft()));
    connect(ui->btnExportAddr, SIGNAL(clicked()), this, SLOT(exportAddress()));
    connect(ui->btnImportAddr, SIGNAL(clicked()), this, SLOT(importAddress()));
    connect(ui->btnCreateAddr, SIGNAL(clicked()), this, SLOT(createAddress()));
    
    connect(ui->btnSign0, SIGNAL(clicked()), this, SLOT(signAddress0()));
    connect(ui->btnSign1, SIGNAL(clicked()), this, SLOT(signAddress1()));
    connect(ui->btnSign2, SIGNAL(clicked()), this, SLOT(signAddress2()));

    fNewRecipientAllowed = true;

    currentIndex = -1;
    isTxCreate = false;
    isComplete = false;
    connect(ui->comboBoxAddrList, SIGNAL(currentIndexChanged(int)), this, SLOT(handleAddrSelectionChanged(int)));
}

void MultiSigDialog::setModel(WalletModel *model)
{
    this->model = model;
    this->wallet = model->getWallet();

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            entry->setModel(model);
        }
    }
    if(model && model->getOptionsModel())
    {
        setSharedBalance(model->getSharedBalance(), model->getSharedUnconfirmedBalance(), model->getSharedImmatureBalance());
        connect(model, SIGNAL(sharedBalanceChanged(qint64, qint64, qint64)), this, SLOT(setSharedBalance(qint64, qint64, qint64)));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
    }
}

MultiSigDialog::~MultiSigDialog()
{
    delete ui;
}

void MultiSigDialog::createAddress()
{
    //if(!model)
        //return;

    CreateMultiSigAddrDialog dlg(this);
    if(dlg.exec())
    {
        updateAddressList();
        updateAddressBalance();
        updateAddressDetail();
    }
}

void MultiSigDialog::createRawTransaction()
{
    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    if(!model)
        return;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            if(entry->validate())
            {
                recipients.append(entry->getValue());
            }
            else
            {
                valid = false;
            }
        }
    }

    if(!valid || recipients.isEmpty())
    {
        return;
    }

    if ( !coinControl->HasSelected() )
    {
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The amount exceeds your balance."),
            QMessageBox::Ok, QMessageBox::Ok);

        return;
    }

    // Format confirmation message
    QStringList formatted;
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
#if QT_VERSION < 0x050000
        formatted.append(tr("<b>%1</b> to %2 (%3)").arg(BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, rcp.amount), Qt::escape(rcp.label), rcp.address));
#else
        formatted.append(tr("<b>%1</b> to %2 (%3)").arg(BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, rcp.amount), rcp.label.toHtmlEscaped(), rcp.address));
#endif
    }

    fNewRecipientAllowed = false;

    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send coins"),
                          tr("Are you sure you want to send %1?").arg(formatted.join(tr(" and "))),
          QMessageBox::Yes|QMessageBox::Cancel,
          QMessageBox::Cancel);

    if(retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }

    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        fNewRecipientAllowed = true;
        return;
    }

    rawTx->SetNull();
    WalletModel::SendCoinsReturn sendstatus = model->createRawTransaction(recipients, *rawTx, coinControl, true);
    switch(sendstatus.status)
    {
    case WalletModel::InvalidAddress:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The recipient address is not valid, please recheck."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::InvalidAmount:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The amount to pay must be larger than 0."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::AmountExceedsBalance:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The amount exceeds your balance."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The total exceeds your balance when the %1 transaction fee is included.").
            arg(BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, sendstatus.fee)),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::DuplicateAddress:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Duplicate address found, can only send to each address once per send operation."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::TransactionCreationFailed:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Error: Transaction creation failed!"),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::TransactionCommitFailed:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Error: The transaction was rejected. This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::Aborted: // User aborted, nothing to do
        break;
    case WalletModel::OK:
        isTxCreate = true;
        break;
    }
    fNewRecipientAllowed = true;
}

bool writeString(const QString &filename, const QString& hex)
{
    QFile file(filename);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    
    QTextStream out(&file);
    out << hex;
    file.close();

    return file.error() == QFile::NoError;
}

void MultiSigDialog::exportAddress()
{
    if ( currentIndex < 0 )
        return;

    QString s = ui->comboBoxAddrList->currentText();
    CBitcoinAddress address(s.toStdString());
    
    CScript subscript;
    CScriptID scriptID;
    address.GetScriptID(scriptID);
    pwalletMain->GetCScript(scriptID, subscript);

    json_spirit::Object addrJson;
    addrJson.push_back(json_spirit::Pair("address", address.ToString()));
    addrJson.push_back(json_spirit::Pair("redeemScript", HexStr(subscript.begin(), subscript.end())));
    std::string ss = json_spirit::write_string(json_spirit::Value(addrJson), false);
    QString addrJsonStr = QString::fromStdString(ss);

    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Save MultiSig Address"), QString(),
            tr("MultiSig Address file (*.fma)"));

    if (filename.isNull()) return;

    if(!writeString(filename, addrJsonStr))
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}

void MultiSigDialog::importAddress()
{
    QString filename = GUIUtil::getLoadFileName(
            this,
            tr("Load MultiSig Address"), QString(),
            tr("MultiSig Address file (*.fma)"));

    if (filename.isNull()) return;
    
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString addrJsonStr;
    QTextStream strin(&file);
    strin >> addrJsonStr;

    Value addrJsonV;
    if (!json_spirit::read_string(addrJsonStr.toStdString(), addrJsonV))
        return;

    const json_spirit::Object& addrJson = addrJsonV.get_obj();
    if (addrJson.empty())
        return;

    const json_spirit::Value& addressV = json_spirit::find_value(addrJson, "address");
    const json_spirit::Value& scriptV  = json_spirit::find_value(addrJson, "redeemScript");

    printf("importAddress redeemScript=%s\n", scriptV.get_str().c_str());
    std::vector<unsigned char> scriptData(ParseHex(scriptV.get_str()));
    CScript scriptPubKey(scriptData.begin(), scriptData.end());

    if (!IsStandard(scriptPubKey))
        return;

    //if ( !scriptPubKey.IsPayToScriptHash())
        //return;

    CScriptID innerID = scriptPubKey.GetID();
    CBitcoinAddress address(innerID);
    if ( addressV.get_str() == address.ToString() )
    {
        printf("importAddress %s\n", address.ToString().c_str());
        pwalletMain->AddCScript(scriptPubKey);
        std::string strAccount;
        pwalletMain->SetAddressBookName(innerID, strAccount);
        updateAddressList();
        updateAddressBalance();
        updateAddressDetail();
    }
}

void MultiSigDialog::importDraft()
{
    QString filename = GUIUtil::getLoadFileName(
            this,
            tr("Load Fusioncoin Transaction"), QString(),
            tr("Fusioncoin transaction file (*.ftx)"));

    if (filename.isNull()) return;
    
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString hex;
    QTextStream hexin(&file);
    hexin >> hex;

    std::vector<unsigned char> txData(ParseHex(hex.toStdString()));
    CTransaction tx;
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> tx;
    }
    catch (std::exception &e) {
        return;
    }

    clear();
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];
        std::vector<CTxDestination> addresses;
        txnouttype whichType;
        int nRequired;
        if (!ExtractDestinations(txout.scriptPubKey, whichType, addresses, nRequired))
            continue;
        
        SendCoinsRecipient rv;
        rv.address = QString::fromStdString(CBitcoinAddress(addresses[0]).ToString());
        rv.amount = txout.nValue;
        pasteEntry(rv);
    }
    
    *rawTx = tx;
    isTxCreate = true;
    editEnable(false);
}

void MultiSigDialog::exportDraft()
{
    if ( !isTxCreate )
        createRawTransaction();
    
    if ( isTxCreate )
    {
        QString filename = GUIUtil::getSaveFileName(
                this,
                tr("Save Fusioncoin Transaction"), QString(),
                tr("Fusioncoin transaction file (*.ftx)"));

        if (filename.isNull()) return;

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << (*rawTx);
        QString hex = QString::fromStdString(HexStr(ss.begin(), ss.end()));

        if(!writeString(filename, hex))
        {
            QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                                  QMessageBox::Abort, QMessageBox::Abort);
        }
    }
}

void MultiSigDialog::sendRawTransaction()
{
    uint256 hashTx = rawTx->GetHash();

    bool fHave = false;
    CCoinsViewCache &view = *pcoinsTip;
    CCoins existingCoins;
    {
        fHave = view.GetCoins(hashTx, existingCoins);
        if (!fHave) {
            // push to local node
            CValidationState state;
            if (!rawTx->AcceptToMemoryPool(state, true, false))
                return;
                //throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX rejected");
        }
    }
    if (fHave) {
        return;
        //if (existingCoins.nHeight < 1000000000)
            //throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "transaction already in block chain");
        // Not in block, but already in the memory pool; will drop
        // through to re-relay it.
    } else {
        SyncWithWallets(hashTx, *rawTx, NULL, true);
    }
    RelayTransaction(*rawTx, hashTx);
    clear();
}

void MultiSigDialog::signAddress0()
{
    signTransaction();
}

void MultiSigDialog::signAddress1()
{
    signTransaction();
}

void MultiSigDialog::signAddress2()
{
    signTransaction();
}

void MultiSigDialog::signTransaction()
{
    if ( !isTxCreate )
        createRawTransaction();

    // if created, sign
    if ( isTxCreate )
    {
        isComplete = true;
        // Fetch previous transactions (inputs):
        CCoinsView viewDummy;
        CCoinsViewCache view(viewDummy);
        {
            LOCK(mempool.cs);
            CCoinsViewCache &viewChain = *pcoinsTip;
            CCoinsViewMemPool viewMempool(viewChain, mempool);
            view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

            BOOST_FOREACH(const CTxIn& txin, rawTx->vin) {
                const uint256& prevHash = txin.prevout.hash;
                CCoins coins;
                view.GetCoins(prevHash, coins); // this is certainly allowed to fail
            }

            view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
        }

        CTransaction txv(*rawTx);
        // Sign what we can:
        for (unsigned int i = 0; i < rawTx->vin.size(); i++)
        {
            CTxIn& txin = rawTx->vin[i];
            CCoins coins;
            if (!view.GetCoins(txin.prevout.hash, coins) || !coins.IsAvailable(txin.prevout.n))
            {
                isComplete = false;
                continue;
            }
            const CScript& prevPubKey = coins.vout[txin.prevout.n].scriptPubKey;

            txin.scriptSig.clear();
            SignSignature(*pwalletMain, prevPubKey, *rawTx, i, SIGHASH_ALL);
            txin.scriptSig = CombineSignatures(prevPubKey, *rawTx, i, txin.scriptSig, txv.vin[i].scriptSig);

            if (!VerifyScript(txin.scriptSig, prevPubKey, *rawTx, i, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, 0))
                isComplete = false;
        }
    
        editEnable(false);
        ui->sendButton->setEnabled(false);
    }

    if ( isComplete )
    {
        ui->sendButton->setEnabled(true);
    }
}

void MultiSigDialog::on_sendButton_clicked()
{
    // if complete, send
    if ( isTxCreate && isComplete )
    {
        sendRawTransaction();
        return;
    }
}

void MultiSigDialog::editEnable(bool enable)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            entry->setFieldEnable(enable);
        }
        ui->comboBoxAddrList->setEnabled(enable);
    }
}

void MultiSigDialog::clear()
{
    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    addEntry();

    updateRemoveEnabled();

    ui->sendButton->setDefault(true);
    isTxCreate = false;
    isComplete = false;
    ui->sendButton->setEnabled(false);
    ui->comboBoxAddrList->setEnabled(true);
}

void MultiSigDialog::reject()
{
    clear();
}

void MultiSigDialog::accept()
{
    clear();
}

SendCoinsEntry *MultiSigDialog::addEntry()
{
    SendCoinsEntry *entry = new SendCoinsEntry(this);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));

    updateRemoveEnabled();

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());
    return entry;
}

void MultiSigDialog::updateRemoveEnabled()
{
    // Remove buttons are enabled as soon as there is more than one send-entry
    bool enabled = (ui->entries->count() > 1);
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            entry->setRemoveEnabled(enabled);
        }
    }
    setupTabChain(0);
}

void MultiSigDialog::removeEntry(SendCoinsEntry* entry)
{
    entry->deleteLater();
    updateRemoveEnabled();
}

QWidget *MultiSigDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->addButton);
    QWidget::setTabOrder(ui->addButton, ui->sendButton);
    return ui->sendButton;
}

void MultiSigDialog::setAddress(const QString &address)
{
    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setAddress(address);
}

void MultiSigDialog::pasteEntry(const SendCoinsRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setValue(rv);
}

bool MultiSigDialog::handleURI(const QString &uri)
{
    SendCoinsRecipient rv;
    // URI has to be valid
    if (GUIUtil::parseBitcoinURI(uri, &rv))
    {
        CBitcoinAddress address(rv.address.toStdString());
        if (!address.IsValid())
            return false;
        pasteEntry(rv);
        return true;
    }

    return false;
}

void MultiSigDialog::setSharedBalance(qint64 balance, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    //printf("setSharedBalance %s\n", BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance).toStdString().c_str());
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);
    if(!model || !model->getOptionsModel())
        return;

    int unit = model->getOptionsModel()->getDisplayUnit();
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance));
    updateAddressList();
    updateAddressBalance();
}

void MultiSigDialog::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update labelBalance with the current balance and the current unit
        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), model->getSharedBalance()));
    }
}

void MultiSigDialog::updateAddressList()
{
    ui->comboBoxAddrList->clear();
    {
        LOCK(wallet->cs_wallet);
        BOOST_FOREACH(const PAIRTYPE(CTxDestination, std::string)& item, wallet->mapAddressBook)
        {
            const CBitcoinAddress& address = item.first;
            const std::string& strName = item.second;
            //bool fMine = IsMine(*wallet, address.Get());
            bool fMyShare = IsMyShare(*wallet, address.Get());
            if ( fMyShare )
            {
                ui->comboBoxAddrList->addItem(QString::fromStdString(address.ToString()), QVariant(""));
            }
        }
    }

    int n = ui->comboBoxAddrList->count();
    QString num_str = QString::number(n);
    ui->labelAddressesNum->setText(num_str);
}

void MultiSigDialog::updateAddressBalance()
{
    if ( currentIndex < 0 )
        return;

    QString s = ui->comboBoxAddrList->currentText();
    CBitcoinAddress address(s.toStdString());

    coinControl->SetNull();
    coinControl->destChange = address.Get();
    
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address.Get());

    int64 nAmount = 0;
    {
        LOCK(pwalletMain->cs_wallet);
        for (std::map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
        {
            const CWalletTx& wtx = (*it).second;
            //if (wtx.IsCoinBase() || !wtx.IsFinal())
                //continue;

            //printf("updateAddressBalance wtx %s \n", wtx.GetHash().ToString().c_str());
            for (unsigned int i = 0; i < wtx.vout.size(); i++)
            {
                const CTxOut* txout = &wtx.vout[i];
                if ( scriptPubKey == txout->scriptPubKey && !wtx.IsSpent(i)
                    ){
                    //printf("updateAddressBalance GetDepthInMainChain %d\n", wtx.GetDepthInMainChain());
                    //printf("updateAddressBalance nValue %s\n", BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), txout.nValue).toStdString().c_str());
                    //if (wtx.GetDepthInMainChain() > 0)
                    //if (wtx.IsConfirmed())
                    nAmount += txout->nValue;
                    COutPoint outpt(wtx.GetHash(), i);
                    coinControl->Select(outpt);
                }
            }
        }
    }

    //printf("updateAddressBalance nAmount %s\n", BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), nAmount).toStdString().c_str());
    if(model && model->getOptionsModel())
    {
        ui->labelAvailableCoins->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), nAmount));
    }
}

void MultiSigDialog::updateAddressDetail()
{
    QString s = ui->comboBoxAddrList->currentText();
    CBitcoinAddress address(s.toStdString());
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address.Get());

    CScript subscript;
    CScriptID scriptID;
    address.GetScriptID(scriptID);
    pwalletMain->GetCScript(scriptID, subscript);
    std::vector<CTxDestination> addresses;
    txnouttype whichType;
    int nRequired;
    ExtractDestinations(subscript, whichType, addresses, nRequired);

    ui->labelRequireAddr2->setVisible(false);
    ui->btnSign2->setVisible(false);
    int i = 0;
    BOOST_FOREACH(const CTxDestination& addr, addresses){
        if ( i == 0 )
        {
            ui->labelRequireAddr0->setText(QString::fromStdString(CBitcoinAddress(addr).ToString()));
            if ( IsMine(*pwalletMain, addr) )
                ui->btnSign0->setVisible(true);
            else
                ui->btnSign0->setVisible(false);
        }
        else if ( i == 1 )
        {
            ui->labelRequireAddr1->setText(QString::fromStdString(CBitcoinAddress(addr).ToString()));
            if ( IsMine(*pwalletMain, addr) )
                ui->btnSign1->setVisible(true);
            else
                ui->btnSign1->setVisible(false);
        }
        else if ( i == 2 )
        {
            ui->labelRequireAddr2->setVisible(true);
            ui->labelRequireAddr2->setText(QString::fromStdString(CBitcoinAddress(addr).ToString()));
            if ( IsMine(*pwalletMain, addr) )
                ui->btnSign2->setVisible(true);
            else
                ui->btnSign2->setVisible(false);
        }
        else
            break;
        
        i += 1;
    }
    
    ui->labelRequire->setText(QString("Require ") + QString::number(nRequired) + QString(" of ") + QString::number(i) + QString(" signatures ") );
}

void MultiSigDialog::handleAddrSelectionChanged(int idx)
{
    //QVariant v = ui->comboBoxAddrList->itemData(idx);

    if ( currentIndex != idx )
    {
        currentIndex = idx;
        updateAddressBalance();
        updateAddressDetail();
    }
}

