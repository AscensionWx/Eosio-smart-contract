// Copyright (c) Microsoft Corporation. All rights reserved.
// Copyright (c) 2020 Syed Jafri. All rights reserved.
// Licensed under the MIT License..

#pragma once

namespace bigint {
  using checksum256 = intx::uint256;
}

namespace eosio_evm
{

  static constexpr auto WORD_SIZE = 32u;

  /**
   * Conversions
   */
  static inline std::string bin2hex(const std::vector<uint8_t>& bin)
  {
    std::string res;
    const char hex[] = "0123456789abcdef";
    for(auto byte : bin) {
      res += hex[byte >> 4];
      res += hex[byte & 0xf];
    }

    return res;
  }

  template<unsigned N, typename T>
  static inline std::string bin2hex(const std::array<T, N>& bin)
  {
    std::string res;
    const char hex[] = "0123456789abcdef";
    for(auto byte : bin) {
      res += hex[byte >> 4];
      res += hex[byte & 0xf];
    }

    return res;
  }

  inline constexpr bool is_precompile(uint256_t address) {
    return address >= 1 && address <= 65535;
  }

  inline constexpr int64_t num_words(uint64_t size_in_bytes)
  {
    return (static_cast<int64_t>(size_in_bytes) + (WORD_SIZE - 1)) / WORD_SIZE;
  }

  template <typename T>
  static T shrink(uint256_t i)
  {
    return static_cast<T>(i & std::numeric_limits<T>::max());
  }

  template<typename T>
  static inline std::vector<T> pad(std::vector<T> vector, uint64_t padding, bool prepend){
    if(vector.size() >= padding){
        return vector;
    }
    vector.insert(prepend ? vector.begin() : vector.end(), (padding - vector.size()), 0);
    return vector;
  }

  inline std::array<uint8_t, 32u> toBin(const Address& address)
  {
    std::array<uint8_t, 32> address_bytes = {};
    intx::be::unsafe::store(address_bytes.data(), address);
    return address_bytes;
  }

  inline uint8_t toBin(char c) {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return 0;
  }

  inline std::array<uint8_t, 20u> toBin(const std::string& input) {
      std::array<uint8_t, 20> output = {};
      auto i = input.begin();
      uint8_t* out_pos = (uint8_t*)& output;
      uint8_t* out_end = out_pos + 20;
      while (i != input.end() && out_end != out_pos) {
          *out_pos = toBin((char)(*i)) << 4;
          ++i;
          if (i != input.end()) {
              *out_pos |= toBin((char)(*i));
              ++i;
          }
          ++out_pos;
      }
      return output;
  }

  inline const std::array<uint8_t, 32u> fromChecksum160(const eosio::checksum160 input)
  {
    std::array<uint8_t, 32U> output = {};
    auto input_bytes = input.extract_as_byte_array();
    std::copy(std::begin(input_bytes), std::end(input_bytes), std::begin(output) + 12);
    return output;
  }

  inline eosio::checksum160 toChecksum160(const std::array<uint8_t, 32u>& input)
  {
    std::array<uint8_t, 20> output = {};
    std::copy(std::begin(input) + 12, std::end(input), std::begin(output));
    return eosio::checksum160(output);
  }
  

  inline eosio::checksum160 toChecksum160(const std::string& input)
  {
    return eosio::checksum160( toBin(input) );
  }

  inline eosio::checksum256 toChecksum256(const Address& address)
  {
    return eosio::checksum256( toBin(address) );
  }

  static inline eosio::checksum256 pad160(const eosio::checksum160 input)
  {
    return eosio::checksum256( fromChecksum160(input) );
  }

  static inline Address checksum160ToAddress(const eosio::checksum160& input) {
    const std::array<uint8_t, 32u>& checksum = fromChecksum160(input);
    return intx::be::unsafe::load<uint256_t>(checksum.data());
  }
  static inline eosio::checksum160 addressToChecksum160(const Address& input) {
    return toChecksum160( toBin(input) );
  }

  // Do not use for addresses, only key for Account States
  static inline uint256_t checksum256ToValue(const eosio::checksum256& input) {
    std::array<uint8_t, 32U> output = {};
    auto input_bytes = input.extract_as_byte_array();
    std::copy(std::begin(input_bytes), std::end(input_bytes), std::begin(output));

    return intx::be::unsafe::load<uint256_t>(output.data());
  }

  /**
   *  Serialize a fixed size int
   *
   *  @param ds - The stream to write
   *  @param v - The value to serialize
   *  @tparam Stream - Type of datastream buffer
   *  @tparam T - Type of the object contained in the array
   *  @tparam N - Number of bits
   *  @return datastream<Stream>& - Reference to the datastream
   */
  template<typename Stream>
  eosio::datastream<Stream>& operator<< ( eosio::datastream<Stream>& ds, const bigint::checksum256& v ) {
      auto bytes = toBin(v);
      ds << bytes;
      return ds;
  }

  /**
   *  Deserialize a fixed size int
   *
   *  @brief Deserialize a fixed size std::array
   *  @param ds - The stream to read
   *  @param v - The destination for deserialized value
   *  @tparam Stream - Type of datastream buffer
   *  @tparam N - Number of bits
   *  @return datastream<Stream>& - Reference to the datastream
   */
  template<typename Stream>
  eosio::datastream<Stream>& operator>> ( eosio::datastream<Stream>& ds, bigint::checksum256& v ) {
      std::array<uint8_t, 32> tmp;
      ds >> tmp;
      v = intx::be::unsafe::load<uint256_t>(tmp.data());
      return ds;
  }

 
} // namespace eosio_evm

namespace rlp {
    // Forward declaration
    class RLPValue;
    extern const RLPValue& NullRLPValue;

    enum RLP_constants {
	    RLP_maxUintLen		= 8,
	    RLP_bufferLenStart	= 0x80,
	    RLP_listStart		= 0xc0,
    };

    class RLPValue {
    public:
        enum VType { VARR, VBUF, };

        std::vector<unsigned char> value;
        std::vector<RLPValue> values;
        VType typ = VBUF;

        RLPValue() {}
        ~RLPValue() {}

        std::string get_value() const { return std::string((const char *) value.data(), value.size()); }
        inline void clear() { typ = VBUF; value.clear(); values.clear(); }
        inline bool set_array() { clear(); typ = VARR; return true; }

        inline void prefix_multiple_length(size_t total_length, std::vector<uint8_t>& bs)
        {
            // "If the total payload of a list (i.e. the combined length of all its
            // items being RLP encoded) is 0-55 bytes long
            if (total_length <= 55) {
                bs.insert(bs.begin(), 0xc0 + total_length);
                return;
            }

            // "If the total payload of a list is more than 55 bytes long
            auto total_length_as_bytes = to_byte_string(total_length);
            const uint8_t length_of_total_length = total_length_as_bytes.size();

            total_length_as_bytes.insert(total_length_as_bytes.begin(), 0xf7 + length_of_total_length);
            bs.insert(bs.begin(), total_length_as_bytes.begin(), total_length_as_bytes.end());
        }

        inline std::vector<uint8_t> to_byte_string(const std::vector<uint8_t>& bs)
        {
            return bs;
        }

        inline std::vector<uint8_t> to_byte_string(const std::string& s)
        {
            return to_byte_string(std::vector<uint8_t>(s.begin(), s.end()));
        }

        template <size_t N>
        std::vector<uint8_t> to_byte_string(const std::array<uint8_t, N>& a)
        {
            return std::vector<uint8_t>(a.begin(), a.end());
        }

        template <typename T, size_t N>
        std::vector<uint8_t> to_byte_string(const std::array<T, N>& a)
        {
            std::vector<uint8_t> combined;

            for (const auto& e : a) {
                const auto next = encode_single(e);
                combined.insert(combined.end(), next.begin(), next.end());
            }

            prefix_multiple_length(combined.size(), combined);

            return combined;
        }

        template <typename T>
        std::vector<uint8_t> to_byte_string(const std::vector<T>& v)
        {
            std::vector<uint8_t> combined;

            for (const auto& e : v) {
                const auto next = encode_single(e);
                combined.insert(combined.end(), next.begin(), next.end());
            }

            prefix_multiple_length(combined.size(), combined);

            return combined;
        }

        inline std::vector<uint8_t> to_byte_string(uint64_t n)
        {
            // "positive RLP integers must be represented in big endian binary form
            // with no leading zeroes"
            bool started = false;

            std::vector<uint8_t> bs;
            for (size_t byte_index = 0; byte_index < sizeof(n); ++byte_index) {
                const uint64_t offset = (7 - byte_index) * 8;
                const uint64_t mask = (uint64_t)0xff << offset;

                const uint8_t current_byte = (mask & n) >> offset;

                if (started || current_byte != 0) {
                    bs.push_back(current_byte);
                    started = true;
                }
            }

            // NB: If n is 0, this returns an empty list - this is correct.
            return bs;
        }

        inline std::vector<uint8_t> to_byte_string(const uint256_t& n)
        {
            std::vector<uint8_t> bs;

            // Need to special-case 0 to return an empty list, not a list containing
            // a single 0 byte
            if (n != 0) {
            // Get big-endian form
                uint8_t arr[32] = {};
                intx::be::store(arr, n);

                // No leading zeroes - count signficant bytes
                const auto n_bytes = intx::count_significant_words<uint8_t>(n);
                bs.resize(n_bytes);

                std::memcpy(bs.data(), arr + 32 - n_bytes, n_bytes);
            }

            return bs;
        }

        template <typename T>
        inline void encode_single (T& item) {
            RLPValue rlp;
            auto bytes = to_byte_string(item);
            rlp.value.assign(bytes.begin(), bytes.end());
            values.push_back(rlp);
        }

        // Access by array
        const RLPValue& operator[](size_t index) const
        {
            if (typ != VARR || index >= values.size()) {
                return NullRLPValue;
            } else {
                return values.at(index);
            }
        }

        std::string write() const
        {
            std::string s;
            if (typ == VARR) {
                writeArray(s);
            } else if (typ == VBUF) {
                writeBuffer(s);
            }
            return s;
        }

        inline uint64_t toInteger(const unsigned char *raw, size_t len)
        {
            if (len == 0)
                return 0;
            else if (len == 1)
                return *raw;
            else
                return (raw[len - 1]) + (toInteger(raw, len - 1) * 256);
        }

        inline void writeBuffer(std::string& s) const
        {
            const unsigned char *p = value.size() ? &value[0] : nullptr;
            size_t sz = value.size();

            if ((sz == 1) && (p[0] < 0x80)) {
                s.append((const char *) p, 1);
            } else {
                s += encodeLength(sz, 0x80) + get_value();
            }
        }

        inline void writeArray(std::string& s) const
        {
            std::string tmp;
            for (const RLPValue& value: values) {
                tmp += value.write();
            }
            s += encodeLength(tmp.size(), 0xC0) + tmp;
        }

        inline std::string encodeBinary(uint64_t n) const
        {
            // do nothing; return empty string
            if (n == 0) return {};

            // Encode
            std::string rs;
            rs.assign(encodeBinary(n / 256));
            unsigned char ch = n % 256;
            rs.append((const char *) &ch, 1);
            return rs;
        }

        inline std::string encodeLength(size_t n, unsigned char offset) const
        {
            std::string rs;

            if (n < 56) {
                unsigned char ch = n + offset;
                rs.assign((const char *) &ch, 1);
            } else {
                // assert(n too big);
                std::string binlen = encodeBinary(n);

                unsigned char ch = binlen.size() + offset + 55;
                rs.assign((const char *) &ch, 1);
                rs.append(binlen);
            }

            return rs;
        }

        bool readArray(
            const unsigned char *raw,
            size_t len,
            size_t uintlen,
            size_t payloadlen,
            size_t& consumed,
            size_t& wanted
        )
        {
            const size_t prefixlen = 1;

            // validate list length, including possible addition overflows.
            size_t expected = prefixlen + uintlen + payloadlen;
            if ((expected > len) || (payloadlen > len)) {
                wanted = expected > payloadlen ? expected : payloadlen;
                return false;
            }

            // we are type=array
            set_array();

            size_t child_len = payloadlen;
            size_t child_wanted = 0;
            size_t total_consumed = 0;

            const unsigned char *list_ent = raw + prefixlen + uintlen;

            // recursively read until payloadlen bytes parsed, or error
            while (child_len > 0) {
                RLPValue childVal;
                size_t child_consumed = 0;

                if (!childVal.read(list_ent, child_len, child_consumed, child_wanted))
                    return false;

                total_consumed += child_consumed;
                list_ent       += child_consumed;
                child_len      -= child_consumed;

                values.push_back(childVal);
            }

            consumed = total_consumed;
            return true;
        }

        bool read(
            const unsigned char *raw,
            size_t len,
            size_t& consumed,
            size_t& wanted
        )
        {
            clear();
            consumed = 0;
            wanted = 0;

            std::vector<unsigned char> buf;
            const unsigned char* end = raw + len;

            const size_t prefixlen = 1;

            unsigned char ch = *raw;

            if (len < 1) {
                wanted = 1;
                goto out_fail;
            }

            // Case 1: [prefix is 1-byte data buffer]
            if (ch <= 0x7f) {
                const unsigned char *tok_start = raw;
                const unsigned char *tok_end = tok_start + prefixlen;
                if (tok_end > end)
                    goto out_fail;

                // parsing done; assign data buffer value.
                buf.assign(tok_start, tok_end);
                value.assign(buf.begin(), buf.end());

                consumed = buf.size();

            // Case 2: [prefix, including buffer length][data]
            } else if ((ch >= 0x80) && (ch <= 0xb7)) {
                size_t blen = ch - 0x80;
                size_t expected = prefixlen + blen;

                if (len < expected) {
                    wanted = expected;
                    goto out_fail;
                }

                const unsigned char *tok_start = raw + 1;
                const unsigned char *tok_end = tok_start + blen;

                if (tok_end > end)
                    goto out_fail;

                // require minimal encoding
                if ((blen == 1) && (tok_start[0] <= 0x7f))
                    goto out_fail;

                // parsing done; assign data buffer value.
                buf.assign(tok_start, tok_end);
                value.assign(buf.begin(), buf.end());

                consumed = expected;

            // Case 3: [prefix][buffer length][data]
            } else if ((ch >= 0xb8) && (ch <= 0xbf)) {
                size_t uintlen = ch - 0xb7;
                size_t expected = prefixlen + uintlen;

                if (len < expected) {
                    wanted = expected;
                    goto out_fail;
                }

                if (uintlen <= 0 || uintlen > RLP_maxUintLen)
                    goto out_fail;

                const unsigned char *tok_start = raw + prefixlen;
                if ((uintlen > 1) && (tok_start[0] == 0))	// no leading zeroes
                    goto out_fail;

                // read buffer length
                uint64_t slen = toInteger(tok_start, uintlen);

                // validate buffer length, including possible addition overflows.
                expected = prefixlen + uintlen + slen;
                if (
                    (slen < (RLP_listStart - RLP_bufferLenStart - RLP_maxUintLen)) ||
                    (expected > len) || (slen > len)
                ) {
                    wanted = slen > expected ? slen : expected;
                    goto out_fail;
                }

                // parsing done; assign data buffer value.
                tok_start = raw + prefixlen + uintlen;
                const unsigned char *tok_end = tok_start + slen;
                buf.assign(tok_start, tok_end);
                value.assign(buf.begin(), buf.end());

                consumed = expected;

            // Case 4: [prefix][list]
            } else if ((ch >= 0xc0) && (ch <= 0xf7)) {
                size_t payloadlen = ch - 0xc0;
                size_t expected = prefixlen + payloadlen;
                size_t list_consumed = 0;
                size_t list_wanted = 0;

                // read list payload
                if (!readArray(raw, len, 0, payloadlen, list_consumed, list_wanted)) {
                    wanted = list_wanted;
                    goto out_fail;
                }

                if (list_consumed != payloadlen)
                    goto out_fail;

                consumed = expected;

            // Case 5: [prefix][list length][list]
            } else {
                if (ch < 0xf8 || ch > 0xff)
                    goto out_fail;

                size_t uintlen = ch - 0xf7;
                size_t expected = prefixlen + uintlen;

                if (len < expected) {
                    wanted = expected;
                    goto out_fail;
                }

                if (uintlen <= 0 || uintlen > RLP_maxUintLen)
                    goto out_fail;

                const unsigned char *tok_start = raw + prefixlen;

                if ((uintlen > 1) && (tok_start[0] == 0))	// no leading zeroes
                    goto out_fail;

                // read list length
                size_t payloadlen = toInteger(tok_start, uintlen);

            // special requirement for non-immediate length
            if (payloadlen < (0x100 - RLP_listStart - RLP_maxUintLen))
                goto out_fail;

                size_t list_consumed = 0;
                size_t list_wanted = 0;

                // read list payload
                if (!readArray(raw, len, uintlen, payloadlen, list_consumed, list_wanted)) {
                    wanted = list_wanted;
                    goto out_fail;
                }

                if (list_consumed != payloadlen)
                    goto out_fail;

                consumed = prefixlen + uintlen + payloadlen;
            }

            return true;

        out_fail:
            clear();
            return false;
        }
    };

    template <typename ... Args>
    static std::string encode(Args&& ... args){
        RLPValue rlp;
        if (sizeof...(Args) > 1) {
            rlp.set_array();
        }
        (rlp.encode_single(args), ...);
        return rlp.write();
    }

    static RLPValue decode(std::vector<int8_t> bytes){
        RLPValue rlp;
	    size_t consumed, wanted;
        eosio::check(bytes.size() > 0, "Invalid Transaction: RLP nothing to decode");
    	bool valid = rlp.read((unsigned char *) bytes.data(), bytes.size(), consumed, wanted);
        eosio::check(valid, "Invalid Transaction: RLP could not be decoded");
        return rlp;
    }
}
