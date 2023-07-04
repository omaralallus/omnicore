#ifndef BITCOIN_OMNICORE_DBBASE_H
#define BITCOIN_OMNICORE_DBBASE_H

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <leveldb/db.h>

#include <clientversion.h>
#include <fs.h>
#include <streams.h>

#include <assert.h>
#include <functional>
#include <memory>
#include <stddef.h>
#include <string_view>
#include <type_traits>
#include <variant>

class CStringWriter
{
    const int nType;
    const int nVersion;
    std::string& data;
    uint32_t limit;

public:
    CStringWriter(int nType_, int nVersion_, std::string& s, uint32_t limit = UINT32_MAX) : nType(nType_), nVersion(nVersion_), data(s), limit(limit) {}

    template<typename T>
    CStringWriter& operator<<(const T& obj)
    {
        ::Serialize(*this, obj);
        return *this;
    }

    void write(Span<const std::byte> src)
    {
        if (limit) {
            data.append((const char*)src.data(), src.size());
            --limit;
        }
    }

    int GetVersion() const { return nVersion; }
    int GetType() const { return nType; }
    size_t size() const { return data.size(); }
};

template<typename T>
bool StringToValue(const std::string_view& s, T& value)
{
    try {
        CDataStream ssValue(s.data(), s.data() + s.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> value;
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

template<typename T>
std::string ValueToString(const T& value)
{
    std::string s;
    CStringWriter writer(SER_DISK, CLIENT_VERSION, s);
    writer << value;
    return s;
}

inline std::string ValueToString(const std::string& value)
{
    return value;
}

template<typename T>
std::string KeyToString(const T& key)
{
    std::string s;
    static_assert(sizeof(T::prefix) == 1, "Prefix needs to be 1 byte");
    CStringWriter writer(SER_DISK, CLIENT_VERSION, s);
    writer << T::prefix << key;
    return s;
}

inline std::string_view KeyToString(const std::string& key)
{
    return key;
}

inline std::string_view KeyToString(const std::string_view& key)
{
    return key;
}

template<size_t N>
std::string_view KeyToString(const char (&key)[N])
{
    return {key, N > 0 && key[N - 1] == '\0' ? N - 1 : N};
}

template<typename T>
bool StringToKey(const std::string_view& s, T& key)
{
    auto prefix = T::prefix;
    static_assert(sizeof(T::prefix) == 1, "Prefix needs to be 1 byte");
    try {
        Span v((const uint8_t *)s.data(), s.size());
        SpanReader reader(SER_DISK, CLIENT_VERSION, v);
        reader >> prefix >> key;
    } catch (const std::exception&) {
        return false;
    }
    return prefix == T::prefix;
}

/** Base class for LevelDB based storage.
 */
class CDBBase
{
    //! Options used when iterating over values of the database
    leveldb::ReadOptions iteroptions;

    //! Database options used
    leveldb::Options options;

    //! Options used when reading from the database
    leveldb::ReadOptions readoptions;

    //! Options used when writing to the database
    leveldb::WriteOptions writeoptions;

    //! Options used when sync writing to the database
    leveldb::WriteOptions syncoptions;

    //! The database itself
    std::unique_ptr<leveldb::DB> pdb;

protected:
    //! Number of entries read
    unsigned int nRead = 0;

    //! Number of entries written
    unsigned int nWritten = 0;

    CDBBase()
    {
        options.paranoid_checks = true;
        options.create_if_missing = true;
        options.compression = leveldb::kNoCompression;
        options.max_open_files = 64;
        readoptions.verify_checksums = true;
        iteroptions.verify_checksums = true;
        iteroptions.fill_cache = false;
        syncoptions.sync = true;
    }

    virtual ~CDBBase()
    {
        Close();
    }

    /**
     * Creates and returns a new LevelDB iterator.
     *
     * It is expected that the database is not closed. The iterator is owned by the
     * caller, and the object has to be deleted explicitly.
     *
     * @return A new LevelDB iterator
     */
    std::unique_ptr<leveldb::Iterator> NewIterator() const
    {
        assert(pdb);
        return std::unique_ptr<leveldb::Iterator>{pdb->NewIterator(iteroptions)};
    }

    template<typename K, typename V>
    bool Write(const K& key, const V& value)
    {
        return Write(KeyToString(key), ValueToString(value));
    }

    template<typename K, typename V>
    bool Read(const K& key, V&& value) const
    {
        if constexpr (std::is_same_v<std::string, std::decay_t<V>>) {
            return Read(KeyToString(key), value);
        } else {
            std::string strValue;
            return Read(KeyToString(key), strValue)
                && StringToValue(strValue, value);
        }
    }

    template<typename K>
    bool Delete(const K& key)
    {
        return Delete(KeyToString(key));
    }

    bool Write(const std::string_view& key, const std::string& value)
    {
        assert(pdb);
        return pdb->Put(writeoptions, {key.data(), key.size()}, value).ok();
    }

    bool Read(const std::string_view& key, std::string& value) const
    {
        assert(pdb);
        return pdb->Get(readoptions, {key.data(), key.size()}, &value).ok();
    }

    bool Delete(const std::string_view& key)
    {
        assert(pdb);
        return pdb->Delete(writeoptions, {key.data(), key.size()}).ok();
    }

    bool WriteBatch(leveldb::WriteBatch& batch)
    {
        assert(pdb);
        return pdb->Write(syncoptions, &batch).ok();
    }

    /**
     * Opens or creates a LevelDB based database.
     *
     * If the database is wiped before opening, it's content is destroyed, including
     * all log files and meta data.
     *
     * @param path   The path of the database to open
     * @param fWipe  Whether to wipe the database before opening
     * @return A Status object, indicating success or failure
     */
    leveldb::Status Open(const fs::path& path, bool fWipe = false);

    /**
     * Deinitializes and closes the database.
     */
    void Close();

public:
    /**
     * Deletes all entries of the database, and resets the counters.
     */
    void Clear();
};

class CPartialKey {
    std::string data;
public:
    CPartialKey() = default;
    explicit CPartialKey(uint8_t prefix) : data(1, prefix) {}
    explicit CPartialKey(std::string s) : data(std::move(s)) {}

    operator leveldb::Slice() const {
        return data;
    }
};

template<typename T, typename... Args>
CPartialKey PartialKey(Args&&... args)
{
    std::string s;
    CStringWriter writer(SER_DISK, CLIENT_VERSION, s, sizeof...(Args) + 1);
    writer << T::prefix << T{std::forward<Args>(args)...};
    return CPartialKey{std::move(s)};
}

class CDBaseIterator
{
private:
    CPartialKey partialKey;
    std::unique_ptr<leveldb::Iterator> it;

public:
    explicit CDBaseIterator(std::unique_ptr<leveldb::Iterator> i, const std::string_view& first = {}) : it(std::move(i))
    {
        assert(it);
        first.empty() ? it->SeekToFirst() : Seek(first);
    }

    template<typename T>
    explicit CDBaseIterator(std::unique_ptr<leveldb::Iterator> i, const T& key) : partialKey{T::prefix}, it(std::move(i))
    {
        assert(it);
        Seek(key);
    }

    explicit CDBaseIterator(std::unique_ptr<leveldb::Iterator> i, CPartialKey key) : partialKey(std::move(key)), it(std::move(i))
    {
        assert(it);
        it->Seek(partialKey);
    }

    CDBaseIterator(CDBaseIterator&&) = default;
    CDBaseIterator& operator=(CDBaseIterator&&) = default;

    void Seek(const std::string_view& key)
    {
        partialKey = CPartialKey{};
        it->Seek({key.data(), key.size()});
    }

    void Seek(CPartialKey key)
    {
        partialKey = std::move(key);
        it->Seek(partialKey);
    }

    template<typename T>
    void Seek(const T& key)
    {
        partialKey = CPartialKey{T::prefix};
        it->Seek(KeyToString(key));
    }

    CDBaseIterator& operator++()
    {
        assert(Valid());
        it->Next();
        return *this;
    }

    CDBaseIterator& operator--()
    {
        assert(Valid());
        it->Prev();
        return *this;
    }

    operator bool() const
    {
        return Valid();
    }

    bool Valid() const
    {
        return it->Valid() && it->key().starts_with(partialKey);
    }

    leveldb::Slice Key() const
    {
        assert(Valid());
        return it->key();
    }

    template<typename T>
    T Key() const
    {
        assert(Valid());
        T key;
        auto slKey = it->key();
        return StringToKey({slKey.data(), slKey.size()}, key) ? key : T{};
    }

    leveldb::Slice Value() const
    {
        assert(Valid());
        return it->value();
    }

    template<typename T>
    bool Value(T&& value) const
    {
        assert(Valid());
        auto slValue = it->value();
        return StringToValue({slValue.data(), slValue.size()}, value);
    }

    template<typename T>
    T Value() const
    {
        T value;
        return Value(value) ? value : T{};
    }
};

template<typename T>
class CRef {
    using ref = std::reference_wrapper<T>;
    using cref = std::reference_wrapper<const T>;
    std::variant<ref, cref> value;

public:
    CRef(T& ref) : value(std::ref(ref)) {}
    CRef(const T& ref) : value(ref) {}
    CRef(T&&) = delete;
    CRef(const T&&) = delete;
    CRef(const CRef&) = delete;
    CRef(CRef&&) = default;

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        std::visit([&](auto& v) { ::Serialize(s, v.get()); }, value);
    }
    template<typename Stream>
    void Unserialize(Stream& s)
    {
        if (auto v = std::get_if<ref>(&value)) {
            ::Unserialize(s, v->get());
        }
    }
};

#endif // BITCOIN_OMNICORE_DBBASE_H
