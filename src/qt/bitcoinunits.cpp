// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/bitcoinunits.h>

#include <omnicore/omnicore.h>
#include <omnicore/sp.h>

#include <consensus/amount.h>

#include <QStringList>

#include <cassert>

using namespace mastercore;

static constexpr auto MAX_DIGITS_BTC = 16;

BitcoinUnits::BitcoinUnits(QObject *parent):
        QAbstractListModel(parent),
        unitlist(availableUnits())
{
}

QList<BitcoinUnit> BitcoinUnits::availableUnits()
{
    static QList<Unit> unitlist{
        BTC, mBTC, uBTC, SAT
    };
    if (fOmniSafeAddresses) {
        LOCK(cs_tally);
        static uint32_t lastSPID = 1;
        auto nextSPID = pDbSpInfo->peekNextSPID(1);
        if (lastSPID != nextSPID) {
            for (int property = lastSPID; property < nextSPID; property++) {
                if (!isPropertyNonFungible(property)) {
                    auto newProperty = property + SAT;
                    if (newProperty > 128) break;
                    unitlist.append(newProperty);
                }
            }
            lastSPID = nextSPID;
        }
    }
    return unitlist;
}

QString BitcoinUnits::longName(Unit unit)
{
    switch (unit) {
    case BTC: return QString("BTC");
    case mBTC: return QString("mBTC");
    case uBTC: return QString::fromUtf8("µBTC (bits)");
    case SAT: return QString("Satoshi (sat)");
    }
    if (fOmniSafeAddresses) {
        auto property = availableUnits().at(unit) - SAT;
        return QString::fromStdString(getPropertyName(property));
    }
    return QString{};
}

QString BitcoinUnits::shortName(Unit unit)
{
    switch (unit) {
    case BTC: return longName(unit);
    case mBTC: return longName(unit);
    case uBTC: return QString("bits");
    case SAT: return QString("sat");
    }
    if (fOmniSafeAddresses) {
        auto property = availableUnits().at(unit) - SAT;
        return QString::fromStdString(getTokenLabel(property));
    }
    return QString{};
}

QString BitcoinUnits::description(Unit unit)
{
    switch (unit) {
    case BTC: return QString("Bitcoins");
    case mBTC: return QString("Milli-Bitcoins (1 / 1" THIN_SP_UTF8 "000)");
    case uBTC: return QString("Micro-Bitcoins (bits) (1 / 1" THIN_SP_UTF8 "000" THIN_SP_UTF8 "000)");
    case SAT: return QString("Satoshi (sat) (1 / 100" THIN_SP_UTF8 "000" THIN_SP_UTF8 "000)");
    }
    return QString("Omni layer token");
}

qint64 BitcoinUnits::factor(Unit unit)
{
    switch (unit) {
    case BTC: return 100'000'000;
    case mBTC: return 100'000;
    case uBTC: return 100;
    case SAT: return 1;
    }
    if (fOmniSafeAddresses) {
        auto property = availableUnits().at(unit) - SAT;
        return isPropertyDivisible(property) ? 100'000'000 : 1;
    }
    return 1;
}

int BitcoinUnits::decimals(Unit unit)
{
    switch (unit) {
    case BTC: return 8;
    case mBTC: return 5;
    case uBTC: return 2;
    case SAT: return 0;
    }
    if (fOmniSafeAddresses) {
        auto property = availableUnits().at(unit) - SAT;
        return isPropertyDivisible(property) ? 8 : 0;
    }
    return 0;
}

QString BitcoinUnits::format(Unit unit, const CAmount& nIn, bool fPlus, SeparatorStyle separators, bool justify)
{
    // Note: not using straight sprintf here because we do NOT want
    // localized number formatting.
    qint64 n = (qint64)nIn;
    qint64 coin = factor(unit);
    int num_decimals = decimals(unit);
    qint64 n_abs = (n > 0 ? n : -n);
    qint64 quotient = n_abs / coin;
    QString quotient_str = QString::number(quotient);
    if (justify) {
        quotient_str = quotient_str.rightJustified(MAX_DIGITS_BTC - num_decimals, ' ');
    }

    // Use SI-style thin space separators as these are locale independent and can't be
    // confused with the decimal marker.
    QChar thin_sp(THIN_SP_CP);
    int q_size = quotient_str.size();
    if (separators == SeparatorStyle::ALWAYS || (separators == SeparatorStyle::STANDARD && q_size > 4))
        for (int i = 3; i < q_size; i += 3)
            quotient_str.insert(q_size - i, thin_sp);

    if (n < 0)
        quotient_str.insert(0, '-');
    else if (fPlus && n > 0)
        quotient_str.insert(0, '+');

    if (num_decimals > 0) {
        qint64 remainder = n_abs % coin;
        QString remainder_str = QString::number(remainder).rightJustified(num_decimals, '0');
        return quotient_str + QString(".") + remainder_str;
    } else {
        return quotient_str;
    }
}


// NOTE: Using formatWithUnit in an HTML context risks wrapping
// quantities at the thousands separator. More subtly, it also results
// in a standard space rather than a thin space, due to a bug in Qt's
// XML whitespace canonicalisation
//
// Please take care to use formatHtmlWithUnit instead, when
// appropriate.

QString BitcoinUnits::formatWithUnit(Unit unit, const CAmount& amount, bool plussign, SeparatorStyle separators)
{
    return format(unit, amount, plussign, separators) + QString(" ") + shortName(unit);
}

QString BitcoinUnits::formatHtmlWithUnit(Unit unit, const CAmount& amount, bool plussign, SeparatorStyle separators)
{
    QString str(formatWithUnit(unit, amount, plussign, separators));
    str.replace(QChar(THIN_SP_CP), QString(THIN_SP_HTML));
    return QString("<span style='white-space: nowrap;'>%1</span>").arg(str);
}

QString BitcoinUnits::formatWithPrivacy(Unit unit, const CAmount& amount, SeparatorStyle separators, bool privacy)
{
    assert(amount >= 0);
    QString value;
    if (privacy) {
        value = format(unit, 0, false, separators, true).replace('0', '#');
    } else {
        value = format(unit, amount, false, separators, true);
    }
    return value + QString(" ") + shortName(unit);
}

bool BitcoinUnits::parse(Unit unit, const QString& value, CAmount* val_out)
{
    if (value.isEmpty()) {
        return false; // Refuse to parse invalid unit or empty string
    }
    int num_decimals = decimals(unit);

    // Ignore spaces and thin spaces when parsing
    QStringList parts = removeSpaces(value).split(".");

    if(parts.size() > 2)
    {
        return false; // More than one dot
    }
    QString whole = parts[0];
    QString decimals;

    if(parts.size() > 1)
    {
        decimals = parts[1];
    }
    if(decimals.size() > num_decimals)
    {
        return false; // Exceeds max precision
    }
    bool ok = false;
    QString str = whole + decimals.leftJustified(num_decimals, '0');

    if(str.size() > 18)
    {
        return false; // Longer numbers will exceed 63 bits
    }
    CAmount retvalue(str.toLongLong(&ok));
    if(val_out)
    {
        *val_out = retvalue;
    }
    return ok;
}

QString BitcoinUnits::getAmountColumnTitle(Unit unit)
{
    return QObject::tr("Amount") + " (" + shortName(unit) + ")";
}

bool BitcoinUnits::canFetchMore(const QModelIndex&) const
{
    return availableUnits().size() != unitlist.size();
}

void BitcoinUnits::fetchMore(const QModelIndex& parent)
{
    if (canFetchMore(parent)) {
        beginResetModel();
        unitlist = availableUnits();
        endResetModel();
    }
}

int BitcoinUnits::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return unitlist.size();
}

QVariant BitcoinUnits::data(const QModelIndex &index, int role) const
{
    int row = index.row();
    if(row >= 0 && row < unitlist.size())
    {
        Unit unit = unitlist.at(row);
        switch(role)
        {
        case Qt::EditRole:
        case Qt::DisplayRole:
            return QVariant(longName(unit));
        case Qt::ToolTipRole:
            return QVariant(description(unit));
        case UnitRole:
            return QVariant::fromValue(unit);
        }
    }
    return QVariant();
}

CAmount BitcoinUnits::maxMoney()
{
    return MAX_MONEY;
}
