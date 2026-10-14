#pragma once
#include "../scenario/json_io.hpp"
#include <array>
#include <fstream>
#include <functional>
#include <stop_token>

namespace simforge::workspace::detail {
// IEEE CRC-32, identical to the v1/v2 canonical JSON checksum.
class Crc32 {
    std::uint32_t state_{0xffffffff};
    static constexpr auto table() {
        std::array<std::uint32_t,256> result{};
        for(std::uint32_t i=0;i<256;++i) {auto c=i;for(int b=0;b<8;++b)c=(c>>1)^((c&1)?0xedb88320:0);result[i]=c;}
        return result;
    }
public:
    void append(std::string_view bytes) {static constexpr auto values=table();for(unsigned char c:bytes)state_=values[(state_^c)&255]^(state_>>8);}
    std::uint32_t value() const {return ~state_;}
};
struct Digest {
    std::uint32_t crc{0};std::size_t bytes{0};
    // Concatenate checksums without keeping either input. The matrix applies the
    // same polynomial as Crc32, advancing the left checksum by right.bytes.
    void append(Digest right) {
        if(!right.bytes)return;
        const auto times=[](const std::array<std::uint32_t,32>& matrix,std::uint32_t v){std::uint32_t sum=0;for(unsigned i=0;v;++i,v>>=1)if(v&1)sum^=matrix[i];return sum;};
        const auto square=[&](const auto& matrix){std::array<std::uint32_t,32> out{};for(unsigned i=0;i<32;++i)out[i]=times(matrix,matrix[i]);return out;};
        std::array<std::uint32_t,32> odd{};odd[0]=0xedb88320;std::uint32_t row=1;for(unsigned i=1;i<32;++i){odd[i]=row;row<<=1;}
        auto even=square(odd);odd=square(even);auto length=right.bytes;auto left=crc;
        do {even=square(odd);if(length&1)left=times(even,left);length>>=1;if(!length)break;odd=square(even);if(length&1)left=times(odd,left);length>>=1;}while(length);
        crc=left^right.crc;bytes+=right.bytes;
    }
    void append(std::string_view text) {Crc32 c;c.append(text);append({c.value(),text.size()});}
};
inline void checkpoint(std::stop_token stop) {if(stop.stop_requested())throw std::runtime_error("Project operation cancelled");}

// A bounded input buffer also checks raw NUL, cancellation and total bytes before
// the JSON lexer can consume them. Never loads the entire file into a string.
class InputBuffer : public std::streambuf {
    std::ifstream file_;std::array<char,64*1024> buffer_{};
    std::stop_token stop_;std::size_t limit_,bytes_{0};
    std::function<void(std::size_t)> progress_;
protected:
    int_type underflow() override {
        checkpoint(stop_);file_.read(buffer_.data(),static_cast<std::streamsize>(buffer_.size()));const auto count=static_cast<std::size_t>(file_.gcount());
        if(file_.bad())throw std::runtime_error("Project read failed");
        if(!count)return traits_type::eof();
        if(count>limit_-bytes_)throw std::length_error("Project exceeds 256 MiB limit");
        bytes_+=count;
        if(std::find(buffer_.begin(),buffer_.begin()+count,'\0')!=buffer_.begin()+count)throw std::invalid_argument("JSON: raw NUL bytes are not permitted");
        if(progress_)progress_(bytes_);setg(buffer_.data(),buffer_.data(),buffer_.data()+count);return traits_type::to_int_type(*gptr());
    }
public:
    InputBuffer(const std::filesystem::path& path,std::size_t limit,std::stop_token stop={},std::function<void(std::size_t)> progress={}):file_(path,std::ios::binary),stop_(stop),limit_(limit),progress_(std::move(progress)) {
        if(!file_)throw std::runtime_error("Cannot open project input");
        std::error_code error;const auto size=std::filesystem::file_size(path,error);if(!error && size>limit)throw std::length_error("Project exceeds 256 MiB limit");
    }
    std::size_t bytes() const {return bytes_;}
};
}
