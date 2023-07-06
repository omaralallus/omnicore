// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/walletview.h>

#include <qt/addressbookpage.h>
#include <qt/askpassphrasedialog.h>
#include <qt/balancesdialog.h>
#include <qt/clientmodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/overviewpage.h>
#include <qt/platformstyle.h>
#include <qt/receivecoinsdialog.h>
#include <qt/sendcoinsdialog.h>
#include <qt/sendmpdialog.h>
#include <qt/lookupspdialog.h>
#include <qt/lookuptxdialog.h>
#include <qt/lookupaddressdialog.h>
#include <qt/metadexcanceldialog.h>
#include <qt/metadexdialog.h>
#include <qt/signverifymessagedialog.h>
#include <qt/tradehistorydialog.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/txhistorydialog.h>
#include <qt/walletmodel.h>

#include <interfaces/node.h>
#include <node/interface_ui.h>
#include <util/strencodings.h>

#include <QAction>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

WalletView::WalletView(WalletModel* wallet_model, const PlatformStyle* _platformStyle, QWidget* parent)
    : QStackedWidget(parent),
      clientModel(nullptr),
      walletModel(wallet_model),
      platformStyle(_platformStyle)
{
    assert(walletModel);

    // Create tabs
    overviewPage = new OverviewPage(platformStyle);
    overviewPage->setWalletModel(walletModel);

    // Transactions page, Omni transactions in first tab, BTC only transactions in second tab
    transactionsPage = new QWidget(this);
    bitcoinTXTab = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    QHBoxLayout *hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(platformStyle, this);
    transactionView->setModel(walletModel);

    vbox->addWidget(transactionView);
    QPushButton *exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons()) {
        exportButton->setIcon(platformStyle->SingleColorIcon(":/icons/export"));
    }
    hbox_buttons->addStretch();
    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);
    bitcoinTXTab->setLayout(vbox);
    mpTXTab = new TXHistoryDialog(platformStyle);
    mpTXTab->setWalletModel(walletModel);
    transactionsPage = new QWidget(this);
    QVBoxLayout *txvbox = new QVBoxLayout();
    txTabHolder = new QTabWidget();
    txTabHolder->addTab(mpTXTab,tr("Omni Layer"));
    txTabHolder->addTab(bitcoinTXTab,tr("Bitcoin"));
    txvbox->addWidget(txTabHolder);
    transactionsPage->setLayout(txvbox);

    balancesPage = new BalancesDialog();
    balancesPage->setWalletModel(walletModel);
    receiveCoinsPage = new ReceiveCoinsDialog(platformStyle);
    receiveCoinsPage->setModel(walletModel);

    usedSendingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    usedSendingAddressesPage->setModel(walletModel->getAddressTableModel());

    usedReceivingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);
    usedReceivingAddressesPage->setModel(walletModel->getAddressTableModel());

    // sending page
    sendCoinsPage = new QWidget(this);
    QVBoxLayout *svbox = new QVBoxLayout();
    sendCoinsTab = new SendCoinsDialog(platformStyle);
    sendCoinsTab->setModel(walletModel);
    sendMPTab = new SendMPDialog(platformStyle);
    sendMPTab->setWalletModel(walletModel);
    sendTabHolder = new QTabWidget();
    sendTabHolder->addTab(sendMPTab, tr("Omni Layer"));
    sendTabHolder->addTab(sendCoinsTab, tr("Bitcoin"));
    svbox->addWidget(sendTabHolder);
    sendCoinsPage->setLayout(svbox);

    exchangePage = new QWidget(this);
    QVBoxLayout *exvbox = new QVBoxLayout();
    metaDExTab = new MetaDExDialog();
    metaDExTab->setWalletModel(walletModel);
    cancelTab = new MetaDExCancelDialog();
    cancelTab->setWalletModel(walletModel);
    QTabWidget *exTabHolder = new QTabWidget();
    tradeHistoryTab = new TradeHistoryDialog(platformStyle);
    tradeHistoryTab->setWalletModel(walletModel);
    // exTabHolder->addTab(new QWidget(),tr("Trade Bitcoin/Mastercoin")); not yet implemented
    exTabHolder->addTab(metaDExTab,tr("Trade Omni Layer Properties"));
    exTabHolder->addTab(tradeHistoryTab,tr("Trade History"));
    exTabHolder->addTab(cancelTab,tr("Cancel Orders"));
    exvbox->addWidget(exTabHolder);
    exchangePage->setLayout(exvbox);

    // toolbox page
    toolboxPage = new QWidget(this);
    QVBoxLayout *tvbox = new QVBoxLayout();
    addressLookupTab = new LookupAddressDialog();
    addressLookupTab->setWalletModel(walletModel);
    spLookupTab = new LookupSPDialog();
    txLookupTab = new LookupTXDialog();
    txLookupTab->setWalletModel(walletModel);
    QTabWidget *tTabHolder = new QTabWidget();
    tTabHolder->addTab(addressLookupTab,tr("Lookup Address"));
    tTabHolder->addTab(spLookupTab,tr("Lookup Property"));
    tTabHolder->addTab(txLookupTab,tr("Lookup Transaction"));
    tvbox->addWidget(tTabHolder);
    toolboxPage->setLayout(tvbox);

    addWidget(overviewPage);
    addWidget(balancesPage);
    addWidget(transactionsPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);
    addWidget(exchangePage);
    addWidget(toolboxPage);

    connect(overviewPage, &OverviewPage::transactionClicked, this, &WalletView::transactionClicked);
    // Clicking on a transaction on the overview pre-selects the transaction on the transaction history page
    connect(overviewPage, &OverviewPage::transactionClicked, transactionView, qOverload<const QModelIndex&>(&TransactionView::focusTransaction));

    connect(overviewPage, &OverviewPage::outOfSyncWarningClicked, this, &WalletView::outOfSyncWarningClicked);

    connect(sendCoinsTab, &SendCoinsDialog::coinsSent, this, &WalletView::coinsSent);
    // Highlight transaction after send
    connect(sendCoinsTab, &SendCoinsDialog::coinsSent, transactionView, qOverload<const uint256&>(&TransactionView::focusTransaction));

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, &QPushButton::clicked, transactionView, &TransactionView::exportClicked);

    // Pass through messages from sendCoinsTab
    connect(sendCoinsTab, &SendCoinsDialog::message, this, &WalletView::message);
    // Pass through messages from sendCoinsTab
    connect(sendMPTab, &SendMPDialog::message, this, &WalletView::message);
    // Pass through messages from transactionView
    connect(transactionView, &TransactionView::message, this, &WalletView::message);

    connect(this, &WalletView::setPrivacy, overviewPage, &OverviewPage::setPrivacy);

    // Receive and pass through messages from wallet model
    connect(walletModel, &WalletModel::message, this, &WalletView::message);

    // Handle changes in encryption status
    connect(walletModel, &WalletModel::encryptionStatusChanged, this, &WalletView::encryptionStatusChanged);

    // Balloon pop-up for new transaction
    connect(walletModel->getTransactionTableModel(), &TransactionTableModel::rowsInserted, this, &WalletView::processNewTransaction);

    // Ask for passphrase if needed
    connect(walletModel, &WalletModel::requireUnlock, this, &WalletView::unlockWallet);

    // Show progress dialog
    connect(walletModel, &WalletModel::showProgress, this, &WalletView::showProgress);
}

WalletView::~WalletView() = default;

void WalletView::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    overviewPage->setClientModel(_clientModel);
    balancesPage->setClientModel(_clientModel);
    sendCoinsTab->setClientModel(_clientModel);
    walletModel->setClientModel(_clientModel);
    metaDExTab->setClientModel(_clientModel);
    sendMPTab->setClientModel(_clientModel);
    cancelTab->setClientModel(_clientModel);
    mpTXTab->setClientModel(_clientModel);
    tradeHistoryTab->setClientModel(_clientModel);
}

void WalletView::processNewTransaction(const QModelIndex& parent, int start, int /*end*/)
{
    // Prevent balloon-spam when initial block download is in progress
    if (!clientModel || clientModel->node().isInitialBlockDownload()) {
        return;
    }

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    if (!ttm || ttm->processingQueuedTransactions())
        return;

    QString date = ttm->index(start, TransactionTableModel::Date, parent).data().toString();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
    QString type = ttm->index(start, TransactionTableModel::Type, parent).data().toString();
    QModelIndex index = ttm->index(start, 0, parent);
    QString address = ttm->data(index, TransactionTableModel::AddressRole).toString();
    QString label = GUIUtil::HtmlEscape(ttm->data(index, TransactionTableModel::LabelRole).toString());

    Q_EMIT incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address, label, GUIUtil::HtmlEscape(walletModel->getWalletName()));
}

void WalletView::gotoOverviewPage()
{
    setCurrentWidget(overviewPage);
}

void WalletView::gotoBalancesPage()
{
    setCurrentWidget(balancesPage);
}

void WalletView::gotoHistoryPage()
{
    setCurrentWidget(transactionsPage);
}

void WalletView::gotoBitcoinHistoryTab()
{
    setCurrentWidget(transactionsPage);
    txTabHolder->setCurrentIndex(1);
}

void WalletView::gotoOmniHistoryTab()
{
    setCurrentWidget(transactionsPage);
    txTabHolder->setCurrentIndex(0);
}

void WalletView::gotoReceiveCoinsPage()
{
    setCurrentWidget(receiveCoinsPage);
}

void WalletView::gotoExchangePage()
{
    setCurrentWidget(exchangePage);
}

void WalletView::gotoToolboxPage()
{
    setCurrentWidget(toolboxPage);
}

void WalletView::gotoSendCoinsPage(QString addr)
{
    setCurrentWidget(sendCoinsPage);

    if (!addr.isEmpty())
        sendCoinsTab->setAddress(addr);
}

void WalletView::gotoSignMessageTab(QString addr)
{
    // calls show() in showTab_SM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_SM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void WalletView::gotoVerifyMessageTab(QString addr)
{
    // calls show() in showTab_VM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_VM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

bool WalletView::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    if (recipient.unit == BitcoinUnits::BTC) {
        sendTabHolder->setCurrentIndex(1);
        return sendCoinsTab->handlePaymentRequest(recipient);
    } else {
        sendTabHolder->setCurrentIndex(0);
        return sendMPTab->handlePaymentRequest(recipient);
    }
}

void WalletView::showOutOfSyncWarning(bool fShow)
{
    overviewPage->showOutOfSyncWarning(fShow);
}

void WalletView::encryptWallet()
{
    auto dlg = new AskPassphraseDialog(AskPassphraseDialog::Encrypt, this);
    dlg->setModel(walletModel);
    connect(dlg, &QDialog::finished, this, &WalletView::encryptionStatusChanged);
    GUIUtil::ShowModalDialogAsynchronously(dlg);
}

void WalletView::backupWallet()
{
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Backup Wallet"), QString(),
        //: Name of the wallet data file format.
        tr("Wallet Data") + QLatin1String(" (*.dat)"), nullptr);

    if (filename.isEmpty())
        return;

    if (!walletModel->wallet().backupWallet(filename.toLocal8Bit().data())) {
        Q_EMIT message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
        }
    else {
        Q_EMIT message(tr("Backup Successful"), tr("The wallet data was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase()
{
    auto dlg = new AskPassphraseDialog(AskPassphraseDialog::ChangePass, this);
    dlg->setModel(walletModel);
    GUIUtil::ShowModalDialogAsynchronously(dlg);
}

void WalletView::unlockWallet()
{
    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked) {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        // A modal dialog must be synchronous here as expected
        // in the WalletModel::requestUnlock() function.
        dlg.exec();
    }
}

void WalletView::usedSendingAddresses()
{
    GUIUtil::bringToFront(usedSendingAddressesPage);
}

void WalletView::usedReceivingAddresses()
{
    GUIUtil::bringToFront(usedReceivingAddressesPage);
}

void WalletView::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0) {
        progressDialog = new QProgressDialog(title, tr("Cancel"), 0, 100);
        GUIUtil::PolishProgressDialog(progressDialog);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    } else if (nProgress == 100) {
        if (progressDialog) {
            progressDialog->close();
            progressDialog->deleteLater();
            progressDialog = nullptr;
        }
    } else if (progressDialog) {
        if (progressDialog->wasCanceled()) {
            getWalletModel()->wallet().abortRescan();
        } else {
            progressDialog->setValue(nProgress);
        }
    }
}
